// =============================================================================
//  Contas do Aurea — cadastro OBRIGATÓRIO com e-mail e senha.
//
//  Rotas (JSON; só HTTPS — o workers.dev entrega o TLS):
//    POST /api/auth/signup   {email, password} → 201 {token, email, users}
//    POST /api/auth/login    {email, password} → 200 {token, email}
//    GET  /api/auth/session  Bearer <token>    → 200 {email} | 401
//    POST /api/auth/logout   Bearer <token>    → 200 {ok}
//    GET  /api/stats/users                     → 200 {count}  (público, cache curto)
//
//  Onde mora cada coisa:
//    D1 (AUREA_DB)  users(id, email UNIQUE, password_hash, created_at, last_login_at)
//                   counters('users') — somado NA MESMA transação do INSERT, então
//                   o número de cadastrados é exato mesmo com cadastros simultâneos.
//    KV (AUREA_KV)  sess:<sha256(token)>  a sessão (TTL 30 dias). O token em si
//                   NUNCA é gravado: só o hash dele. Vazou o KV, não vazou sessão.
//                   rl:<escopo>:<sha256(id)>:<janela>  contadores de limite (TTL).
//
//  Senha: PBKDF2-SHA256 pelo WebCrypto, sal aleatório de 16 bytes por usuário,
//  100 000 iterações (o teto do Workers). Guardada como
//  `pbkdf2_sha256$<iter>$<sal b64>$<hash b64>` — nada em texto puro, nada no log.
// =============================================================================

export const PBKDF2_ITERACOES = 100000;
export const SENHA_MIN = 8;
export const SENHA_MAX = 128;
export const SESSAO_TTL_S = 30 * 24 * 3600;
const CORPO_MAX = 4096;

// Limites (tentativas por janela). Contam TODAS as tentativas, certas ou não.
// O de IP é folgado de propósito: rede móvel põe milhares de aparelhos atrás
// do mesmo IP (CGNAT). Quem segura a força bruta numa conta é o limite por e-mail.
export const LIMITES = {
  cadastroIp: { max: 20, janela: 3600 },
  cadastroEmail: { max: 5, janela: 3600 },
  loginIp: { max: 60, janela: 900 },
  loginEmail: { max: 8, janela: 900 },
};

const enc = (s) => new TextEncoder().encode(s);

export function json(corpo, status = 200, extra = {}) {
  return new Response(JSON.stringify(corpo), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
      "strict-transport-security": "max-age=31536000",
      "referrer-policy": "no-referrer",
      ...extra,
    },
  });
}

export function aleatorio(n) {
  const b = new Uint8Array(n);
  crypto.getRandomValues(b);
  return b;
}

