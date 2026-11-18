// =============================================================================
//  AureaMCP — push para acordar o app quando um agente pede algo com o app
//  fechado (fila do relay, mcp_relay.js). Dois caminhos:
//
//    Android  FCM HTTP v1, mensagem de DADOS de prioridade alta {t:"mcp_wake"}:
//             o app (McpPushService) sobe o serviço do agente, conecta e drena
//             a fila. Nada aparece para a pessoa além da notificação do serviço.
//    iOS      APNs, alerta VISÍVEL ("O agente quer editar 'Projeto X'"): o iOS
//             não deixa um app suspenso trabalhar sozinho; o toque abre o app,
//             que conecta e drena a fila.
//
//  Segredos (nenhum no código; `wrangler secret put ...` pelo dono):
//    FCM_SERVICE_ACCOUNT   JSON da conta de serviço do projeto Firebase com o papel
//                          "Firebase Cloud Messaging API Admin" (project_id vem dele).
//    APNS_KEY_ID           id da chave .p8 de APNs (Apple Developer → Keys)
//    APNS_TEAM_ID          Team ID da conta Apple
//    APNS_PRIVATE_KEY      conteúdo do .p8 (PEM)
//    APNS_TOPIC            bundle id do app (opcional; senão APPLE_BUNDLE_ID)
//  Sem eles, o envio é desligado e a fila só roda quando a pessoa abre o app.
//
//  A assinatura JWT é a mesma da conferência de compras (store_verify.js).
// =============================================================================

import { signedJwt, pemBytes, googleServiceAccessToken } from "./store_verify.js";

export const fcmReady = (env) => !!env.FCM_SERVICE_ACCOUNT;
export const apnsReady = (env) => !!(env.APNS_KEY_ID && env.APNS_TEAM_ID && env.APNS_PRIVATE_KEY && (env.APNS_TOPIC || env.APPLE_BUNDLE_ID));
/** O provedor deste aparelho tem credenciais no Worker? */
export const pushReady = (env, provider) => (provider === "fcm" ? fcmReady(env) : provider === "apns" ? apnsReady(env) : false);

/** Token do aparelho: FCM (letras, dígitos, ":", "-", "_") ou APNs (hex). */
export const PUSH_TOKEN_RE = /^[A-Za-z0-9_:.-]{20,4096}$/;

/** Limpa o {provider, token, env} que o app manda no hello/state. null = inválido. */
export function cleanPush(p) {
  if (!p || typeof p !== "object") return null;
  const provider = p.provider === "fcm" || p.provider === "apns" ? p.provider : null;
  const token = typeof p.token === "string" && PUSH_TOKEN_RE.test(p.token) ? p.token : null;
  if (!provider || !token) return null;
  return { provider, token, env: p.env === "sandbox" ? "sandbox" : "production" };
}

// O texto do alerta do iOS (o app manda o idioma no hello). Os mesmos 7 idiomas do app.
const ALERT = {
  pt: { title: "Aurea", project: (t) => `O agente quer editar "${t}". Toque para continuar.`, none: "Um agente de IA quer trabalhar no Aurea. Toque para continuar." },
  en: { title: "Aurea", project: (t) => `The agent wants to edit "${t}". Tap to continue.`, none: "An AI agent wants to work in Aurea. Tap to continue." },
  es: { title: "Aurea", project: (t) => `El agente quiere editar "${t}". Toca para continuar.`, none: "Un agente de IA quiere trabajar en Aurea. Toca para continuar." },
  ru: { title: "Aurea", project: (t) => `Агент хочет изменить «${t}». Нажмите, чтобы продолжить.`, none: "ИИ-агент хочет поработать в Aurea. Нажмите, чтобы продолжить." },
  hi: { title: "Aurea", project: (t) => `एजेंट "${t}" को एडिट करना चाहता है। जारी रखने के लिए टैप करें।`, none: "एक AI एजेंट Aurea में काम करना चाहता है। जारी रखने के लिए टैप करें।" },
  ar: { title: "Aurea", project: (t) => `يريد الوكيل تعديل "${t}". اضغط للمتابعة.`, none: "يريد وكيل ذكاء اصطناعي العمل في Aurea. اضغط للمتابعة." },
  id: { title: "Aurea", project: (t) => `Agen ingin mengedit "${t}". Ketuk untuk melanjutkan.`, none: "Agen AI ingin bekerja di Aurea. Ketuk untuk melanjutkan." },
};

/** Título e corpo do alerta no idioma do aparelho (padrão: inglês). */
export function alertText(lang, projectTitle) {
  const key = String(lang ?? "").toLowerCase().slice(0, 2);
  const t = ALERT[key] ?? ALERT.en;
  const title = String(projectTitle ?? "").replace(/[\u0000-\u001f]/g, "").slice(0, 60);
  return { title: t.title, body: title ? t.project(title) : t.none };
}

// --- FCM HTTP v1 ---------------------------------------------------------------------

let fcmCache = null; // {token, until, account}

