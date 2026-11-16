// =============================================================================
//  AureaMCP — credenciais dos agentes (docs/aureamcp/DESIGN.md §1.2, §3).
//
//  Dois caminhos, a mesma tabela (mcp_tokens, só o SHA-256 do token):
//    A. Token pessoal (Claude Code, Codex CLI): o app cria com a sessão da conta.
//    B. OAuth 2.1 + registro dinâmico (Claude.ai, Claude Desktop, ChatGPT):
//         GET  /.well-known/oauth-protected-resource[/mcp]   RFC 9728
//         GET  /.well-known/oauth-authorization-server         RFC 8414
//         POST /mcp/oauth/register                             RFC 7591 (clientes públicos)
//         GET  /mcp/oauth/authorize                            página com o código de pareamento
//         GET  /mcp/oauth/authorize/status?req=                a página consulta (cookie do navegador)
//         POST /mcp/oauth/token                                authorization_code (PKCE S256) | refresh_token
//         POST /mcp/oauth/revoke                               RFC 7009
//       A página NÃO pede senha: mostra um código; a pessoa digita no app (que tem
//       a sessão da conta) e aprova. O app usa:
//         POST /api/mcp/pair/lookup  {code}         → quem está pedindo
//         POST /api/mcp/pair/approve {code, allow}  → aprova/recusa
//  Gestão no app (sessão da conta):
//         GET  /api/mcp/tokens                 → agentes com acesso
//         POST /api/mcp/tokens {name}          → token pessoal (mostrado UMA vez)
//         POST /api/mcp/tokens/revoke {id}|{all:true}
// =============================================================================

import { json, aleatorio, b64, sha256hex, dentroDoLimite, lerJson, lerSessao } from "./contas.js";

export const TOKEN_RE = /^amcp_[A-Za-z0-9_-]{43}$/;
export const ACCESS_TTL_MS = 24 * 3600 * 1000;
export const REFRESH_TTL_MS = 90 * 24 * 3600 * 1000;
export const PAT_TTL_MS = 365 * 24 * 3600 * 1000;
export const REQUEST_TTL_MS = 10 * 60 * 1000;
export const CODE_TTL_MS = 60 * 1000;
const PAT_MAX = 20;
const GRANTS_MAX = 50;

export const LIMITES_MCP = {
  registro: { max: 20, janela: 3600 },
  autorizar: { max: 30, janela: 3600 },
  parConta: { max: 10, janela: 600 },
  parIp: { max: 30, janela: 3600 },
  token: { max: 60, janela: 900 },
  criarToken: { max: 10, janela: 3600 },
};

const b64url = (bytes) => b64(bytes).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
export const novoToken = () => "amcp_" + b64url(aleatorio(32));
const ip = (req) => req.headers.get("cf-connecting-ip") ?? "local";
export const origem = (url) => `${url.protocol}//${url.host}`;

// Código de pareamento: 8 letras de um alfabeto sem 0/O/1/I (32 símbolos → sem viés com bytes).
const ALFABETO = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
export function novoCodigo() {
  const b = aleatorio(8);
  let s = "";
  for (const x of b) s += ALFABETO[x % 32];
  return s.slice(0, 4) + "-" + s.slice(4);
}
export const normalizarCodigo = (c) => {
  const s = String(c ?? "").toUpperCase().replace(/[^A-Z0-9]/g, "");
  return s.length === 8 && [...s].every((x) => ALFABETO.includes(x)) ? s : null;
};

const CORS = {
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET, POST, DELETE, OPTIONS",
  "access-control-allow-headers": "authorization, content-type, mcp-session-id, mcp-protocol-version, last-event-id",
  "access-control-expose-headers": "mcp-session-id, www-authenticate",
  "access-control-max-age": "86400",
};
export const jsonCors = (corpo, status = 200, extra = {}) => json(corpo, status, { ...CORS, ...extra });
export const preflight = () => new Response(null, { status: 204, headers: CORS });

