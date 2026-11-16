// =============================================================================
//  AureaMcpRelay — o elo entre os agentes (MCP) e o app Aurea da conta.
//  Um Durable Object por conta: idFromName("mcp:" + uid). Ver DESIGN.md §1.3.
//
//  O app abre um WebSocket (sessão da conta, validada pelo Worker) e fica
//  ouvindo; o objeto aceita com a API de HIBERNAÇÃO, então conexão parada não
//  custa duração. O ping "ping"→"pong" é respondido sem acordar o objeto.
//
//  Mensagens app → relay:
//    {t:"hello", device:{name,platform,app}, engineTools:[...], hostTools:[...], permissions:{...}}
//    {t:"result", id, ok, content:[...], error?}
//    {t:"focus"}  {t:"pause"}  {t:"resume"}
//  Relay → app:
//    {t:"welcome", paused, agents}  {t:"call", id, tool, args, agent}  {t:"cancel", id}
//    {t:"agents", agents}  {t:"log", at, agent, tool, ok, ms}  {t:"paused", paused}
//
//  O Worker fala com o objeto por RPC: openSession, getSession, closeSession,
//  revokeSessions, status, listTools, callTool, selectDevice.
// =============================================================================

import { DurableObject } from "cloudflare:workers";
import { SESSION_TOOLS, HOST_TOOLS, HEAVY, deviceTools, publicTool } from "./mcp_catalog.js";

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

const now = () => Date.now();
const errorResult = (text) => ({ content: [{ type: "text", text }], isError: true });

