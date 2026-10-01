import test from 'node:test';
import assert from 'node:assert/strict';
import worker from '../worker.js';
import { sha256hex, lerSessao } from '../contas.js';
import { chaveDoAparelho } from '../ai_video.js';
import { d1Falso, kvFalso, pedido } from './falsos.mjs';

const ctx = { waitUntil: () => {} };
async function fixture() {
  const env = { AUREA_DB: d1Falso(), AUREA_KV: kvFalso(), AI_DEVICE_HMAC_SECRET: 'test-only-server-secret',
    AI_VIDEO_ENABLED: 'true', AI_VIDEO_COST_PER_JOB_USD: '0.5' };
  const users = [];
  for (let i = 0; i < 2; ++i) {
    const uid = crypto.randomUUID(), token = String(i + 1).repeat(43), hash = await sha256hex(token);
    env.AUREA_DB.sqlite.prepare('INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)')
      .run(uid, `security${i}@example.com`, 'not-a-password', Date.now());
    env.AUREA_DB.sqlite.prepare('INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)')
      .run(hash, uid, 'untrusted-old-email@example.com', Date.now(), Date.now() + 86400000);
    users.push({ uid, token, hash });
  }
  const jobs = new Map();
  env.COFRE_DE_VIDEO = { idFromName: n => n, get: () => ({
    job: async ({ jobId }) => jobs.get(jobId), cota: async () => ({ used: 0, limit: 5, remaining: 5 }),
  }) };
  const call = (path, options) => worker.fetch(pedido(path, options), env, ctx);
  return { env, users, jobs, call };
}

test('security: expired and revoked D1 sessions never fall back to a stale KV session', async () => {
  for (const expires of [0, Date.now() - 1]) {
    const { env, users: [u], call } = await fixture();
    await env.AUREA_KV.put('sess:' + u.hash, JSON.stringify({ uid: u.uid, email: 'spoof@example.com', criado: Date.now() }));
    env.AUREA_DB.sqlite.prepare('UPDATE sessions SET expires_at=? WHERE token_hash=?').run(expires, u.hash);
    assert.equal((await call('/api/auth/session', { token: u.token })).status, 401);
  }
});

test('security: logout revokes access even when distributed KV deletion fails', async () => {
  const { env, users: [u], call } = await fixture();
  await env.AUREA_KV.put('sess:' + u.hash, JSON.stringify({ uid: u.uid, criado: Date.now() }));
  env.AUREA_KV.delete = async () => { throw new Error('unavailable'); };
  assert.equal((await call('/api/auth/logout', { metodo: 'POST', token: u.token })).status, 200);
  assert.equal((await call('/api/auth/session', { token: u.token })).status, 401);
  assert.equal(env.AUREA_DB.sqlite.prepare('SELECT expires_at FROM sessions WHERE token_hash=?').get(u.hash).expires_at, 0);
});

test('security: legacy migration validates the real account, date and concurrent revocation', async () => {
  const { env, users: [u] } = await fixture();
  env.AUREA_DB.sqlite.prepare('DELETE FROM sessions').run();
  const legacy = { uid: u.uid, email: 'spoof@example.com', criado: Date.now() };
  await env.AUREA_KV.put('sess:' + u.hash, JSON.stringify(legacy));
  const request = pedido('/api/auth/session', { token: u.token });
  assert.equal((await lerSessao(env, request)).email, 'security0@example.com');
  env.AUREA_DB.sqlite.prepare('DELETE FROM sessions').run();
  env.AUREA_KV.get = async () => {
    env.AUREA_DB.sqlite.prepare('INSERT OR IGNORE INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,0)')
      .run(u.hash, u.uid, legacy.email, legacy.criado);
    return legacy;
  };
  assert.equal(await lerSessao(env, request), null);
});

test('security: storage failures deny login and existing sessions without leaking errors', async () => {
  const { env, users: [u], call } = await fixture();
  await env.AUREA_KV.put('sess:' + u.hash, JSON.stringify({ uid: u.uid, criado: Date.now() }));
  env.AUREA_DB = { prepare: () => { throw new Error('SQL secret=never-leak'); } };
  for (const [path, options] of [
    ['/api/auth/session', { token: u.token }],
    ['/api/auth/login', { metodo: 'POST', corpo: { email: 'security0@example.com', password: 'secret' } }],
  ]) {
    const r = await call(path, options);
    assert.equal(r.status, 503);
    assert.equal(await r.text(), '{"error":"servico_indisponivel"}');
    assert.equal(r.headers.get('cache-control'), 'no-store');
  }
});

