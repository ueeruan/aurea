import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { communityRoute } from '../community.js';
import { sha256hex } from '../contas.js';
import { d1Falso, kvFalso, pedido } from './falsos.mjs';

async function fixture() {
  const env = { AUREA_DB: d1Falso(), AUREA_KV: kvFalso() };
  const files = new Map();
  env.COMMUNITY_FILES = {
    async put(id, data) { const bytes = data instanceof ReadableStream ? new Uint8Array(await new Response(data).arrayBuffer()) : data; files.set(id, bytes); return { size: bytes.length }; },
    async get(id) { const bytes = files.get(id); return bytes && { body: bytes, size: bytes.length }; },
    async delete(id) { files.delete(id); },
  };
  const users = [];
  for (let i = 0; i < 3; i++) {
    const uid = crypto.randomUUID(), token = String(i + 1).repeat(43);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, i === 0 ? 'ruanpablombl@gmail.com' : `person${i}@example.com`, 'not-a-password', 1);
    env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)').run(await sha256hex(token), uid, i === 2 ? 'ruanpablombl@gmail.com' : 'unused@example.com', Date.now(), Date.now() + 3600000);
    if (i === 0) env.AUREA_DB.sqlite.prepare("INSERT INTO account_roles(uid,role) VALUES(?,'community_admin')").run(uid);
    users.push({ uid, token });
  }
  const call = async (path, user = 1, method = 'GET', body) => {
    const req = pedido('/api/community' + path, { metodo: method, corpo: body, token: user === null ? undefined : users[user].token });
    const r = await communityRoute(req, env, {}, new URL(req.url));
    return { status: r.status, data: await r.json() };
  };
  for (let i = 0; i < 3; ++i) assert.equal((await call('/me', i, 'PUT', { username: `person_${i}`, name: `Person ${i}`, bio: 'Bio' })).status, 200);
  return { env, users, call, files };
}
test('profiles use existing accounts, unique usernames and never expose email/session', async () => {
  const { call, users } = await fixture();
  assert.equal((await call('/me', null)).status, 401);
  const me = await call('/me');
  assert.equal(me.data.profile.id, users[1].uid);
  assert.equal(me.data.canVerify, false);
  assert.equal(JSON.stringify(me.data).includes('@'), false);
  assert.equal((await call('/me', 2, 'PUT', { username: 'PERSON_1', name: 'Other', bio: '' })).status, 409);
  assert.equal((await call('/me', 1, 'PUT', { username: '../bad', name: 'No', bio: '' })).status, 400);
  assert.equal((await call('/profiles?q=person_')).data.items.length, 3);
});
test('only the owner account grants every badge, spoofed session email and profile field fail', async () => {
  const { call, users, env } = await fixture();
  const path = `/profiles/${users[1].uid}/verification`;
  assert.equal((await call(path, 1, 'PUT', { verification: 'gold' })).status, 403);
  assert.equal((await call(path, 2, 'PUT', { verification: 'gold' })).status, 403);
  await call('/me', 1, 'PUT', { username: 'person_1', name: 'Person', bio: '', verification: 'gold', email: 'ruanpablombl@gmail.com' });
  assert.equal((await call('/me')).data.profile.verification, '');
  for (const verification of ['blue', 'green', 'gold', '']) {
    assert.equal((await call(path, 0, 'PUT', { verification })).data.profile.verification, verification);
  }
  assert.equal(env.AUREA_DB.sqlite.prepare('SELECT COUNT(*) n FROM community_verifications').get().n, 4);
  assert.equal((await call(path, 0, 'PUT', { verification: 'admin' })).status, 400);
});
test('follow, like and unlike are idempotent; comments and deletions enforce ownership', async () => {
  const { call, users } = await fixture();
  const id = (await call('/posts', 1, 'POST', { body: 'My project' })).data.id;
  for (let i = 0; i < 2; ++i) {
    await call(`/posts/${id}/like`, 2, 'PUT');
    await call(`/profiles/${users[1].uid}/follow`, 2, 'PUT');
  }
  assert.equal((await call(`/profiles/${users[1].uid}`, 2)).data.profile.followers, 1);
  assert.equal((await call('/posts?following=1', 2)).data.items.length, 1);
  assert.equal((await call('/posts', 2)).data.items[0].likes, 1);
  const comment = (await call(`/posts/${id}/comments`, 2, 'POST', { body: 'Thanks!' })).data.id;
  assert.equal((await call(`/posts/${id}/comments`)).data.items[0].body, 'Thanks!');
  assert.equal((await call(`/comments/${comment}`, 1, 'DELETE')).status, 403);
  assert.equal((await call(`/comments/${comment}`, 2, 'DELETE')).status, 200);
  assert.equal((await call(`/posts/${id}`, 2, 'DELETE')).status, 403);
  await call(`/posts/${id}/like`, 2, 'DELETE'); await call(`/posts/${id}/like`, 2, 'DELETE');
  assert.equal((await call('/posts', 2)).data.items[0].likes, 0);
  await call(`/posts/${id}`, 1, 'DELETE');
  assert.equal((await call(`/posts/${id}/like`, 2, 'PUT')).status, 404);
  assert.equal((await call(`/profiles/${users[1].uid}/follow`, 1, 'PUT')).status, 400);
});

