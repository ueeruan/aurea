// Social data is authoritative in D1. Credentials never appear in public
// profiles, posts or chat payloads. Existing KV posts/media remain compatible.
const fields = 'id,handle,name,bio,avatar,verified,role,created_at';
const reply = (body, status = 200) => Response.json(body, {status, headers: {
  'access-control-allow-origin': '*', 'cache-control': 'no-store',
}});
class ApiError extends Error { constructor(message, status = 400) { super(message); this.status = status; } }
const fail = (message, status) => { throw new ApiError(message, status); };
const stmt = (env, sql, ...args) => env.SOCIAL.prepare(sql).bind(...args);
const one = (env, sql, ...args) => stmt(env, sql, ...args).first();
const rows = async (env, sql, ...args) => (await stmt(env, sql, ...args).all()).results;
const run = (env, sql, ...args) => stmt(env, sql, ...args).run();
const now = () => new Date().toISOString();
const token = (request) => (request.headers.get('authorization') ?? '').replace(/^Bearer\s+/i, '').trim().toLowerCase();
export async function hashCode(code) {
  return [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(code)))].map(x => x.toString(16).padStart(2,'0')).join('');
}
function newCode() { return [...crypto.getRandomValues(new Uint8Array(24))].map(x => x.toString(16).padStart(2,'0')).join(''); }
export function identityKey(value) {
  return String(value ?? '').normalize('NFKD').toLowerCase().replace(/[\u0300-\u036f]/g,'')
    .replace(/[013457@$!]/g,c => ({0:'o',1:'i',3:'e',4:'a',5:'s',7:'t','@':'a','$':'s','!':'i'})[c])
    .replace(/[аероісхуԝ]/g,c=>({'а':'a','е':'e','р':'p','о':'o','і':'i','с':'c','х':'x','у':'y','ԝ':'w'})[c])
    .replace(/[^a-z0-9]/g,'').replace(/(.)\1+/g,'$1');
}
export function reserved(value, role = 'user') {
  const key = identityKey(value);
  if (key.includes('ruanzitw') && role !== 'owner') return true;
  return /^(aurea|aureaapp|aureaoficial|aureaofficial|admin|suporte|moderacao|equipe)$/.test(key) && role !== 'official' && role !== 'owner';
}
function handle(value, role) {
  const h = String(value ?? '').trim().toLowerCase();
  if (!/^[a-z0-9_.]{3,24}$/.test(h)) fail('Use de 3 a 24 letras, numeros, ponto ou sublinhado.',422);
  if (reserved(h, role)) fail('Esse nome pertence ao criador ou ao Aurea oficial.',403);
  return h;
}
function publicProfile(p) {
  return {id:p.id, apelido:p.handle, nome:p.name || p.handle, bio:p.bio || '',
    avatar:p.avatar || null, verificado:p.verified === 1, oficial:p.role === 'official',
    criador:p.role === 'owner', criadaEm:p.created_at};
}
async function body(request) {
  if (Number(request.headers.get('content-length')) > 16384) fail('Pedido muito grande.',413);
  const text = await request.text();
  if (new TextEncoder().encode(text).length > 16384) fail('Pedido muito grande.',413);
  try { const b = JSON.parse(text); if (!b || Array.isArray(b) || typeof b !== 'object') throw 0; return b; }
  catch { fail('Pedido invalido.',400); }
}
async function limit(env, key, max, seconds = 3600) {
  const bucket = Math.floor(Date.now()/1000/seconds);
  const result = await one(env, `INSERT INTO social_limits(key,count,expires) VALUES(?,1,?)
    ON CONFLICT(key) DO UPDATE SET count=count+1 RETURNING count`, `${key}:${bucket}`, (bucket+2)*seconds);
  if (result.count > max) fail('Muitas tentativas. Tente mais tarde.',429);
}
async function profile(env, id) {
  const p = await one(env, `SELECT ${fields} FROM profiles WHERE id=? AND deleted_at IS NULL`, id);
  if (!p) fail('Perfil nao encontrado.',404);
  return p;
}
// Import a legacy account without allowing legacy aliases to impersonate staff.
export async function importProfile(env, id, old) {
  const existing = await one(env, 'SELECT * FROM profiles WHERE id=?', id);
  if (existing) return existing;
  let h = String(old.apelido ?? '').normalize('NFKD').toLowerCase().replace(/[\u0300-\u036f]/g,'').replace(/[^a-z0-9_.]/g,'').slice(0,24);
  if (h.length < 3 || reserved(h) || await one(env,'SELECT id FROM profiles WHERE handle_key=?', identityKey(h))) h = `editor_${id.replace(/-/g,'').slice(0,12)}`;
  await run(env, `INSERT OR IGNORE INTO profiles(id,handle,handle_key,name,created_at) VALUES(?,?,?,?,?)`,
    id,h,identityKey(h),reserved(old.apelido) ? h : String(old.apelido || h).slice(0,50),old.criadaEm || now());
  return one(env,'SELECT * FROM profiles WHERE id=?',id);
}
export async function socialAuth(request, env) {
  if (!env.SOCIAL) return null;
  const code = token(request);
  if (!/^[a-f0-9]{48}$/.test(code)) return null;
  const hash = await hashCode(code);
  let p = await one(env, 'SELECT p.* FROM credentials c JOIN profiles p ON p.id=c.user_id WHERE c.hash=?', hash);
  if (!p) {
    const id = await env.MURAL.get(`codigo:${code}`);
    if (!id) return null;
    const old = await env.MURAL.get(`conta:${id}`);
    if (!old) return null;
    p = await importProfile(env,id,JSON.parse(old));
    if (!p.deleted_at) await run(env,'INSERT OR IGNORE INTO credentials(hash,user_id) VALUES(?,?)',hash,id);
  }
  if (p.deleted_at) return null;
  return {...publicProfile(p), role:p.role};
}
async function blocked(env,a,b) {
  return !!await one(env, 'SELECT 1 FROM blocks WHERE (user_id=? AND target=?) OR (user_id=? AND target=?)',a,b,b,a);
}
async function enrich(env, posts, me) {
  if(!posts.length)return [];
  const flat=posts.flatMap(p=>p.original?[p,p.original]:[p]);
  const authors=[...new Set(flat.map(p=>p.autorId).filter(Boolean))];
  const ids=[...new Set(flat.map(p=>p.id))];
  const marks=list=>list.map(()=>'?').join(',');
  const profiles=new Map((authors.length?await rows(env,`SELECT * FROM profiles WHERE id IN(${marks(authors)})`,...authors):[]).map(p=>[p.id,p]));
  const banned=new Set(me?(await rows(env,'SELECT CASE WHEN user_id=? THEN target ELSE user_id END id FROM blocks WHERE user_id=? OR target=?',me.id,me.id,me.id)).map(p=>p.id):[]);
  const counts=new Map((await rows(env,`SELECT post_id,COUNT(*) n,MAX(CASE WHEN user_id=? THEN 1 ELSE 0 END) liked FROM likes WHERE post_id IN(${marks(ids)}) GROUP BY post_id`,me?.id??'',...ids)).map(p=>[p.post_id,p]));
  const decorate=(post,nested=false)=>{
    const p=profiles.get(post.autorId);
    if(p?.deleted_at || banned.has(post.autorId))return null;
    const original=post.original&&!nested?decorate(post.original,true):null;
    if(post.original&&!nested&&!original)return null;
    return {...post,...(p?{autor:p.handle,perfil:publicProfile(p)}:{}),...(post.original?{original}:{}),curtidas:counts.get(post.id)?.n??0,curtiu:counts.get(post.id)?.liked===1};
  };
  return posts.map(p=>decorate(p)).filter(Boolean);
}
export async function enrichPosts(env, posts, me) { return env.SOCIAL ? enrich(env, posts, me) : posts; }
export async function indexPost(env,post) {
  if (!env.SOCIAL || !post?.id) return;
  await run(env,'INSERT OR REPLACE INTO social_posts(id,author,parent,created_at,payload) VALUES(?,?,?,?,?)',post.id,post.autorId || '',post.respondeA || null,post.quando,JSON.stringify(post));
}
async function kvPosts(env, prefix, cursor, count = 24) {
  const list = await env.MURAL.list({prefix, limit:count, ...(cursor ? {cursor} : {})});
  const posts = (await Promise.all(list.keys.map(async key => {
    const raw = await env.MURAL.get(key.name); try { return JSON.parse(raw); } catch { return null; }
  }))).filter(Boolean);
  return {posts, cursor:list.list_complete ? null : list.cursor};
}
async function requirePost(env,id) {
  const key = await env.MURAL.get(`indice:${id}`);
  if (!key) fail('Publicacao nao encontrada.',404);
  const raw = await env.MURAL.get(key);
  if (!raw) fail('Publicacao nao encontrada.',404);
  return JSON.parse(raw);
}

