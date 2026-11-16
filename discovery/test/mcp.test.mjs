// AureaMCP: JSON-RPC, tokens, OAuth + pareamento, relay (Durable Object) e o AureaOS.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";

import worker from "../worker.js";
import { AureaMcpRelay } from "../mcp_relay.js";
import { forgetBrainCache, beats, fxEntries, page, PAGE } from "../aureaos.js";
import { engineToolsToMcp, deviceTools, DEFAULT_ENGINE_TOOLS } from "../mcp_catalog.js";
import { normalizarCodigo, novoCodigo, redirectValido } from "../mcp_auth.js";
import { buildBundle, writeBundle } from "../scripts/bundle-aureaos.mjs";
import { sha256hex } from "../contas.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

// --- falsos do Durable Object -------------------------------------------------------
function storageFalso() {
  const m = new Map();
  return {
    mapa: m,
    async get(k) { return structuredClone(m.get(k)); },
    async put(k, v) { m.set(k, structuredClone(v)); },
    async delete(k) { for (const x of Array.isArray(k) ? k : [k]) m.delete(x); },
    async list({ prefix = "" } = {}) { return new Map([...m].filter(([k]) => k.startsWith(prefix)).map(([k, v]) => [k, structuredClone(v)])); },
  };
}
function ctxFalso() {
  const sockets = [];
  return {
    storage: storageFalso(),
    acceptWebSocket(ws, tags) { ws.tags = tags; sockets.push(ws); },
    getWebSockets(tag) { return sockets.filter((w) => w.readyState === 1 && (!tag || w.tags.includes(tag))); },
    setWebSocketAutoResponse() {},
  };
}
class WsFalso {
  constructor() { this.sent = []; this.readyState = 1; this.att = null; this.onCall = null; }
  send(m) { const v = typeof m === "string" && m.startsWith("{") ? JSON.parse(m) : m; this.sent.push(v); if (v?.t === "call" && this.onCall) queueMicrotask(() => this.onCall(v)); }
  serializeAttachment(a) { this.att = structuredClone(a); }
  deserializeAttachment() { return structuredClone(this.att); }
  close() { this.readyState = 3; }
}

function ambiente() {
  forgetBrainCache();
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso() };
  const relays = new Map();
  env.AUREA_MCP = {
    idFromName: (n) => n,
    get: (id) => { if (!relays.has(id)) relays.set(id, new AureaMcpRelay(ctxFalso(), env)); return relays.get(id); },
  };
  const files = new Map();
  env.COMMUNITY_FILES = {
    async put(k, v) { files.set(k, v); },
    async get(k) { return files.has(k) ? { body: files.get(k), async text() { return String(files.get(k)); } } : null; },
    async delete(k) { files.delete(k); },
  };
  return { env, relays, files };
}
const ctx = { waitUntil: (p) => p };

async function conta(env, i = 1) {
  const uid = crypto.randomUUID(), token = String(i).repeat(43);
  env.AUREA_DB.sqlite.prepare("INSERT INTO users(id,email,password_hash,created_at) VALUES(?,?,?,?)").run(uid, `p${i}@example.com`, "x", 1);
  env.AUREA_DB.sqlite.prepare("INSERT INTO sessions(token_hash,uid,email,created_at,expires_at) VALUES(?,?,?,?,?)").run(await sha256hex(token), uid, "x", Date.now(), Date.now() + 3600000);
  return { uid, sessao: token };
}

const chamar = async (env, caminho, opcoes) => {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  const t = await r.text();
  let corpo = null; try { corpo = JSON.parse(t); } catch { corpo = t; }
  return { status: r.status, corpo, h: r.headers };
};
async function patDe(env, sessao) {
  const r = await chamar(env, "/api/mcp/tokens", { metodo: "POST", corpo: { name: "Claude Code" }, token: sessao });
  assert.equal(r.status, 201);
  assert.match(r.corpo.token, /^amcp_[A-Za-z0-9_-]{43}$/);
  return r.corpo.token;
}
function mcp(env, token, sessionId) {
  let n = 0;
  return async (method, params) => {
    const h = { authorization: `Bearer ${token}`, accept: "application/json, text/event-stream" };
    if (sessionId) h["mcp-session-id"] = sessionId;
    const r = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", id: ++n, method, params }, cabecalhos: h });
    return r;
  };
}
async function iniciar(env, token) {
  const r = await mcp(env, token)("initialize", { protocolVersion: "2025-06-18", clientInfo: { name: "claude-code", version: "2.0" }, capabilities: {} });
  assert.equal(r.status, 200);
  const sid = r.h.get("mcp-session-id");
  assert.match(sid, /^[0-9a-f]{32}$/);
  return { call: mcp(env, token, sid), init: r.corpo.result, sid };
}
function app(env, uid, { permissions = {}, device = "phone-0001" } = {}) {
  const relay = env.AUREA_MCP.get("mcp:" + uid);
  const ws = new WsFalso();
  relay.acceptSocket(ws, device);
  return {
    ws, relay,
    hello: (extra = {}) => relay.webSocketMessage(ws, JSON.stringify({ t: "hello", device: { name: "Pixel", platform: "android", app: "0.5.3" }, engineTools: DEFAULT_ENGINE_TOOLS, hostTools: ["list_projects", "delete_project", "render_frame", "export_video"], permissions, project: { title: "Reel", open: true }, ...extra })),
    answer: (fn) => { ws.onCall = (c) => relay.webSocketMessage(ws, JSON.stringify(fn(c))); },
  };
}