test('feed categories preserve old attachments and only admins can publish an announcement', async () => {
  const { env, call, users } = await fixture();
  const db = env.AUREA_DB.sqlite;
  const insertAsset = db.prepare('INSERT INTO community_assets(id,uid,kind,name,mime,bytes,created_at) VALUES(?,?,?,?,?,?,?)');
  const project = crypto.randomUUID(), preset = crypto.randomUUID();
  insertAsset.run(project, users[1].uid, 'project', 'edit.aureaproj', 'application/octet-stream', 50, 1);
  insertAsset.run(preset, users[1].uid, 'preset', 'preset.json', 'application/json', 30, 1);
  const legacy = crypto.randomUUID();
  db.prepare('INSERT INTO community_posts(id,uid,body,asset,created_at) VALUES(?,?,?,?,?)').run(legacy, users[1].uid, '', project, 1);
  const create = async (body, user = 1) => {
    const result = await call('/posts', user, 'POST', body);
    assert.equal(result.status, 201);
    return result.data.id;
  };
  assert.equal((await call('/me')).data.feedFilters, true);
  const text = await create({ body: 'Plain post' });
  const showcase = await create({ body: 'My edit', category: 'showcase', asset: project });
  const projectPost = await create({ body: '', asset: project });
  const presetPost = await create({ body: '', asset: preset });
  const announcement = await create({ body: 'Release notes', category: 'announcement' }, 0);
  const otherAdminPost = await create({ body: 'Personal post' }, 0);
  assert.equal((await call('/posts', 1, 'POST', { body: 'Fake notice', category: 'announcement' })).status, 403);
  assert.equal((await call('/posts', 2, 'POST', { body: 'Fake notice', category: 'announcement' })).status, 403);
  assert.equal((await call('/posts', 1, 'POST', { body: 'Fake attachment', category: 'project' })).status, 400);
  for (const [category, ids] of Object.entries({
    text: [text, otherAdminPost], showcase: [showcase], project: [legacy, projectPost], preset: [presetPost], announcement: [announcement],
  })) {
    const result = await call(`/posts?category=${category}`, 2);
    assert.equal(result.status, 200);
    assert.ok(result.data.items.every(p => p.category === category));
    assert.deepEqual(result.data.items.map(p => p.id).sort(), ids.sort());
  }
  assert.equal((await call('/posts?sort=unknown')).status, 400);
  assert.equal((await call('/posts?category=unknown')).status, 400);
});

