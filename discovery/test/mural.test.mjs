import test from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import { muralRoute } from '../mural.js';
import { communityRoute } from '../community.js';
import { sha256hex } from '../contas.js';
import { d1Falso, kvFalso, pedido } from './falsos.mjs';

const realFetch = globalThis.fetch;
const { privateKey } = generateKeyPairSync('rsa', { modulusLength: 2048 });
const account = JSON.stringify({ client_email: 'mural@test', private_key: privateKey.export({ type: 'pkcs8', format: 'pem' }) });

/** A Play de mentira: `purchases` diz o estado de cada token. */
function fakePlay(purchases) {
  globalThis.fetch = async (url) => {
    const text = String(url);
    if (text.includes('oauth2.googleapis.com')) return new Response(JSON.stringify({ access_token: 'ok' }));
    const token = decodeURIComponent(text.split('/tokens/')[1]);
    return token in purchases ? new Response(JSON.stringify({ purchaseState: purchases[token] })) : new Response('{}', { status: 404 });
  };
}

async function fixture({ paid = true, storage = true } = {}) {
  const env = { AUREA_DB: d1Falso(), AUREA_KV: kvFalso() };
  if (paid) env.GOOGLE_PLAY_SERVICE_ACCOUNT = account;
  const files = new Map();
  if (storage) env.COMMUNITY_FILES = {
    async put(key, data) { files.set(key, new Uint8Array(await new Response(data).arrayBuffer())); },
    async get(key, options) {
      const all = files.get(key); if (!all) return null;
      const r = options?.range; const bytes = r ? all.subarray(r.offset, r.offset + r.length) : all;
      return { body: bytes, size: all.length };
    },
    async delete(key) { files.delete(key); },
  };
  const users = [];
  for (let i = 0; i < 5; i++) {
    const uid = crypto.randomUUID(), token = String(i + 1).repeat(43);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, `u${i}@example.com`, 'x', 1);
    env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)')
      .run(await sha256hex(token), uid, `u${i}@example.com`, Date.now(), Date.now() + 1e9);
    users.push({ uid, token });
  }
  const community = async (path, user, method, body) => {
    const req = pedido('/api/community' + path, { metodo: method, corpo: body, token: users[user].token });
    return communityRoute(req, env, {}, new URL(req.url));
  };
  for (let i = 0; i < 5; ++i) assert.equal((await community('/me', i, 'PUT', { username: `person_${i}`, name: `P ${i}`, bio: '' })).status, 200);
  const call = async (path, user = 0, method = 'GET', body) => {
    const req = pedido('/api/mural' + path, { metodo: method, corpo: body, token: user === null ? undefined : users[user].token });
    const r = await muralRoute(req, env, {}, new URL(req.url));
    return { status: r.status, data: await r.json() };
  };
  const put = async (path, bytes, user = 0) => {
    const req = new Request('https://x.dev/api/mural' + path, { method: 'PUT', body: bytes, duplex: 'half',
      headers: { authorization: `Bearer ${users[user].token}`, 'content-length': String(bytes.length) } });
    const r = await muralRoute(req, env, {}, new URL(req.url));
    return { status: r.status, data: await r.json() };
  };
  return { env, users, call, put, community, files };
}

const video = () => { const b = new Uint8Array(64); b.set([0, 0, 0, 32, 0x66, 0x74, 0x79, 0x70]); return b; };
const meta = (creditId) => ({ creditId, caption: 'Meu edit', bytes: 64, width: 1080, height: 1920, durationMs: 9000 });

async function posted(f, user = 0, token = 'pago-' + user) {
  const credit = await f.call('/credits', user, 'POST', { platform: 'google', token });
  assert.ok(credit.status === 201 || credit.status === 200, JSON.stringify(credit));
  const post = await f.call('/posts', user, 'POST', meta(credit.data.id));
  assert.equal(post.status, 201, JSON.stringify(post));
  assert.equal((await f.put(`/posts/${post.data.id}/video`, video(), user)).status, 200);
  assert.equal((await f.put(`/posts/${post.data.id}/thumb`, new Uint8Array([255, 216, 255]), user)).status, 200);
  assert.equal((await f.call(`/posts/${post.data.id}/publish`, user, 'POST')).status, 200);
  return post.data.id;
}

test.afterEach(() => { globalThis.fetch = realFetch; });

test('sem chave da loja ou sem R2 a publicação fica fechada, e ver continua aberto', async () => {
  const f = await fixture({ paid: false });
  assert.equal((await f.call('/config', null)).data.posting, false);
  assert.equal((await f.call('/credits', 0, 'POST', { platform: 'google', token: 'x' })).status, 503);
  assert.equal((await f.call('/feed', null)).status, 200);
  const g = await fixture({ storage: false });
  assert.equal((await g.call('/config', null)).data.posting, false);
  assert.equal((await g.call('/posts', 0, 'POST', meta('x'))).status, 503);
});