// --- descoberta e 401 -------------------------------------------------------------------
test("descoberta OAuth (RFC 9728/8414) e 401 com WWW-Authenticate apontando para ela", async () => {
  const { env } = ambiente();
  const pr = await chamar(env, "/.well-known/oauth-protected-resource");
  assert.equal(pr.status, 200);
  assert.equal(pr.corpo.resource, "https://aurea-ai-discovery.aureaapp.workers.dev/mcp");
  const as = await chamar(env, "/.well-known/oauth-authorization-server");
  assert.deepEqual(as.corpo.code_challenge_methods_supported, ["S256"]);
  assert.ok(as.corpo.registration_endpoint.endsWith("/mcp/oauth/register"));
  const r = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", id: 1, method: "tools/list" } });
  assert.equal(r.status, 401);
  assert.match(r.h.get("www-authenticate"), /resource_metadata="https:\/\/aurea-ai-discovery\.aureaapp\.workers\.dev\/\.well-known\/oauth-protected-resource"/);
  assert.equal((await chamar(env, "/mcp", { cabecalhos: { authorization: "Bearer amcp_" + "x".repeat(43) } })).status, 405, "GET /mcp: sem stream do servidor");
});

test("kill switch: AUREAMCP_ENABLED=false desliga /mcp e o OAuth", async () => {
  const { env } = ambiente();
  env.AUREAMCP_ENABLED = "false";
  assert.equal((await chamar(env, "/mcp", { metodo: "POST", corpo: {} })).status, 503);
  assert.equal((await chamar(env, "/.well-known/oauth-authorization-server")).status, 503);
});

// --- token pessoal + JSON-RPC -----------------------------------------------------------
test("token pessoal: só o hash fica no D1; initialize cria sessão; tools/list sem app usa o catálogo padrão", async () => {
  const { env } = ambiente();
  const { sessao } = await conta(env);
  const pat = await patDe(env, sessao);
  const linhas = env.AUREA_DB.sqlite.prepare("SELECT * FROM mcp_tokens").all();
  assert.equal(linhas.length, 1);
  assert.ok(!JSON.stringify(linhas).includes(pat), "token cru nunca é gravado");
  assert.equal(linhas[0].token_hash, await sha256hex(pat));

  const { call, init } = await iniciar(env, pat);
  assert.equal(init.protocolVersion, "2025-06-18");
  assert.equal(init.serverInfo.name, "aureamcp");
  assert.match(init.instructions, /aureaos_metodo/);
  const lista = (await call("tools/list")).corpo.result.tools.map((t) => t.name);
  for (const n of ["aurea_status", "get_project_state", "run_ae_script", "list_projects", "export_video", "aureaos_search", "search", "fetch"]) assert.ok(lista.includes(n), n);
  assert.ok(!lista.includes("list_device_media"), "galeria do aparelho fica de fora sem permissão");

  const st = (await call("tools/call", { name: "aurea_status", arguments: {} })).corpo.result;
  assert.match(st.content[0].text, /No Aurea app is online/);
  const off = (await call("tools/call", { name: "get_project_state", arguments: {} })).corpo.result;
  assert.equal(off.isError, true);
  assert.match(off.content[0].text, /offline/);

  // notificação → 202; método desconhecido → -32601; sessão inventada → 404
  const no = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", method: "notifications/initialized" }, cabecalhos: { authorization: `Bearer ${pat}` } });
  assert.equal(no.status, 202);
  assert.equal((await call("nada/disso")).corpo.error.code, -32601);
  const fantasma = await mcp(env, pat, "0".repeat(32))("tools/list");
  assert.equal(fantasma.status, 404);
  assert.equal((await chamar(env, "/mcp", { metodo: "POST", corpo: [{ jsonrpc: "2.0", id: 1, method: "ping" }], cabecalhos: { authorization: `Bearer ${pat}` } })).status, 400);
});

test("revogar o token derruba o acesso e a sessão; outra conta não enxerga o token", async () => {
  const { env } = ambiente();
  const a = await conta(env, 1), b = await conta(env, 2);
  const pat = await patDe(env, a.sessao);
  const { sid } = await iniciar(env, pat);
  const lista = await chamar(env, "/api/mcp/tokens", { token: a.sessao });
  assert.equal(lista.corpo.tokens.length, 1);
  assert.equal((await chamar(env, "/api/mcp/tokens", { token: b.sessao })).corpo.tokens.length, 0);
  const id = lista.corpo.tokens[0].id;
  assert.equal((await chamar(env, "/api/mcp/tokens/revoke", { metodo: "POST", corpo: { id }, token: b.sessao })).corpo.revoked, 0, "outra conta não revoga");
  assert.equal((await chamar(env, "/api/mcp/tokens/revoke", { metodo: "POST", corpo: { id }, token: a.sessao })).corpo.revoked, 1);
  assert.equal((await mcp(env, pat, sid)("tools/list")).status, 401);
  assert.equal(await env.AUREA_MCP.get("mcp:" + a.uid).ctx.storage.get(`sess:${sid}`), undefined, "sessão derrubada no relay");
});

// --- relay ---------------------------------------------------------------------------------
test("relay: a chamada vai ao app e volta; permissões desligadas somem do tools/list e são recusadas", async () => {
  const { env } = ambiente();
  const { uid, sessao } = await conta(env);
  const { call } = await iniciar(env, await patDe(env, sessao));
  const a = app(env, uid, { permissions: { edit: false } });
  await a.hello();
  assert.equal(a.ws.sent.find((m) => m.t === "welcome").paused, false);
  a.answer((c) => ({ t: "result", id: c.id, ok: true, content: [{ type: "text", text: JSON.stringify({ ok: true, tool: c.tool, agent: c.agent }) }] }));

  const tools = (await call("tools/list")).corpo.result.tools.map((t) => t.name);
  assert.ok(tools.includes("get_project_state"));
  assert.ok(!tools.includes("run_ae_script"), "edit desligado");
  const r = (await call("tools/call", { name: "get_project_state", arguments: {} })).corpo.result;
  assert.deepEqual(JSON.parse(r.content[0].text), { ok: true, tool: "get_project_state", agent: "claude-code" });
  const bloqueada = (await call("tools/call", { name: "run_ae_script", arguments: { code: "1" } })).corpo.result;
  assert.equal(bloqueada.isError, true);
  assert.match(bloqueada.content[0].text, /turned off/);
  assert.ok(a.ws.sent.some((m) => m.t === "log" && m.tool === "get_project_state" && m.ok === true), "o app recebe a linha do registro");
  assert.ok(a.ws.sent.find((m) => m.t === "welcome").agents.some((x) => x.name === "claude-code"), "o app vê o agente conectado");

  const st = (await call("tools/call", { name: "aurea_status", arguments: {} })).corpo.result;
  assert.match(st.content[0].text, /Pixel \(android, Aurea 0\.5\.3\).*project "Reel"/);
});