// -----------------------------------------------------------------------------
//  Leitura do token de um pedido ao /mcp
// -----------------------------------------------------------------------------
export async function lerTokenMcp(env, req, agora = Date.now()) {
  const m = /^Bearer (amcp_[A-Za-z0-9_-]{43})$/.exec(req.headers.get("authorization") ?? "");
  if (!m) return null;
  const hash = await sha256hex(m[1]);
  const t = await env.AUREA_DB.prepare(
    "SELECT t.id, t.uid, t.kind, t.name, t.client_id, t.token_expires_at, t.revoked_at, t.last_used_at FROM mcp_tokens t JOIN users u ON u.id = t.uid WHERE t.token_hash = ?")
    .bind(hash).first();
  if (!t || t.revoked_at || t.token_expires_at <= agora) return null;
  // last_used_at no máximo a cada 5 min (o app mostra "último uso"; o D1 não precisa de uma escrita por chamada).
  if (!t.last_used_at || agora - t.last_used_at > 5 * 60 * 1000) {
    await env.AUREA_DB.prepare("UPDATE mcp_tokens SET last_used_at = ? WHERE id = ?").bind(agora, t.id).run().catch(() => {});
  }
  return { id: t.id, uid: t.uid, kind: t.kind, name: t.name, clientId: t.client_id };
}

export function naoAutorizado(url, detalhe = "invalid_token") {
  const meta = `${origem(url)}/.well-known/oauth-protected-resource`;
  return jsonCors({ jsonrpc: "2.0", id: null, error: { code: -32001, message: "Unauthorized: connect AureaMCP from the Aurea app (AureaMCP → Connect agent) or use a token." } }, 401,
    { "www-authenticate": `Bearer resource_metadata="${meta}", error="${detalhe}"` });
}

// -----------------------------------------------------------------------------
//  Metadados (descoberta)
// -----------------------------------------------------------------------------
export function metadadosRecurso(url) {
  return {
    resource: `${origem(url)}/mcp`,
    authorization_servers: [origem(url)],
    bearer_methods_supported: ["header"],
    resource_name: "AureaMCP",
    resource_documentation: `${origem(url)}/mcp/oauth/authorize`,
  };
}

export function metadadosServidor(url) {
  const o = origem(url);
  return {
    issuer: o,
    authorization_endpoint: `${o}/mcp/oauth/authorize`,
    token_endpoint: `${o}/mcp/oauth/token`,
    registration_endpoint: `${o}/mcp/oauth/register`,
    revocation_endpoint: `${o}/mcp/oauth/revoke`,
    response_types_supported: ["code"],
    grant_types_supported: ["authorization_code", "refresh_token"],
    code_challenge_methods_supported: ["S256"],
    token_endpoint_auth_methods_supported: ["none"],
    revocation_endpoint_auth_methods_supported: ["none"],
    scopes_supported: ["aurea"],
  };
}

// -----------------------------------------------------------------------------
//  Registro dinâmico (RFC 7591): só clientes públicos, redirect https ou loopback
// -----------------------------------------------------------------------------
export function redirectValido(u) {
  if (typeof u !== "string" || u.length > 512 || u.includes("#")) return false;
  let p;
  try { p = new URL(u); } catch { return false; }
  if (p.username || p.password) return false;
  if (p.protocol === "https:") return true;
  return p.protocol === "http:" && ["127.0.0.1", "localhost", "[::1]"].includes(p.hostname);
}

