// =============================================================================
//  Configuração remota ASSINADA dos apps (0.3.2): versão mínima (atualização
//  obrigatória), política do AureaHyper e um aviso opcional para a Home.
//
//  Rotas (JSON, cache-control: no-store):
//    GET  /api/app-config?platform=android|ios[&version=<versionCode>]
//         → 200 {payload: "<json>", signature: "<base64>"}
//         → 400 plataforma_invalida · 503 config_indisponivel (sem chave)
//    POST /api/admin/app-config    Bearer <AUREA_ADMIN_SECRET>
//         {platform, minVersionCode?, latestVersionCode?, updateUrl?,
//          hyperMode?: "free"|"entitled"|"off", message?: {pt-BR,en,…}|null,
//          ttlDays?: 1..30}
//         → 200 {ok, platform, config} · 400 campo inválido · 401 · 429 · 503
//
//  `payload` é o JSON canônico (chaves numa ordem fixa) que os apps VERIFICAM
//  e só depois leem:
//    {v:1, platform, minVersionCode, latestVersionCode, updateUrl,
//     hyper:{mode}, message:{…}|null, issuedAt, expiresAt}   (segundos Unix)
//  `signature` = ECDSA P-256 com SHA-256 sobre os bytes UTF-8 de `payload`, no
//  formato CRU r||s (64 bytes, IEEE P1363 — o que o WebCrypto produz), em
//  base64. O Android converte para DER antes do `SHA256withECDSA`; o iOS usa
//  `P256.Signing.ECDSASignature(rawRepresentation:)`.
//
//  A chave privada é o secret AUREA_CONFIG_SIGNING_KEY (PKCS8 em base64, gerada
//  por scripts/gen-config-key.mjs). Os apps embutem só a pública (SPKI).
//  Sem nada guardado: minVersionCode 0, hyper "free", validade de 7 dias. O
//  `version` da consulta não muda a resposta (fica para diagnóstico).
// =============================================================================

import { json, lerJson, dentroDoLimite } from "./contas.js";

export const PLATAFORMAS = Object.freeze(["android", "ios"]);
export const MODOS_HYPER = Object.freeze(["free", "entitled", "off"]);
export const IDIOMAS = Object.freeze(["pt-BR", "en", "es", "hi", "id", "ru", "ar"]);
export const VALIDADE_PADRAO_DIAS = 7;
export const LIMITE_ADMIN_CONFIG = { max: 30, janela: 900 };
const CORPO_ADMIN_MAX = 8 * 1024;
const MENSAGEM_MAX = 500;
const URL_MAX = 500;
const SEGREDO_MIN = 16;
const VERSAO_MAX = 2_100_000_000;

const ip = (req) => req.headers.get("cf-connecting-ip") ?? "local";
const chaveKv = (plataforma) => `appconfig:${plataforma}`;

export const URL_PADRAO = Object.freeze({
  android: "https://play.google.com/store/apps/details?id=com.aurea.editor",   // o app da Play (build "play")
  ios: "",
});

function segredoConfere(recebido, esperado) {
  if (typeof recebido !== "string" || typeof esperado !== "string") return false;
  if (recebido.length !== esperado.length) return false;
  let dif = 0;
  for (let i = 0; i < recebido.length; i++) dif |= recebido.charCodeAt(i) ^ esperado.charCodeAt(i);
  return dif === 0;
}

function b64(bytes) {
  let s = "";
  const u = new Uint8Array(bytes);
  for (let i = 0; i < u.length; i++) s += String.fromCharCode(u[i]);
  return btoa(s);
}

function deB64(texto) {
  const s = atob(texto.replace(/\s+/g, ""));
  const u = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i);
  return u;
}

let memoChave = { texto: null, chave: null };
async function chavePrivada(env) {
  const texto = env.AUREA_CONFIG_SIGNING_KEY;
  if (typeof texto !== "string" || texto.trim().length < 32) return null;
  if (memoChave.texto === texto) return memoChave.chave;
  try {
    const chave = await crypto.subtle.importKey("pkcs8", deB64(texto.trim()), { name: "ECDSA", namedCurve: "P-256" },
      false, ["sign"]);
    memoChave = { texto, chave };
    return chave;
  } catch {
    console.error("app-config: AUREA_CONFIG_SIGNING_KEY ilegivel");
    return null;
  }
}

/** O que fica no KV, já com os padrões. */
export function normalizarGuardado(plataforma, g) {
  const o = g && typeof g === "object" ? g : {};
  return {
    minVersionCode: Number.isSafeInteger(o.minVersionCode) ? o.minVersionCode : 0,
    latestVersionCode: Number.isSafeInteger(o.latestVersionCode) ? o.latestVersionCode : 0,
    updateUrl: typeof o.updateUrl === "string" ? o.updateUrl : URL_PADRAO[plataforma],
    hyperMode: MODOS_HYPER.includes(o.hyperMode) ? o.hyperMode : "free",
    message: o.message && typeof o.message === "object" ? o.message : null,
    ttlDays: Number.isSafeInteger(o.ttlDays) && o.ttlDays >= 1 && o.ttlDays <= 30 ? o.ttlDays : VALIDADE_PADRAO_DIAS,
  };
}