test("relay: apagar projeto pede confirmação; pausa, tempo esgotado, uma pesada por vez e limite por minuto", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  const R = a.relay;
  // confirmação: o app recebe confirm:true
  a.answer((c) => ({ t: "result", id: c.id, ok: false, error: c.confirm ? "denied by the person" : "?" }));
  const del = await R.callTool({ tool: "delete_project", args: { id: "x" }, agent: "Claude" });
  assert.equal(del.isError, true);
  assert.match(del.content[0].text, /denied/);
  // tempo esgotado (app mudo)
  a.ws.onCall = null;
  const mudo = await R.callTool({ tool: "list_projects", args: {}, timeoutMs: 20 });
  assert.match(mudo.content[0].text, /did not answer/);
  assert.ok(a.ws.sent.some((m) => m.t === "cancel"));
  // uma pesada por vez
  const p1 = R.callTool({ tool: "render_frame", args: {}, timeoutMs: 50 });
  const p2 = await R.callTool({ tool: "export_video", args: {}, timeoutMs: 50 });
  assert.match(p2.content[0].text, /busy/);
  await p1;
  // pausa
  await R.webSocketMessage(a.ws, JSON.stringify({ t: "pause" }));
  assert.match((await R.callTool({ tool: "list_projects", args: {} })).content[0].text, /paused/);
  await R.webSocketMessage(a.ws, JSON.stringify({ t: "resume" }));
  // limite por minuto
  R.calls = Array.from({ length: 120 }, () => Date.now());
  assert.match((await R.callTool({ tool: "list_projects", args: {} })).content[0].text, /Too many calls/);
  // app desconectou no meio
  R.calls = [];
  const pendente = R.callTool({ tool: "list_projects", args: {}, timeoutMs: 5000 });
  await new Promise((r) => setTimeout(r, 5));
  await R.webSocketClose(a.ws);
  assert.match((await pendente).content[0].text, /disconnected/);
});

test("relay: o mesmo aparelho reconectando troca o socket; o resultado de outro aparelho é ignorado", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid, { device: "phone-0001" });
  await a.hello();
  const b = app(env, uid, { device: "phone-0001" });
  await b.hello();
  assert.equal(a.ws.readyState, 3, "o socket velho fecha");
  const pc = app(env, uid, { device: "pc-000000001" });
  await pc.hello();
  // o PC (último hello) é o ativo; o celular tenta responder pela chamada dele
  pc.ws.onCall = (c) => { b.relay.webSocketMessage(b.ws, JSON.stringify({ t: "result", id: c.id, ok: true })); };
  const r = await pc.relay.callTool({ tool: "list_projects", args: {}, timeoutMs: 30 });
  assert.match(r.content[0].text, /did not answer/);
  assert.equal((await pc.relay.selectDevice({ device: "phone-0001" })).ok, true);
  assert.equal((await pc.relay.selectDevice({ device: "nao-existe-00" })).ok, false);
});

test("catálogo: ferramentas do motor viram MCP; nomes reservados e esquemas enormes ficam de fora", () => {
  const t = engineToolsToMcp([...DEFAULT_ENGINE_TOOLS, { type: "function", function: { name: "aurea_status", parameters: {} } },
    { type: "function", function: { name: "Bad Name", parameters: {} } }, { type: "function", function: { name: "list_projects", parameters: {} } },
    { type: "function", function: { name: "huge_tool", parameters: { type: "object", description: "x".repeat(20000) } } }]);
  assert.equal(t.length, DEFAULT_ENGINE_TOOLS.length);
  assert.equal(t.find((x) => x.name === "get_project_state").annotations.readOnlyHint, true);
  assert.equal(t.find((x) => x.name === "run_ae_script").capability, "edit");
  assert.ok(deviceTools({ permissions: { device_media: true }, hostTools: ["list_device_media"] }).some((x) => x.name === "list_device_media"));
});