async function registrar(req, env) {
  if (!(await dentroDoLimite(env, "mcp-register", ip(req), LIMITES_MCP.registro))) return jsonCors({ error: "slow_down" }, 429, { "retry-after": "3600" });
  const { valor, erro } = await lerJson(req, 8192);
  if (erro) return jsonCors({ error: "invalid_client_metadata", error_description: erro }, 400);
  const uris = valor.redirect_uris;
  if (!Array.isArray(uris) || uris.length < 1 || uris.length > 5 || !uris.every(redirectValido)) {
    return jsonCors({ error: "invalid_redirect_uri", error_description: "https or loopback http redirect URIs only" }, 400);
  }
  const metodo = valor.token_endpoint_auth_method ?? "none";
  if (metodo !== "none") return jsonCors({ error: "invalid_client_metadata", error_description: "public clients only (token_endpoint_auth_method none)" }, 400);
  const grants = valor.grant_types ?? ["authorization_code", "refresh_token"];
  if (!Array.isArray(grants) || grants.some((g) => !["authorization_code", "refresh_token"].includes(g))) return jsonCors({ error: "invalid_client_metadata", error_description: "grant_types" }, 400);
  const nome = String(valor.client_name ?? "MCP client").replace(/[\u0000-\u001f<>]/g, "").trim().slice(0, 80) || "MCP client";
  const id = "amcpc_" + b64url(aleatorio(18));
  const agora = Date.now();
  await env.AUREA_DB.prepare("INSERT INTO mcp_clients (id, name, redirect_uris, created_at) VALUES (?, ?, ?, ?)").bind(id, nome, JSON.stringify(uris), agora).run();
  return jsonCors({
    client_id: id, client_id_issued_at: Math.floor(agora / 1000), client_name: nome, redirect_uris: uris,
    grant_types: ["authorization_code", "refresh_token"], response_types: ["code"], token_endpoint_auth_method: "none",
  }, 201);
}

// -----------------------------------------------------------------------------
//  Autorização: a página com o código
// -----------------------------------------------------------------------------
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);

function paginaHtml(corpo, nonce = "", status = 200, extra = {}) {
  const html = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AureaMCP</title>
<style>:root{color-scheme:dark}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0e0b16;color:#f2f0f7;font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:440px;padding:32px 20px;text-align:center}h1{font-size:22px;margin:0 0 6px}p{color:#b9b2c9;margin:8px 0}
.code{font:700 34px/1.2 ui-monospace,Menlo,Consolas,monospace;letter-spacing:4px;background:#1c1530;border:1px solid #3b2a66;border-radius:14px;padding:14px 18px;margin:20px 0;color:#fff}
a.btn{display:inline-block;margin-top:10px;padding:12px 20px;border-radius:12px;background:linear-gradient(90deg,#7c3aed,#a855f7);color:#fff;text-decoration:none;font-weight:600}
.small{font-size:13px;color:#8a82a0}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#a855f7;margin-right:6px;animation:p 1.2s infinite}@keyframes p{50%{opacity:.25}}</style></head>
<body><main>${corpo}</main></body></html>`;
  return new Response(html, {
    status,
    headers: {
      "content-type": "text/html; charset=utf-8", "cache-control": "no-store", "x-content-type-options": "nosniff", "referrer-policy": "no-referrer",
      "x-frame-options": "DENY",
      "content-security-policy": `default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce || "x"}'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`,
      ...extra,
    },
  });
}

const pt = (req) => /^pt/i.test(req.headers.get("accept-language") ?? "");
const erroPagina = (req, msg) => paginaHtml(`<h1>AureaMCP</h1><p>${esc(msg)}</p>`, "", 400);

function redirecionarErro(redirect, state, erro) {
  const u = new URL(redirect);
  u.searchParams.set("error", erro);
  if (state) u.searchParams.set("state", state);
  return Response.redirect(u.toString(), 302);
}

