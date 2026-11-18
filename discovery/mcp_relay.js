// =============================================================================
//  AureaMcpRelay — o elo entre os agentes (MCP) e o app Aurea da conta.
//  Um Durable Object por conta: idFromName("mcp:" + uid). Ver DESIGN.md §1.3.
//
//  O app abre um WebSocket (sessão da conta, validada pelo Worker) e fica
//  ouvindo; o objeto aceita com a API de HIBERNAÇÃO, então conexão parada não
//  custa duração. O ping "ping"→"pong" é respondido sem acordar o objeto.
//
//  Mensagens app → relay:
//    {t:"hello", device:{name,platform,app}, engineTools:[...], hostTools:[...], permissions:{...},
//              background?, serviced?, askEveryEdit?, lang?, push?:{provider:"fcm"|"apns", token, env}}
//    {t:"result", id, ok, content:[...], error?}
//    {t:"state", permissions?, project?, background?, serviced?, askEveryEdit?, push?}
//    {t:"focus"}  {t:"pause"}  {t:"resume"}
//  Relay → app:
//    {t:"welcome", paused, agents, push}  {t:"call", id, tool, args, agent, job?}  {t:"cancel", id}
//    {t:"agents", agents}  {t:"log", at, agent, tool, ok, ms}  {t:"paused", paused}
//
//  O Worker fala com o objeto por RPC: openSession, getSession, closeSession,
//  revokeSessions, status, listTools, callTool, selectDevice, jobStatus,
//  setQueuePolicy.
//
//  FORA DO APP (18/11/2026). O agente continua trabalhando com o app fechado:
//   · O app diz se está em segundo plano (`background`) e se tem quem o mantenha
//     de pé (`serviced`: o serviço do agente no Android, a tarefa contínua no
//     iOS). Recebe as chamadas o aparelho na frente ou servido; um app em
//     segundo plano sem serviço vai congelar e não recebe chamadas.
//   · Batimento: um socket mudo há mais de HEARTBEAT_MS (nem "ping" no
//     auto-resposta, nem mensagem) é fechado — o app congelado deixava de ser o
//     ativo só quando o TCP caía.
//   · FILA persistente (storage do objeto, chave job:<id>): sem aparelho pronto,
//     ferramenta leve e segura vira um job e a resposta é "queued, job_id"
//     (job_status lê depois). Pesada só com o agente pedindo
//     (aurea_queue_policy / _meta "aurea/queue"); confirmação, destrutiva ou só
//     de interface NUNCA entra na fila nem é repetida sozinha.
//   · O aparelho é acordado por push (mcp_push.js: FCM no Android, alerta APNs
//     no iOS). No hello a fila é reenviada EM ORDEM, uma por vez; o id do job é
//     a chave de idempotência (o app guarda os últimos executados e só reenvia
//     o resultado se o mesmo job chegar de novo).
//   · O alarme do objeto expira jobs velhos, repete o push e derruba sockets mudos.
// =============================================================================

import { DurableObject } from "cloudflare:workers";
import { SESSION_TOOLS, HOST_TOOLS, HEAVY, deviceTools, publicTool, queueClass } from "./mcp_catalog.js";
import { sendWake, cleanPush, pushReady } from "./mcp_push.js";

export const CALL_TIMEOUT_MS = 45_000;
export const CONFIRM_TIMEOUT_MS = 95_000;
export const RATE_PER_MIN = 120;
export const MAX_CONCURRENT = 3;
export const MAX_DEVICES = 4;
const MAX_SESSIONS = 50;
const SESSION_IDLE_MS = 30 * 24 * 3600 * 1000;
const RESULT_MAX = 900 * 1024;
const LOG_MAX = 100;
const DEVICE_RE = /^[A-Za-z0-9_-]{8,64}$/;
/** Sem nada do app (mensagem ou "ping") por este tempo = socket morto (o app manda "ping" a cada 25 s). */
export const HEARTBEAT_MS = 60_000;
/** Um job na fila vale por isto; depois expira (o agente vê em job_status). */
export const JOB_TTL_MS = 30 * 60_000;
/** Jobs terminados ficam para job_status por um dia. */
const JOB_KEEP_MS = 24 * 3600 * 1000;
export const MAX_QUEUED = 50;
const MAX_JOB_RECORDS = 300;
const MAX_JOB_ATTEMPTS = 3;
/** Resultado guardado de um job (o storage tem teto por valor). */
const JOB_RESULT_MAX = 100 * 1024;
/** Depois de mandar o push, a chamada espera um pouco: o Android costuma voltar em segundos. */
export const QUEUE_GRACE_MS = 12_000;
/** Intervalo mínimo entre pushes a um aparelho: na hora (enfileirou) e nas repetições do alarme. */
const PUSH_GAP_MS = 60_000;
const PUSH_RETRY_MS = 5 * 60_000;
const JOB_RE = /^j[0-9a-z]{8,40}$/;

