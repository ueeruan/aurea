// Run against `wrangler dev --local --port 8789 --persist-to ../build/community-local`.
// This script refuses a non-loopback destination and never contacts the live Worker.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
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
  const route = ['image', 'video'].includes(kind) ? 'media' : 'assets';
  const r = await fetch(`${root}/api/community/${route}?kind=${kind}&name=${encodeURIComponent(filename)}`, {
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
const owner = token;
const photo = await upload('image', 'asset.png', image);
assert.equal((await fetch(root + '/api/community/media/' + photo.id)).status, 404);
const photoPost = await call('/api/community/posts', 'POST', { body: 'Local photo', media: photo.id });
assert.deepEqual(Buffer.from(await (await fetch(root + '/api/community/media/' + photo.id)).arrayBuffer()), image);
const bundled = await call('/api/community/me?includeFeed=1');
assert.ok(bundled.feed.items.some(p => p.id === photoPost.id && p.media.kind === 'image'));
if (process.env.AUREA_COMMUNITY_TEST_VIDEO) {
  const bytes = await readFile(process.env.AUREA_COMMUNITY_TEST_VIDEO);
  const video = await upload('video', 'seek.mp4', bytes);
  const videoPost = await call('/api/community/posts', 'POST', { body: '', media: video.id });
  const part = await fetch(root + '/api/community/media/' + video.id, { headers: { Range: 'bytes=100-200' } });
  assert.equal(part.status, 206);
  assert.deepEqual(Buffer.from(await part.arrayBuffer()), bytes.subarray(100, 201));
  await call(`/api/community/posts/${videoPost.id}`, 'DELETE');
  assert.equal((await fetch(root + '/api/community/media/' + video.id)).status, 404);
}
token = (await call('/api/auth/signup', 'POST', {
  email: `reply-${crypto.randomUUID()}@example.com`, password: 'Local-Only-' + crypto.randomUUID(),
})).token;
await call('/api/community/me', 'PUT', { username: 'reply_' + crypto.randomUUID().slice(0, 8), name: 'Local commenter', bio: '' });
const thread = `/api/community/posts/${photoPost.id}/comments`;
const parent = (await call(thread, 'POST', { body: 'Local image comment' })).id;
const commenter = token;
token = owner;
const activity = await call('/api/community/notifications');
assert.ok(activity.items.some(n => n.id === parent && n.kind === 'comment' && n.unread));
await call('/api/community/notifications/read', 'PUT', { through: activity.through });
assert.equal((await call('/api/community/notifications?summary=1')).unread, 0);
const reply = (await call(thread, 'POST', { body: 'Local reply', replyTo: parent })).id;
token = commenter;
assert.ok((await call('/api/community/notifications')).items.some(n => n.id === reply && n.kind === 'reply'));
assert.ok((await call(thread)).items.some(c => c.id === reply && c.replyTo === parent));
token = owner;
await call(`/api/community/posts/${photoPost.id}`, 'DELETE');
console.log('PASS: local workerd, D1, R2, compact feed, photos, media access, personal notifications and replies.');