// --- OAuth + pareamento ---------------------------------------------------------------
const b64url = (buf) => Buffer.from(buf).toString("base64url");
test("OAuth 2.1: registro dinâmico → página com código → app aprova → código → token (PKCE) → refresh com rotação e detecção de reuso", async () => {
  const { env } = ambiente();
  const { uid, sessao } = await conta(env);
  const reg = await chamar(env, "/mcp/oauth/register", { metodo: "POST", corpo: { client_name: "Claude", redirect_uris: ["https://claude.ai/api/mcp/auth_callback"] } });
  assert.equal(reg.status, 201);
  const clientId = reg.corpo.client_id;
  assert.equal((await chamar(env, "/mcp/oauth/register", { metodo: "POST", corpo: { redirect_uris: ["http://evil.example/cb"] } })).status, 400);
  assert.equal((await chamar(env, "/mcp/oauth/register", { metodo: "POST", corpo: { redirect_uris: ["https://x.example/cb"], token_endpoint_auth_method: "client_secret_basic" } })).status, 400);

  const verifier = b64url(crypto.getRandomValues(new Uint8Array(32)));
  const challenge = createHash("sha256").update(verifier).digest("base64url");
  const q = new URLSearchParams({ response_type: "code", client_id: clientId, redirect_uri: "https://claude.ai/api/mcp/auth_callback", code_challenge: challenge, code_challenge_method: "S256", state: "st8", resource: "https://aurea-ai-discovery.aureaapp.workers.dev/mcp" });
  const page1 = await worker.fetch(pedido("/mcp/oauth/authorize?" + q, { cabecalhos: { "accept-language": "pt-BR" } }), env, ctx);
  assert.equal(page1.status, 200);
  const html = await page1.text();
  assert.match(html, /Conectar ao Aurea/);
  assert.match(page1.headers.get("content-security-policy"), /frame-ancestors 'none'/);
  const codigo = /id="c">([A-Z0-9-]{9})</.exec(html)[1];
  const reqId = /status\?req=([0-9a-f-]{36})/.exec(html)[1];
  const cookie = /__Host-amcp_auth=([^;]+)/.exec(page1.headers.get("set-cookie"))[1];
  const status = () => chamar(env, `/mcp/oauth/authorize/status?req=${reqId}`, { cabecalhos: { cookie: `__Host-amcp_auth=${cookie}` } });
  assert.equal((await status()).corpo.status, "pending");
  assert.equal((await chamar(env, `/mcp/oauth/authorize/status?req=${reqId}`, { cabecalhos: { cookie: "__Host-amcp_auth=outro" } })).status, 404, "outro navegador não vê");

  // o app (com sessão) consulta e aprova
  const look = await chamar(env, "/api/mcp/pair/lookup", { metodo: "POST", corpo: { code: codigo.toLowerCase() }, token: sessao });
  assert.equal(look.corpo.client, "Claude");
  assert.equal(look.corpo.redirectHost, "claude.ai");
  assert.equal((await chamar(env, "/api/mcp/pair/lookup", { metodo: "POST", corpo: { code: "AAAA-AAAA" }, token: sessao })).status, 404);
  assert.equal((await chamar(env, "/api/mcp/pair/approve", { metodo: "POST", corpo: { code: codigo, allow: true }, token: sessao })).corpo.status, "approved");
  const st = (await status()).corpo;
  assert.equal(st.status, "approved");
  const red = new URL(st.redirect);
  assert.equal(red.origin + red.pathname, "https://claude.ai/api/mcp/auth_callback");
  assert.equal(red.searchParams.get("state"), "st8");
  const code = red.searchParams.get("code");
  assert.equal((await status()).corpo.status, "issued", "o código só sai uma vez");

  const form = (o) => ({ metodo: "POST", corpo: new URLSearchParams(o).toString(), cabecalhos: { "content-type": "application/x-www-form-urlencoded" } });
  const ruim = await chamar(env, "/mcp/oauth/token", form({ grant_type: "authorization_code", code, client_id: clientId, redirect_uri: "https://claude.ai/api/mcp/auth_callback", code_verifier: "x".repeat(43) }));
  assert.equal(ruim.corpo.error, "invalid_grant", "PKCE errado");
  assert.equal((await chamar(env, "/mcp/oauth/token", form({ grant_type: "authorization_code", code, client_id: clientId, code_verifier: verifier }))).corpo.error, "invalid_grant", "código queimado na primeira tentativa");

  // fluxo limpo de novo para obter o token
  const page2 = await worker.fetch(pedido("/mcp/oauth/authorize?" + q), env, ctx);
  const html2 = await page2.text();
  const codigo2 = /id="c">([A-Z0-9-]{9})</.exec(html2)[1], req2 = /status\?req=([0-9a-f-]{36})/.exec(html2)[1];
  const cookie2 = /__Host-amcp_auth=([^;]+)/.exec(page2.headers.get("set-cookie"))[1];
  await chamar(env, "/api/mcp/pair/approve", { metodo: "POST", corpo: { code: codigo2, allow: true }, token: sessao });
  const code2 = new URL((await chamar(env, `/mcp/oauth/authorize/status?req=${req2}`, { cabecalhos: { cookie: `__Host-amcp_auth=${cookie2}` } })).corpo.redirect).searchParams.get("code");
  const tok = await chamar(env, "/mcp/oauth/token", form({ grant_type: "authorization_code", code: code2, client_id: clientId, redirect_uri: "https://claude.ai/api/mcp/auth_callback", code_verifier: verifier }));
  assert.equal(tok.status, 200);
  assert.equal(tok.corpo.token_type, "Bearer");
  const { call } = await iniciar(env, tok.corpo.access_token);
  assert.ok((await call("tools/list")).corpo.result.tools.length > 10);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT uid FROM mcp_tokens WHERE kind='oauth'").get().uid, uid);

  const ref1 = await chamar(env, "/mcp/oauth/token", form({ grant_type: "refresh_token", refresh_token: tok.corpo.refresh_token, client_id: clientId }));
  assert.equal(ref1.status, 200);
  assert.notEqual(ref1.corpo.refresh_token, tok.corpo.refresh_token);
  assert.equal((await mcp(env, tok.corpo.access_token)("ping")).status, 401, "o access antigo cai na rotação");
  assert.equal((await mcp(env, ref1.corpo.access_token)("ping")).status, 200);
  // reuso do refresh antigo: a concessão inteira cai
  assert.equal((await chamar(env, "/mcp/oauth/token", form({ grant_type: "refresh_token", refresh_token: tok.corpo.refresh_token }))).corpo.error, "invalid_grant");
  assert.equal((await mcp(env, ref1.corpo.access_token)("ping")).status, 401);
});

