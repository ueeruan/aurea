import {DatabaseSync} from 'node:sqlite';
import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import {socialRoutes, socialAuth, hashCode, reserved, enrichPosts,indexPost} from './social.js';
import worker from './worker.js';
const db = new DatabaseSync(':memory:');
db.exec(readFileSync(new URL('./social.sql',import.meta.url),'utf8'));
class Query {
 constructor(sql,args=[]) { this.sql=sql;this.args=args; }
 bind(...args) {return new Query(this.sql,args);}
 async first() {return db.prepare(this.sql).get(...this.args) ?? null;}
 async all() {return {results:db.prepare(this.sql).all(...this.args)};}
 async run() {return db.prepare(this.sql).run(...this.args);}
}
const kv=new Map();
const env={SOCIAL:{prepare:s=>new Query(s),async batch(list) {
 db.exec('BEGIN');try {const r=[];for(const q of list)r.push(await q.run());db.exec('COMMIT');return r;}catch(e){db.exec('ROLLBACK');throw e;}
}},MURAL:{get:async k=>kv.get(k)??null,put:async(k,v)=>kv.set(k,v),delete:async k=>kv.delete(k),
 list:async({prefix='',limit=100,cursor})=>{const all=[...kv.keys()].filter(k=>k.startsWith(prefix)).sort();const start=Number(cursor)||0;return{keys:all.slice(start,start+limit).map(name=>({name})),list_complete:start+limit>=all.length,cursor:String(start+limit)};}}};
