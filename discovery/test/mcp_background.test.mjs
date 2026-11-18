// AureaMCP fora do app (18/11/2026): fila persistente no relay, batimento, aparelho
// em segundo plano/servido, push (FCM / APNs) e job_status.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";
import { generateKeyPairSync } from "node:crypto";

import worker from "../worker.js";
import { AureaMcpRelay, HEARTBEAT_MS, JOB_TTL_MS } from "../mcp_relay.js";
import { DEFAULT_ENGINE_TOOLS, queueClass, deviceTools, SESSION_TOOLS } from "../mcp_catalog.js";
import { sendWake, alertText, cleanPush, forgetPushCache, pushReady } from "../mcp_push.js";
import { sha256hex } from "../contas.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

// --- falsos do Durable Object (com alarme) ------------------------------------------
function storageFalso() {
  const m = new Map();
  let alarm = null;
  return {
    mapa: m,
    get alarmAt() { return alarm; },
    async get(k) { return structuredClone(m.get(k)); },
    async put(k, v) { m.set(k, structuredClone(v)); },
    async delete(k) { for (const x of Array.isArray(k) ? k : [k]) m.delete(x); },
    async list({ prefix = "" } = {}) { return new Map([...m].filter(([k]) => k.startsWith(prefix)).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)).map(([k, v]) => [k, structuredClone(v)])); },
    async getAlarm() { return alarm; },
    async setAlarm(t) { alarm = t; },
  };
}
function ctxFalso() {
  const sockets = [];
  return {
    storage: storageFalso(),
    autoTs: new Map(),
    acceptWebSocket(ws, tags) { ws.tags = tags; sockets.push(ws); },
    getWebSockets(tag) { return sockets.filter((w) => w.readyState === 1 && (!tag || w.tags.includes(tag))); },
    getWebSocketAutoResponseTimestamp(ws) { return this.autoTs.get(ws) ?? null; },
    setWebSocketAutoResponse() {},
  };
}
class WsFalso {
  constructor() { this.sent = []; this.readyState = 1; this.att = null; this.onCall = null; this.closedWith = null; }
  send(m) { const v = typeof m === "string" && m.startsWith("{") ? JSON.parse(m) : m; this.sent.push(v); if (v?.t === "call" && this.onCall) queueMicrotask(() => this.onCall(v)); }
  serializeAttachment(a) { this.att = structuredClone(a); }
  deserializeAttachment() { return structuredClone(this.att); }
  close(code) { this.readyState = 3; this.closedWith = code ?? 1000; }
}

function ambiente() {
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso() };
  const relays = new Map();
  env.AUREA_MCP = {
    idFromName: (n) => n,
    get: (id) => {
      if (!relays.has(id)) {
        const r = new AureaMcpRelay(ctxFalso(), env);
        r.pushes = [];
        r.pushImpl = async (_env, target, info) => { r.pushes.push({ target, info }); return r.pushReply?.(target) ?? { ok: true }; };
        r.queueGraceMs = 0;
        relays.set(id, r);
      }
      return relays.get(id);
    },
  };
  return { env };
}
const ctx = { waitUntil: (p) => p };
const tick = (ms = 5) => new Promise((r) => setTimeout(r, ms));

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
async function iniciar(env, sessao) {
  const pat = (await chamar(env, "/api/mcp/tokens", { metodo: "POST", corpo: { name: "Claude Code" }, token: sessao })).corpo.token;
  const h0 = { authorization: `Bearer ${pat}`, accept: "application/json, text/event-stream" };
  const init = await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", id: 0, method: "initialize", params: { protocolVersion: "2025-06-18", clientInfo: { name: "claude-code", version: "2" }, capabilities: {} } }, cabecalhos: h0 });
  const sid = init.h.get("mcp-session-id");
  let n = 0;
  const call = async (method, params) => (await chamar(env, "/mcp", { metodo: "POST", corpo: { jsonrpc: "2.0", id: ++n, method, params }, cabecalhos: { ...h0, "mcp-session-id": sid } })).corpo;
  return { call, sid };
}