test("OAuth: recusa no app volta ao cliente com access_denied; redirect não registrado não redireciona", async () => {
  const { env } = ambiente();
  const { sessao } = await conta(env);
  const reg = await chamar(env, "/mcp/oauth/register", { metodo: "POST", corpo: { client_name: "ChatGPT", redirect_uris: ["https://chatgpt.com/connector_platform_oauth_redirect"] } });
  const base = { response_type: "code", client_id: reg.corpo.client_id, code_challenge: "a".repeat(43), code_challenge_method: "S256" };
  const fora = await worker.fetch(pedido("/mcp/oauth/authorize?" + new URLSearchParams({ ...base, redirect_uri: "https://evil.example/cb" })), env, ctx);
  assert.equal(fora.status, 400);
  const semPkce = await worker.fetch(pedido("/mcp/oauth/authorize?" + new URLSearchParams({ ...base, code_challenge_method: "plain" })), env, ctx);
  assert.equal(semPkce.status, 302);
  assert.match(semPkce.headers.get("location"), /error=invalid_request/);
  const p = await worker.fetch(pedido("/mcp/oauth/authorize?" + new URLSearchParams(base)), env, ctx);
  const html = await p.text();
  const codigo = /id="c">([A-Z0-9-]{9})</.exec(html)[1], req = /status\?req=([0-9a-f-]{36})/.exec(html)[1];
  const cookie = /__Host-amcp_auth=([^;]+)/.exec(p.headers.get("set-cookie"))[1];
  await chamar(env, "/api/mcp/pair/approve", { metodo: "POST", corpo: { code: codigo, allow: false }, token: sessao });
  const st = (await chamar(env, `/mcp/oauth/authorize/status?req=${req}`, { cabecalhos: { cookie: `__Host-amcp_auth=${cookie}` } })).corpo;
  assert.equal(st.status, "denied");
  assert.match(st.redirect, /error=access_denied/);
});

test("código de pareamento: 8 símbolos sem 0/O/1/I; redirect só https ou loopback", () => {
  for (let i = 0; i < 50; i++) assert.match(novoCodigo(), /^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$/);
  assert.equal(normalizarCodigo("abcd-2345"), "ABCD2345");
  assert.equal(normalizarCodigo("ABCD-0000"), null);
  assert.equal(redirectValido("http://127.0.0.1:3118/callback"), true);
  assert.equal(redirectValido("http://example.com/cb"), false);
  assert.equal(redirectValido("https://a.example/cb#x"), false);
  assert.equal(redirectValido("javascript:alert(1)"), false);
});

test("excluir a conta apaga os tokens do AureaMCP", async () => {
  const { env } = ambiente();
  const { sessao } = await conta(env);
  await patDe(env, sessao);
  const r = await chamar(env, "/api/auth/delete", { metodo: "POST", corpo: { confirm: true }, token: sessao });
  assert.equal(r.status, 200);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM mcp_tokens").get().n, 0);
});

// --- AureaOS ---------------------------------------------------------------------------------
function aureaosFalso() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "aureaos-"));
  const w = (p, t) => { fs.mkdirSync(path.dirname(path.join(root, p)), { recursive: true }); fs.writeFileSync(path.join(root, p), t); };
  w("os/CLAUDE.md", "# AureaOS — regras\nFLOW: nada para seco.");
  w("skills/metodo-aureaos/SKILL.md", "# PADRÃO AureaOS\nmedir tudo");
  w("skills/diretor-aureaos/SKILL.md", "# DIRETOR\ndecidir antes de animar");
  w("os/referencias/CATALOGO.md", "| slug | formato | cenas | resumo |\n|---|---|---|---|\n| [R07_alpha](_lib/R07_alpha/CENAS.md) | 720×480 | 2 | logo reveal tipográfico |");
  w("os/referencias/_lib/R07_alpha/CENAS.md", "# R07\n" + "x".repeat(PAGE + 10));
  w("os/referencias/_lib/R07_alpha/meta.json", JSON.stringify({ fps: 60, size: [720, 480], dur_s: 6, cuts: [50, 85] }));
  w("os/referencias/_lib/R07_alpha/tags.json", JSON.stringify([{ cena: "C1", a: 0, b: 49, tags: ["texto:typewriter", "uso:logo-reveal", "estilo:minimal"], uso: ["nome de marca digitado"], nota: "Letra seca a cada 4 f." }, { cena: "C2", a: 50, b: 84, tags: ["cam:pull-out", "trans:corte-seco"], uso: ["macro"], nota: "Pull-out 0,932 em 35 f." }]));
  w("os/referencias/_lib/R30_blue/meta.json", JSON.stringify({ fps: 30, size: [360, 640], dur_s: 48.8, cuts: [1, 2, 3] }));
  w("os/referencias/_lib/R30_blue/tags.json", JSON.stringify([{ cena: "C1", a: 0, b: 30, tags: ["uso:dados", "texto:contador", "estilo:tech"], uso: ["números"], nota: "Contador sobe." }]));
  w("os/engine/fx.js", "(function(){\n  A.fx = (tl) => {\n    // ACENDER: cinza→branco + blur 4→0 em 6.5f, power2.out.\n    X.lightUp = (el, f) => {\n      tl.to(el, {});\n    };\n    // TROCA de palavra por corte seco.\n    X.wordSwap = (el, list) => {};\n  };\n})();");
  w("os/tools/beats.py", '"""quebra a fala em batidas"""\nprint(1)');
  const bundle = buildBundle({ src: path.join(root, "os"), skills: path.join(root, "skills") });
  const { dir, manifest } = writeBundle(bundle, path.join(root, "out"), "202611140000");
  return { root, bundle, dir, manifest };
}

async function subirCerebro(env, files) {
  const { dir, manifest } = aureaosFalso();
  for (const f of [...manifest.files.map((x) => x.path), "index.json", "manifest.json"]) files.set(`aureamcp/brain/${manifest.version}/${f}`, fs.readFileSync(path.join(dir, f), "utf8"));
  await env.AUREA_KV.put("aureamcp:brain:current", manifest.version);
  forgetBrainCache();
}

