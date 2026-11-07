import test from 'node:test';
import assert from 'node:assert/strict';
import { deflateRawSync } from 'node:zlib';
import { communityRoute } from '../community.js';
import { readPluginManifest, pluginMeta } from '../store.js';
import { sha256hex } from '../contas.js';
import { d1Falso, kvFalso, pedido } from './falsos.mjs';

// ZIP mínimo (local headers + diretório central), com ou sem deflate.
function zip(entries, deflate = true) {
  const locals = [], centrals = []; let offset = 0;
  for (const [name, content] of entries) {
    const raw = Buffer.from(content), data = deflate ? deflateRawSync(raw) : raw, n = Buffer.from(name);
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4);
    local.writeUInt16LE(deflate ? 8 : 0, 8); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(raw.length, 22); local.writeUInt16LE(n.length, 26);
    const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6);
    central.writeUInt16LE(deflate ? 8 : 0, 10); central.writeUInt32LE(data.length, 20); central.writeUInt32LE(raw.length, 24);
    central.writeUInt16LE(n.length, 28); central.writeUInt32LE(offset, 42);
    locals.push(local, n, data); centrals.push(central, n); offset += 30 + n.length + data.length;
  }
  const dir = Buffer.concat(centrals), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(dir.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, dir, end]);
}
const manifest = (extra = {}) => JSON.stringify({ aureaPlugin: 2, id: 'com.studio.glow', version: '1.0.0', name: { 'pt-BR': 'Brilho', en: 'Glow' }, kind: 'shader', ...extra });
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64');

async function fixture() {
  const env = { AUREA_DB: d1Falso(), AUREA_KV: kvFalso() };
  const files = new Map();
  env.COMMUNITY_FILES = {
    async put(id, data) { files.set(id, data); return { size: data.length }; },
    async get(id) { const b = files.get(id); return b && { body: b, size: b.length }; },
    async delete(id) { files.delete(id); },
  };
  const users = [];
  for (let i = 0; i < 3; i++) {
    const uid = crypto.randomUUID(), token = String(i + 1).repeat(43);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, `p${i}@example.com`, 'x', 1);
    env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)').run(await sha256hex(token), uid, 'x', Date.now(), Date.now() + 3600000);
    if (i === 0) env.AUREA_DB.sqlite.prepare("INSERT INTO account_roles(uid,role) VALUES(?,'community_admin')").run(uid);
    users.push({ uid, token });
  }
  const raw = async (path, user = 1, method = 'GET', body, headers = {}) => {
    const req = pedido('/api/community' + path, { metodo: method, corpo: body, token: users[user].token, cabecalhos: headers });
    return communityRoute(req, env, {}, new URL(req.url));
  };
  const call = async (...a) => { const r = await raw(...a); return { status: r.status, data: await r.json() }; };
  const upload = async (role, name, bytes, user = 1) => {
    const req = new Request(`https://x.test/api/community/store/files?role=${role}&name=${encodeURIComponent(name)}`, {
      method: 'POST', body: bytes, headers: { authorization: `Bearer ${users[user].token}`, 'content-length': String(bytes.length) } });
    const r = await communityRoute(req, env, {}, new URL(req.url));
    return { status: r.status, data: await r.json() };
  };
  for (let i = 0; i < 3; ++i) assert.equal((await call('/me', i, 'PUT', { username: `maker_${i}`, name: `Maker ${i}`, bio: '' })).status, 200);
  return { env, users, call, raw, upload, files };
}

test('the plugin.json is read from stored and deflated packages; unsafe paths and bad manifests fail', async () => {
  for (const deflate of [true, false]) {
    const { manifest: m, names } = await readPluginManifest(new Uint8Array(zip([['shader.frag', 'void main(){}'], ['plugin.json', manifest()]], deflate)));
    const meta = pluginMeta(m, names);
    assert.deepEqual([meta.pluginId, meta.version, meta.name, meta.type, meta.hasScript], ['com.studio.glow', '1.0.0', 'Brilho', 'effect', false]);
  }
  const panel = await readPluginManifest(new Uint8Array(zip([['plugin.json', manifest({ kind: 'panel', script: 'main.js', permissions: { network: ['api.example.com'], clipboard: true } })], ['main.js', '1']])));
  const meta = pluginMeta(panel.manifest, panel.names);
  assert.equal(meta.type, 'plugin'); assert.equal(meta.hasScript, true);
  assert.deepEqual(meta.permissions, { network: ['api.example.com'], clipboard: true, files: false });
  await assert.rejects(readPluginManifest(new Uint8Array(zip([['../plugin.json', manifest()]]))));
  await assert.rejects(readPluginManifest(new Uint8Array(zip([['other.json', '{}']]))));
  assert.throws(() => pluginMeta(JSON.parse(manifest({ id: 'A B' })), []));
  assert.throws(() => pluginMeta(JSON.parse(manifest({ version: '1.0' })), []));
  assert.throws(() => pluginMeta(JSON.parse(manifest({ kind: 'panel', aureaPlugin: 1 })), []));
});

