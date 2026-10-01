import test from 'node:test';
import assert from 'node:assert/strict';
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