const PUSH_FCM = { provider: "fcm", token: "fcm-token-" + "a".repeat(40) };
function app(env, uid, { device = "phone-0001", permissions = {}, push = PUSH_FCM, background = false, serviced = false, askEveryEdit = false, platform = "android" } = {}) {
  const relay = env.AUREA_MCP.get("mcp:" + uid);
  const ws = new WsFalso();
  relay.acceptSocket(ws, device);
  return {
    ws, relay,
    hello: (extra = {}) => relay.webSocketMessage(ws, JSON.stringify({ t: "hello", device: { name: "Pixel", platform, app: "1.0.2" }, engineTools: DEFAULT_ENGINE_TOOLS,
      hostTools: ["list_projects", "delete_project", "render_frame", "export_video", "open_project", "save_project"], permissions, project: { title: "Reel", open: true },
      push, background, serviced, askEveryEdit, lang: "pt-BR", ...extra })),
    state: (m) => relay.webSocketMessage(ws, JSON.stringify({ t: "state", ...m })),
    answer: (fn) => { ws.onCall = (c) => relay.webSocketMessage(ws, JSON.stringify(fn(c))); },
    close: async () => { ws.readyState = 3; await relay.webSocketClose(ws); },
  };
}
const ok = (c) => ({ t: "result", id: c.id, ok: true, content: [{ type: "text", text: JSON.stringify({ ok: true, tool: c.tool, job: c.job === true }) }] });

// --- classificação --------------------------------------------------------------------
test("fila: leve entra; pesada só com pedido; confirmação, destrutiva e só de interface nunca; 'Perguntar antes' barra edições", () => {
  const tools = deviceTools({ engineTools: DEFAULT_ENGINE_TOOLS, hostTools: ["delete_project", "render_frame", "list_projects", "export_video"], permissions: {} });
  const def = (n) => tools.find((t) => t.name === n) ?? { name: n };
  assert.equal(queueClass(def("get_project_state")), "yes");
  assert.equal(queueClass(def("set_property")), "yes");
  assert.equal(queueClass(def("run_ae_script")), "heavy");
  assert.equal(queueClass(def("render_frame")), "heavy");
  assert.equal(queueClass(def("delete_project")), "never");
  assert.equal(queueClass(def("open_panel")), "never");
  assert.equal(queueClass(def("set_property"), { ask: true }), "never");
  assert.equal(queueClass(def("get_project_state"), { ask: true }), "yes");
  assert.equal(queueClass(undefined), "never");
  for (const n of ["job_status", "aurea_queue_policy"]) assert.ok(SESSION_TOOLS.some((t) => t.name === n), n);
});

// --- fila + push + replay ---------------------------------------------------------------
test("app fechado: a chamada vira job, o aparelho é acordado por push, o hello em segundo plano drena em ordem e job_status devolve o resultado", async () => {
  const { env } = ambiente();
  const { uid, sessao } = await conta(env);
  const { call } = await iniciar(env, sessao);
  const a = app(env, uid);
  await a.hello();
  await a.close();

  const r1 = (await call("tools/call", { name: "get_project_state", arguments: {} })).result;
  assert.ok(!r1.isError, "fila não é erro");
  assert.equal(r1.structuredContent.queued, true);
  assert.match(r1.content[0].text, /offline right now.*job_status/s);
  const job1 = r1.structuredContent.job_id;
  assert.match(job1, /^j[0-9a-z]+$/);
  const R = env.AUREA_MCP.get("mcp:" + uid);
  assert.equal(R.pushes.length, 1, "push FCM enviado");
  assert.equal(R.pushes[0].target.token, PUSH_FCM.token);
  assert.equal(R.pushes[0].info.project, "Reel");
  assert.ok(R.ctx.storage.alarmAt > Date.now(), "alarme marcado");

  const r2 = (await call("tools/call", { name: "set_property", arguments: { layer: 1, property: "opacity", value: 0.5 } })).result;
  const job2 = r2.structuredContent.job_id;
  assert.ok(job2 > job1, "ids em ordem de criação");
  assert.equal(R.pushes.length, 1, "um push por minuto por aparelho");

  const st = (await call("tools/call", { name: "aurea_status", arguments: {} })).result.content[0].text;
  assert.match(st, /can be woken up/);
  assert.match(st, /2 call\(s\) queued/);

  // Acordou (FCM → serviço): hello em segundo plano, servido.
  const b = app(env, uid, { background: true, serviced: true });
  const order = [];
  let inFlight = 0, maxInFlight = 0;
  b.ws.onCall = (c) => {
    order.push(c.id); inFlight++; maxInFlight = Math.max(maxInFlight, inFlight);
    setTimeout(() => { inFlight--; R.webSocketMessage(b.ws, JSON.stringify(ok(c))); }, 3);
  };
  await b.hello();
  await R.draining.get("phone-0001");
  assert.deepEqual(order, [job1, job2], "na ordem");
  assert.equal(maxInFlight, 1, "um por vez");
  assert.ok(b.ws.sent.filter((m) => m.t === "call").every((m) => m.job === true));

  const s1 = (await call("tools/call", { name: "job_status", arguments: { job_id: job1 } })).result;
  assert.match(s1.content[0].text, /done/);
  assert.deepEqual(JSON.parse(s1.content[1].text), { ok: true, tool: "get_project_state", job: true });
  const lista = (await call("tools/call", { name: "job_status", arguments: {} })).result;
  assert.equal(lista.structuredContent.jobs.length, 2);
  assert.ok(lista.structuredContent.jobs.every((j) => j.state === "done"));

  // Com o aparelho servido, a chamada nova vai direto (sem fila).
  const direta = (await call("tools/call", { name: "get_project_state", arguments: {} })).result;
  assert.equal(direta.structuredContent, undefined);
  assert.deepEqual(JSON.parse(direta.content[0].text), { ok: true, tool: "get_project_state", job: false });
});