test('feed migration backfills the category and popularity of existing posts', () => {
  const db = new DatabaseSync(':memory:');
  const migrate = name => db.exec(readFileSync(new URL(`../migrations/${name}.sql`, import.meta.url), 'utf8'));
  try {
    migrate('0001_contas');
    migrate('0004_comunidade');
    db.exec(`INSERT INTO users(id,email,password_hash,created_at) VALUES('u','legacy@example.com','fixture',1);
      INSERT INTO community_profiles(uid,created_at,updated_at) VALUES('u',1,1);
      INSERT INTO community_assets(id,uid,kind,name,mime,bytes,created_at)
      VALUES('a','u','project','archive.aureaproj','application/octet-stream',100,1);
      INSERT INTO community_posts(id,uid,body,asset,created_at) VALUES('p','u','Legacy edit','a',1);
      INSERT INTO community_likes(post,uid) VALUES('p','u');
      INSERT INTO community_comments(id,post,uid,body,created_at) VALUES('c','p','u','Comment',2);`);
    migrate('0016_community_feed');
    const row = db.prepare("SELECT category, popularity FROM community_posts WHERE id='p'").get();
    assert.deepEqual({ ...row }, { category: 'project', popularity: 2 });
  } finally { db.close(); }
});

test('every feed sort paginates across ties and popular tracks likes and comments', async () => {
  const { env, call, users } = await fixture();
  const insert = env.AUREA_DB.sqlite.prepare('INSERT INTO community_posts(id,uid,body,created_at) VALUES(?,?,?,?)');
  const ids = Array.from({ length: 34 }, (_, i) => `00000000-0000-4000-8000-${String(i).padStart(12, '0')}`);
  ids.forEach(id => insert.run(id, users[1].uid, 'Tie', 123));
  await call(`/posts/${ids[1]}/like`, 2, 'PUT');
  await call(`/posts/${ids[1]}/like`, 2, 'PUT');
  const comment = await call(`/posts/${ids[0]}/comments`, 2, 'POST', { body: 'Nice edit' });
  const desc = [...ids].reverse();
  for (const [sort, expected] of Object.entries({ newest: desc, oldest: ids, popular: [ids[1], ids[0], ...desc.slice(0, -2)] })) {
    const page = await call(`/posts?sort=${sort}&category=text`);
    assert.equal(page.data.items.length, 30);
    const more = await call(`/posts?sort=${sort}&category=text&cursor=${encodeURIComponent(page.data.cursor)}`);
    assert.equal(more.data.cursor, null);
    assert.deepEqual([...page.data.items, ...more.data.items].map(p => p.id), expected);
  }
  await call(`/posts/${ids[1]}/like`, 2, 'DELETE');
  assert.equal((await call('/posts?sort=popular')).data.items[0].id, ids[0]);
  await call(`/comments/${comment.data.id}`, 2, 'DELETE');
  assert.equal((await call('/posts?sort=popular')).data.items[0].id, desc[0]);
});
test('stable pagination across equal timestamps does not repeat or drop posts/comments', async () => {
  const { call, env, users } = await fixture();
  const ids = Array.from({ length: 65 }, () => crypto.randomUUID()).sort().reverse();
  for (const id of ids) env.AUREA_DB.sqlite.prepare('INSERT INTO community_posts(id,uid,body,created_at) VALUES(?,?,?,?)').run(id, users[1].uid, 'Post', 99);
  const first = await call('/posts'), second = await call('/posts?cursor=' + first.data.cursor), third = await call('/posts?cursor=' + second.data.cursor);
  assert.deepEqual([...first.data.items, ...second.data.items, ...third.data.items].map(v => v.id), ids);
  assert.equal(third.data.cursor, null);
});
test('attachments remain private until published; cannot attach another account upload', async () => {
  const { call, env, users, files } = await fixture();
  async function upload(kind, name, bytes, claimed = bytes.length) {
    const req = new Request(`https://local/api/community/assets?kind=${kind}&name=${name}`, { method: 'POST', headers: { authorization: `Bearer ${users[1].token}`, 'content-length': String(claimed) }, body: bytes });
    const r = await communityRoute(req, env, {}, new URL(req.url)); return { status: r.status, data: await r.json() };
  }
  const preset = await upload('preset', 'My.json', new TextEncoder().encode(JSON.stringify({ aurea_preset: 1, kind: 'effects', name: 'My' })));
  assert.equal(preset.status, 201);
  assert.equal((await call('/assets/' + preset.data.id)).status, 404);
  assert.equal((await call('/posts', 2, 'POST', { body: 'stolen', asset: preset.data.id })).status, 400);
  assert.equal((await call('/posts', 1, 'POST', { body: 'Preset', asset: preset.data.id })).status, 201);
  const req = pedido('/api/community/assets/' + preset.data.id);
  const download = await communityRoute(req, env, {}, new URL(req.url));
  assert.equal(download.status, 200); assert.equal((await download.json()).kind, 'effects');
  assert.equal((await upload('avatar', 'bad.jpg', new TextEncoder().encode('<html>bad</html>'))).status, 400);
  assert.equal((await upload('preset', 'bad.json', new TextEncoder().encode('[]'))).status, 400);
  assert.equal((await upload('project', 'bad.aureaproj', new Uint8Array([1,2,3,4]))).status, 400);
  assert.equal((await upload('project', 'ok.aureaproj', new Uint8Array([80,75,3,4,0,0]))).status, 201);
  assert.equal((await upload('project', 'bad.aureaproj', new Uint8Array([80,75,3,4]), 80 * 1024 * 1024)).status, 413);
  assert.equal(files.size, 2);
});
test('deleted/expired accounts lose access even if a cached session still exists', async () => {
  const { call, env, users } = await fixture();
  env.AUREA_DB.sqlite.prepare('UPDATE sessions SET expires_at=1 WHERE uid=?').run(users[1].uid);
  assert.equal((await call('/me')).status, 401);
  env.AUREA_DB.sqlite.prepare('DELETE FROM community_profiles WHERE uid=?').run(users[2].uid);
  env.AUREA_DB.sqlite.prepare('DELETE FROM users WHERE id=?').run(users[2].uid);
  assert.equal((await call('/me', 2)).status, 401);
});