async function autorizar(req, env, url) {
  const q = url.searchParams;
  const clientId = q.get("client_id") ?? "";
  const cliente = /^amcpc_[A-Za-z0-9_-]{24}$/.test(clientId)
    ? await env.AUREA_DB.prepare("SELECT id, name, redirect_uris FROM mcp_clients WHERE id = ?").bind(clientId).first() : null;
  if (!cliente) return erroPagina(req, pt(req) ? "Aplicativo desconhecido. Adicione o conector de novo." : "Unknown client. Add the connector again.");
  const uris = JSON.parse(cliente.redirect_uris);
  const redirect = q.get("redirect_uri") ?? (uris.length === 1 ? uris[0] : "");
  if (!uris.includes(redirect)) return erroPagina(req, pt(req) ? "Endereço de retorno não registrado." : "Redirect URI not registered.");
  const state = (q.get("state") ?? "").slice(0, 1024);
  if (q.get("response_type") !== "code") return redirecionarErro(redirect, state, "unsupported_response_type");
  const challenge = q.get("code_challenge") ?? "";
  if (q.get("code_challenge_method") !== "S256" || !/^[A-Za-z0-9_-]{43,128}$/.test(challenge)) return redirecionarErro(redirect, state, "invalid_request");
  const resource = q.get("resource") ?? "";
  if (resource && resource.replace(/\/+$/, "") !== `${origem(url)}/mcp` && resource.replace(/\/+$/, "") !== origem(url)) return redirecionarErro(redirect, state, "invalid_target");
  if (!(await dentroDoLimite(env, "mcp-authorize", ip(req), LIMITES_MCP.autorizar))) return erroPagina(req, pt(req) ? "Muitas tentativas. Tente daqui a pouco." : "Too many attempts. Try again later.");

  const id = crypto.randomUUID();
  const codigo = novoCodigo();
  const navegador = b64url(aleatorio(32));
  const agora = Date.now();
  await env.AUREA_DB.prepare(
    "INSERT INTO mcp_auth_requests (id, client_id, redirect_uri, code_challenge, state, scope, resource, user_code_hash, browser_hash, status, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?, ?)")
    .bind(id, cliente.id, redirect, challenge, state, (q.get("scope") ?? "").slice(0, 200), resource, await sha256hex(normalizarCodigo(codigo)), await sha256hex(navegador), agora, agora + REQUEST_TTL_MS).run();

  const P = pt(req);
  const nonce = b64url(aleatorio(16));
  const host = new URL(redirect).host;
  const corpo = `<h1>${P ? "Conectar ao Aurea" : "Connect to Aurea"}</h1>
<p>${P ? `<b>${esc(cliente.name)}</b> (${esc(host)}) quer controlar o seu Aurea pelo AureaMCP.` : `<b>${esc(cliente.name)}</b> (${esc(host)}) wants to control your Aurea through AureaMCP.`}</p>
<p>${P ? "Abra o Aurea → AureaMCP → <b>Conectar agente</b> e digite:" : "Open Aurea → AureaMCP → <b>Connect agent</b> and enter:"}</p>
<div class="code" id="c">${esc(codigo)}</div>
<a class="btn" href="aurea://mcp/pair?code=${encodeURIComponent(codigo)}">${P ? "Abrir no Aurea" : "Open in Aurea"}</a>
<p class="small" id="s"><span class="dot"></span>${P ? "Esperando a aprovação no app…" : "Waiting for approval in the app…"}</p>
<p class="small">${P ? "Só aprove um código que você está vendo agora no seu navegador. Vale 10 minutos." : "Only approve a code you see right now in your own browser. Valid for 10 minutes."}</p>
<script nonce="${nonce}">
(function(){var s=document.getElementById('s');var t=setInterval(function(){fetch('/mcp/oauth/authorize/status?req=${id}',{credentials:'same-origin',cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
if(d.redirect){clearInterval(t);s.textContent=${JSON.stringify(P ? "Aprovado. Voltando…" : "Approved. Returning…")};location.href=d.redirect;}
else if(d.status==='expired'||d.status==='gone'){clearInterval(t);s.textContent=${JSON.stringify(P ? "O código venceu. Comece de novo no seu agente." : "The code expired. Start again in your agent.")};}
else if(d.status==='issued'){clearInterval(t);s.textContent=${JSON.stringify(P ? "Pronto. Pode fechar esta página." : "Done. You can close this page.")};}
}).catch(function(){})},2000);})();
</script>`;
  return paginaHtml(corpo, nonce, 200, {
    "set-cookie": `__Host-amcp_auth=${navegador}; Path=/; Max-Age=${REQUEST_TTL_MS / 1000}; HttpOnly; Secure; SameSite=Lax`,
  });
}