test("fila: pesada falha na hora sem pedido; com aurea_queue_policy ou _meta entra; delete_project e open_panel nunca; 'Perguntar antes' barra edição", async () => {
  const { env } = ambiente();
  const { uid, sessao } = await conta(env);
  const { call } = await iniciar(env, sessao);
  const a = app(env, uid);
  await a.hello();
  await a.close();

  const heavy = (await call("tools/call", { name: "run_ae_script", arguments: { code: "1" } })).result;
  assert.equal(heavy.isError, true);
  assert.match(heavy.content[0].text, /aurea_queue_policy/);
  const meta = (await call("tools/call", { name: "run_ae_script", arguments: { code: "1" }, _meta: { "aurea/queue": true } })).result;
  assert.equal(meta.structuredContent?.queued, true, "_meta pede a fila");
  assert.match((await call("tools/call", { name: "aurea_queue_policy", arguments: { allowHeavy: true } })).result.content[0].text, /queued too/);
  assert.equal((await call("tools/call", { name: "render_frame", arguments: {} })).result.structuredContent?.queued, true);

  const del = (await call("tools/call", { name: "delete_project", arguments: { id: "x" } })).result;
  assert.equal(del.isError, true);
  assert.match(del.content[0].text, /never queued/);
  const ui = (await call("tools/call", { name: "open_panel", arguments: { panel: "media" } })).result;
  assert.match(ui.content[0].text, /never queued/);

  // "Perguntar antes": edições não entram; leituras sim.
  const b = app(env, uid, { askEveryEdit: true });
  await b.hello();
  await b.close();
  assert.match((await call("tools/call", { name: "set_property", arguments: { layer: 1, property: "opacity", value: 1 } })).result.content[0].text, /approval/);
  assert.equal((await call("tools/call", { name: "get_project_state", arguments: {} })).result.structuredContent?.queued, true);
});

test("push acorda e o app volta dentro da espera: o agente recebe o resultado de verdade (sem job_status)", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  await a.close();
  const R = env.AUREA_MCP.get("mcp:" + uid);
  R.queueGraceMs = 2000;
  R.pushReply = () => {
    // O FCM chega, o serviço sobe e conecta.
    setTimeout(async () => { const b = app(env, uid, { background: true, serviced: true }); b.answer(ok); await b.hello(); }, 10);
    return { ok: true };
  };
  const r = await R.callTool({ tool: "get_project_state", args: {}, agent: "Claude", tokenId: "t1" });
  assert.equal(r.structuredContent, undefined);
  assert.deepEqual(JSON.parse(r.content[0].text), { ok: true, tool: "get_project_state", job: true });
});