test('avatar uploads reject oversized decoded dimensions and truncated image headers', async () => {
  const { env, users, files } = await fixture();
  async function upload(bytes) {
    const req = new Request('https://local/api/community/assets?kind=avatar&name=avatar.jpg', {
      method: 'POST', headers: { authorization: `Bearer ${users[1].token}`, 'content-length': String(bytes.length) }, body: bytes,
    });
    return communityRoute(req, env, {}, new URL(req.url));
  }
  // JPEG SOF0 segment with a 512 x 512 frame and three components.
  const jpeg = new Uint8Array([255,216,255,192,0,17,8,2,0,2,0,3,1,17,0,2,17,1,3,17,1,255,217]);
  assert.equal((await upload(jpeg)).status, 201);
  const oversized = jpeg.slice(); oversized[9] = 127;
  assert.equal((await upload(oversized)).status, 400);
  assert.equal((await upload(jpeg.slice(0, 12))).status, 400);
  const empty = jpeg.slice(); empty[7] = 0;
  assert.equal((await upload(empty)).status, 400);
  const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64');
  assert.equal((await upload(png)).status, 201);
  const widePng = Buffer.from(png); widePng.writeUInt32BE(65535, 16);
  assert.equal((await upload(widePng)).status, 400);
  assert.equal(files.size, 2);
});
// Moderação (Google Play: conteúdo gerado pelo usuário e IA generativa).
test('reports: one per person and target, three people hide a post from all but its author, admin resolves', async () => {
  const { env, call, users } = await fixture();
  const id = (await call('/posts', 1, 'POST', { body: 'Look at this' })).data.id;
  assert.equal((await call('/reports', 2, 'POST', { kind: 'post', id, reason: 'bogus' })).status, 400);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'post', id: crypto.randomUUID(), reason: 'spam' })).status, 404);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'post', id, reason: 'spam', details: 'x'.repeat(900) })).status, 201);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'post', id, reason: 'spam' })).status, 200);   // repetida: não conta
  assert.equal((await call('/reports', 0, 'POST', { kind: 'post', id, reason: 'hate' })).status, 201);
  assert.equal((await call('/posts', 2)).data.items.length, 1);   // 2 pessoas: ainda aparece
  // Uma terceira pessoa (conta direto no banco) denuncia.
  const third = crypto.randomUUID();
  env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(third, 'third@example.com', 'x', 1);
  env.AUREA_DB.sqlite.prepare("INSERT INTO community_reports(id,reporter,target_kind,target_id,reason,created_at) VALUES(?,?,'post',?,'nudity',?)")
    .run(crypto.randomUUID(), third, id, Date.now());
  assert.equal((await call('/posts', 2)).data.items.length, 0);   // escondido para os outros
  assert.equal((await call('/posts', 1)).data.items.length, 1);   // o autor ainda vê
  // Só o admin revisa; "removed" apaga o post.
  assert.equal((await call('/reports', 2)).status, 403);
  const open = (await call('/reports', 0)).data.items;
  assert.equal(open.length, 1);
  assert.equal(open[0].reports, 3);
  assert.deepEqual(open[0].reasons.sort(), ['hate', 'nudity', 'spam']);
  assert.equal((await call(`/reports/post/${id}`, 2, 'PUT', { status: 'removed' })).status, 403);
  assert.equal((await call(`/reports/post/${id}`, 0, 'PUT', { status: 'removed' })).status, 200);
  assert.equal((await call(`/posts/${id}/like`, 2, 'PUT')).status, 404);
  assert.equal((await call('/reports', 0)).data.items.length, 0);
  // Perfil, vídeo de IA e preset de legenda também podem ser denunciados.
  assert.equal((await call('/reports', 2, 'POST', { kind: 'user', id: users[1].uid, reason: 'harassment' })).status, 201);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'user', id: users[2].uid, reason: 'spam' })).status, 400);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'ai_video', id: 'job_8s:123', reason: 'violence' })).status, 201);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'ai_video', id: '../x', reason: 'violence' })).status, 400);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'preset', id: crypto.randomUUID(), reason: 'copyright' })).status, 201);
});
test('blocking hides both people from each other, ends follows and can be undone; admin moderates any comment', async () => {
  const { call, users } = await fixture();
  const post1 = (await call('/posts', 1, 'POST', { body: 'From one' })).data.id;
  await call('/posts', 2, 'POST', { body: 'From two' });
  await call(`/profiles/${users[1].uid}/follow`, 2, 'PUT');
  await call(`/profiles/${users[2].uid}/follow`, 1, 'PUT');
  const comment = (await call(`/posts/${post1}/comments`, 2, 'POST', { body: 'Hi' })).data.id;
  assert.equal((await call(`/profiles/${users[2].uid}/block`, 2, 'PUT')).status, 400);   // a si mesmo, não
  const blocked = await call(`/profiles/${users[1].uid}/block`, 2, 'PUT');
  assert.equal(blocked.status, 200);
  assert.equal(blocked.data.profile.blocked, true);
  assert.equal(blocked.data.profile.followers, 0);
  assert.equal((await call(`/profiles/${users[2].uid}`, 1)).data.profile.followers, 0);
  assert.deepEqual((await call('/posts', 2)).data.items.map(p => p.body), ['From two']);
  assert.deepEqual((await call('/posts', 1)).data.items.map(p => p.body), ['From one']);
  assert.equal((await call(`/posts/${post1}/comments`, 1)).data.items.length, 0);
  assert.equal((await call(`/profiles/${users[1].uid}/block`, 2, 'DELETE')).data.profile.blocked, false);
  assert.equal((await call('/posts', 2)).data.items.length, 2);
  // Moderação: o admin apaga o comentário de outra pessoa; quem não é admin, não.
  assert.equal((await call(`/comments/${comment}`, 1, 'DELETE')).status, 403);
  assert.equal((await call(`/comments/${comment}`, 0, 'DELETE')).status, 200);
  assert.equal((await call(`/posts/${post1}`, 0, 'DELETE')).status, 200);
});