function cookie(req, nome) {
  for (const parte of (req.headers.get("cookie") ?? "").split(";")) {
    const [k, ...v] = parte.trim().split("=");
    if (k === nome) return v.join("=");
  }
  return null;
}

async function statusAutorizacao(req, env, url) {
  const id = url.searchParams.get("req") ?? "";
  const navegador = cookie(req, "__Host-amcp_auth");
  if (!/^[0-9a-f-]{36}$/.test(id) || !navegador) return jsonCors({ status: "gone" }, 404);
  const r = await env.AUREA_DB.prepare("SELECT id, redirect_uri, state, status, browser_hash, expires_at FROM mcp_auth_requests WHERE id = ?").bind(id).first();
  if (!r || r.browser_hash !== (await sha256hex(navegador))) return jsonCors({ status: "gone" }, 404);
  const agora = Date.now();
  if (r.status === "pending") return jsonCors({ status: r.expires_at <= agora ? "expired" : "pending" });
  if (r.status === "denied") {
    const u = new URL(r.redirect_uri); u.searchParams.set("error", "access_denied"); if (r.state) u.searchParams.set("state", r.state);
    return jsonCors({ status: "denied", redirect: u.toString() });
  }
  if (r.status !== "approved") return jsonCors({ status: "issued" });
  // Aprovado: o código nasce AGORA, só para quem tem o cookie desta página, e vale 60 s.
  const codigo = b64url(aleatorio(32));
  const virou = await env.AUREA_DB.prepare("UPDATE mcp_auth_requests SET status = 'issued', code_hash = ?, code_expires_at = ? WHERE id = ? AND status = 'approved' RETURNING id")
    .bind(await sha256hex(codigo), agora + CODE_TTL_MS, id).first();
  if (!virou) return jsonCors({ status: "issued" });
  const u = new URL(r.redirect_uri);
  u.searchParams.set("code", codigo);
  if (r.state) u.searchParams.set("state", r.state);
  u.searchParams.set("iss", origem(url));
  return jsonCors({ status: "approved", redirect: u.toString() });
}

// -----------------------------------------------------------------------------
//  Token
// -----------------------------------------------------------------------------
async function lerForm(req) {
  const tipo = (req.headers.get("content-type") ?? "").toLowerCase();
  if (tipo.startsWith("application/json")) { const { valor } = await lerJson(req, 8192); return valor ?? null; }
  if (!tipo.startsWith("application/x-www-form-urlencoded")) return null;
  const corpo = await req.text();
  if (corpo.length > 8192) return null;
  return Object.fromEntries(new URLSearchParams(corpo));
}

const erroToken = (erro, descricao) => jsonCors({ error: erro, ...(descricao ? { error_description: descricao } : {}) }, 400, { "cache-control": "no-store" });

async function s256(verifier) {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier)));
  return b64url(d);
}

