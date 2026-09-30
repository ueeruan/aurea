// Run against `wrangler dev --local --port 8789 --persist-to ../build/community-local`.
// This script refuses a non-loopback destination and never contacts the live Worker.
import assert from 'node:assert/strict';
const root = 'http://127.0.0.1:8789';
const email = `community-${crypto.randomUUID()}@example.com`;
let token;
async function call(path, method = 'GET', body) {
  const r = await fetch(root + path, { method, headers: { ...(token ? { authorization: `Bearer ${token}` } : {}), ...(body ? { 'content-type': 'application/json' } : {}) }, body: body ? JSON.stringify(body) : undefined });
  const value = await r.json(); assert.ok(r.ok, `${method} ${path}: ${r.status} ${JSON.stringify(value)}`); return value;
}
const signup = await call('/api/auth/signup', 'POST', { email, password: 'Local-Only-' + crypto.randomUUID() });
token = signup.token;
const account = await call('/api/community/me');
assert.equal(account.canVerify, false); assert.equal(account.profile.username, '');
const name = 'local_' + crypto.randomUUID().slice(0, 8);
await call('/api/community/me', 'PUT', { username: name, name: 'Local integration', bio: 'Local fixture' });
async function upload(kind, filename, bytes) {
  const r = await fetch(`${root}/api/community/assets?kind=${kind}&name=${encodeURIComponent(filename)}`, {
    method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-length': String(bytes.length) }, body: bytes,
  });
  const value = await r.json(); assert.equal(r.status, 201, JSON.stringify(value)); return value;
}
const image = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=', 'base64');
const avatar = await upload('avatar', 'avatar.png', image);
await call('/api/community/me', 'PUT', { username: name, name: 'Local integration', bio: 'Local fixture', avatar: avatar.id });
assert.deepEqual(Buffer.from(await (await fetch(root + '/api/community/assets/' + avatar.id)).arrayBuffer()), image);
const presetBytes = Buffer.from(JSON.stringify({ aurea_preset: 1, kind: 'animation', name: 'Local motion', transform: {} }));
const preset = await upload('preset', 'motion.json', presetBytes);
const post = await call('/api/community/posts', 'POST', { body: 'A local-only preset', asset: preset.id });
await call(`/api/community/posts/${post.id}/like`, 'PUT');
await call(`/api/community/posts/${post.id}/comments`, 'POST', { body: 'Local comment' });
const feed = await call('/api/community/posts');
assert.equal(feed.items[0].author.username, name); assert.equal(feed.items[0].likes, 1); assert.equal(feed.items[0].comments, 1);
assert.deepEqual(Buffer.from(await (await fetch(root + '/api/community/assets/' + preset.id)).arrayBuffer()), presetBytes);
// Exercises FixedLengthStream in workerd, rather than Node's fallback stream.
const projectBytes = Buffer.alloc(1024 * 1024, 42); projectBytes.set([80, 75, 3, 4]);
const project = await upload('project', 'fixture.aureaproj', projectBytes);
const projectPost = await call('/api/community/posts', 'POST', { body: 'Local binary stream', asset: project.id });
assert.deepEqual(Buffer.from(await (await fetch(root + '/api/community/assets/' + project.id)).arrayBuffer()), projectBytes);
await call(`/api/community/posts/${projectPost.id}`, 'DELETE');
await call(`/api/community/posts/${post.id}`, 'DELETE');
console.log('PASS: actual Workers runtime, local D1 account/session/profile, R2 avatar/preset/project round trips, posts, likes, comments and deletion.');