/** O payload canônico: as chaves SEMPRE nesta ordem (os apps verificam estes bytes). */
export function montarPayload(plataforma, guardado, agoraS = Math.floor(Date.now() / 1000)) {
  const c = normalizarGuardado(plataforma, guardado);
  let message = null;
  if (c.message) {
    message = {};
    for (const idioma of IDIOMAS) if (typeof c.message[idioma] === "string") message[idioma] = c.message[idioma];
    if (Object.keys(message).length === 0) message = null;
  }
  return JSON.stringify({
    v: 1,
    platform: plataforma,
    minVersionCode: c.minVersionCode,
    latestVersionCode: c.latestVersionCode,
    updateUrl: c.updateUrl,
    hyper: { mode: c.hyperMode },
    message,
    issuedAt: agoraS,
    expiresAt: agoraS + c.ttlDays * 86400,
  });
}

export async function assinar(chave, payload) {
  const sig = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, chave, new TextEncoder().encode(payload));
  return b64(sig);   // r||s cru, 64 bytes
}

async function lerGuardado(env, plataforma) {
  try {
    return await env.AUREA_KV.get(chaveKv(plataforma), "json");
  } catch {
    return null;
  }
}

async function consultar(req, env, url) {
  const plataforma = url.searchParams.get("platform");
  if (!PLATAFORMAS.includes(plataforma)) return json({ error: "plataforma_invalida" }, 400);
  const chave = await chavePrivada(env);
  // Sem chave não há config: o app segue com a última válida (nunca bloqueia por isto).
  if (!chave) return json({ error: "config_indisponivel" }, 503);
  const payload = montarPayload(plataforma, await lerGuardado(env, plataforma));
  return json({ payload, signature: await assinar(chave, payload) });
}

function validarMensagem(m) {
  if (m === null) return { ok: true, valor: null };
  if (typeof m !== "object" || Array.isArray(m)) return { ok: false };
  const out = {};
  for (const [k, v] of Object.entries(m)) {
    if (!IDIOMAS.includes(k) || typeof v !== "string" || [...v].length > MENSAGEM_MAX) return { ok: false };
    if (v.trim()) out[k] = v.trim();
  }
  return { ok: true, valor: Object.keys(out).length ? out : null };
}

async function administrar(req, env) {
  const segredo = env.AUREA_ADMIN_SECRET;
  if (typeof segredo !== "string" || segredo.length < SEGREDO_MIN) return json({ error: "admin_desativado" }, 503);
  if (!(await dentroDoLimite(env, "admin-app-config-ip", ip(req), LIMITE_ADMIN_CONFIG))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": String(LIMITE_ADMIN_CONFIG.janela) });
  }
  const auth = req.headers.get("authorization") ?? "";
  if (!segredoConfere(auth.startsWith("Bearer ") ? auth.slice(7) : "", segredo)) return json({ error: "nao_autorizado" }, 401);

  const { valor, erro } = await lerJson(req, CORPO_ADMIN_MAX);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const plataforma = valor.platform;
  if (!PLATAFORMAS.includes(plataforma)) return json({ error: "plataforma_invalida" }, 400);
  const atual = normalizarGuardado(plataforma, await lerGuardado(env, plataforma));
  const versao = (x) => Number.isSafeInteger(x) && x >= 0 && x <= VERSAO_MAX;
  if (valor.minVersionCode !== undefined) {
    if (!versao(valor.minVersionCode)) return json({ error: "versao_invalida" }, 400);
    atual.minVersionCode = valor.minVersionCode;
  }
  if (valor.latestVersionCode !== undefined) {
    if (!versao(valor.latestVersionCode)) return json({ error: "versao_invalida" }, 400);
    atual.latestVersionCode = valor.latestVersionCode;
  }
  if (valor.updateUrl !== undefined) {
    const u = valor.updateUrl;
    if (typeof u !== "string" || u.length > URL_MAX || (u !== "" && !/^https:\/\/[^\s]+$/.test(u))) {
      return json({ error: "url_invalida" }, 400);
    }
    atual.updateUrl = u;
  }
  if (valor.hyperMode !== undefined) {
    if (!MODOS_HYPER.includes(valor.hyperMode)) return json({ error: "modo_invalido" }, 400);
    atual.hyperMode = valor.hyperMode;
  }
  if (valor.message !== undefined) {
    const m = validarMensagem(valor.message);
    if (!m.ok) return json({ error: "mensagem_invalida" }, 400);
    atual.message = m.valor;
  }
  if (valor.ttlDays !== undefined) {
    if (!Number.isSafeInteger(valor.ttlDays) || valor.ttlDays < 1 || valor.ttlDays > 30) return json({ error: "validade_invalida" }, 400);
    atual.ttlDays = valor.ttlDays;
  }
  if (atual.latestVersionCode < atual.minVersionCode) atual.latestVersionCode = atual.minVersionCode;
  await env.AUREA_KV.put(chaveKv(plataforma), JSON.stringify(atual));
  return json({ ok: true, platform: plataforma, config: atual });
}

/** Devolve a resposta quando a rota é de app-config; null para o Worker seguir. */
export async function rotaDeAppConfig(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (rota !== "/api/app-config" && rota !== "/api/admin/app-config") return null;
  if (!env.AUREA_KV) return json({ error: "config_indisponivel" }, 503);
  if (rota === "/api/app-config" && req.method === "GET") return consultar(req, env, url);
  if (rota === "/api/admin/app-config" && req.method === "POST") {
    if (!env.AUREA_DB) return json({ error: "admin_desativado" }, 503);
    return administrar(req, env);
  }
  return json({ error: "nao_encontrado" }, 404);
}