export function b64(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

function deB64(s) {
  try {
    const bin = atob(s);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  } catch {
    return null;
  }
}

const b64url = (bytes) => b64(bytes).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");

export async function sha256hex(texto) {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", enc(texto)));
  return Array.from(d, (n) => n.toString(16).padStart(2, "0")).join("");
}

/** Comparação em tempo constante (o tamanho do hash é fixo: 32 bytes). */
export function iguais(a, b) {
  if (!(a instanceof Uint8Array) || !(b instanceof Uint8Array) || a.length !== b.length) return false;
  let dif = 0;
  for (let i = 0; i < a.length; i++) dif |= a[i] ^ b[i];
  return dif === 0;
}

async function derivar(senha, sal, iteracoes) {
  const chave = await crypto.subtle.importKey("raw", enc(senha), "PBKDF2", false, ["deriveBits"]);
  const bits = await crypto.subtle.deriveBits({ name: "PBKDF2", hash: "SHA-256", salt: sal, iterations: iteracoes }, chave, 256);
  return new Uint8Array(bits);
}

export async function hashSenha(senha, sal = aleatorio(16), iteracoes = PBKDF2_ITERACOES) {
  const h = await derivar(senha, sal, iteracoes);
  return `pbkdf2_sha256$${iteracoes}$${b64(sal)}$${b64(h)}`;
}

export async function conferirSenha(senha, guardado) {
  const p = String(guardado ?? "").split("$");
  if (p.length !== 4 || p[0] !== "pbkdf2_sha256") return false;
  const iteracoes = Number(p[1]);
  if (!Number.isInteger(iteracoes) || iteracoes < 1 || iteracoes > PBKDF2_ITERACOES) return false;
  const sal = deB64(p[2]);
  const esperado = deB64(p[3]);
  if (!sal || !esperado || sal.length < 16 || esperado.length !== 32) return false;
  return iguais(await derivar(senha, sal, iteracoes), esperado);
}

// Hash de mentira: um e-mail sem conta gasta o MESMO tempo de PBKDF2 que uma
// senha errada. Sem isso, o tempo da resposta diria quais e-mails existem.
const HASH_FALSO = `pbkdf2_sha256$${PBKDF2_ITERACOES}$${"A".repeat(22)}==$${"A".repeat(43)}=`;

/** trim + minúsculas + formato. Devolve null quando não é um e-mail. */
export function normalizarEmail(valor) {
  if (typeof valor !== "string") return null;
  const e = valor.trim().toLowerCase();
  if (e.length < 6 || e.length > 254) return null;
  const arroba = e.indexOf("@");
  if (arroba < 1 || arroba !== e.lastIndexOf("@")) return null;
  const local = e.slice(0, arroba);
  const dominio = e.slice(arroba + 1);
  if (local.length > 64 || local.startsWith(".") || local.endsWith(".") || local.includes("..")) return null;
  if (!/^[a-z0-9.!#$%&'*+/=?^_`{|}~-]+$/.test(local)) return null;
  const rotulos = dominio.split(".");
  if (rotulos.length < 2) return null;
  for (const r of rotulos) if (!/^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/.test(r)) return null;
  if (!/^[a-z]{2,63}$/.test(rotulos[rotulos.length - 1])) return null;
  return e;
}

/** A senha não é aparada: espaço é caractere dela. Conta pontos de código. */
export function erroDaSenha(senha) {
  if (typeof senha !== "string") return "senha_invalida";
  const n = [...senha].length;
  if (n < SENHA_MIN) return "senha_curta";
  if (n > SENHA_MAX) return "senha_longa";
  return null;
}

/** A storage failure must never silently disable brute-force protection. */
export async function dentroDoLimite(env, escopo, id, { max, janela }, agoraMs = Date.now()) {
  const bloco = Math.floor(agoraMs / 1000 / janela);
  const chave = `rl:${escopo}:${await sha256hex("aurea-rl:" + id)}:${bloco}`;
  try {
    const r = await env.AUREA_DB.prepare(
      "INSERT INTO rate_limits (key, count, expires_at) VALUES (?, 1, ?) " +
      "ON CONFLICT(key) DO UPDATE SET count = count + 1 RETURNING count")
      .bind(chave, agoraMs + janela * 2000).first();
    const count = Number(r?.count);
    if (!Number.isSafeInteger(count) || count < 1) throw new Error('invalid_limit_state');
    return count <= max;
  } catch {
    throw new Error("security_storage_unavailable");
  }
}

/** Corpo JSON com teto de tamanho, lido em pedaços (nunca um corpo gigante na memória). */
export async function lerJson(req, limite) {
  if (!(req.headers.get("content-type") ?? "").toLowerCase().startsWith("application/json")) return { erro: "content_type" };
  const declarado = Number(req.headers.get("content-length"));
  if (Number.isFinite(declarado) && declarado > limite) return { erro: "corpo_grande" };
  if (!req.body) return { erro: "json_invalido" };
  const leitor = req.body.getReader();
  const partes = [];
  let total = 0;
  try {
    for (;;) {
      const { done, value } = await leitor.read();
      if (done) break;
      total += value.length;
      if (total > limite) return { erro: "corpo_grande" };
      partes.push(value);
    }
  } finally {
    leitor.cancel().catch(() => {});
  }
  const bytes = new Uint8Array(total);
  let o = 0;
  for (const p of partes) { bytes.set(p, o); o += p.length; }
  try {
    const valor = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    if (!valor || typeof valor !== "object" || Array.isArray(valor)) return { erro: "json_invalido" };
    return { valor };
  } catch {
    return { erro: "json_invalido" };
  }
}

const ip = (req) => req.headers.get("cf-connecting-ip") ?? "local";

function tokenDoCabecalho(req) {
  const m = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(req.headers.get("authorization") ?? "");
  return m ? m[1] : null;
}

async function criarSessao(env, usuario) {
  const token = b64url(aleatorio(32));
  const registro = { uid: usuario.id, email: usuario.email, criado: Date.now() };
  await env.AUREA_DB.prepare("INSERT INTO sessions (token_hash, uid, email, created_at, expires_at) VALUES (?, ?, ?, ?, ?)")
    .bind(await sha256hex(token), usuario.id, usuario.email, registro.criado, registro.criado + SESSAO_TTL_S * 1000).run();
  return token;
}

/** A sessão do cabeçalho Authorization, ou null. Usada também pelo crash.js. */
export async function lerSessao(env, req) {
  const token = tokenDoCabecalho(req);
  if (!token) return null;
  const hash = await sha256hex(token);
  const now = Date.now();
  // Read even expired/revoked rows: they are authoritative tombstones and must
  // not fall through to an eventually-consistent legacy KV session.
  let d = await env.AUREA_DB.prepare("SELECT uid, created_at, expires_at FROM sessions WHERE token_hash = ?")
    .bind(hash).first();
  if (!d && env.AUREA_KV) {
    const legacy = await env.AUREA_KV.get("sess:" + hash, "json");
    if (!legacy || typeof legacy.uid !== 'string' || !Number.isSafeInteger(legacy.criado) ||
        legacy.criado > now || legacy.criado + SESSAO_TTL_S * 1000 <= now) return null;
    const account = await env.AUREA_DB.prepare("SELECT email FROM users WHERE id = ?").bind(legacy.uid).first();
    if (!account) return null;
    await env.AUREA_DB.prepare("INSERT OR IGNORE INTO sessions (token_hash,uid,email,created_at,expires_at) VALUES (?,?,?,?,?)")
      .bind(hash, legacy.uid, account.email, legacy.criado, legacy.criado + SESSAO_TTL_S * 1000).run();
    // A concurrent logout may have written a tombstone while KV was loading.
    d = await env.AUREA_DB.prepare("SELECT uid, created_at, expires_at FROM sessions WHERE token_hash = ?").bind(hash).first();
  }
  if (!d || d.expires_at <= now || d.created_at > now || d.created_at + SESSAO_TTL_S * 1000 <= now) return null;
  const account = await env.AUREA_DB.prepare("SELECT email FROM users WHERE id = ?").bind(d.uid).first();
  return account ? { uid: d.uid, email: account.email, hash } : null;
}

async function contarUsuarios(env) {
  const r = await env.AUREA_DB.prepare("SELECT value FROM counters WHERE name = 'users'").first();
  return Number(r?.value ?? 0);
}

// Memo por isolate: o app pede o número a cada abertura; o D1 não precisa ver todas.
let memo = { valor: -1, ate: 0 };
export function esquecerMemo() { memo = { valor: -1, ate: 0 }; }

function localOuHttps(url) {
  return url.protocol === "https:" || url.hostname === "127.0.0.1" || url.hostname === "localhost";
}

async function cadastrar(req, env) {
  if (!(await dentroDoLimite(env, "signup-ip", ip(req), LIMITES.cadastroIp))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "3600" });
  }
  const { valor, erro } = await lerJson(req, CORPO_MAX);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const email = normalizarEmail(valor.email);
  if (!email) return json({ error: "email_invalido" }, 400);
  const erroSenha = erroDaSenha(valor.password);
  if (erroSenha) return json({ error: erroSenha }, 400);
  if (!(await dentroDoLimite(env, "signup-email", email, LIMITES.cadastroEmail))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "3600" });
  }

  const usuario = { id: crypto.randomUUID(), email };
  const hash = await hashSenha(valor.password);
  try {
    // Uma transação: o usuário e o contador entram juntos ou não entram.
    await env.AUREA_DB.batch([
      env.AUREA_DB.prepare("INSERT INTO users (id, email, password_hash, created_at) VALUES (?, ?, ?, ?)")
        .bind(usuario.id, email, hash, Date.now()),
      env.AUREA_DB.prepare("INSERT INTO counters (name, value) VALUES ('users', 1) ON CONFLICT(name) DO UPDATE SET value = value + 1"),
    ]);
  } catch (e) {
    if (/UNIQUE/i.test(String(e?.message ?? e))) return json({ error: "email_em_uso" }, 409);
    console.error("contas: cadastro falhou no D1");
    return json({ error: "contas_indisponiveis" }, 503);
  }
  esquecerMemo();
  const token = await criarSessao(env, usuario);
  const users = await contarUsuarios(env).catch(() => undefined);
  return json({ token, email, users }, 201);
}

