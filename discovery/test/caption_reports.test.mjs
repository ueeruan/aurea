// Denúncia de presets de legenda (Google Play: conteúdo gerado pelo usuário),
// com o Durable Object rodando sobre um SQLite em memória (sem wrangler dev).
import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { CaptionCommunity } from '../caption_community.js';

const part = (kind, fields) => JSON.stringify({ aurea_preset: 1, kind, ...fields });
const preset = (name) => ({ schema: 1, minAppVersion: 2113, name, width: 1080, height: 1920,
  caption: part('caption', { caption: { mode: 0, style: 0, maxWords: 5, maxChars: 32, maxLines: 2 } }),
  text: part('text', { style: { size: 70, color: [1, 1, 1, 1] }, animators: [] }),
  effects: '', animation: '', transform: [.5, .8, 0, 1, 1, 1, 0, 0, 0, 1] });

function durableObject() {
  const db = new DatabaseSync(':memory:');
  const sql = {
    exec(query, ...params) {
      if (!params.length && /;\s*\S/.test(query.trim().replace(/;\s*$/, ''))) { db.exec(query); return { toArray: () => [] }; }
      const statement = db.prepare(query);
      const rows = /^\s*SELECT/i.test(query) ? statement.all(...params) : (statement.run(...params), []);
      return { toArray: () => rows.map(r => ({ ...r })) };
    },
  };
  const ctx = { storage: { sql, transactionSync: (fn) => fn() }, waitUntil: () => {} };
  return new CaptionCommunity(ctx, {});
}

let ip = 0;
const call = (dobj, path, { method = 'GET', token, body } = {}) => dobj.fetch(new Request('https://x.dev/api/captions' + path, {
  method, body: body === undefined ? undefined : JSON.stringify(body),
  headers: { 'content-type': 'application/json', 'CF-Connecting-IP': `203.0.113.${++ip % 250}`, ...(token ? { authorization: `Bearer ${token}` } : {}) },
}));

test('three different sessions reporting a caption preset hide it from the list; repeats do not count', async () => {
  const dobj = durableObject();
  const sessions = [];
  for (let i = 0; i < 4; ++i) sessions.push((await (await call(dobj, '/session', { method: 'POST' })).json()).token);
  const published = await call(dobj, '/presets', { method: 'POST', token: sessions[0], body: { preset: preset('Pop'), author: 'Ana' } });
  assert.equal(published.status, 201);
  const { id } = await published.json();
  const list = async () => (await (await call(dobj, '/presets')).json()).items.map(p => p.id);
  assert.deepEqual(await list(), [id]);
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', token: sessions[1], body: { reason: 'bogus' } })).status, 400);
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', body: { reason: 'spam' } })).status, 401);
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', token: sessions[1], body: { reason: 'spam' } })).status, 201);
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', token: sessions[1], body: { reason: 'spam' } })).status, 200);
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', token: sessions[2], body: { reason: 'hate', details: 'x' } })).status, 201);
  assert.deepEqual(await list(), [id]);   // 2 sessões: ainda aparece
  assert.equal((await call(dobj, `/presets/${id}/report`, { method: 'POST', token: sessions[3], body: { reason: 'copyright' } })).status, 201);
  assert.deepEqual(await list(), []);     // 3 sessões: escondido
  assert.equal((await call(dobj, `/presets/${crypto.randomUUID()}/report`, { method: 'POST', token: sessions[1], body: { reason: 'spam' } })).status, 404);
});