test('publicar sem crédito pago é recusado, e compra inválida não vira crédito', async () => {
  fakePlay({ devolvida: 1 });
  const f = await fixture();
  assert.equal((await f.call('/posts', 0, 'POST', meta(crypto.randomUUID()))).status, 402);
  assert.equal((await f.call('/credits', 0, 'POST', { platform: 'google', token: 'inventada' })).status, 402);
  assert.equal((await f.call('/credits', 0, 'POST', { platform: 'google', token: 'devolvida' })).status, 402);
});

test('um pagamento vale uma publicação, nem na mesma conta nem em outra', async () => {
  fakePlay({ a: 0 });
  const f = await fixture();
  const first = await f.call('/credits', 0, 'POST', { platform: 'google', token: 'a' });
  assert.equal(first.status, 201);
  const again = await f.call('/credits', 0, 'POST', { platform: 'google', token: 'a' });
  assert.equal(again.data.id, first.data.id);
  assert.equal((await f.call('/credits', 1, 'POST', { platform: 'google', token: 'a' })).status, 409);
  await posted(f, 0, 'a');
  assert.equal((await f.call('/posts', 0, 'POST', meta(first.data.id))).status, 409);
  assert.equal((await f.call('/posts', 1, 'POST', meta(first.data.id))).status, 402);
});

test('o app caiu no meio: a mesma compra retoma a mesma publicação', async () => {
  fakePlay({ b: 0 });
  const f = await fixture();
  const credit = await f.call('/credits', 0, 'POST', { platform: 'google', token: 'b' });
  const post = await f.call('/posts', 0, 'POST', meta(credit.data.id));
  const again = await f.call('/credits', 0, 'POST', { platform: 'google', token: 'b' });
  assert.equal(again.data.postId, post.data.id);
  assert.equal((await f.call('/posts', 0, 'POST', meta(credit.data.id))).data.id, post.data.id);
});

test('só entra no ar com vídeo e capa; arquivo que não é vídeo é recusado', async () => {
  fakePlay({ c: 0 });
  const f = await fixture();
  const credit = await f.call('/credits', 0, 'POST', { platform: 'google', token: 'c' });
  const post = await f.call('/posts', 0, 'POST', meta(credit.data.id));
  assert.equal((await f.call(`/posts/${post.data.id}/publish`, 0, 'POST')).status, 400);
  assert.equal((await f.put(`/posts/${post.data.id}/video`, new Uint8Array(64))).status, 200);
  assert.equal((await f.put(`/posts/${post.data.id}/thumb`, new Uint8Array([255]))).status, 200);
  assert.equal((await f.call(`/posts/${post.data.id}/publish`, 0, 'POST')).status, 400);
  assert.equal((await f.call('/feed', 1)).data.items.length, 0);
  assert.equal((await f.put(`/posts/${post.data.id}/video`, video(), 1)).status, 404);
});

test('publica na hora, todo mundo vê sem conta, e o vídeo atende Range', async () => {
  fakePlay({ 'pago-0': 0 });
  const f = await fixture();
  const id = await posted(f);
  const feed = await f.call('/feed', null);
  assert.equal(feed.data.items[0].id, id);
  assert.equal(feed.data.items[0].author.username, 'person_0');
  const req = new Request(`https://x.dev/api/mural/media/${id}/video`, { headers: { range: 'bytes=4-7' } });
  const r = await muralRoute(req, f.env, {}, new URL(req.url));
  assert.equal(r.status, 206);
  assert.equal(r.headers.get('content-range'), 'bytes 4-7/64');
  assert.equal(new TextDecoder().decode(await r.arrayBuffer()), 'ftyp');
});

test('três denúncias tiram do Mural para os outros, mas não para o autor', async () => {
  fakePlay({ 'pago-0': 0 });
  const f = await fixture();
  const id = await posted(f);
  for (const user of [1, 2, 3]) assert.equal((await f.community('/reports', user, 'POST', { kind: 'mural', id, reason: 'spam' })).status, 201);
  assert.equal((await f.call('/feed', 4)).data.items.length, 0);
  assert.equal((await f.call('/feed', null)).data.items.length, 0);
  assert.equal((await f.call('/feed', 0)).data.items.length, 1);
  const req = new Request(`https://x.dev/api/mural/media/${id}/video`);
  assert.equal((await muralRoute(req, f.env, {}, new URL(req.url))).status, 404);
});

test('só o autor apaga, e o arquivo sai do armazenamento', async () => {
  fakePlay({ 'pago-0': 0 });
  const f = await fixture();
  const id = await posted(f);
  assert.equal((await f.call(`/posts/${id}`, 1, 'DELETE')).status, 403);
  assert.equal((await f.call(`/posts/${id}`, 0, 'DELETE')).status, 200);
  assert.equal(f.files.size, 0);
  assert.equal((await f.call('/feed', null)).data.items.length, 0);
});