async function emitir(env, { uid, kind, name, clientId, scope, ttl }) {
  const access = novoToken();
  const refresh = kind === "oauth" ? novoToken() : null;
  const agora = Date.now();
  const id = crypto.randomUUID();
  await env.AUREA_DB.prepare(
    "INSERT INTO mcp_tokens (id, uid, kind, name, client_id, token_hash, token_expires_at, refresh_hash, refresh_expires_at, scope, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
    .bind(id, uid, kind, name, clientId ?? null, await sha256hex(access), agora + ttl, refresh ? await sha256hex(refresh) : null, refresh ? agora + REFRESH_TTL_MS : null, scope ?? "", agora).run();
  // Teto de concessões por conta: as mais antigas caem.
  const max = kind === "pat" ? PAT_MAX : GRANTS_MAX;
  await env.AUREA_DB.prepare(
    `UPDATE mcp_tokens SET revoked_at = ? WHERE uid = ? AND kind = ? AND revoked_at IS NULL AND id NOT IN (SELECT id FROM mcp_tokens WHERE uid = ? AND kind = ? AND revoked_at IS NULL ORDER BY created_at DESC LIMIT ${max})`)
    .bind(agora, uid, kind, uid, kind).run();
  return { id, access, refresh };
}

async function token(req, env) {
  if (!(await dentroDoLimite(env, "mcp-token", ip(req), LIMITES_MCP.token))) return jsonCors({ error: "slow_down" }, 429, { "retry-after": "900" });
  const f = await lerForm(req);
  if (!f) return erroToken("invalid_request", "form body expected");
  const agora = Date.now();
  if (f.grant_type === "authorization_code") {
    const code = String(f.code ?? "");
    if (!/^[A-Za-z0-9_-]{43}$/.test(code)) return erroToken("invalid_grant");
    // Queima o código na hora (uso único), antes de conferir o resto.
    const r = await env.AUREA_DB.prepare(
      "UPDATE mcp_auth_requests SET status = 'used' WHERE code_hash = ? AND status = 'issued' RETURNING client_id, redirect_uri, code_challenge, uid, scope, code_expires_at")
      .bind(await sha256hex(code)).first();
    if (!r || !r.uid || r.code_expires_at <= agora) return erroToken("invalid_grant");
    if (f.client_id && f.client_id !== r.client_id) return erroToken("invalid_grant", "client mismatch");
    if ((f.redirect_uri ?? r.redirect_uri) !== r.redirect_uri) return erroToken("invalid_grant", "redirect_uri mismatch");
    const verifier = String(f.code_verifier ?? "");
    if (!/^[A-Za-z0-9._~-]{43,128}$/.test(verifier) || (await s256(verifier)) !== r.code_challenge) return erroToken("invalid_grant", "PKCE");
    const cliente = await env.AUREA_DB.prepare("SELECT name FROM mcp_clients WHERE id = ?").bind(r.client_id).first();
    const t = await emitir(env, { uid: r.uid, kind: "oauth", name: cliente?.name ?? "MCP client", clientId: r.client_id, scope: r.scope, ttl: ACCESS_TTL_MS });
    return jsonCors({ access_token: t.access, token_type: "Bearer", expires_in: ACCESS_TTL_MS / 1000, refresh_token: t.refresh, scope: r.scope || "aurea" }, 200, { "cache-control": "no-store" });
  }
  if (f.grant_type === "refresh_token") {
    const rt = String(f.refresh_token ?? "");
    if (!TOKEN_RE.test(rt)) return erroToken("invalid_grant");
    const h = await sha256hex(rt);
    const t = await env.AUREA_DB.prepare("SELECT id, client_id, refresh_expires_at, revoked_at, scope FROM mcp_tokens WHERE refresh_hash = ?").bind(h).first();
    if (!t) {
      // Refresh já trocado reapareceu: vazou. A concessão inteira cai.
      await env.AUREA_DB.prepare("UPDATE mcp_tokens SET revoked_at = ? WHERE prev_refresh_hash = ? AND revoked_at IS NULL").bind(agora, h).run();
      return erroToken("invalid_grant");
    }
    if (t.revoked_at || t.refresh_expires_at <= agora || (f.client_id && f.client_id !== t.client_id)) return erroToken("invalid_grant");
    const access = novoToken(), refresh = novoToken();
    const ok = await env.AUREA_DB.prepare(
      "UPDATE mcp_tokens SET token_hash = ?, token_expires_at = ?, prev_refresh_hash = refresh_hash, refresh_hash = ?, refresh_expires_at = ? WHERE id = ? AND refresh_hash = ? AND revoked_at IS NULL RETURNING id")
      .bind(await sha256hex(access), agora + ACCESS_TTL_MS, await sha256hex(refresh), agora + REFRESH_TTL_MS, t.id, h).first();
    if (!ok) return erroToken("invalid_grant");
    return jsonCors({ access_token: access, token_type: "Bearer", expires_in: ACCESS_TTL_MS / 1000, refresh_token: refresh, scope: t.scope || "aurea" }, 200, { "cache-control": "no-store" });
  }
  return erroToken("unsupported_grant_type");
}

async function revogarOAuth(req, env, kick) {
  const f = await lerForm(req);
  const t = String(f?.token ?? "");
  if (TOKEN_RE.test(t)) {
    const h = await sha256hex(t);
    const r = await env.AUREA_DB.prepare("UPDATE mcp_tokens SET revoked_at = ? WHERE (token_hash = ? OR refresh_hash = ?) AND revoked_at IS NULL RETURNING id, uid")
      .bind(Date.now(), h, h).first();
    if (r && kick) await kick(env, r.uid, [r.id]).catch(() => {});
  }
  return jsonCors({}, 200); // RFC 7009: sempre 200
}

// -----------------------------------------------------------------------------
//  Rotas do app (sessão da conta): pareamento e gestão dos agentes
// -----------------------------------------------------------------------------
async function pedidoPorCodigo(env, codigo, agora) {
  const n = normalizarCodigo(codigo);
  if (!n) return null;
  return env.AUREA_DB.prepare(
    "SELECT r.id, r.redirect_uri, r.expires_at, c.name FROM mcp_auth_requests r JOIN mcp_clients c ON c.id = r.client_id WHERE r.user_code_hash = ? AND r.status = 'pending' AND r.expires_at > ?")
    .bind(await sha256hex(n), agora).first();
}

async function parear(req, env, s, aprovar) {
  if (!(await dentroDoLimite(env, "mcp-pair-uid", s.uid, LIMITES_MCP.parConta)) || !(await dentroDoLimite(env, "mcp-pair-ip", ip(req), LIMITES_MCP.parIp))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "600" });
  }
  const { valor, erro } = await lerJson(req, 1024);
  if (erro) return json({ error: erro }, 400);
  const agora = Date.now();
  const r = await pedidoPorCodigo(env, valor.code, agora);
  if (!r) return json({ error: "codigo_invalido" }, 404);
  if (!aprovar) return json({ client: r.name, redirectHost: new URL(r.redirect_uri).host, expiresIn: Math.max(0, Math.round((r.expires_at - agora) / 1000)) });
  const status = valor.allow === true ? "approved" : "denied";
  const ok = await env.AUREA_DB.prepare("UPDATE mcp_auth_requests SET status = ?, uid = ? WHERE id = ? AND status = 'pending' RETURNING id").bind(status, s.uid, r.id).first();
  if (!ok) return json({ error: "codigo_invalido" }, 404);
  return json({ ok: true, status, client: r.name });
}

