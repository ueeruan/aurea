// Páginas públicas da Play/App Store: política de privacidade e exclusão de conta.
import test from 'node:test';
import assert from 'node:assert/strict';
import { rotaDePaginas } from '../paginas.js';

const base = 'https://aurea-ai-discovery.aureaapp.workers.dev';
const get = (path, method = 'GET') => rotaDePaginas(new Request(base + path, { method }), new URL(base + path));

test('privacy policy and account deletion pages are served in both languages, with a strict CSP', async () => {
  for (const path of ['/privacidade', '/privacy', '/privacy/']) {
    const r = get(path);
    assert.equal(r.status, 200);
    assert.match(r.headers.get('content-type'), /text\/html/);
    const html = await r.text();
    assert.match(html, /Política de Privacidade/);
    assert.match(html, /Privacy Policy/);
    assert.match(html, /excluir-conta/);
  }
  for (const path of ['/excluir-conta', '/delete-account']) {
    const r = get(path);
    const html = await r.text();
    assert.match(html, /\/api\/auth\/login/);
    assert.match(html, /\/api\/auth\/delete/);
    assert.match(r.headers.get('content-security-policy'), /connect-src 'self'/);
    assert.match(r.headers.get('content-security-policy'), /frame-ancestors 'none'/);
  }
  assert.equal(get('/privacidade', 'POST'), null);
  assert.equal(get('/server'), null);
  assert.equal(await get('/privacy', 'HEAD').text(), '');
});