const now = () => Date.now();
const errorResult = (text) => ({ content: [{ type: "text", text }], isError: true });
/** Resposta de uma chamada de job que não chegou (socket caiu, pausa, sem resposta): o job volta para a fila. */
const LOST = Object.freeze({ lost: true });
const clean = (s, n) => String(s ?? "").replace(/[\u0000-\u001f]/g, "").slice(0, n);
/** 2 = na frente; 1 = em segundo plano, mas servido (serviço/tarefa do agente); 0 = vai congelar. */
const rank = (att) => (att.background !== true ? 2 : att.serviced === true ? 1 : 0);
const open = (j) => j.state === "queued" || j.state === "sent";

export class AureaMcpRelay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.pending = new Map();   // id da chamada → {resolve, timer, tool, device, job?}
    this.calls = [];            // instantes das últimas chamadas (limite por minuto)
    this.heavy = null;          // ferramenta pesada em andamento (uma por vez)
    this.log = [];
    this.seq = 0;
    this.jobWaiters = new Map(); // id do job → [resolve] (quem enfileirou espera um pouco)
    this.draining = new Map();   // aparelho → promessa da drenagem em curso
    this.pushImpl = sendWake;    // trocado nos testes
    this.queueGraceMs = QUEUE_GRACE_MS;
    try {
      if (typeof WebSocketRequestResponsePair !== "undefined") ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
    } catch { /* ambiente sem a API (testes) */ }
  }

  // --- sockets do app ---------------------------------------------------------

  /** O Worker repassa o upgrade já autenticado: x-aurea-uid e ?device=. */
  async fetch(req) {
    if (req.headers.get("upgrade")?.toLowerCase() !== "websocket") return new Response("expected websocket", { status: 426 });
    const device = new URL(req.url).searchParams.get("device") ?? "";
    if (!DEVICE_RE.test(device)) return new Response("bad device", { status: 400 });
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.acceptSocket(server, device);
    return new Response(null, { status: 101, webSocket: client });
  }

  /** Separado do fetch para os testes (o Node não tem WebSocketPair). */
  acceptSocket(ws, device) {
    // O mesmo aparelho reconectando substitui a conexão velha.
    for (const old of this.ctx.getWebSockets(device)) { try { old.close(4000, "replaced"); } catch { /* já fechado */ } }
    const others = this.sockets().filter((s) => s.device !== device);
    if (others.length >= MAX_DEVICES) {
      const oldest = others.sort((a, b) => a.att.since - b.att.since)[0];
      try { oldest.ws.close(4001, "too many devices"); } catch { /* já fechado */ }
    }
    this.ctx.acceptWebSocket(ws, [device]);
    ws.serializeAttachment({ device, since: now(), seen: now(), hello: false, name: "", platform: "", app: "", focus: now() });
  }

  /** Sockets abertos com o anexo (device, nome, foco). */
  sockets() {
    const out = [];
    for (const ws of this.ctx.getWebSockets()) {
      if (ws.readyState !== undefined && ws.readyState > 1) continue;
      const att = ws.deserializeAttachment?.() ?? {};
      if (att.device) out.push({ ws, device: att.device, att });
    }
    return out;
  }

  /** Último sinal de vida do socket: mensagem do app ou o "ping" respondido pelo auto-resposta. */
  lastSeen(s) {
    let auto = 0;
    try { auto = this.ctx.getWebSocketAutoResponseTimestamp?.(s.ws)?.getTime?.() ?? 0; } catch { /* sem a API */ }
    return Math.max(s.att.seen ?? s.att.since ?? 0, auto);
  }

  /**
   * Batimento: fecha os sockets mudos há mais de HEARTBEAT_MS (app congelado ou
   * rede caída sem FIN) e solta as chamadas presas neles. Devolve os vivos.
   */
  liveSockets() {
    const t = now();
    const live = [];
    for (const s of this.sockets()) {
      if (t - this.lastSeen(s) <= HEARTBEAT_MS) { live.push(s); continue; }
      try { s.ws.close(4002, "silent"); } catch { /* já fechado */ }
      this.failDevice(s.device, "The Aurea app stopped answering (asleep or offline).");
    }
    return live;
  }

  /** Chamadas pendentes deste aparelho: as do agente falham; as de job voltam para a fila. */
  failDevice(device, text) {
    for (const [id, p] of this.pending) if (p.device === device) this.finish(id, p.job ? LOST : errorResult(text), p);
  }

  /** Aparelhos prontos para receber chamadas: hello feito, vivos e na frente ou servidos. */
  readySockets() { return this.liveSockets().filter((s) => s.att.hello && rank(s.att) > 0); }

  send(ws, msg) { try { ws.send(JSON.stringify(msg)); } catch { /* socket caiu */ } }
  broadcast(msg) { for (const s of this.sockets()) if (s.att.hello) this.send(s.ws, msg); }

  async webSocketMessage(ws, data) {
    if (typeof data !== "string" || data.length > 1024 * 1024) return;
    if (data === "ping") return this.send(ws, "pong");
    let m;
    try { m = JSON.parse(data); } catch { return; }
    const raw = ws.deserializeAttachment?.() ?? {};
    if (!raw.device || !m || typeof m !== "object") return;
    // Toda mensagem do app é sinal de vida (o batimento).
    const att = { ...raw, seen: now() };
    switch (m.t) {
      case "hello": {
        const d = m.device ?? {};
        const permissions = {};
        for (const [k, v] of Object.entries(m.permissions ?? {}).slice(0, 20)) if (typeof v === "boolean") permissions[k] = v;
        const old = (await this.ctx.storage.get(`catalog:${att.device}`)) ?? {};
        const catalog = {
          engineTools: Array.isArray(m.engineTools) ? m.engineTools.slice(0, 120) : null,
          hostTools: Array.isArray(m.hostTools) ? m.hostTools.filter((x) => typeof x === "string").slice(0, 60) : [],
          permissions,
          project: m.project && typeof m.project === "object" ? { title: clean(m.project.title, 120), open: m.project.open === true } : null,
          ask: m.askEveryEdit === true,
          lang: clean(m.lang, 12),
          // Um hello sem push (app velho, ou a pessoa desligou o modo fora do app) apaga o token guardado.
          push: cleanPush(m.push),
          lastPush: old.lastPush ?? 0,
        };
        if (JSON.stringify(catalog).length > 256 * 1024) catalog.engineTools = null;
        ws.serializeAttachment({ ...att, hello: true, name: clean(d.name, 60), platform: clean(d.platform, 20), app: clean(d.app, 20), focus: now(),
          background: m.background === true, serviced: m.serviced === true });
        await this.ctx.storage.put(`catalog:${att.device}`, catalog);
        // Um app que acordou em segundo plano (push) não rouba a vez do aparelho que a pessoa está usando.
        if (m.background !== true || !(await this.ctx.storage.get("active"))) await this.ctx.storage.put("active", att.device);
        this.send(ws, { t: "welcome", paused: (await this.ctx.storage.get("paused")) === true, agents: await this.agents(), log: this.log.slice(-30),
          push: !!catalog.push && pushReady(this.env ?? {}, catalog.push.provider) });
        this.kickDrain(att.device);
        return;
      }
      case "state": {
        // O app avisa mudanças de permissão, projeto, primeiro/segundo plano ou push sem refazer o hello.
        const catalog = (await this.ctx.storage.get(`catalog:${att.device}`)) ?? {};
        if (m.permissions && typeof m.permissions === "object") {
          catalog.permissions = Object.fromEntries(Object.entries(m.permissions).filter(([, v]) => typeof v === "boolean").slice(0, 20));
        }
        if (m.project && typeof m.project === "object") catalog.project = { title: clean(m.project.title, 120), open: m.project.open === true };
        if (typeof m.askEveryEdit === "boolean") catalog.ask = m.askEveryEdit;
        if ("push" in m) catalog.push = cleanPush(m.push);
        await this.ctx.storage.put(`catalog:${att.device}`, catalog);
        const next = { ...att };
        if (typeof m.background === "boolean") next.background = m.background;
        if (typeof m.serviced === "boolean") next.serviced = m.serviced;
        ws.serializeAttachment(next);
        if (rank(next) > rank(att)) this.kickDrain(att.device);
        return;
      }
      case "focus":
        ws.serializeAttachment({ ...att, focus: now(), background: false });
        await this.ctx.storage.put("active", att.device);
        this.kickDrain(att.device);
        return;
      case "pause":
      case "resume": {
        ws.serializeAttachment(att);
        const paused = m.t === "pause";
        await this.ctx.storage.put("paused", paused);
        // Pausa: as chamadas em andamento caem; os jobs voltam para a fila (rodam no Retomar).
        if (paused) for (const [id, p] of this.pending) this.finish(id, p.job ? LOST : errorResult("The person paused AureaMCP in the Aurea app."), p);
        this.broadcast({ t: "paused", paused });
        if (!paused) for (const s of this.readySockets()) this.kickDrain(s.device);
        return;
      }
      case "result": {
        ws.serializeAttachment(att);
        const p = this.pending.get(m.id);
        if (!p || p.device !== att.device) return;
        let content = Array.isArray(m.content) ? m.content.filter((c) => c && (c.type === "text" || c.type === "image")).slice(0, 8) : [];
        if (!content.length) content = [{ type: "text", text: m.ok === false ? String(m.error ?? "failed") : "ok" }];
        const result = { content, ...(m.ok === false ? { isError: true } : {}) };
        if (m.structuredContent && typeof m.structuredContent === "object") result.structuredContent = m.structuredContent;
        this.finish(m.id, JSON.stringify(result).length > RESULT_MAX ? errorResult("Result too large.") : result, p);
        return;
      }
      default:
        return;
    }
  }

  async webSocketClose(ws) {
    const att = ws.deserializeAttachment?.() ?? {};
    if (att.device) this.failDevice(att.device, "The Aurea app disconnected.");
  }

  async webSocketError(ws) { return this.webSocketClose(ws); }

  // --- dispositivo ativo ------------------------------------------------------

  /**
   * O aparelho que recebe as chamadas: o escolhido (último foco / aurea_select_device)
   * se estiver pronto; senão o pronto mais "acordado" (na frente > servido) e com
   * foco mais recente. null = nenhum pronto (a chamada vai para a fila).
   */
  async activeSocket() {
    const ready = this.readySockets();
    if (!ready.length) return null;
    const chosen = await this.ctx.storage.get("active");
    return ready.find((s) => s.device === chosen)
      ?? ready.sort((a, b) => rank(b.att) - rank(a.att) || (b.att.focus ?? 0) - (a.att.focus ?? 0))[0];
  }

  // --- sessões MCP ------------------------------------------------------------

  async openSession({ id, clientInfo, tokenId, tokenName }) {
    const name = String(clientInfo?.name ?? tokenName ?? "agent").replace(/[\u0000-\u001f]/g, "").slice(0, 80);
    const version = String(clientInfo?.version ?? "").slice(0, 40);
    await this.ctx.storage.put(`sess:${id}`, { id, name, version, tokenId, tokenName: String(tokenName ?? "").slice(0, 80), created: now(), seen: now() });
    // Teto de sessões guardadas: as mais antigas saem.
    const all = [...(await this.ctx.storage.list({ prefix: "sess:" })).values()];
    if (all.length > MAX_SESSIONS) {
      const old = all.sort((a, b) => a.seen - b.seen).slice(0, all.length - MAX_SESSIONS).map((s) => `sess:${s.id}`);
      await this.ctx.storage.delete(old);
    }
    this.pushLog({ agent: name, event: "connected" });
    this.broadcast({ t: "agents", agents: await this.agents() });
    return { ok: true };
  }

  async getSession({ id, tokenId }) {
    const s = await this.ctx.storage.get(`sess:${id}`);
    if (!s || s.tokenId !== tokenId || now() - s.seen > SESSION_IDLE_MS) return null;
    if (now() - s.seen > 60_000) { s.seen = now(); await this.ctx.storage.put(`sess:${id}`, s); }
    return s;
  }

  async closeSession({ id }) {
    await this.ctx.storage.delete(`sess:${id}`);
    this.broadcast({ t: "agents", agents: await this.agents() });
    return { ok: true };
  }

  async revokeSessions({ tokenIds }) {
    const ids = new Set(tokenIds ?? []);
    const del = [];
    for (const [k, s] of await this.ctx.storage.list({ prefix: "sess:" })) if (ids.has(s.tokenId)) del.push(k);
    if (del.length) await this.ctx.storage.delete(del);
    // Token revogado: os jobs dele saem da fila (nada roda em nome de quem perdeu o acesso).
    for (const j of await this.jobs()) if (open(j) && ids.has(j.tokenId)) await this.finishJob(j, errorResult("Access revoked."), "failed");
    this.broadcast({ t: "agents", agents: await this.agents() });
    return { ok: true, closed: del.length };
  }

  /** Agentes vistos nas últimas 24 h (o app mostra o nome e o último contato). */
  async agents() {
    const out = [];
    for (const s of (await this.ctx.storage.list({ prefix: "sess:" })).values()) {
      if (now() - s.seen < 24 * 3600 * 1000) out.push({ session: s.id.slice(0, 8), name: s.name, version: s.version, tokenId: s.tokenId, seen: s.seen });
    }
    return out.sort((a, b) => b.seen - a.seen).slice(0, 20);
  }

  pushLog(entry) {
    const line = { at: now(), ...entry };
    this.log.push(line);
    if (this.log.length > LOG_MAX) this.log.splice(0, this.log.length - LOG_MAX);
    this.broadcast({ t: "log", ...line });
  }

  // --- ferramentas ------------------------------------------------------------

  async status() {
    const active = await this.activeSocket();
    const devices = [];
    for (const s of this.liveSockets().filter((x) => x.att.hello)) {
      const c = (await this.ctx.storage.get(`catalog:${s.device}`)) ?? {};
      devices.push({ id: s.device, name: s.att.name, platform: s.att.platform, app: s.att.app, active: s.device === active?.device,
        project: c.project ?? null, permissions: c.permissions ?? {},
        state: rank(s.att) === 2 ? "foreground" : rank(s.att) === 1 ? "background" : "asleep", wakeable: !!c.push });
    }
    // Aparelhos fora do ar que um push consegue acordar (o agente sabe que a fila anda).
    const sleeping = [];
    for (const [k, c] of await this.ctx.storage.list({ prefix: "catalog:" })) {
      const id = k.slice("catalog:".length);
      if (c?.push && !devices.some((d) => d.id === id)) sleeping.push({ id, platform: c.push.provider === "apns" ? "ios" : "android", project: c.project ?? null });
    }
    const queued = (await this.jobs()).filter(open).length;
    return { paused: (await this.ctx.storage.get("paused")) === true, devices, sleeping, queued };
  }

  /** As ferramentas do dispositivo ativo; offline, as do último que conectou (o agente vê o catálogo mesmo assim). */
  async listTools() {
    const active = await this.activeSocket();
    const device = active?.device ?? (await this.ctx.storage.get("active"));
    const catalog = device ? await this.ctx.storage.get(`catalog:${device}`) : null;
    return deviceTools(catalog ?? undefined).map(publicTool);
  }

  async selectDevice({ device }) {
    const s = this.liveSockets().find((x) => x.att.hello && x.device === device);
    if (!s) return { ok: false, error: "That device is not online." };
    await this.ctx.storage.put("active", device);
    return { ok: true };
  }

  finish(id, result, p = this.pending.get(id)) {
    if (!p) return;
    clearTimeout(p.timer);
    this.pending.delete(id);
    if (this.heavy === id) {
      this.heavy = null;
      // Um job pesado pode ter ficado esperando esta: a fila anda de novo.
      queueMicrotask(() => { for (const s of this.readySockets()) this.kickDrain(s.device); });
    }
    if (result !== LOST) this.pushLog({ agent: p.agent, tool: p.tool, ok: !result.isError, ms: now() - p.started, ...(p.job ? { job: true } : {}) });
    p.resolve(result);
  }

  missingText(catalog, tool) {
    const exists = HOST_TOOLS.some((h) => h.name === tool) || deviceTools({ ...catalog, permissions: {} }).some((x) => x.name === tool);
    return exists ? `The person turned off this permission in the Aurea app (${tool}).` : `Unknown tool: ${tool}`;
  }

  /** Uma chamada de ferramenta do agente: valida, manda ao app e espera o resultado (ou põe na fila). */
  async callTool({ tool, args, agent = "agent", timeoutMs, session = null, tokenId = null, requestId = null, queueHeavy = false }) {
    if ((await this.ctx.storage.get("paused")) === true) return errorResult("The person paused AureaMCP in the Aurea app. Ask them to tap Resume.");
    const t = now();
    this.calls = this.calls.filter((x) => t - x < 60_000);
    if (this.calls.length >= RATE_PER_MIN) return errorResult("Too many calls: wait a minute (limit 120 per minute).");
    if (this.pending.size >= MAX_CONCURRENT) return errorResult("Too many calls at once: wait for the running ones.");
    const s = await this.activeSocket();
    if (!s) return this.enqueue({ tool, args, agent, session, tokenId, requestId, queueHeavy });
    const catalog = (await this.ctx.storage.get(`catalog:${s.device}`)) ?? {};
    const def = deviceTools(catalog).find((x) => x.name === tool);
    if (!def) return errorResult(this.missingText(catalog, tool));
    // Ainda há fila deste aparelho: a chamada nova entra atrás dela (a ordem do agente vale).
    if (queueClass(def, { ask: catalog.ask === true }) !== "never" && (await this.queuedFor(s.device)) > 0) {
      return this.enqueue({ tool, args, agent, session, tokenId, requestId, queueHeavy: true, device: s.device,
        waitMs: timeoutMs ?? (def.confirm ? CONFIRM_TIMEOUT_MS : CALL_TIMEOUT_MS) });
    }
    if (HEAVY.has(tool)) {
      if (this.heavy) return errorResult("Aurea is busy with another heavy step; try again when it finishes.");
    }
    this.calls.push(t);
    const id = `c${t.toString(36)}${(++this.seq).toString(36)}`;
    if (HEAVY.has(tool)) this.heavy = id;
    const wait = Math.min(timeoutMs ?? (def.confirm ? CONFIRM_TIMEOUT_MS : CALL_TIMEOUT_MS), CONFIRM_TIMEOUT_MS);
    return new Promise((resolve) => {
      // session/tokenId/requestId: quem pediu (o notifications/cancelled do agente cita o requestId).
      const p = { resolve, tool, agent, device: s.device, started: t, timer: null, session, tokenId, requestId: requestId == null ? null : String(requestId) };
      p.timer = setTimeout(() => {
        this.send(s.ws, { t: "cancel", id });
        this.finish(id, errorResult(def.confirm ? "The person did not approve in time." : "The Aurea app did not answer in time."), p);
      }, wait);
      this.pending.set(id, p);
      this.send(s.ws, { t: "call", id, tool, args: args ?? {}, agent, confirm: def.confirm === true });
    });
  }

  // --- fila (app fechado ou dormindo) --------------------------------------------

  /** Todos os jobs guardados, na ordem de criação (a chave é ordenável). */
  async jobs() { return [...(await this.ctx.storage.list({ prefix: "job:" })).values()]; }

  async queuedFor(device) {
    return (await this.jobs()).filter((j) => open(j) && (!j.device || j.device === device) && j.expiresAt > now()).length;
  }

  /**
   * Sem aparelho pronto: põe a chamada na fila (ou recusa na hora, ver queueClass),
   * acorda o aparelho por push e espera um pouco; voltou a tempo = o resultado de
   * verdade, senão "queued" com o job_id.
   */
  async enqueue({ tool, args, agent, session, tokenId, requestId, queueHeavy, device = null, waitMs = null }) {
    const bound = device ?? (await this.ctx.storage.get("active")) ?? null;
    const catalog = bound ? await this.ctx.storage.get(`catalog:${bound}`) : null;
    if (!catalog) return errorResult("The Aurea app is offline. Ask the person to open Aurea → AureaMCP on their phone or PC.");
    const def = deviceTools(catalog).find((x) => x.name === tool);
    if (!def) return errorResult(this.missingText(catalog, tool));
    const kind = queueClass(def, { ask: catalog.ask === true });
    if (kind === "never") {
      return errorResult(`The Aurea app is offline right now (closed or asleep). ${tool} ${def.confirm || catalog.ask ? "needs the person's approval in the app" : "only makes sense with the app open"}, so it is never queued. Ask the person to open Aurea.`);
    }
    if (kind === "heavy" && !queueHeavy) {
      return errorResult(`The Aurea app is offline right now (closed or asleep). ${tool} is a heavy step and is not queued by default: call aurea_queue_policy {"allowHeavy": true} (or send _meta {"aurea/queue": true}) to queue it, or ask the person to open Aurea. Reads and light edits are queued automatically.`);
    }
    const all = await this.jobs();
    if (all.filter(open).length >= MAX_QUEUED) return errorResult(`The queue is full (${MAX_QUEUED} calls waiting for the Aurea app). Wait for the app to come back.`);
    const t = now();
    this.calls.push(t);
    const rand = Array.from(crypto.getRandomValues(new Uint8Array(3)), (b) => b.toString(16).padStart(2, "0")).join("");
    const id = `j${t.toString(36).padStart(9, "0")}${(++this.seq).toString(36).padStart(4, "0")}${rand}`;
    const job = { id, tool, args: args ?? {}, agent, session, tokenId, requestId: requestId == null ? null : String(requestId),
      device: bound, created: t, expiresAt: t + JOB_TTL_MS, state: "queued", attempts: 0 };
    if (JSON.stringify(job).length > 120 * 1024) return errorResult("Arguments too large to queue.");
    await this.ctx.storage.put(`job:${id}`, job);
    await this.trimJobs(all);
    await this.scheduleAlarm(job.expiresAt);
    this.pushLog({ agent, tool, event: "queued" });
    const done = new Promise((resolve) => this.jobWaiters.set(id, [...(this.jobWaiters.get(id) ?? []), resolve]));
    // Aparelho pronto (a fila está na frente da chamada nova): só drenar. Senão, acordar.
    const woke = device ? 0 : await this.wakeDevices(job, PUSH_GAP_MS);
    this.kickDrain(bound);
    const grace = waitMs ?? (woke > 0 ? this.queueGraceMs : 0);
    if (grace > 0) {
      let timer;
      const result = await Promise.race([done, new Promise((r) => { timer = setTimeout(() => r(null), grace); })]);
      clearTimeout(timer);
      if (result) return result;
    }
    this.jobWaiters.delete(id);
    return {
      content: [{ type: "text", text: `Queued as job_id=${id}: the Aurea app is offline right now (closed or asleep). `
        + (woke > 0 ? "Aurea was asked to wake up; " : "It runs as soon as the Aurea app comes back; ")
        + `queued calls run in order. Check the result with job_status {"job_id":"${id}"} (expires in ${Math.round(JOB_TTL_MS / 60_000)} min).` }],
      structuredContent: { queued: true, job_id: id, state: "queued", expiresAt: job.expiresAt, wakeSent: woke > 0 },
    };
  }

  /** Guarda no máximo MAX_JOB_RECORDS: saem os terminados mais velhos. */
  async trimJobs(all) {
    if (all.length + 1 <= MAX_JOB_RECORDS) return;
    const old = all.filter((j) => j.finished).sort((a, b) => a.finished - b.finished).slice(0, all.length + 1 - MAX_JOB_RECORDS).map((j) => `job:${j.id}`);
    if (old.length) await this.ctx.storage.delete(old);
  }

  async scheduleAlarm(at) {
    try {
      const cur = await this.ctx.storage.getAlarm?.();
      const when = Math.min(at, now() + 60_000);
      if (!cur || cur > when) await this.ctx.storage.setAlarm?.(when);
    } catch { /* sem alarme (ambiente de teste antigo) */ }
  }

  /**
   * Push aos aparelhos que podem rodar o job (o vinculado, ou todos com token)
   * que não estão prontos e não receberam push há menos de `gap`. Devolve quantos foram.
   */
  async wakeDevices(job, gap) {
    const ready = new Set(this.readySockets().map((s) => s.device));
    const queued = (await this.jobs()).filter(open).length;
    let sent = 0;
    for (const [k, c] of await this.ctx.storage.list({ prefix: "catalog:" })) {
      const device = k.slice("catalog:".length);
      if (!c?.push || ready.has(device) || (job?.device && job.device !== device)) continue;
      if (now() - (c.lastPush ?? 0) < gap) continue;
      const r = await this.pushImpl(this.env ?? {}, c.push, { jobs: queued, project: c.project?.title, lang: c.lang });
      c.lastPush = now();
      if (r?.gone) c.push = null; // token morto: o app manda outro no próximo hello
      await this.ctx.storage.put(k, c);
      if (r?.ok) { sent++; this.pushLog({ event: "wake" }); }
    }
    return sent;
  }

  /** Começa (uma vez por aparelho) a reenviar a fila. Não espera: o hello segue. */
  kickDrain(device) {
    if (!device) return null;
    if (this.draining.has(device)) return this.draining.get(device);
    const run = this.drain(device).catch(() => {}).finally(() => this.draining.delete(device));
    this.draining.set(device, run);
    return run;
  }

  /** Reenvia os jobs deste aparelho EM ORDEM, um por vez, enquanto ele estiver pronto. */
  async drain(device) {
    for (;;) {
      if ((await this.ctx.storage.get("paused")) === true) return;
      const s = this.readySockets().find((x) => x.device === device);
      if (!s) return;
      const job = (await this.jobs()).find((j) => open(j) && (!j.device || j.device === device) && !this.pending.has(j.id));
      if (!job) return;
      if (job.expiresAt <= now()) { await this.finishJob(job, errorResult("Expired: the Aurea app did not come back in time."), "expired"); continue; }
      if ((await this.runJob(s, job)) === "lost") return;
    }
  }

  /** Um job no aparelho: "next" (resolvido, segue) ou "lost" (não chegou, fica na fila). */
  async runJob(s, job) {
    const catalog = (await this.ctx.storage.get(`catalog:${s.device}`)) ?? {};
    const def = deviceTools(catalog).find((x) => x.name === job.tool);
    if (!def) { await this.finishJob(job, errorResult(this.missingText(catalog, job.tool)), "failed"); return "next"; }
    // Nunca repetir sozinho o que pede aprovação ou destrói (a pessoa pode ter ligado o "Perguntar antes").
    if (queueClass(def, { ask: catalog.ask === true }) === "never") {
      await this.finishJob(job, errorResult(`${job.tool} needs the person's approval now, so the queued call was not replayed. Call it again with the app open.`), "failed");
      return "next";
    }
    if ((job.attempts ?? 0) >= MAX_JOB_ATTEMPTS) { await this.finishJob(job, errorResult("The Aurea app did not finish this queued call after several tries."), "failed"); return "next"; }
    if (HEAVY.has(job.tool) && this.heavy) return "lost";
    job.state = "sent"; job.attempts = (job.attempts ?? 0) + 1; job.device = s.device;
    await this.ctx.storage.put(`job:${job.id}`, job);
    if (HEAVY.has(job.tool)) this.heavy = job.id;
    const result = await new Promise((resolve) => {
      const p = { resolve, tool: job.tool, agent: job.agent, device: s.device, started: now(), timer: null,
        session: job.session, tokenId: job.tokenId, requestId: job.requestId, job: true };
      p.timer = setTimeout(() => { this.send(s.ws, { t: "cancel", id: job.id }); this.finish(job.id, LOST, p); }, CALL_TIMEOUT_MS);
      this.pending.set(job.id, p);
      this.send(s.ws, { t: "call", id: job.id, tool: job.tool, args: job.args ?? {}, agent: job.agent, confirm: false, job: true, queuedAt: job.created });
    });
    if (result === LOST) {
      const cur = await this.ctx.storage.get(`job:${job.id}`);
      if (cur && !cur.finished) { cur.state = "queued"; await this.ctx.storage.put(`job:${job.id}`, cur); }
      return "lost";
    }
    if (result.cancelled) return "next"; // cancelJob já gravou
    await this.finishJob(job, result, result.isError ? "failed" : "done");
    return "next";
  }

  async finishJob(job, result, state) {
    let kept = result;
    if (JSON.stringify(kept).length > JOB_RESULT_MAX) {
      const text = (result.content ?? []).filter((c) => c.type === "text").map((c) => c.text).join("\n").slice(0, JOB_RESULT_MAX / 2);
      kept = { content: [{ type: "text", text: text || "(result too large to keep; the image was dropped)" }], ...(result.isError ? { isError: true } : {}) };
    }
    const rec = { ...job, state, result: kept, finished: now() };
    delete rec.args;
    await this.ctx.storage.put(`job:${job.id}`, rec);
    const waiters = this.jobWaiters.get(job.id) ?? [];
    this.jobWaiters.delete(job.id);
    for (const w of waiters) w(state === "expired" ? null : result);
  }

  /** job_status: o job deste token (ou os recentes dele). null = não existe para este token. */
  async jobStatus({ tokenId, jobId = null }) {
    const view = (j) => ({ job_id: j.id, tool: j.tool, state: j.state, created: j.created, expiresAt: j.expiresAt, finished: j.finished ?? null, attempts: j.attempts ?? 0 });
    if (jobId) {
      if (!JOB_RE.test(String(jobId))) return null;
      const j = await this.ctx.storage.get(`job:${jobId}`);
      if (!j || j.tokenId !== tokenId) return null;
      return { ...view(j), result: j.result ?? null };
    }
    return { jobs: (await this.jobs()).filter((j) => j.tokenId === tokenId).slice(-20).reverse().map(view) };
  }

  /** aurea_queue_policy: a sessão MCP aceita (ou não) pôr na fila as pesadas. */
  async setQueuePolicy({ id, tokenId, allowHeavy }) {
    const s = id ? await this.ctx.storage.get(`sess:${id}`) : null;
    if (!s || s.tokenId !== tokenId) return { ok: false };
    s.queueHeavy = allowHeavy === true;
    await this.ctx.storage.put(`sess:${id}`, s);
    return { ok: true };
  }

  /** Alarme: expira jobs, repete o push com fila parada, derruba sockets mudos, limpa o velho. */
  async alarm() {
    const t = now();
    const ready = this.readySockets();
    let next = Infinity;
    let queued = 0;
    for (const j of await this.jobs()) {
      if (open(j)) {
        if (this.pending.has(j.id)) { queued++; continue; }
        if (j.expiresAt <= t) { await this.finishJob(j, errorResult("Expired: the Aurea app did not come back in time."), "expired"); continue; }
        queued++;
        next = Math.min(next, j.expiresAt);
      } else if (j.finished && t - j.finished > JOB_KEEP_MS) {
        await this.ctx.storage.delete(`job:${j.id}`);
      } else if (j.finished) {
        next = Math.min(next, j.finished + JOB_KEEP_MS);
      }
    }
    if (queued) {
      await this.wakeDevices(null, PUSH_RETRY_MS);
      for (const s of ready) this.kickDrain(s.device);
      next = Math.min(next, t + 60_000);
    }
    if (Number.isFinite(next)) { try { await this.ctx.storage.setAlarm?.(Math.max(next, t + 1000)); } catch { /* sem alarme */ } }
  }

  /** Cancelamento vindo do agente (notifications/cancelled). */
  async cancelCall({ id }) {
    const p = this.pending.get(id);
    if (!p) return { ok: false };
    if (p.job) return this.cancelJob(id, p);
    const s = this.sockets().find((x) => x.device === p.device);
    if (s) this.send(s.ws, { t: "cancel", id });
    this.finish(id, errorResult("Cancelled."), p);
    return { ok: true };
  }

  /** Cancela um job que está no aparelho agora. */
  async cancelJob(id, p) {
    const s = this.sockets().find((x) => x.device === p.device);
    if (s) this.send(s.ws, { t: "cancel", id });
    const j = await this.ctx.storage.get(`job:${id}`);
    if (j) await this.finishJob(j, errorResult("Cancelled."), "failed");
    this.finish(id, { ...errorResult("Cancelled."), cancelled: true }, p);
    return { ok: true };
  }

  /**
   * notifications/cancelled do agente: acha a chamada pelo requestId do JSON-RPC,
   * só do mesmo token (e da mesma sessão MCP, quando há): um agente nunca cancela
   * a chamada de outro. O app recebe {t:"cancel", id} e aborta a ferramenta.
   * Um job ainda na fila só sai dela.
   */
  async cancelRequest({ session = null, tokenId = null, requestId }) {
    const rid = requestId == null ? null : String(requestId);
    if (rid == null) return { ok: false };
    for (const [id, p] of this.pending) {
      if (p.requestId !== rid || p.tokenId !== tokenId || (p.session ?? null) !== (session ?? null)) continue;
      return this.cancelCall({ id });
    }
    for (const j of await this.jobs()) {
      if (!open(j) || j.requestId !== rid || j.tokenId !== tokenId || (j.session ?? null) !== (session ?? null)) continue;
      await this.finishJob(j, errorResult("Cancelled."), "failed");
      return { ok: true };
    }
    return { ok: false };
  }
}

export const SESSION_TOOL_NAMES = new Set(SESSION_TOOLS.map((t) => t.name));