async function entrar(req, env, ctx) {
  if (!(await dentroDoLimite(env, "login-ip", ip(req), LIMITES.loginIp))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "900" });
  }
  const { valor, erro } = await lerJson(req, CORPO_MAX);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const email = normalizarEmail(valor.email);
  if (!email) return json({ error: "email_invalido" }, 400);
  if (typeof valor.password !== "string" || valor.password.length === 0 || [...valor.password].length > SENHA_MAX) {
    return json({ error: "credenciais_invalidas" }, 401);
  }
  if (!(await dentroDoLimite(env, "login-email", email, LIMITES.loginEmail))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "900" });
  }
  let linha;
  try {
    linha = await env.AUREA_DB.prepare("SELECT id, email, password_hash FROM users WHERE email = ?").bind(email).first();
  } catch (e) {
    console.error("contas: login falhou no D1");
    return json({ error: "contas_indisponiveis" }, 503);
  }
  // Mesma mensagem e o mesmo trabalho para "não existe" e "senha errada".
  const confere = await conferirSenha(valor.password, linha?.password_hash ?? HASH_FALSO);
  if (!linha || !confere) return json({ error: "credenciais_invalidas" }, 401);

  const atualizar = env.AUREA_DB.prepare("UPDATE users SET last_login_at = ? WHERE id = ?").bind(Date.now(), linha.id).run()
    .catch(() => {});
  if (ctx?.waitUntil) ctx.waitUntil(atualizar); else await atualizar;
  const token = await criarSessao(env, { id: linha.id, email: linha.email });
  return json({ token, email: linha.email });
}