export class AureaMcpRelay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.pending = new Map();   // id da chamada → {resolve, timer, tool, device}
    this.calls = [];            // instantes das últimas chamadas (limite por minuto)
    this.heavy = null;          // ferramenta pesada em andamento (uma por vez)
    this.log = [];
    this.seq = 0;
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
    ws.serializeAttachment({ device, since: now(), hello: false, name: "", platform: "", app: "", focus: now() });
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

  send(ws, msg) { try { ws.send(JSON.stringify(msg)); } catch { /* socket caiu */ } }
  broadcast(msg) { for (const s of this.sockets()) if (s.att.hello) this.send(s.ws, msg); }

  async webSocketMessage(ws, data) {
    if (typeof data !== "string" || data.length > 1024 * 1024) return;
    if (data === "ping") return this.send(ws, "pong");
    let m;
    try { m = JSON.parse(data); } catch { return; }
    const att = ws.deserializeAttachment?.() ?? {};
    if (!att.device || !m || typeof m !== "object") return;
    switch (m.t) {
      case "hello": {
        const d = m.device ?? {};
        const clean = (s, n) => String(s ?? "").replace(/[\u0000-\u001f]/g, "").slice(0, n);
        const permissions = {};
        for (const [k, v] of Object.entries(m.permissions ?? {}).slice(0, 20)) if (typeof v === "boolean") permissions[k] = v;
        const catalog = {
          engineTools: Array.isArray(m.engineTools) ? m.engineTools.slice(0, 120) : null,
          hostTools: Array.isArray(m.hostTools) ? m.hostTools.filter((x) => typeof x === "string").slice(0, 60) : [],
          permissions,
          project: m.project && typeof m.project === "object" ? { title: clean(m.project.title, 120), open: m.project.open === true } : null,
        };
        if (JSON.stringify(catalog).length > 256 * 1024) catalog.engineTools = null;
        ws.serializeAttachment({ ...att, hello: true, name: clean(d.name, 60), platform: clean(d.platform, 20), app: clean(d.app, 20), focus: now() });
        await this.ctx.storage.put(`catalog:${att.device}`, catalog);
        await this.ctx.storage.put("active", att.device);
        this.send(ws, { t: "welcome", paused: (await this.ctx.storage.get("paused")) === true, agents: await this.agents(), log: this.log.slice(-30) });
        return;
      }
      case "state": {
        // O app avisa mudanças de permissão ou de projeto sem refazer o hello.
        const catalog = (await this.ctx.storage.get(`catalog:${att.device}`)) ?? {};
        if (m.permissions && typeof m.permissions === "object") {
          catalog.permissions = Object.fromEntries(Object.entries(m.permissions).filter(([, v]) => typeof v === "boolean").slice(0, 20));
        }
        if (m.project && typeof m.project === "object") catalog.project = { title: String(m.project.title ?? "").slice(0, 120), open: m.project.open === true };
        await this.ctx.storage.put(`catalog:${att.device}`, catalog);
        return;
      }
      case "focus":
        ws.serializeAttachment({ ...att, focus: now() });
        await this.ctx.storage.put("active", att.device);
        return;
      case "pause":
      case "resume": {
        const paused = m.t === "pause";
        await this.ctx.storage.put("paused", paused);
        if (paused) for (const [id, p] of this.pending) this.finish(id, errorResult("The person paused AureaMCP in the Aurea app."), p);
        this.broadcast({ t: "paused", paused });
        return;
      }
      case "result": {
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
    for (const [id, p] of this.pending) if (p.device === att.device) this.finish(id, errorResult("The Aurea app disconnected."), p);
  }

  async webSocketError(ws) { return this.webSocketClose(ws); }

  // --- dispositivo ativo ------------------------------------------------------

  async activeSocket() {
    const online = this.sockets().filter((s) => s.att.hello);
    if (!online.length) return null;
    const chosen = await this.ctx.storage.get("active");
    return online.find((s) => s.device === chosen) ?? online.sort((a, b) => (b.att.focus ?? 0) - (a.att.focus ?? 0))[0];
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
    for (const s of this.sockets().filter((x) => x.att.hello)) {
      const c = (await this.ctx.storage.get(`catalog:${s.device}`)) ?? {};
      devices.push({ id: s.device, name: s.att.name, platform: s.att.platform, app: s.att.app, active: s.device === active?.device, project: c.project ?? null, permissions: c.permissions ?? {} });
    }
    return { paused: (await this.ctx.storage.get("paused")) === true, devices };
  }

  /** As ferramentas do dispositivo ativo; offline, as do último que conectou (o agente vê o catálogo mesmo assim). */
  async listTools() {
    const active = await this.activeSocket();
    const device = active?.device ?? (await this.ctx.storage.get("active"));
    const catalog = device ? await this.ctx.storage.get(`catalog:${device}`) : null;
    return deviceTools(catalog ?? undefined).map(publicTool);
  }

  async selectDevice({ device }) {
    const s = this.sockets().find((x) => x.att.hello && x.device === device);
    if (!s) return { ok: false, error: "That device is not online." };
    await this.ctx.storage.put("active", device);
    return { ok: true };
  }

  finish(id, result, p = this.pending.get(id)) {
    if (!p) return;
    clearTimeout(p.timer);
    this.pending.delete(id);
    if (this.heavy === id) this.heavy = null;
    this.pushLog({ agent: p.agent, tool: p.tool, ok: !result.isError, ms: now() - p.started });
    p.resolve(result);
  }

  /** Uma chamada de ferramenta do agente: valida, manda ao app e espera o resultado. */
  async callTool({ tool, args, agent = "agent", timeoutMs, session = null, tokenId = null, requestId = null }) {
    if ((await this.ctx.storage.get("paused")) === true) return errorResult("The person paused AureaMCP in the Aurea app. Ask them to tap Resume.");
    const t = now();
    this.calls = this.calls.filter((x) => t - x < 60_000);
    if (this.calls.length >= RATE_PER_MIN) return errorResult("Too many calls: wait a minute (limit 120 per minute).");
    if (this.pending.size >= MAX_CONCURRENT) return errorResult("Too many calls at once: wait for the running ones.");
    const s = await this.activeSocket();
    if (!s) return errorResult("The Aurea app is offline. Ask the person to open Aurea → AureaMCP on their phone or PC.");
    const catalog = (await this.ctx.storage.get(`catalog:${s.device}`)) ?? {};
    const def = deviceTools(catalog).find((x) => x.name === tool);
    if (!def) {
      const exists = HOST_TOOLS.some((h) => h.name === tool) || deviceTools({ ...catalog, permissions: {} }).some((x) => x.name === tool);
      return errorResult(exists ? `The person turned off this permission in the Aurea app (${tool}).` : `Unknown tool: ${tool}`);
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

  /** Cancelamento vindo do agente (notifications/cancelled). */
  async cancelCall({ id }) {
    const p = this.pending.get(id);
    if (!p) return { ok: false };
    const s = this.sockets().find((x) => x.device === p.device);
    if (s) this.send(s.ws, { t: "cancel", id });
    this.finish(id, errorResult("Cancelled."), p);
    return { ok: true };
  }

  /**
   * notifications/cancelled do agente: acha a chamada pelo requestId do JSON-RPC,
   * só do mesmo token (e da mesma sessão MCP, quando há): um agente nunca cancela
   * a chamada de outro. O app recebe {t:"cancel", id} e aborta a ferramenta.
   */
  async cancelRequest({ session = null, tokenId = null, requestId }) {
    const rid = requestId == null ? null : String(requestId);
    if (rid == null) return { ok: false };
    for (const [id, p] of this.pending) {
      if (p.requestId !== rid || p.tokenId !== tokenId || (p.session ?? null) !== (session ?? null)) continue;
      return this.cancelCall({ id });
    }
    return { ok: false };
  }
}

export const SESSION_TOOL_NAMES = new Set(SESSION_TOOLS.map((t) => t.name));
