// Loja da Comunidade no runtime de verdade (workerd): D1 e R2 locais.
// Run against `wrangler dev --local --port 8789 --persist-to ../build/community-local`.
// Recusa destino que não seja loopback e nunca fala com o Worker publicado.
import assert from 'node:assert/strict';
import { deflateRawSync } from 'node:zlib';
const root = 'http://127.0.0.1:8789';
let token;
async function call(path, method = 'GET', body) {
  const r = await fetch(root + path, { method, headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined });
  const value = await r.json(); assert.ok(r.ok, `${method} ${path}: ${r.status} ${JSON.stringify(value)}`); return value;
}
async function upload(role, name, bytes) {
  const r = await fetch(`${root}/api/community/store/files?role=${role}&name=${encodeURIComponent(name)}`, {
    method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-length': String(bytes.length) }, body: bytes });
  const value = await r.json(); assert.equal(r.status, 201, JSON.stringify(value)); return value;
}
function zip(entries) {
  const locals = [], centrals = []; let offset = 0;
  for (const [name, content] of entries) {
    const raw = Buffer.from(content), data = deflateRawSync(raw), n = Buffer.from(name);
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(8, 8);
    local.writeUInt32LE(data.length, 18); local.writeUInt32LE(raw.length, 22); local.writeUInt16LE(n.length, 26);
    const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(8, 10);
    central.writeUInt32LE(data.length, 20); central.writeUInt32LE(raw.length, 24); central.writeUInt16LE(n.length, 28); central.writeUInt32LE(offset, 42);
    locals.push(local, n, data); centrals.push(central, n); offset += 30 + n.length + data.length;
  }
  const dir = Buffer.concat(centrals), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(dir.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, dir, end]);
}
token = (await call('/api/auth/signup', 'POST', { email: `store-${crypto.randomUUID()}@example.com`, password: 'Local-Only-' + crypto.randomUUID() })).token;
await call('/api/community/me', 'PUT', { username: 'store_' + crypto.randomUUID().slice(0, 8), name: 'Store integration', bio: '' });
const pluginId = `com.local.glow${Date.now()}`;
const pkgBytes = zip([['plugin.json', JSON.stringify({ aureaPlugin: 2, id: pluginId, version: '1.0.0', name: 'Glow', kind: 'shader', permissions: { network: ['api.example.com'] } })], ['shader.frag', 'void main(){}']]);
const pkg = await upload('package', 'glow.aureaplugin', pkgBytes);
assert.equal(pkg.meta.pluginId, pluginId);   // DecompressionStream('deflate-raw') no workerd
assert.deepEqual(pkg.meta.permissions.network, ['api.example.com']);
const photo = await upload('photo', 'sample.png', Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64'));
const { item } = await call('/api/community/store', 'POST', { package: pkg.id, name: 'Glow', description: 'Local', tutorialUrl: 'https://example.com/t', photos: [photo.id] });
assert.equal(item.status, 'pending');
assert.equal((await call('/api/community/store?mine=1')).items[0].id, item.id);
assert.deepEqual(Buffer.from(await (await fetch(`${root}/api/community/store/${item.id}/package`, { headers: { authorization: `Bearer ${token}` } })).arrayBuffer()), pkgBytes);
const image = await fetch(`${root}/api/community/store/${item.id}/photos/0`, { headers: { authorization: `Bearer ${token}` } });
assert.equal(image.headers.get('content-type'), 'image/png');
const preset = await upload('package', 'pop.json', Buffer.from(JSON.stringify({ aurea_preset: 1, kind: 'animation', name: 'Pop' })));
const p = await call('/api/community/store', 'POST', { package: preset.id, name: 'Pop in' });
assert.equal(p.item.status, 'approved');
await call(`/api/community/store/${item.id}`, 'DELETE');
await call(`/api/community/store/${p.item.id}`, 'DELETE');
console.log('PASS: workerd + D1/R2 locais: plugin.json lido do ZIP (deflate), fotos, pacote, pendente/aprovado e exclusão.');