async function sessao(req, env) {
  const s = await lerSessao(env, req);
  if (!s) return json({ error: "nao_autorizado" }, 401);
  return json({ email: s.email });
}

async function sair(req, env) {
  const s = await lerSessao(env, req);
  if (s) await apagarSessao(env, s);
  return json({ ok: true });
}

async function apagarSessao(env, s) {
  await env.AUREA_DB.prepare("UPDATE sessions SET expires_at = 0 WHERE token_hash = ?").bind(s.hash).run();
  if (env.AUREA_KV) await env.AUREA_KV.delete("sess:" + s.hash).catch(() => {});
}

async function estatisticas(env) {
  const agora = Date.now();
  if (memo.valor < 0 || agora > memo.ate) {
    try {
      memo = { valor: await contarUsuarios(env), ate: agora + 30_000 };
    } catch {
      return json({ error: "contas_indisponiveis" }, 503);
    }
  }
  return json({ count: memo.valor }, 200, { "cache-control": "public, max-age=60" });
}

/** Devolve a resposta quando a rota é de contas; null para o Worker seguir. */
export async function rotaDeContas(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  const conhecida = rota === "/api/stats/users" || rota.startsWith("/api/auth/");
  if (!conhecida) return null;
  if (!localOuHttps(url)) return json({ error: "https_obrigatorio" }, 403);
  if (!env.AUREA_DB) return json({ error: "contas_indisponiveis" }, 503);

  if (rota === "/api/stats/users" && (req.method === "GET" || req.method === "HEAD")) return estatisticas(env);
  if (rota === "/api/auth/signup" && req.method === "POST") return cadastrar(req, env);
  if (rota === "/api/auth/login" && req.method === "POST") return entrar(req, env, ctx);
  if (rota === "/api/auth/session" && req.method === "GET") return sessao(req, env);
  if (rota === "/api/auth/logout" && req.method === "POST") return sair(req, env);
  return json({ error: "nao_encontrado" }, 404);
}
