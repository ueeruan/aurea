// 1.0.9: fotos e vídeos dos posts no bucket COMMUNITY_MEDIA servido pela CDN, e a
// entrada da comunidade com menos idas ao D1 (que fica longe, em WEUR).
import test from 'node:test';
import assert from 'node:assert/strict';
import { communityRoute } from '../community.js';
import { lerSessao, sha256hex } from '../contas.js';
import { d1Falso, kvFalso, pedido } from './falsos.mjs';

function bucket() {
  const files = new Map(), meta = new Map();
  return {
    files, meta,
    async put(key, data, options) {
      const bytes = data instanceof ReadableStream ? new Uint8Array(await new Response(data).arrayBuffer()) : data;
      files.set(key, bytes); meta.set(key, options?.httpMetadata); return { size: bytes.length };
    },
    async head(key) { const b = files.get(key); return b && { size: b.length }; },
    async get(key) { const b = files.get(key); return b && { body: b, size: b.length }; },
    async delete(keys) { for (const k of [].concat(keys)) files.delete(k); },
  };
}

async function fixture() {
  const env = { AUREA_DB: d1Falso(), AUREA_KV: kvFalso(), COMMUNITY_FILES: bucket(), COMMUNITY_MEDIA: bucket(),
    COMMUNITY_MEDIA_URL: 'https://media.example.com' };
  const users = [];
  for (let i = 0; i < 3; i++) {
    const uid = crypto.randomUUID(), token = String(i + 4).repeat(43);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, `cdn${i}@example.com`, 'x', 1);
    env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)')
      .run(await sha256hex(token), uid, 'unused@example.com', Date.now(), Date.now() + 3600000);
    users.push({ uid, token });
  }
  const call = async (path, user, method = 'GET', body) => {
    const req = pedido('/api/community' + path, { metodo: method, corpo: body, token: users[user].token });
    const r = await communityRoute(req, env, {}, new URL(req.url));
    return { status: r.status, data: await r.json() };
  };
  for (let i = 0; i < 3; ++i) assert.equal((await call('/me', i, 'PUT', { username: `cdn_${i}`, name: `Cdn ${i}`, bio: '' })).status, 200);
  const send = async (user, kind, name, bytes) => {
    const req = new Request(`https://test.local/api/community/media?kind=${kind}&name=${name}`, {
      method: 'POST', headers: { authorization: `Bearer ${users[user].token}`, 'content-length': String(bytes.length) }, body: bytes,
    });
    return communityRoute(req, env, {}, new URL(req.url));
  };
  const media = async (id) => {
    const req = new Request(`https://test.local/api/community/media/${id}`, { headers: { range: 'bytes=0-9' } });
    return communityRoute(req, env, {}, new URL(req.url));
  };
  return { env, users, call, send, media };
}

test('post media goes to the CDN bucket with a cacheable key; the Worker checks moderation and redirects', async () => {
  const { env, call, send, media } = await fixture();
  const clip = Buffer.alloc(2048); clip.write('ftypqt  ', 4);
  const upload = await send(1, 'video', 'take.mov', clip);
  assert.equal(upload.status, 201);
  const { id } = await upload.json();
  const key = `community/media/${id}.mp4`;
  assert.deepEqual([...env.COMMUNITY_MEDIA.files.keys()], [key]);
  assert.equal(env.COMMUNITY_FILES.files.size, 0);
  assert.equal(env.COMMUNITY_MEDIA.meta.get(key).contentType, 'video/quicktime');
  assert.match(env.COMMUNITY_MEDIA.meta.get(key).cacheControl, /^public, max-age=\d+$/);

  assert.equal((await media(id)).status, 404, 'unpublished media is not served');
  const post = await call('/posts', 1, 'POST', { body: 'clip', media: id });
  assert.equal(post.status, 201);
  const redirect = await media(id);
  assert.equal(redirect.status, 302);
  assert.equal(redirect.headers.get('location'), `https://media.example.com/${key}`);
  assert.equal(redirect.headers.get('cache-control'), 'no-store');

  for (const user of [0, 2, 1]) await call('/reports', user, 'POST', { kind: 'post', id: post.data.id, reason: 'spam' });
  assert.equal((await media(id)).status, 404, 'hidden after three reports');

  assert.equal((await call('/posts/' + post.data.id, 1, 'DELETE')).status, 200);
  assert.equal(env.COMMUNITY_MEDIA.files.size, 0);
});

test('photos keep their own extension and the Worker serves bytes itself without a CDN address', async () => {
  const { env, call, send, media } = await fixture();
  delete env.COMMUNITY_MEDIA_URL;
  const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64');
  const { id } = await (await send(1, 'image', 'frame.png', png)).json();
  assert.ok(env.COMMUNITY_MEDIA.files.has(`community/media/${id}.png`));
  await call('/posts', 1, 'POST', { body: '', media: id });
  const part = await media(id);
  assert.equal(part.status, 206);
  assert.equal(part.headers.get('content-type'), 'image/png');
});

test('session and account come from one D1 query; a deleted account or a password change ends the session', async () => {
  const env = { AUREA_DB: d1Falso() };
  const uid = crypto.randomUUID(), token = '7'.repeat(43);
  env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, 'one@example.com', 'x', 1);
  env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)')
    .run(await sha256hex(token), uid, 'stale@example.com', Date.now() - 1000, Date.now() + 3600000);
  const req = () => pedido('/api/community/me', { token });
  let queries = 0;
  const prepare = env.AUREA_DB.prepare.bind(env.AUREA_DB);
  env.AUREA_DB.prepare = (sql) => { queries++; return prepare(sql); };
  assert.deepEqual(await lerSessao(env, req()), { uid, email: 'one@example.com', hash: await sha256hex(token) });
  assert.equal(queries, 1);
  env.AUREA_DB.sqlite.prepare('UPDATE users SET password_changed_at=? WHERE id=?').run(Date.now(), uid);
  assert.equal(await lerSessao(env, req()), null);
  env.AUREA_DB.sqlite.prepare('DELETE FROM users WHERE id=?').run(uid);
  assert.equal(await lerSessao(env, req()), null);
});

test('an authenticated community read touches D1 three times: session, role+profile, the feed', async () => {
  const { env, call } = await fixture();
  let queries = 0;
  const prepare = env.AUREA_DB.prepare.bind(env.AUREA_DB);
  env.AUREA_DB.prepare = (sql) => { queries++; return prepare(sql); };
  assert.equal((await call('/posts', 1)).status, 200);
  assert.equal(queries, 3);
});