test("job que caiu no meio volta para a fila e é reenviado com o MESMO id (o app deduplica); 3 tentativas no máximo", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  await a.close();
  const R = env.AUREA_MCP.get("mcp:" + uid);
  const q = await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" });
  const id = q.structuredContent.job_id;

  const b = app(env, uid, { background: true, serviced: true });
  b.ws.onCall = () => { b.close(); }; // caiu antes de responder
  await b.hello();
  await R.draining.get("phone-0001");
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id })).state, "queued");
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id })).attempts, 1);

  const c = app(env, uid, { background: true, serviced: true });
  const seen = [];
  c.ws.onCall = (m) => { seen.push(m.id); R.webSocketMessage(c.ws, JSON.stringify(ok(m))); };
  await c.hello();
  await R.draining.get("phone-0001");
  assert.deepEqual(seen, [id], "o mesmo id");
  const st = await R.jobStatus({ tokenId: "t1", jobId: id });
  assert.equal(st.state, "done");
  assert.equal(st.attempts, 2);
  assert.equal(await R.jobStatus({ tokenId: "outro", jobId: id }), null, "outro token não vê o job");
});

test("nunca repete sozinho o que passou a pedir aprovação: a pessoa ligou 'Perguntar antes' com o job na fila", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  await a.close();
  const R = env.AUREA_MCP.get("mcp:" + uid);
  const id = (await R.callTool({ tool: "set_property", args: { layer: 1, property: "opacity", value: 0 }, tokenId: "t1" })).structuredContent.job_id;
  const b = app(env, uid, { askEveryEdit: true });
  const calls = [];
  b.ws.onCall = (c) => calls.push(c);
  await b.hello();
  await R.draining.get("phone-0001");
  assert.equal(calls.length, 0);
  const st = await R.jobStatus({ tokenId: "t1", jobId: id });
  assert.equal(st.state, "failed");
  assert.match(st.result.content[0].text, /approval/);
});

test("batimento: socket mudo há mais de 60 s sai (e a chamada vai para a fila); o ping do auto-resposta mantém vivo", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  const R = a.relay;
  a.ws.att.seen = Date.now() - HEARTBEAT_MS - 1000;
  a.ws.att.since = a.ws.att.seen;
  R.ctx.autoTs.set(a.ws, new Date()); // respondeu "ping" agora
  assert.equal((await R.status()).devices.length, 1, "vivo pelo ping");
  R.ctx.autoTs.set(a.ws, new Date(Date.now() - HEARTBEAT_MS - 1000));
  const st = await R.status();
  assert.equal(st.devices.length, 0, "mudo: fora");
  assert.equal(a.ws.closedWith, 4002);
  assert.equal(st.sleeping.length, 1, "mas acordável por push");
  const r = await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" });
  assert.equal(r.structuredContent.queued, true);
});

test("aparelho ativo: em segundo plano sem serviço não recebe; servido recebe; na frente ganha do servido", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const phone = app(env, uid, { device: "phone-0001" });
  await phone.hello();
  phone.answer(ok);
  const R = phone.relay;
  // Foi para trás sem o serviço: não está pronto (vai congelar) → fila.
  await phone.state({ background: true, serviced: false });
  assert.equal((await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" })).structuredContent?.queued, true);
  // O serviço segurou: drena a fila e recebe direto.
  await phone.state({ serviced: true });
  await R.draining.get("phone-0001");
  const direto = await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" });
  assert.equal(direto.structuredContent, undefined);
  assert.equal((await R.status()).devices[0].state, "background");
  // O PC na frente ganha do celular servido quando o PC é o escolhido; o hello em segundo plano não rouba a vez.
  const pc = app(env, uid, { device: "pc-000000001", push: null, platform: "pc" });
  await pc.hello();
  pc.answer((c) => ({ t: "result", id: c.id, ok: true, content: [{ type: "text", text: "pc" }] }));
  assert.equal((await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" })).content[0].text, "pc");
  const phone2 = app(env, uid, { device: "phone-0001", background: true, serviced: true });
  await phone2.hello();
  assert.equal(await R.ctx.storage.get("active"), "pc-000000001");
});

test("alarme: expira job velho, repete o push no máximo a cada 5 min e apaga token morto", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  await a.close();
  const R = a.relay;
  const id = (await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" })).structuredContent.job_id;
  assert.equal(R.pushes.length, 1);
  await R.alarm();
  assert.equal(R.pushes.length, 1, "repetição só depois de 5 min");
  const c = await R.ctx.storage.get("catalog:phone-0001");
  c.lastPush = Date.now() - 6 * 60_000;
  await R.ctx.storage.put("catalog:phone-0001", c);
  R.pushReply = () => ({ ok: false, gone: true });
  await R.alarm();
  assert.equal(R.pushes.length, 2);
  assert.equal((await R.ctx.storage.get("catalog:phone-0001")).push, null, "token morto apagado");
  const j = await R.ctx.storage.get(`job:${id}`);
  j.expiresAt = Date.now() - 1;
  await R.ctx.storage.put(`job:${id}`, j);
  await R.alarm();
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id })).state, "expired");
  assert.ok(JOB_TTL_MS >= 10 * 60_000);
});