test("bundle-aureaos: monta índice com formato, tags e cenas; arquivo grande demais fica de fora", () => {
  const { bundle, manifest } = aureaosFalso();
  const r07 = bundle.refs.find((r) => r.slug === "R07_alpha");
  assert.equal(r07.formato, "horizontal");
  assert.equal(r07.resumo, "logo reveal tipográfico");
  assert.deepEqual(r07.parts, ["cenas", "meta", "tags"]);
  assert.equal(r07.cenas.length, 2);
  assert.equal(bundle.refs.find((r) => r.slug === "R30_blue").formato, "vertical");
  assert.ok(manifest.files.some((f) => f.path === "tools/README.md"));
  assert.ok(manifest.files.every((f) => /^[0-9a-f]{64}$/.test(f.sha256)));
});

const darAureaos = (env, uid) => env.AUREA_DB.sqlite.prepare("INSERT OR REPLACE INTO entitlements(user_id,product,granted_at,source) VALUES(?,?,?,?)").run(uid, "aurea_aureaos", 1, "admin");

test("AureaOS pelo MCP: busca, referência paginada, documento, fx, estilo, batidas, recursos e prompts", async () => {
  const { env, files } = ambiente();
  const { uid, sessao } = await conta(env);
  darAureaos(env, uid);
  const { call } = await iniciar(env, await patDe(env, sessao));
  const semCerebro = (await call("tools/call", { name: "aureaos_search", arguments: { query: "logo" } })).corpo.result;
  assert.equal(semCerebro.isError, true);
  await subirCerebro(env, files);

  const t = async (name, args) => (await call("tools/call", { name, arguments: args })).corpo.result;
  const busca = await t("aureaos_search", { query: "typewriter logo" });
  assert.match(busca.content[0].text, /^R07_alpha C1 f0–49/);
  assert.match((await t("aureaos_search", { query: "", tags: ["trans:corte-seco"] })).content[0].text, /R07_alpha C2/);
  assert.match((await t("aureaos_search", { query: "contador", formato: "vertical" })).content[0].text, /R30_blue C1/);
  const ref = await t("aureaos_reference", { slug: "R07_alpha", part: "cenas" });
  assert.match(ref.content[0].text, /Call again with offset=60000/);
  assert.equal((await t("aureaos_reference", { slug: "R07_alpha", part: "cenas", offset: 60000 })).content[0].text.length, 16);
  assert.equal((await t("aureaos_reference", { slug: "../etc", part: "cenas" })).isError, true);
  assert.equal((await t("aureaos_reference", { slug: "R07_alpha", part: "deep" })).isError, true);
  assert.match((await t("aureaos_doc", { name: "rules" })).content[0].text, /FLOW/);
  assert.match((await t("aureaos_fx", {})).content[0].text, /lightUp: ACENDER/);
  assert.match((await t("aureaos_fx", { name: "lightUp" })).content[0].text, /X\.lightUp = /);
  const estilo = await t("aureaos_style_match", { formato: "vertical", tipo: "voz-off", estilos: ["tech"], script: "Você perde 3 clientes por dia. Mas existe um método. Clica no link." });
  assert.match(estilo.content[0].text, /\| 1 \| R30_blue \| 7 \|/);
  assert.match(estilo.content[0].text, /B01 .*\[numero/);
  const b = await t("aureaos_beats", { text: "Quantos leads você perde? Mas, imagine que um método resolve, e cresce, e vende, e fatura." });
  assert.match(b.content[0].text, /B01 .*\[pergunta, voce\]/);
  const s = JSON.parse((await t("search", { query: "logo" })).content[0].text);
  assert.ok(s.results.some((x) => x.id === "ref/R07_alpha/cenas.md"));
  const f = JSON.parse((await t("fetch", { id: "rules/CLAUDE.md" })).content[0].text);
  assert.match(f.text, /FLOW/);

  const res = (await call("resources/list")).corpo.result.resources.map((r) => r.uri);
  assert.ok(res.includes("aureaos://rules") && res.includes("aureaos://ref/R07_alpha/cenas"));
  assert.match((await call("resources/read", { uri: "aureaos://ref/R07_alpha/tags" })).corpo.result.contents[0].text, /typewriter/);
  assert.equal((await call("resources/read", { uri: "aureaos://ref/R07_alpha/../../x" })).corpo.error.code, -32002);
  const prompts = (await call("prompts/list")).corpo.result.prompts.map((p) => p.name);
  assert.deepEqual(prompts, ["aureamcp_editar", "aureaos_metodo", "aureaos_diretor", "aureaos_recriar"]);
  const met = (await call("prompts/get", { name: "aureaos_metodo" })).corpo.result;
  assert.match(met.messages[0].content.text, /medir tudo[\s\S]*FLOW[\s\S]*KeyframeEase/);
  assert.match((await call("prompts/get", { name: "aureaos_diretor", arguments: { briefing: "reel de 30 s" } })).corpo.result.messages[0].content.text, /reel de 30 s/);
  assert.equal((await call("prompts/get", { name: "aureaos_recriar", arguments: { slug: "nope" } })).corpo.error.code, -32602);
});

test("batidas: palavras com tempo, pistas com acento (você) e paginação", () => {
  const r = beats({ words: [{ w: "Você", t0: 0, t1: 0.3 }, { w: "sabe?", t0: 0.3, t1: 0.6 }, { w: "Depois", t0: 2, t1: 2.3 }, { w: "vem.", t0: 2.3, t1: 2.5 }], fps: 30 });
  assert.equal(r.beats.length, 2);
  assert.deepEqual(r.beats[0].pistas, ["pergunta", "voce"]);
  assert.equal(r.beats[1].f0, 60);
  assert.equal(fxEntries("X.a = 1;\n// nota b\nX.b = 2;").map((e) => e.name).join(), "a,b");
  assert.equal(page("abc", 1), "bc");
});

// --- AureaOS pago (aurea_aureaos) ----------------------------------------------------------
test("AureaOS pago: sem o direito, lista marcada e conteúdo recusado; com o direito, sai", async () => {
  const { env, files } = ambiente();
  const { uid, sessao } = await conta(env);
  await subirCerebro(env, files);
  const { call } = await iniciar(env, await patDe(env, sessao));
  const tools = (await call("tools/list")).corpo.result.tools;
  const busca = tools.find((t) => t.name === "aureaos_search");
  assert.ok(busca.description.startsWith("[Paid: AureaOS]"), "lista, mas marcada como paga");
  assert.ok(!tools.find((t) => t.name === "get_project_state").description.startsWith("[Paid"), "editor é livre");
  const negado = (await call("tools/call", { name: "aureaos_search", arguments: { query: "logo" } })).corpo.result;
  assert.equal(negado.isError, true);
  assert.match(negado.content[0].text, /paid add-on[\s\S]*Activate/);
  assert.doesNotMatch(negado.content[0].text, /R07_alpha/, "nada do conteúdo");
  for (const name of ["aureaos_doc", "aureaos_reference", "aureaos_fx", "search", "fetch"]) {
    assert.equal((await call("tools/call", { name, arguments: { name: "rules", slug: "R07_alpha", part: "cenas", query: "x", id: "rules/CLAUDE.md" } })).corpo.result.isError, true, name);
  }
  const prompt = (await call("prompts/get", { name: "aureaos_metodo" })).corpo;
  assert.equal(prompt.error.code, -32003);
  assert.match(prompt.error.message, /AureaOS is a paid add-on/);
  assert.ok((await call("prompts/list")).corpo.result.prompts.find((p) => p.name === "aureaos_metodo").description.startsWith("[Paid"));
  assert.ok((await call("prompts/get", { name: "aureamcp_editar" })).corpo.result.messages.length, "guia do editor é livre");
  const recurso = (await call("resources/read", { uri: "aureaos://rules" })).corpo;
  assert.equal(recurso.error.code, -32003);
  assert.ok(!JSON.stringify(recurso).includes("FLOW"));
  assert.ok((await call("resources/list")).corpo.result.resources.every((r) => r.description.startsWith("[Paid")));
  assert.match((await call("tools/call", { name: "aurea_status", arguments: {} })).corpo.result.content[0].text, /AureaOS: not active/);

  // Com o direito: o mesmo pedido devolve o conteúdo.
  darAureaos(env, uid);
  assert.ok(!(await call("tools/list")).corpo.result.tools.find((t) => t.name === "aureaos_search").description.startsWith("[Paid"));
  assert.match((await call("tools/call", { name: "aureaos_search", arguments: { query: "typewriter logo" } })).corpo.result.content[0].text, /R07_alpha/);
  assert.match((await call("prompts/get", { name: "aureaos_metodo" })).corpo.result.messages[0].content.text, /FLOW/);
  assert.match((await call("resources/read", { uri: "aureaos://rules" })).corpo.result.contents[0].text, /FLOW/);
  assert.match((await call("tools/call", { name: "aurea_status", arguments: {} })).corpo.result.content[0].text, /AureaOS: active/);
  // D1 fora: não libera.
  const outra = await conta(env, 2);
  const { call: c2 } = await iniciar(env, await patDe(env, outra.sessao));
  const prepare = env.AUREA_DB.prepare.bind(env.AUREA_DB);
  env.AUREA_DB.prepare = (sql) => (sql.includes("FROM entitlements") ? { bind: () => ({ first: async () => { throw new Error("d1"); } }) } : prepare(sql));
  const fora = (await c2("tools/call", { name: "aureaos_search", arguments: { query: "logo" } })).corpo.result;
  env.AUREA_DB.prepare = prepare;
  assert.equal(fora.isError, true);
  assert.match(fora.content[0].text, /could not be verified/);
});

test("AureaOS na loja: a Play confere, reconhece a compra e a mesma compra não vale em duas contas", async () => {
  const { generateKeyPairSync } = await import("node:crypto");
  const { privateKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
  const { env } = ambiente();
  const a = await conta(env, 1), b = await conta(env, 2);
  const buy = (token, sessao, extra = {}) => chamar(env, "/api/entitlements/purchase", { metodo: "POST", corpo: { platform: "google", product: "aurea_aureaos", token, ...extra }, token: sessao });
  assert.equal((await buy("tok-1", a.sessao)).status, 503, "sem as chaves da loja: indisponível, nunca libera");
  assert.deepEqual((await chamar(env, "/api/entitlements", { token: a.sessao })).corpo.purchasable, []);
  env.AUREAOS_PURCHASES_ENABLED = "true";
  env.GOOGLE_PLAY_SERVICE_ACCOUNT = JSON.stringify({ client_email: "aureaos@test", private_key: privateKey.export({ type: "pkcs8", format: "pem" }) });
  const purchases = { "tok-1": { purchaseState: 0, acknowledgementState: 0, orderId: "GPA.1" }, "tok-2": { purchaseState: 1 }, "tok-1b": { purchaseState: 0, acknowledgementState: 1, orderId: "GPA.1" } };
  const acks = [];
  const realFetch = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    const text = String(url);
    if (text.includes("oauth2.googleapis.com")) return new Response(JSON.stringify({ access_token: "ok" }));
    assert.ok(text.includes("/purchases/products/aurea_aureaos/tokens/"), "o produto certo");
    if (text.endsWith(":acknowledge")) { acks.push(text); return new Response("{}"); }
    const token = decodeURIComponent(text.split("/tokens/")[1]);
    return token in purchases ? new Response(JSON.stringify(purchases[token])) : new Response("{}", { status: 404 });
  };
  try {
    assert.deepEqual((await chamar(env, "/api/entitlements", { token: a.sessao })).corpo.purchasable, ["aurea_aureaos"]);
    assert.deepEqual((await chamar(env, "/api/entitlements?platform=apple", { token: a.sessao })).corpo.purchasable, []);
    assert.deepEqual((await chamar(env, "/api/entitlements?platform=google", { token: a.sessao })).corpo.purchasable, ["aurea_aureaos"]);
    assert.equal((await buy("tok-1", a.sessao, { product: "aureahyper_lifetime" })).status, 400, "só produtos da loja");
    assert.equal((await buy("nao-existe", a.sessao)).status, 402);
    assert.equal((await buy("tok-2", a.sessao)).status, 402, "não pago");
    const ok = await buy("tok-1", a.sessao);
    assert.equal(ok.status, 200);
    assert.deepEqual(ok.corpo.products, ["aurea_aureaos"]);
    assert.equal(acks.length, 1, "a compra foi reconhecida na Play");
    assert.equal((await buy("tok-1", a.sessao)).status, 200, "restaurar na mesma conta");
    assert.equal((await buy("tok-1b", b.sessao)).status, 409, "a mesma compra (orderId) em outra conta");
    assert.deepEqual((await chamar(env, "/api/entitlements", { token: b.sessao })).corpo.products, []);
    // A second legitimate receipt must not make the first receipt transferable.
    purchases["tok-second"] = { purchaseState: 0, acknowledgementState: 1, orderId: "GPA.2" };
    assert.equal((await buy("tok-second", a.sessao)).status, 200);
    assert.equal((await buy("tok-1b", b.sessao)).status, 409);
    purchases["tok-race"] = { purchaseState: 0, acknowledgementState: 1, orderId: "GPA.race" };
    const concurrent = await Promise.all([buy("tok-race", a.sessao), buy("tok-race", b.sessao)]);
    assert.deepEqual(concurrent.map((r) => r.status).sort(), [200, 409]);
    env.AUREAOS_PURCHASES_ENABLED = "false";
    assert.deepEqual((await chamar(env, "/api/entitlements?platform=google", { token: a.sessao })).corpo.purchasable, []);
    assert.equal((await buy("tok-1", a.sessao)).status, 503, "closed beta blocks checkout even with store credentials");
    assert.equal((await chamar(env, "/api/entitlements/purchase", { metodo: "POST", corpo: { platform: "google", product: "aurea_aureaos", token: "tok-1" } })).status, 401);
  } finally {
    globalThis.fetch = realFetch;
  }
  // O MCP já enxerga o direito da conta A.
  const { call } = await iniciar(env, await patDe(env, a.sessao));
  assert.ok(!(await call("tools/list")).corpo.result.tools.find((t) => t.name === "aureaos_doc").description.startsWith("[Paid"));
});

// --- cancelamento e catálogo da fase C -------------------------------------------------------
test("cancelamento: notifications/cancelled do agente chega ao app e encerra a chamada; outro token não cancela", async () => {
  const { env } = ambiente();
  const { uid, sessao } = await conta(env);
  const pat = await patDe(env, sessao);
  const { sid } = await iniciar(env, pat);
  const a = app(env, uid);
  await a.hello();
  a.ws.onCall = null;   // o app "trabalha" sem responder
  const h = { authorization: `Bearer ${pat}`, "mcp-session-id": sid };
  const pendente = chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", id: 77, method: "tools/call", params: { name: "list_projects", arguments: {} } }, cabecalhos: h });
  await new Promise((r) => setTimeout(r, 10));
  const call = a.ws.sent.find((m) => m.t === "call");
  assert.ok(call);
  // Outro token (outra conta não chega ao relay; o mesmo uid com outro token não cancela).
  const pat2 = await patDe(env, sessao);
  const alheio = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", method: "notifications/cancelled", params: { requestId: 77 } }, cabecalhos: { authorization: `Bearer ${pat2}` } });
  assert.equal(alheio.status, 202);
  assert.ok(!a.ws.sent.some((m) => m.t === "cancel"), "token diferente não cancela");
  const r = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", method: "notifications/cancelled", params: { requestId: 77, reason: "user" } }, cabecalhos: h });
  assert.equal(r.status, 202);
  assert.deepEqual(a.ws.sent.find((m) => m.t === "cancel"), { t: "cancel", id: call.id }, "o app recebe o cancelamento da chamada");
  const fim = (await pendente).corpo.result;
  assert.equal(fim.isError, true);
  assert.match(fim.content[0].text, /Cancelled/);
});

test("catálogo da fase C: leituras são read-only, escritas pedem edit, galeria sem áudio", () => {
  const t = engineToolsToMcp(DEFAULT_ENGINE_TOOLS);
  for (const n of ["get_layer_details", "get_keyframes", "list_effect_types", "get_effect", "list_presets", "list_fonts", "get_markers", "get_work_area"]) {
    const tool = t.find((x) => x.name === n);
    assert.ok(tool, n);
    assert.equal(tool.capability, "read", n);
    assert.equal(tool.annotations.readOnlyHint, true, n);
  }
  for (const n of ["set_property", "add_keyframes", "delete_keyframes", "move_keyframe", "set_keyframe_easing", "add_effect", "remove_effect", "move_effect", "set_effect_enabled", "apply_preset", "set_text_style", "set_marker", "delete_marker", "set_work_area"]) {
    assert.equal(t.find((x) => x.name === n)?.capability, "edit", n);
  }
  const gal = deviceTools({ permissions: { device_media: true }, hostTools: ["list_device_media", "import_device_media"] });
  assert.deepEqual(gal.find((x) => x.name === "list_device_media").inputSchema.properties.kind.enum, ["all", "video", "image"]);
  assert.ok(!deviceTools({ permissions: {}, hostTools: ["list_device_media"] }).some((x) => x.name === "list_device_media"), "galeria só com o interruptor ligado");
});