async function listarTokens(env, s) {
  const r = await env.AUREA_DB.prepare(
    "SELECT id, kind, name, created_at, last_used_at, token_expires_at, refresh_expires_at FROM mcp_tokens WHERE uid = ? AND revoked_at IS NULL ORDER BY created_at DESC LIMIT 100")
    .bind(s.uid).all();
  const agora = Date.now();
  const tokens = (r.results ?? [])
    .filter((t) => (t.kind === "pat" ? t.token_expires_at : t.refresh_expires_at ?? t.token_expires_at) > agora)
    .map((t) => ({ id: t.id, kind: t.kind, name: t.name, createdAt: t.created_at, lastUsedAt: t.last_used_at, expiresAt: t.kind === "pat" ? t.token_expires_at : t.refresh_expires_at }));
  return json({ tokens });
}

async function criarTokenPessoal(req, env, s, url) {
  if (!(await dentroDoLimite(env, "mcp-pat", s.uid, LIMITES_MCP.criarToken))) return json({ error: "muitas_tentativas" }, 429, { "retry-after": "3600" });
  const { valor, erro } = await lerJson(req, 1024);
  if (erro) return json({ error: erro }, 400);
  const nome = String(valor.name ?? "").replace(/[\u0000-\u001f<>]/g, "").trim().slice(0, 60) || "Claude Code / Codex";
  const t = await emitir(env, { uid: s.uid, kind: "pat", name: nome, scope: "aurea", ttl: PAT_TTL_MS });
  return json({ id: t.id, token: t.access, name: nome, url: `${origem(url)}/mcp`, expiresAt: Date.now() + PAT_TTL_MS }, 201);
}