async function fcmAccess(env) {
  const now = Date.now();
  if (fcmCache && fcmCache.account === env.FCM_SERVICE_ACCOUNT && fcmCache.until > now) return fcmCache.token;
  const token = await googleServiceAccessToken(env.FCM_SERVICE_ACCOUNT, "https://www.googleapis.com/auth/firebase.messaging");
  fcmCache = { token, until: now + 50 * 60_000, account: env.FCM_SERVICE_ACCOUNT };
  return token;
}

/** {ok} | {ok:false, gone:true} (token morto: o relay apaga) | {ok:false, error}. */
async function sendFcm(env, target, data) {
  const account = JSON.parse(env.FCM_SERVICE_ACCOUNT);
  const access = await fcmAccess(env);
  const r = await fetch(`https://fcm.googleapis.com/v1/projects/${encodeURIComponent(account.project_id)}/messages:send`, {
    method: "POST",
    headers: { authorization: `Bearer ${access}`, "content-type": "application/json" },
    // Só dados (sem "notification"): o app decide o que mostrar. Prioridade alta = o
    // Android deixa o app subir o serviço em primeiro plano mesmo fechado (isenção do FCM).
    body: JSON.stringify({ message: { token: target.token, data, android: { priority: "HIGH", ttl: "600s" } } }),
  });
  if (r.ok) return { ok: true };
  const body = await r.json().catch(() => ({}));
  const code = body?.error?.details?.find?.((d) => d?.errorCode)?.errorCode ?? body?.error?.status;
  if (r.status === 404 || code === "UNREGISTERED" || (r.status === 400 && code === "INVALID_ARGUMENT")) return { ok: false, gone: true };
  return { ok: false, error: `fcm_${r.status}` };
}

// --- APNs ----------------------------------------------------------------------------

let apnsCache = null; // {jwt, until, kid}

async function apnsJwt(env) {
  const now = Date.now();
  // A Apple recusa um token renovado mais de uma vez a cada 20 min e aceita até 60 min.
  if (apnsCache && apnsCache.kid === env.APNS_KEY_ID && apnsCache.until > now) return apnsCache.jwt;
  const key = await crypto.subtle.importKey("pkcs8", pemBytes(env.APNS_PRIVATE_KEY), { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  const jwt = await signedJwt({ alg: "ES256", kid: env.APNS_KEY_ID }, { iss: env.APNS_TEAM_ID, iat: Math.floor(now / 1000) }, key, { name: "ECDSA", hash: "SHA-256" });
  apnsCache = { jwt, until: now + 45 * 60_000, kid: env.APNS_KEY_ID };
  return jwt;
}

/**
 * APNs exige HTTP/2. O `fetch` do Workers negocia HTTP/2 com a Apple? NÃO COMPROVADO
 * aqui (precisa de teste depois do deploy). Se falhar, a alternativa documentada no
 * relatório é mandar o iOS pelo FCM (chave APNs no Firebase + FirebaseMessaging).
 */
async function sendApns(env, target, alert, data) {
  const host = target.env === "sandbox" ? "api.sandbox.push.apple.com" : "api.push.apple.com";
  const r = await fetch(`https://${host}/3/device/${encodeURIComponent(target.token)}`, {
    method: "POST",
    headers: {
      authorization: `bearer ${await apnsJwt(env)}`,
      "apns-topic": env.APNS_TOPIC || env.APPLE_BUNDLE_ID,
      "apns-push-type": "alert",
      "apns-priority": "10",
      "apns-expiration": String(Math.floor(Date.now() / 1000) + 600),
      // Uma notificação por conta: a próxima substitui a anterior na Central.
      "apns-collapse-id": "aurea-mcp-wake",
      "content-type": "application/json",
    },
    body: JSON.stringify({ aps: { alert, sound: "default", "thread-id": "aurea-mcp", "interruption-level": "time-sensitive" }, ...data }),
  });
  if (r.ok) return { ok: true };
  const body = await r.json().catch(() => ({}));
  if (r.status === 410 || body?.reason === "BadDeviceToken" || body?.reason === "Unregistered" || body?.reason === "DeviceTokenNotForTopic") return { ok: false, gone: true };
  return { ok: false, error: `apns_${r.status}_${body?.reason ?? ""}` };
}

/**
 * Acorda um aparelho. `target` = {provider, token, env}; `info` = {jobs, project, lang}.
 * Nunca lança (o relay segue com a fila mesmo sem push).
 */
export async function sendWake(env, target, info = {}) {
  try {
    if (!pushReady(env, target?.provider)) return { ok: false, error: "push_not_configured" };
    const data = { t: "mcp_wake", jobs: String(info.jobs ?? 1) };
    if (target.provider === "fcm") return await sendFcm(env, target, data);
    return await sendApns(env, target, alertText(info.lang, info.project), { aurea: data });
  } catch (e) {
    return { ok: false, error: String(e?.message ?? e).slice(0, 80) };
  }
}

/** Testes: esquece os tokens em cache. */
export function forgetPushCache() { fcmCache = null; apnsCache = null; }