test("cancelar (notifications/cancelled) tira o job da fila; pausa devolve o job em andamento à fila; revogar o token derruba os jobs", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid);
  await a.hello();
  await a.close();
  const R = a.relay;
  const id = (await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1", session: null, requestId: 7 })).structuredContent.job_id;
  assert.equal((await R.cancelRequest({ tokenId: "t1", requestId: 7 })).ok, true);
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id })).state, "failed");

  const id2 = (await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" })).structuredContent.job_id;
  const b = app(env, uid, { background: true, serviced: true });
  b.ws.onCall = () => { R.webSocketMessage(b.ws, JSON.stringify({ t: "pause" })); };
  await b.hello();
  await R.draining.get("phone-0001");
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id2 })).state, "queued", "pausa: volta para a fila");
  await R.revokeSessions({ tokenIds: ["t1"] });
  assert.equal((await R.jobStatus({ tokenId: "t1", jobId: id2 })).state, "failed");
});

test("nenhum app conectou nunca: continua 'offline' (nada para acordar, nada na fila)", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const R = env.AUREA_MCP.get("mcp:" + uid);
  const r = await R.callTool({ tool: "get_project_state", args: {}, tokenId: "t1" });
  assert.equal(r.isError, true);
  assert.match(r.content[0].text, /offline/);
  assert.equal(R.pushes.length, 0);
});

test("o hello guarda o token de push limpo e o welcome diz se o Worker sabe mandar push", async () => {
  const { env } = ambiente();
  const { uid } = await conta(env);
  const a = app(env, uid, { push: { provider: "fcm", token: "bad token with spaces" } });
  await a.hello();
  assert.equal((await a.relay.ctx.storage.get("catalog:phone-0001")).push, null);
  assert.equal(a.ws.sent.find((m) => m.t === "welcome").push, false);
  env.FCM_SERVICE_ACCOUNT = "{}";
  const b = app(env, uid);
  await b.hello();
  assert.equal(b.ws.sent.find((m) => m.t === "welcome").push, true);
  await b.state({ push: null });
  assert.equal((await b.relay.ctx.storage.get("catalog:phone-0001")).push, null, "a pessoa desligou: o token sai");
});

// --- envio de push --------------------------------------------------------------------
test("push: textos do alerta nos 7 idiomas; token validado; sem credenciais não envia", async () => {
  assert.equal(alertText("pt-BR", "Reel").body, 'O agente quer editar "Reel". Toque para continuar.');
  assert.match(alertText("es", "X").body, /El agente quiere editar "X"/);
  for (const l of ["en", "ru", "hi", "ar", "id"]) assert.ok(alertText(l, "X").body.includes('"X"') || alertText(l, "X").body.includes("«X»"), l);
  assert.match(alertText("xx", "").body, /AI agent/);
  assert.deepEqual(cleanPush({ provider: "apns", token: "ab".repeat(32), env: "sandbox" }), { provider: "apns", token: "ab".repeat(32), env: "sandbox" });
  assert.equal(cleanPush({ provider: "x", token: "ab".repeat(32) }), null);
  assert.equal(cleanPush({ provider: "fcm", token: "short" }), null);
  assert.equal(pushReady({}, "fcm"), false);
  assert.deepEqual(await sendWake({}, PUSH_FCM, {}), { ok: false, error: "push_not_configured" });
});