export async function socialRoutes(request, env, helpers) {
  if (!env.SOCIAL) return null;
  const url = new URL(request.url), path = url.pathname.replace(/\/+$/,'');
  const method = request.method;
  if (!(path.startsWith('/social/') || path === '/conta' || path === '/conta/entrar')) return null;
  try {
    // Bootstrap is bound to the supplied owner's credential HASH in a secret.
    if (path === '/social/admin/bootstrap' && method === 'POST') {
      if (!env.OWNER_CODE_HASH || await hashCode(token(request)) !== env.OWNER_CODE_HASH) fail('Sem permissao.',403);
      const id = await env.MURAL.get(`codigo:${token(request)}`);
      const old = id && await env.MURAL.get(`conta:${id}`);
      if (!old) fail('A conta do criador precisa existir.',404);
      await importProfile(env,id,JSON.parse(old));
      await env.SOCIAL.batch([
        stmt(env, "UPDATE profiles SET handle='ruanzitwo',handle_key='ruanzitwo',name='Ruanzitwo',verified=1,role='owner' WHERE id=?",id),
        stmt(env,'INSERT OR IGNORE INTO credentials(hash,user_id) VALUES(?,?)',await hashCode(token(request)),id),
      ]);
      let official = await one(env,"SELECT * FROM profiles WHERE role='official'");
      let codigo;
      if (!official) {
        codigo = newCode(); const uid = crypto.randomUUID();
        await env.SOCIAL.batch([
          stmt(env,"INSERT INTO profiles(id,handle,handle_key,name,bio,verified,role,created_at) VALUES(?,?,?,?,?,1,'official',?)",
            uid,'aurea','aurea','Aurea','O perfil oficial do Aurea. Novidades, tutoriais e comunidade.',now()),
          stmt(env,'INSERT INTO credentials(hash,user_id) VALUES(?,?)',await hashCode(codigo),uid),
        ]);
        official = await profile(env,uid);
      }
      return reply({criador:publicProfile(await profile(env,id)), aurea:publicProfile(official), ...(codigo ? {codigoOficial:codigo} : {})});
    }
    const me = await socialAuth(request,env);
    if (path === '/conta' && method === 'POST') {
      await limit(env,`register:${request.headers.get('cf-connecting-ip') || 'unknown'}`,20);
      const b = await body(request), h = handle(b.apelido,'user');
      const reason = helpers.recusarApelido(h); if (reason) fail(reason,422);
      const id = crypto.randomUUID(), code = newCode(), created = now();
      await env.SOCIAL.batch([
        stmt(env,'INSERT INTO profiles(id,handle,handle_key,name,created_at) VALUES(?,?,?,?,?)',id,h,identityKey(h),h,created),
        stmt(env,'INSERT INTO credentials(hash,user_id) VALUES(?,?)',await hashCode(code),id),
      ]);
      // Compatibility for already installed app versions and existing media API.
      await env.MURAL.put(`conta:${id}`,JSON.stringify({apelido:h,criadaEm:created}));
      await env.MURAL.put(`codigo:${code}`,id);
      return reply({...publicProfile(await profile(env,id)),codigo:code},201);
    }
    if (path === '/conta/entrar' && method === 'POST') {
      if (!me) fail('Codigo de acesso invalido.',401);
      return reply(publicProfile(await profile(env,me.id)));
    }
    if (!me) fail('Entre na sua conta para continuar.',401);
    if (path === '/social/admin/migrate' && method === 'POST') {
      if (me.role !== 'owner') fail('Sem permissao.',403);
      const b = await body(request);
      const list = await env.MURAL.list({prefix:'conta:',limit:8,...(b.cursor ? {cursor:String(b.cursor)} : {})});
      for (const key of list.keys) {
        const old = await env.MURAL.get(key.name); if (old) await importProfile(env,key.name.slice(6),JSON.parse(old));
      }
      return reply({importadas:list.keys.length,cursor:list.list_complete ? null : list.cursor});
    }
    if (path === '/social/admin/migrate-posts' && method === 'POST') {
      if (me.role !== 'owner') fail('Sem permissao.',403);
      const b=await body(request);
      const list=await kvPosts(env,b.respostas?'resp:':'post:',b.cursor,30);
      for (const post of list.posts) await indexPost(env,post);
      return reply({importadas:list.posts.length,cursor:list.cursor});
    }
    if (path === '/social/me' && method === 'GET') return reply(publicProfile(await profile(env,me.id)));
    if ((path === '/conta' || path === '/social/me') && method === 'PATCH') {
      await limit(env,`profile:${me.id}`,30);
      const b = await body(request), old = await profile(env,me.id);
      const h = b.apelido == null ? old.handle : handle(b.apelido,me.role);
      const name = String(b.nome ?? old.name).trim(), bio = String(b.bio ?? old.bio).trim();
      if (name.length > 50 || bio.length > 180) fail('Nome: ate 50 caracteres. Bio: ate 180.',422);
      if (reserved(name,me.role)) fail('Esse nome e reservado.',403);
      if (helpers.ofensivo?.(`${name} ${bio}`)) fail('Ofensas nao sao permitidas no perfil.',422);
      const avatar = b.avatar === null ? null : (b.avatar ?? old.avatar);
      if (avatar && (!String(avatar).startsWith(`${url.origin}/midia/`) || !/\/midia\/[0-9a-f-]{36}\.(png|jpg|webp)$/.test(String(avatar)))) fail('Envie sua foto pela galeria do app.',422);
      await run(env,'UPDATE profiles SET handle=?,handle_key=?,name=?,bio=?,avatar=? WHERE id=?',h,identityKey(h),name,bio,avatar,me.id);
      await env.MURAL.put(`conta:${me.id}`,JSON.stringify({apelido:h,criadaEm:old.created_at}));
      return reply(publicProfile(await profile(env,me.id)));
    }
    if (path === '/social/me' && method === 'DELETE') {
      const b = await body(request);
      if (b.confirmar !== 'EXCLUIR') fail('Confirme a exclusao.',422);
      if (me.role !== 'user') fail('Contas oficiais nao podem ser excluidas por este atalho.',403);
      await env.SOCIAL.batch([
        stmt(env,"UPDATE profiles SET deleted_at=?,name='',bio='',avatar=NULL,verified=0 WHERE id=?",now(),me.id),
        stmt(env,'DELETE FROM credentials WHERE user_id=?',me.id),
        stmt(env,'DELETE FROM follows WHERE follower=? OR followed=?',me.id,me.id),
        stmt(env,'DELETE FROM likes WHERE user_id=?',me.id),
        stmt(env,'DELETE FROM messages WHERE sender=? OR recipient=?',me.id,me.id),
        stmt(env,'DELETE FROM social_posts WHERE author=?',me.id),
      ]);
      await env.MURAL.delete(`conta:${me.id}`);
      return reply({ok:true});
    }
    if (path === '/social/feed' && method === 'GET') {
      const author=url.searchParams.get('autor'), followed=url.searchParams.get('seguindo')==='1';
      const cursor=(url.searchParams.get('cursor') || '').split('|');
      const args=[me.id,me.id];
      let filter=`parent IS NULL AND NOT EXISTS(SELECT 1 FROM profiles p WHERE p.id=s.author AND p.deleted_at IS NOT NULL)
        AND NOT EXISTS(SELECT 1 FROM blocks b WHERE (b.user_id=? AND b.target=s.author) OR (b.target=? AND b.user_id=s.author))`;
      if(author){filter+=' AND author=?';args.push(author);}
      if(followed){filter+=' AND author IN(SELECT followed FROM follows WHERE follower=?)';args.push(me.id);}
      if(cursor.length===2){filter+=' AND (created_at<? OR(created_at=? AND id<?))';args.push(cursor[0],cursor[0],cursor[1]);}
      const list=await rows(env,`SELECT * FROM social_posts s WHERE ${filter} ORDER BY created_at DESC,id DESC LIMIT 24`,...args);
      return reply({posts:await enrich(env,list.map(p=>JSON.parse(p.payload)),me),cursor:list.length===24?`${list.at(-1).created_at}|${list.at(-1).id}`:null});
    }
    if (path === '/social/profiles' && method === 'GET') {
      const query = String(url.searchParams.get('q') ?? '').slice(0,40).replace(/[%_]/g,'');
      const result = await rows(env,`SELECT ${fields} FROM profiles WHERE deleted_at IS NULL AND (handle LIKE ? OR name LIKE ?) ORDER BY verified DESC,handle LIMIT 30`,`${query}%`,`${query}%`);
      return reply({perfis:result.map(publicProfile)});
    }
    const p = path.match(/^\/social\/profiles\/([^/]+)(?:\/(follow|followers|following|block))?$/);
    if (p) {
      const target = await profile(env,decodeURIComponent(p[1]));
      if (!p[2] && method === 'GET') {
        const counts = await one(env,`SELECT (SELECT count(*) FROM follows WHERE followed=?) seguidores,(SELECT count(*) FROM follows WHERE follower=?) seguindo`,target.id,target.id);
        return reply({...publicProfile(target),...counts,
          euSigo:!!await one(env,'SELECT 1 FROM follows WHERE follower=? AND followed=?',me.id,target.id),
          bloqueado:!!await one(env,'SELECT 1 FROM blocks WHERE user_id=? AND target=?',me.id,target.id)});
      }
      if ((p[2] === 'followers' || p[2] === 'following') && method === 'GET') {
        const incoming = p[2] === 'followers';
        const list = await rows(env,`SELECT p.* FROM follows f JOIN profiles p ON p.id=f.${incoming?'follower':'followed'} WHERE f.${incoming?'followed':'follower'}=? AND p.deleted_at IS NULL AND p.handle>? ORDER BY p.handle LIMIT 40`,target.id,url.searchParams.get('after') || '');
        return reply({perfis:list.map(publicProfile),after:list.length===40?list.at(-1).handle:null});
      }
      if (me.id === target.id) fail('Escolha outro perfil.',422);
      if (p[2] === 'follow' && ['POST','DELETE'].includes(method)) {
        await limit(env,`follow:${me.id}`,120);
        if (await blocked(env,me.id,target.id)) fail('Essa conexao nao esta disponivel.',403);
        await run(env,method === 'POST'?'INSERT OR IGNORE INTO follows(follower,followed) VALUES(?,?)':'DELETE FROM follows WHERE follower=? AND followed=?',me.id,target.id);
        return reply({ok:true});
      }
      if (p[2] === 'block' && ['POST','DELETE'].includes(method)) {
        if (method === 'DELETE') await run(env,'DELETE FROM blocks WHERE user_id=? AND target=?',me.id,target.id);
        else await env.SOCIAL.batch([
          stmt(env,'INSERT OR IGNORE INTO blocks(user_id,target) VALUES(?,?)',me.id,target.id),
          stmt(env,'DELETE FROM follows WHERE (follower=? AND followed=?) OR (follower=? AND followed=?)',me.id,target.id,target.id,me.id),
        ]);
        return reply({ok:true});
      }
    }
    const like = path.match(/^\/social\/posts\/([^/]+)\/like$/);
    if (like && ['POST','DELETE'].includes(method)) {
      const post = await requirePost(env,like[1]);
      if (await blocked(env,me.id,post.autorId)) fail('Publicacao indisponivel.',403);
      await limit(env,`like:${me.id}`,300);
      await run(env,method==='POST'?'INSERT OR IGNORE INTO likes(post_id,user_id) VALUES(?,?)':'DELETE FROM likes WHERE post_id=? AND user_id=?',like[1],me.id);
      return reply({curtidas:(await one(env,'SELECT count(*) n FROM likes WHERE post_id=?',like[1])).n,curtiu:method==='POST'});
    }
    if (path === '/social/chats' && method === 'GET') {
      const list = await rows(env,`SELECT m.*, CASE WHEN m.sender=? THEN m.recipient ELSE m.sender END peer FROM messages m
        WHERE m.id IN (SELECT MAX(id) FROM messages WHERE sender=? OR recipient=? GROUP BY CASE WHEN sender=? THEN recipient ELSE sender END) ORDER BY m.id DESC LIMIT 50`,me.id,me.id,me.id,me.id);
      if(!list.length)return reply({conversas:[]});
      const peers=list.map(m=>m.peer);
      const profiles=new Map((await rows(env,`SELECT * FROM profiles WHERE deleted_at IS NULL AND id IN(${peers.map(()=>'?').join(',')})`,...peers)).map(p=>[p.id,p]));
      const unread=new Map((await rows(env,'SELECT sender,COUNT(*) n FROM messages WHERE recipient=? AND read_at IS NULL GROUP BY sender',me.id)).map(p=>[p.sender,p.n]));
      const banned=new Set((await rows(env,'SELECT CASE WHEN user_id=? THEN target ELSE user_id END id FROM blocks WHERE user_id=? OR target=?',me.id,me.id,me.id)).map(p=>p.id));
      const conversations=[];
      for (const m of list) {
        if (banned.has(m.peer)||!profiles.has(m.peer)) continue;
        conversations.push({perfil:publicProfile(profiles.get(m.peer)),ultima:m.text,quando:m.created_at,naoLidas:unread.get(m.peer)??0});
      }
      return reply({conversas:conversations});
    }
    const chat = path.match(/^\/social\/chats\/([^/]+)(?:\/(read))?$/);
    if (chat) {
      const peer = await profile(env,chat[1]);
      if (peer.id === me.id || await blocked(env,me.id,peer.id)) fail('Conversa indisponivel.',403);
      if (chat[2] === 'read' && method === 'POST') {
        await run(env,'UPDATE messages SET read_at=? WHERE sender=? AND recipient=? AND read_at IS NULL',now(),peer.id,me.id);
        return reply({ok:true});
      }
      if (method === 'GET') {
        const after = Math.max(0,Number(url.searchParams.get('after'))||0), before = Math.max(0,Number(url.searchParams.get('before'))||0);
        const messages = await rows(env,`SELECT id,sender,recipient,text,created_at,read_at FROM messages WHERE ((sender=? AND recipient=?) OR (sender=? AND recipient=?)) AND id>? ${before?'AND id<?':''} ORDER BY id ${after?'ASC':'DESC'} LIMIT 50`,me.id,peer.id,peer.id,me.id,after,...(before?[before]:[]));
        if (!after) messages.reverse();
        return reply({mensagens:messages});
      }
      if (method === 'POST') {
        const b = await body(request), text = String(b.texto ?? '').trim(), nonce = String(b.clientId ?? '');
        if (!text || text.length > 2000 || !/^[a-zA-Z0-9-]{8,80}$/.test(nonce)) fail('Mensagem invalida.',422);
        await limit(env,`chat:${me.id}`,120);
        await run(env,'INSERT OR IGNORE INTO messages(sender,recipient,text,created_at,client_id) VALUES(?,?,?,?,?)',me.id,peer.id,text,now(),nonce);
        return reply({ok:true},201);
      }
    }
    if (path === '/social/reports' && method === 'POST') {
      const b = await body(request); await limit(env,`report:${me.id}`,10);
      const target=String(b.alvo ?? '').slice(0,100), reason=String(b.motivo ?? '').trim();
      if (!target || reason.length<3 || reason.length>500) fail('Descreva a denuncia.',422);
      await run(env,'INSERT INTO reports VALUES(?,?,?,?,?)',crypto.randomUUID(),me.id,target,reason,now());
      return reply({ok:true},201);
    }
    const verify = path.match(/^\/social\/admin\/verify\/([^/]+)$/);
    if (verify && method === 'POST') {
      if (me.role !== 'owner') fail('Somente o criador pode verificar contas.',403);
      await profile(env,verify[1]); const b=await body(request);
      await run(env,"UPDATE profiles SET verified=? WHERE id=? AND role='user'",b.verificado===true?1:0,verify[1]);
      return reply({ok:true});
    }
    if (path === '/social/admin/reports' && method === 'GET') {
      if (me.role !== 'owner') fail('Sem permissao.',403);
      return reply({denuncias:await rows(env,'SELECT * FROM reports ORDER BY created_at DESC LIMIT 100')});
    }
    if (path === '/social/admin/notice' && method === 'POST') {
      if (me.role !== 'owner') fail('Sem permissao.',403);
      const b=await body(request), text=String(b.texto ?? '').trim();
      if (!text || text.length>1000) fail('Aviso invalido.',422);
      await helpers.gravarAvisos(env,[{id:crypto.randomUUID(),texto:text,nivel:'info',quando:now()}]);
      return reply({ok:true});
    }
    return reply({erro:'Endereco nao encontrado.'},404);
  } catch (error) {
    if (error instanceof ApiError) return reply({erro:error.message},error.status);
    if (/UNIQUE constraint/i.test(String(error))) return reply({erro:'Esse nome ja esta em uso.'},409);
    console.error('Social request failed',path, error.name);
    return reply({erro:'Nao foi possivel concluir. Tente novamente.'},503);
  }
}