test('security: sessions cannot exceed 30 days or belong to a deleted account', async () => {
  const { env, users: [u], call } = await fixture();
  env.AUREA_DB.sqlite.prepare('UPDATE sessions SET created_at=? WHERE token_hash=?').run(Date.now() - 31 * 86400000, u.hash);
  assert.equal((await call('/api/auth/session', { token: u.token })).status, 401);
  env.AUREA_DB.sqlite.prepare('UPDATE sessions SET created_at=? WHERE token_hash=?').run(Date.now(), u.hash);
  env.AUREA_DB.sqlite.prepare('DELETE FROM users WHERE id=?').run(u.uid);
  assert.equal((await call('/api/auth/session', { token: u.token })).status, 401);
});

test('security: a missing rate counter is a failed check, not an allowed login', async () => {
  const { env, call } = await fixture();
  env.AUREA_DB = { prepare: () => ({ bind: () => ({ first: async () => null }) }) };
  assert.equal((await call('/api/auth/login', { metodo: 'POST', corpo: { email: 'a@example.com', password: 'password' } })).status, 503);
});

test('security: changing an email to the owner email never grants administrator rights', async () => {
  const { env, users: [u], call } = await fixture();
  env.AUREA_DB.sqlite.prepare('UPDATE users SET email=? WHERE id=?').run('ruanpablombl@gmail.com', u.uid);
  const r = await call('/api/community/me', { token: u.token });
  assert.equal(r.status, 200);
  assert.equal((await r.json()).canVerify, false);
});

test('security: copying a device ID cannot read, cancel or download another account AI job', async () => {
  const { env, users: [owner, other], jobs, call } = await fixture();
  const jobId = 'private-job-0123456789';
  jobs.set(jobId, { jobId, aparelho: await chaveDoAparelho(env.AI_DEVICE_HMAC_SECRET, 'account-v1', owner.uid),
    estado: 'COMPLETED', criado: Date.now() - 1000, fim: Date.now(), saida: 'https://cdn.example.com/private.mp4' });
  const headers = { 'x-aurea-device': 'copied-device-0123456789', 'x-aurea-platform': 'android' };
  for (const [tail, metodo] of [['', 'GET'], ['/cancel', 'POST'], ['/video', 'GET']]) {
    const path = '/api/ai/video/jobs/' + jobId + tail;
    assert.equal((await call(path, { metodo, cabecalhos: headers })).status, 401);
    assert.equal((await call(path, { metodo, cabecalhos: headers, token: other.token })).status, 404);
  }
  assert.equal((await call('/api/ai/video/jobs/' + jobId, { cabecalhos: headers, token: owner.token })).status, 200);
  // A different device on the same authenticated account retains ownership.
  headers['x-aurea-device'] = 'another-device-0123456789'; headers['x-aurea-platform'] = 'ios';
  assert.equal((await call('/api/ai/video/jobs/' + jobId, { cabecalhos: headers, token: owner.token })).status, 200);
});

test('security: streaming upload without Content-Length stops at the byte limit', async () => {
  const { env, users: [u] } = await fixture();
  let reads = 0, cancelled = false;
  const body = new ReadableStream({ pull(c) { ++reads; c.enqueue(new Uint8Array(1024 * 1024)); }, cancel() { cancelled = true; } });
  const r = await worker.fetch(new Request('https://local/api/ai/video/images', { method: 'POST', duplex: 'half', body,
    headers: { authorization: 'Bearer ' + u.token, 'x-aurea-device': 'device-0123456789', 'x-aurea-platform': 'ios' } }), env, ctx);
  assert.equal(r.status, 413); assert.ok(cancelled); assert.ok(reads <= 10);
  assert.equal([...env.AUREA_KV.mapa.keys()].filter(k => k.startsWith('img:')).length, 0);
});

test('security: plaintext requests are rejected before credentials or payloads are processed', async () => {
  const { env } = await fixture();
  const r = await worker.fetch(new Request('http://aurea.example/api/auth/login', { method: 'POST' }), env, ctx);
  assert.equal(r.status, 403); assert.equal((await r.json()).error, 'https_obrigatorio');
});