test("push: FCM HTTP v1 (conta de serviço, dados de prioridade alta) e APNs (JWT ES256, alerta visível); token morto = gone", async () => {
  forgetPushCache();
  const rsa = generateKeyPairSync("rsa", { modulusLength: 2048 }).privateKey.export({ type: "pkcs8", format: "pem" });
  const ec = generateKeyPairSync("ec", { namedCurve: "prime256v1" }).privateKey.export({ type: "pkcs8", format: "pem" });
  const env = {
    FCM_SERVICE_ACCOUNT: JSON.stringify({ project_id: "aurea-test", client_email: "fcm@test", private_key: rsa }),
    APNS_KEY_ID: "KEY1234567", APNS_TEAM_ID: "TEAM123456", APNS_PRIVATE_KEY: ec, APPLE_BUNDLE_ID: "com.aurea.aurea",
  };
  const calls = [];
  const real = globalThis.fetch;
  let fcmStatus = 200;
  globalThis.fetch = async (url, init = {}) => {
    calls.push({ url: String(url), init });
    if (String(url).startsWith("https://oauth2.googleapis.com/token")) return new Response(JSON.stringify({ access_token: "ya29.x" }), { status: 200 });
    if (String(url).includes("fcm.googleapis.com")) return new Response(fcmStatus === 200 ? "{}" : JSON.stringify({ error: { status: "NOT_FOUND", details: [{ errorCode: "UNREGISTERED" }] } }), { status: fcmStatus });
    if (String(url).includes("push.apple.com")) return new Response(String(url).includes("dead") ? JSON.stringify({ reason: "BadDeviceToken" }) : "", { status: String(url).includes("dead") ? 400 : 200 });
    return new Response("no", { status: 500 });
  };
  try {
    assert.deepEqual(await sendWake(env, PUSH_FCM, { jobs: 2 }), { ok: true });
    const fcm = calls.find((c) => c.url.includes("fcm.googleapis.com"));
    assert.equal(fcm.url, "https://fcm.googleapis.com/v1/projects/aurea-test/messages:send");
    assert.equal(fcm.init.headers.authorization, "Bearer ya29.x");
    const body = JSON.parse(fcm.init.body);
    assert.deepEqual(body.message.data, { t: "mcp_wake", jobs: "2" });
    assert.equal(body.message.android.priority, "HIGH");
    assert.equal(body.message.notification, undefined, "só dados");
    const scope = new URLSearchParams(calls[0].init.body).get("assertion").split(".")[1];
    assert.match(Buffer.from(scope, "base64url").toString(), /firebase\.messaging/);
    fcmStatus = 404;
    assert.deepEqual(await sendWake(env, PUSH_FCM, {}), { ok: false, gone: true });

    const apnsToken = "ab".repeat(32);
    assert.deepEqual(await sendWake(env, { provider: "apns", token: apnsToken, env: "sandbox" }, { project: "Reel", lang: "pt" }), { ok: true });
    const ap = calls.find((c) => c.url.includes("push.apple.com"));
    assert.equal(ap.url, `https://api.sandbox.push.apple.com/3/device/${apnsToken}`);
    assert.equal(ap.init.headers["apns-push-type"], "alert");
    assert.equal(ap.init.headers["apns-topic"], "com.aurea.aurea");
    assert.match(ap.init.headers.authorization, /^bearer [\w-]+\.[\w-]+\.[\w-]+$/);
    const header = JSON.parse(Buffer.from(ap.init.headers.authorization.split(" ")[1].split(".")[0], "base64url").toString());
    assert.deepEqual(header, { alg: "ES256", kid: "KEY1234567" });
    const payload = JSON.parse(ap.init.body);
    assert.equal(payload.aps.alert.body, 'O agente quer editar "Reel". Toque para continuar.');
    assert.equal(payload.aurea.t, "mcp_wake");
    assert.deepEqual(await sendWake(env, { provider: "apns", token: "dead" + "ab".repeat(30) }, {}), { ok: false, gone: true });
  } finally {
    globalThis.fetch = real;
    forgetPushCache();
  }
});