const helpers={recusarApelido:()=>null,gravarAvisos:async(_,v)=>kv.set('avisos',v)};
async function call(path,method='GET',code=null,data) {
 const res=await socialRoutes(new Request(`https://test.local${path}`,{method,headers:{...(code?{authorization:`Bearer ${code}`} :{}),'cf-connecting-ip':crypto.randomUUID()},...(data?{body:JSON.stringify(data)}:{})}),env,helpers);
 return {status:res.status,data:await res.json()};
}
const created=[];
for(const name of ['alpha','beta','gamma']){const r=await call('/conta','POST',null,{apelido:name});assert.equal(r.status,201);created.push(r.data);}
const [a,b,c]=created;
assert.equal((await call('/conta','POST',null,{apelido:'alpha'})).status,409);
assert.equal((await call('/conta','POST',null,{apelido:'a.l.p.h.a'})).status,409);
for(const name of ['ruanzitwo','ruanzitwoo','RuAnZiTwO','ruanzitw0','ruan.zitwoo','aurea','aurea_oficial']) assert(reserved(name));
assert.equal((await call('/conta','POST',null,{apelido:'ruanzitwoo'})).status,403);
assert.equal((await call('/social/me','PATCH',a.codigo,{nome:'Ruanzitwoo'})).status,403);
assert.equal((await call('/social/me','PATCH',a.codigo,{nome:'Alpha Editor',bio:'Minha bio',verified:1,role:'owner'})).status,200);
let profile=(await call(`/social/profiles/${a.id}`,'GET',b.codigo)).data;
assert.equal(profile.verificado,false);assert.equal(profile.bio,'Minha bio');assert(!('codigo' in profile));assert(!('hash' in profile));
await call(`/social/profiles/${b.id}/follow`,'POST',a.codigo);await call(`/social/profiles/${b.id}/follow`,'POST',a.codigo);
assert.equal((await call(`/social/profiles/${b.id}`,'GET',a.codigo)).data.seguidores,1);
assert.equal((await call(`/social/profiles/${a.id}/follow`,'POST',a.codigo)).status,422);
for(let i=0;i<2;i++)assert.equal((await call(`/social/chats/${b.id}`,'POST',a.codigo,{texto:'Ola',clientId:'message-test-123'})).status,201);
assert.equal((await call(`/social/chats/${a.id}`,'GET',b.codigo)).data.mensagens.length,1);
assert.equal((await call(`/social/chats/${a.id}`,'GET',c.codigo)).data.mensagens.length,0);
assert.equal((await call('/social/chats','GET',c.codigo)).data.conversas.length,0);
await call(`/social/chats/${a.id}/read`,'POST',b.codigo);
assert((await call(`/social/chats/${b.id}`,'GET',a.codigo)).data.mensagens[0].read_at);
await call(`/social/profiles/${a.id}/block`,'POST',b.codigo);
assert.equal((await call(`/social/chats/${b.id}`,'POST',a.codigo,{texto:'Nao',clientId:'message-test-456'})).status,403);
assert.equal((await call(`/social/profiles/${b.id}`,'GET',a.codigo)).data.seguidores,0);
assert.equal((await call('/social/me','PATCH',c.codigo,{avatar:'https://attacker.invalid/photo.png'})).status,422);
kv.set('indice:p','post:p');kv.set('post:p',JSON.stringify({id:'p',autorId:c.id,texto:'Publicacao'}));
await call('/social/posts/p/like','POST',a.codigo);await call('/social/posts/p/like','POST',a.codigo);
assert.equal((await call('/social/posts/p/like','POST',b.codigo)).data.curtidas,2);
assert.equal((await call('/social/posts/p/like','DELETE',a.codigo)).data.curtidas,1);
assert.equal((await call(`/social/admin/verify/${a.id}`,'POST',a.codigo,{verificado:true})).status,403);
env.OWNER_CODE_HASH=await hashCode(a.codigo);
const boot=await call('/social/admin/bootstrap','POST',a.codigo,{});
assert.equal(boot.status,200);assert(boot.data.criador.verificado);assert(boot.data.aurea.oficial);assert.equal(boot.data.codigoOficial.length,48);
assert.equal((await call('/social/admin/bootstrap','POST',a.codigo,{})).data.codigoOficial,undefined);
assert.equal((await call('/conta/entrar','POST',boot.data.codigoOficial)).data.verificado,true);
assert.equal((await call(`/social/admin/verify/${b.id}`,'POST',a.codigo,{verificado:true})).status,200);
assert.equal((await call(`/social/profiles/${b.id}`,'GET',c.codigo)).data.verificado,true);
// Real worker publication must enter the indexed feed, not just legacy KV.
const published=await worker.fetch(new Request('https://test.local/post',{method:'POST',headers:{authorization:`Bearer ${c.codigo}`},body:JSON.stringify({texto:'Minha primeira publicacao de teste.'})}),env);
assert.equal(published.status,201);
const livePost=(await published.json()).post;
assert(livePost.id);
assert.equal((await call('/social/feed','GET',c.codigo)).data.posts[0].id,livePost.id);
for(let i=0;i<27;i++)await indexPost(env,{id:`feed-${String(i).padStart(2,'0')}`,autorId:c.id,quando:'2026-09-19T10:00:00.000Z',texto:'Teste'});
const page1=(await call('/social/feed','GET',c.codigo)).data;
assert.equal(page1.posts.length,24);assert(page1.cursor);
const page2=(await call('/social/feed?cursor='+encodeURIComponent(page1.cursor),'GET',c.codigo)).data;
assert.equal(page2.posts.length,4);assert(!page2.posts.some(p=>page1.posts.some(q=>q.id===p.id)));
assert.equal((await call('/social/feed?seguindo=1','GET',a.codigo)).data.posts.length,0);
assert.equal((await worker.fetch(new Request(`https://test.local/post/${livePost.id}`,{method:'DELETE',headers:{authorization:`Bearer ${b.codigo}`}}),env)).status,401);
assert.equal((await call('/social/me','DELETE',c.codigo,{confirmar:'EXCLUIR'})).status,200);
assert.equal((await call('/conta/entrar','POST',c.codigo)).status,401);
assert.equal((await enrichPosts(env,[{id:'p',autorId:c.id}],null)).length,0);
assert.equal((await enrichPosts(env,[{id:'repost',autorId:a.id,original:{id:'p',autorId:c.id}}],null)).length,0);
assert.equal((await call('/social/feed','GET',a.codigo)).data.posts.length,0);
assert.equal((await call('/social/me','DELETE',a.codigo,{confirmar:'EXCLUIR'})).status,403);
console.log('Social: registration, uniqueness, reserved names, permissions, profiles, follows, likes, private chat, blocking, deletion and verification passed.');
db.close();