async function revogarTokens(req, env, s, kick) {
  const { valor, erro } = await lerJson(req, 1024);
  if (erro) return json({ error: erro }, 400);
  const agora = Date.now();
  let r;
  if (valor.all === true) r = await env.AUREA_DB.prepare("UPDATE mcp_tokens SET revoked_at = ? WHERE uid = ? AND revoked_at IS NULL RETURNING id").bind(agora, s.uid).all();
  else if (typeof valor.id === "string" && valor.id.length <= 64) r = await env.AUREA_DB.prepare("UPDATE mcp_tokens SET revoked_at = ? WHERE uid = ? AND id = ? AND revoked_at IS NULL RETURNING id").bind(agora, s.uid, valor.id).all();
  else return json({ error: "id_invalido" }, 400);
  const ids = (r.results ?? []).map((x) => x.id);
  if (ids.length && kick) await kick(env, s.uid, ids).catch(() => {});
  return json({ ok: true, revoked: ids.length });
}

/**
 * Devolve a resposta quando a rota é de autenticação do MCP; null para seguir.
 * `kick(env, uid, tokenIds)` derruba as sessões MCP dos tokens revogados (relay).
 */
export async function rotaDeAuthMcp(req, env, ctx, url, { kick } = {}) {
  const rota = url.pathname.replace(/\/+$/, "");
  const ehOAuth = rota.startsWith("/.well-known/oauth-") || rota.startsWith("/mcp/oauth/");
  const ehApp = rota.startsWith("/api/mcp/pair/") || rota === "/api/mcp/tokens" || rota === "/api/mcp/tokens/revoke";
  if (!ehOAuth && !ehApp) return null;
  if (req.method === "OPTIONS" && ehOAuth) return preflight();

  if (rota === "/.well-known/oauth-protected-resource" || rota === "/.well-known/oauth-protected-resource/mcp") {
    return req.method === "GET" ? jsonCors(metadadosRecurso(url), 200, { "cache-control": "public, max-age=3600" }) : jsonCors({ error: "metodo" }, 405);
  }
  if (rota === "/.well-known/oauth-authorization-server" || rota === "/.well-known/oauth-authorization-server/mcp") {
    return req.method === "GET" ? jsonCors(metadadosServidor(url), 200, { "cache-control": "public, max-age=3600" }) : jsonCors({ error: "metodo" }, 405);
  }
  if (!env.AUREA_DB) return json({ error: "contas_indisponiveis" }, 503);
  if (rota === "/mcp/oauth/register" && req.method === "POST") return registrar(req, env);
  if (rota === "/mcp/oauth/authorize" && req.method === "GET") return autorizar(req, env, url);
  if (rota === "/mcp/oauth/authorize/status" && req.method === "GET") return statusAutorizacao(req, env, url);
  if (rota === "/mcp/oauth/token" && req.method === "POST") return token(req, env);
  if (rota === "/mcp/oauth/revoke" && req.method === "POST") return revogarOAuth(req, env, kick);
  if (ehOAuth) return jsonCors({ error: "nao_encontrado" }, 404);

  const s = await lerSessao(env, req);
  if (!s) return json({ error: "nao_autorizado" }, 401);
  if (rota === "/api/mcp/pair/lookup" && req.method === "POST") return parear(req, env, s, false);
  if (rota === "/api/mcp/pair/approve" && req.method === "POST") return parear(req, env, s, true);
  if (rota === "/api/mcp/tokens" && req.method === "GET") return listarTokens(env, s);
  if (rota === "/api/mcp/tokens" && req.method === "POST") return criarTokenPessoal(req, env, s, url);
  if (rota === "/api/mcp/tokens/revoke" && req.method === "POST") return revogarTokens(req, env, s, kick);
  return json({ error: "nao_encontrado" }, 404);
}