test('a plugin waits for approval; a preset is published at once; photos and tutorial are validated', async () => {
  const { call, raw, upload } = await fixture();
  const pkg = await upload('package', 'glow.aureaplugin', zip([['plugin.json', manifest()]]));
  assert.equal(pkg.status, 201); assert.equal(pkg.data.meta.pluginId, 'com.studio.glow');
  const photo = await upload('photo', 'sample.png', png);
  assert.equal(photo.status, 201);
  assert.equal((await upload('photo', 'fake.png', Buffer.from('not an image'))).status, 400);
  assert.equal((await upload('package', 'bad.aureaplugin', Buffer.from('PK nope'))).status, 400);
  assert.equal((await call('/store', 1, 'POST', { package: pkg.data.id, name: 'Glow', tutorialUrl: 'http://insecure.example' })).status, 400);
  const created = await call('/store', 1, 'POST', { package: pkg.data.id, name: 'Glow', description: 'Soft glow', tutorialUrl: 'https://youtu.be/abc', photos: [photo.data.id] });
  assert.equal(created.status, 201); assert.equal(created.data.item.status, 'pending'); assert.equal(created.data.item.photos, 1);
  const id = created.data.item.id;
  // Pendente: só o dono e o admin veem.
  assert.equal((await call('/store', 2)).data.items.length, 0);
  assert.equal((await call(`/store/${id}`, 2)).status, 404);
  assert.equal((await call('/store?mine=1', 1)).data.items[0].status, 'pending');
  assert.equal((await call('/store?status=pending', 2)).status, 403);
  assert.equal((await call('/store?status=pending', 0)).data.items.length, 1);
  assert.equal((await call(`/store/${id}/review`, 2, 'PUT', { status: 'approved' })).status, 404);
  assert.equal((await call(`/store/${id}/review`, 0, 'PUT', { status: 'approved', note: 'ok' })).data.item.status, 'approved');
  const listed = (await call('/store?type=effect', 2)).data.items;
  assert.equal(listed.length, 1); assert.equal(listed[0].tutorialUrl, 'https://youtu.be/abc'); assert.equal(listed[0].reviewNote, undefined);
  // Baixar conta uma vez por pessoa por dia; a foto sai inline.
  const download = await raw(`/store/${id}/package`, 2);
  assert.equal(download.status, 200); assert.match(download.headers.get('content-disposition'), /^attachment/);
  await raw(`/store/${id}/package`, 2);
  assert.equal((await call(`/store/${id}`, 2)).data.item.downloads, 1);
  const image = await raw(`/store/${id}/photos/0`, 2);
  assert.match(image.headers.get('content-disposition'), /^inline/);
  assert.equal((await raw(`/store/${id}/photos/1`, 2)).status, 404);
  // Preset entra aprovado.
  const preset = await upload('package', 'look.json', Buffer.from(JSON.stringify({ aurea_preset: 1, kind: 'animation', name: 'Pop' })), 2);
  const p = await call('/store', 2, 'POST', { package: preset.data.id, name: 'Pop in' });
  assert.equal(p.data.item.status, 'approved'); assert.equal(p.data.item.type, 'preset');
  assert.equal((await call('/store?type=preset', 1)).data.items.length, 1);
  assert.equal((await call('/store?q=pop', 1)).data.items.length, 1);
});

test('another creator cannot take a plugin id; a new version must be newer and goes back to review', async () => {
  const { call, upload } = await fixture();
  const first = await upload('package', 'a.aureaplugin', zip([['plugin.json', manifest()]]));
  const { data: { item } } = await call('/store', 1, 'POST', { package: first.data.id, name: 'Glow' });
  await call(`/store/${item.id}/review`, 0, 'PUT', { status: 'approved' });
  const copy = await upload('package', 'b.aureaplugin', zip([['plugin.json', manifest()]]), 2);
  assert.equal((await call('/store', 2, 'POST', { package: copy.data.id, name: 'Not mine' })).status, 409);
  const same = await upload('package', 'c.aureaplugin', zip([['plugin.json', manifest()]]));
  assert.equal((await call(`/store/${item.id}`, 1, 'PUT', { package: same.data.id })).status, 409);
  const newer = await upload('package', 'd.aureaplugin', zip([['plugin.json', manifest({ version: '1.1.0' })]]));
  const updated = await call(`/store/${item.id}`, 1, 'PUT', { package: newer.data.id, description: 'v1.1' });
  assert.equal(updated.data.item.version, '1.1.0'); assert.equal(updated.data.item.status, 'pending');
  assert.equal((await call(`/store/${item.id}`, 2, 'PUT', { name: 'Hijack' })).status, 404);
});

test('likes, reports hide an item after three people, and the owner or an admin deletes it', async () => {
  const { call, upload, users, env } = await fixture();
  const preset = await upload('package', 'look.json', Buffer.from(JSON.stringify({ aurea_preset: 1, kind: 'effects', name: 'Look' })));
  const { data: { item } } = await call('/store', 1, 'POST', { package: preset.data.id, name: 'Look' });
  assert.equal(item.type, 'effect');
  assert.equal((await call(`/store/${item.id}/like`, 2, 'PUT')).data.likes, 1);
  assert.equal((await call(`/store/${item.id}/like`, 2, 'PUT')).data.likes, 1);
  assert.equal((await call('/reports', 2, 'POST', { kind: 'store', id: item.id, reason: 'spam' })).status, 201);
  // Mais duas pessoas: some do feed de quem não é o dono.
  for (const extra of ['p3', 'p4']) {
    const uid = crypto.randomUUID(), token = extra.repeat(22).slice(0, 43);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)').run(uid, `${extra}@example.com`, 'x', 1);
    env.AUREA_DB.sqlite.prepare("INSERT INTO community_reports(id,reporter,target_kind,target_id,reason,created_at) VALUES(?,?,'store',?,'spam',1)").run(crypto.randomUUID(), uid, item.id);
  }
  assert.equal((await call('/store', 2)).data.items.length, 0);
  assert.equal((await call('/store', 1)).data.items.length, 1);
  assert.equal((await call(`/store/${item.id}`, 2, 'DELETE')).status, 403);
  assert.equal((await call(`/store/${item.id}`, 0, 'DELETE')).status, 200);
  assert.equal((await call('/store?mine=1', 1)).data.items.length, 0);
  void users;
});
