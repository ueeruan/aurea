import { DurableObject } from 'cloudflare:workers';
import { validatePreset, previewSvg, MAX_PRESET_BYTES } from './caption_schema.mjs';
const json = (body,status=200) => new Response(JSON.stringify(body), {status,headers:{'content-type':'application/json','cache-control':'no-store','x-content-type-options':'nosniff'}});
const hash = async value => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(value))),n=>n.toString(16).padStart(2,'0')).join('');
async function body(req) {
  if (!req.headers.get('content-type')?.startsWith('application/json')) throw new Error('content_type');
  const reader=req.body?.getReader();if(!reader)throw new Error('empty_body');
  let size=0;const chunks=[];
  try { while(true){const {done,value}=await reader.read();if(done)break;size+=value.length;if(size>MAX_PRESET_BYTES+2048)throw new Error('body_size');chunks.push(value);} }
  finally { await reader.cancel(); }
  const bytes=new Uint8Array(size);let offset=0;for(const c of chunks){bytes.set(c,offset);offset+=c.length;}
  return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
}
export async function captionRoute(req,env,url) {
  if(!url.pathname.startsWith('/api/captions/'))return null;
  if(!env.CAPTION_COMMUNITY)return json({error:'community_unavailable'},503);
  // Finish the bounded incoming stream before forwarding it to a DO. Otherwise
  // an early 401 can finish the Worker while the RPC is still streaming its body.
  if(req.body) {
    const reader=req.body.getReader(),chunks=[];let size=0;
    try {
      while(true) {
        const {done,value}=await reader.read();if(done)break;
        size+=value.length;if(size>MAX_PRESET_BYTES+2048)return json({error:'body_size'},413);
        chunks.push(value);
      }
    } finally { await reader.cancel(); }
    const bytes=new Uint8Array(size);let offset=0;
    for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}
    req=new Request(req,{body:bytes});
  }
  return env.CAPTION_COMMUNITY.get(env.CAPTION_COMMUNITY.idFromName('catalog-v1')).fetch(req);
}
export class CaptionCommunity extends DurableObject {
  constructor(ctx,env) {
    super(ctx,env);this.sql=ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS users(token TEXT PRIMARY KEY, id TEXT UNIQUE, created INTEGER);
      CREATE TABLE IF NOT EXISTS presets(id TEXT PRIMARY KEY, owner TEXT, name TEXT, author TEXT, version INTEGER, created INTEGER, downloads INTEGER DEFAULT 0, likes INTEGER DEFAULT 0, data TEXT);
      CREATE INDEX IF NOT EXISTS recent ON presets(created DESC,id);
      CREATE INDEX IF NOT EXISTS popular ON presets(likes DESC,downloads DESC);
      CREATE TABLE IF NOT EXISTS versions(id TEXT,version INTEGER,data TEXT,PRIMARY KEY(id,version));
      CREATE TABLE IF NOT EXISTS reactions(id TEXT,user TEXT,kind TEXT,PRIMARY KEY(id,user,kind));
      CREATE TABLE IF NOT EXISTS limits(key TEXT PRIMARY KEY,count INTEGER,expires INTEGER);`);
  }
  rate(key,max,seconds) {
    const now=Math.floor(Date.now()/1000),bucket=Math.floor(now/seconds);key+=`:${bucket}`;
    this.sql.exec('DELETE FROM limits WHERE expires < ?',now);
    const n=this.sql.exec('SELECT count FROM limits WHERE key=?',key).toArray()[0]?.count ?? 0;
    if(n>=max)return false;
    this.sql.exec('INSERT INTO limits VALUES(?,1,?) ON CONFLICT(key) DO UPDATE SET count=count+1',key,now+seconds*2);return true;
  }
  async fetch(req) {
    const url=new URL(req.url),path=url.pathname.replace('/api/captions','');
    const ip=await hash(req.headers.get('CF-Connecting-IP') ?? 'local');
    if(!this.rate(`requests:${ip}`,120,60))return json({error:'rate_limit'},429);
    try {
      if(path==='/session'&&req.method==='POST') {
        if(!this.rate(`register:${ip}`,4,86400)||!this.rate('register-global',2000,86400))return json({error:'registration_limit'},429);
        const token=crypto.randomUUID()+crypto.randomUUID(),id=crypto.randomUUID(),digest=await hash(token);
        this.sql.exec('INSERT INTO users VALUES(?,?,?)',digest,id,Date.now());return json({token,id},201);
      }
      if(path==='/presets'&&req.method==='GET') {
        const q=(url.searchParams.get('q')??'').slice(0,80),offset=Math.max(0,Math.min(10000,Number(url.searchParams.get('offset'))||0));
        const order=url.searchParams.get('sort')==='popular'?'likes DESC,downloads DESC,created DESC':'created DESC,id';
        const rows=this.sql.exec(`SELECT id,name,author,version,created,downloads,likes FROM presets WHERE name LIKE ? ORDER BY ${order} LIMIT 31 OFFSET ?`,'%'+q.replace(/[\\%_]/g,'')+'%',offset).toArray();
        return json({items:rows.slice(0,30).map(p=>({...p,minAppVersion:2113,thumbnail:`/api/captions/presets/${p.id}/preview`})),next:rows.length>30?offset+30:null});
      }
      const match=path.match(/^\/presets\/([a-f0-9-]{36})(?:\/(download|like|preview))?$/);
      if(match&&req.method==='GET'&&match[2]==='preview') {
        const p=this.sql.exec('SELECT name FROM presets WHERE id=?',match[1]).toArray()[0];if(!p)return json({error:'not_found'},404);
        return new Response(previewSvg(p.name),{headers:{'content-type':'image/svg+xml','content-security-policy':"default-src 'none'; style-src 'unsafe-inline'",'cache-control':'public,max-age=300','x-content-type-options':'nosniff'}});
      }
      const token=req.headers.get('authorization')?.replace(/^Bearer /,'')??'';
      if(token.length<64||token.length>100)return json({error:'unauthorized'},401);
      const user=this.sql.exec('SELECT id FROM users WHERE token=?',await hash(token)).toArray()[0]?.id;
      if(!user)return json({error:'unauthorized'},401);
      if(path==='/presets'&&req.method==='POST') {
        const input=await body(req),data=validatePreset(input.preset);
        if(typeof input.author!=='string'||!input.author.trim()||input.author.length>60||/[<>\x00-\x1f]/.test(input.author))return json({error:'author'},400);
        if(!this.rate(`publish:${user}`,10,86400)||!this.rate(`publish-ip:${ip}`,20,86400)||!this.rate('publish-global',500,86400))return json({error:'publish_limit'},429);
        const id=input.id??crypto.randomUUID();if(!/^[a-f0-9-]{36}$/.test(id))return json({error:'id'},400);
        const old=this.sql.exec('SELECT owner,version,downloads,likes FROM presets WHERE id=?',id).toArray()[0];
        if(old&&old.owner!==user)return json({error:'forbidden'},403);
        if(old&&old.version>=20)return json({error:'version_limit'},429);
        const version=(old?.version??0)+1,created=Date.now(),encoded=JSON.stringify(data);
        this.ctx.storage.transactionSync(()=>{
          this.sql.exec('INSERT INTO versions VALUES(?,?,?)',id,version,encoded);
          this.sql.exec('INSERT INTO presets(id,owner,name,author,version,created,data) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,author=excluded.author,version=excluded.version,created=excluded.created,data=excluded.data',id,user,data.name,input.author.trim(),version,created,encoded);
        });
        return json({id,name:data.name,author:input.author,version,created,downloads:old?.downloads??0,likes:old?.likes??0,minAppVersion:2113},201);
      }
      if(match&&req.method==='POST'&&['download','like'].includes(match[2])) {
        const id=match[1],kind=match[2],p=this.sql.exec('SELECT * FROM presets WHERE id=?',id).toArray()[0];if(!p)return json({error:'not_found'},404);
        if(!this.rate(`action:${user}`,120,3600))return json({error:'action_limit'},429);
        this.ctx.storage.transactionSync(()=>{
          const previous=this.sql.exec('SELECT 1 FROM reactions WHERE id=? AND user=? AND kind=?',id,user,kind).toArray();
          if(!previous.length){this.sql.exec('INSERT INTO reactions VALUES(?,?,?)',id,user,kind);this.sql.exec(`UPDATE presets SET ${kind==='like'?'likes=likes+1':'downloads=downloads+1'} WHERE id=?`,id);}
        });
        return kind==='download'?json({id,version:p.version,preset:JSON.parse(p.data)}):json({liked:true});
      }
      return json({error:'not_found'},404);
    } catch(error) { return json({error:'invalid_preset',detail:String(error.message).slice(0,100)},400); }
  }
}
