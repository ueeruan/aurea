// =============================================================================
//  Conferência de compras na loja (Google Play / App Store) para os direitos de
//  entitlements.js: NÃO consumíveis (aurea_aureaos, o AureaOS no AureaMCP) e,
//  desde 16/11/2026, ASSINATURAS mensais (AureaHyper e cada efeito/Texto 3D;
//  docs/release/assinaturas.md). O app só traz o recibo; quem diz se pagou é a loja.
//
//  Mesmos segredos do Mural (mural.js), nenhum novo:
//    GOOGLE_PLAY_SERVICE_ACCOUNT   JSON da conta de serviço com acesso à Play
//    APPLE_IAP_KEY_ID, APPLE_IAP_ISSUER_ID, APPLE_IAP_PRIVATE_KEY (.p8), APPLE_BUNDLE_ID
//
//  Cada verificação devolve {error} ou {key}: `key` identifica a COMPRA na loja
//  (orderId da Play, originalTransactionId da Apple), não o recibo — a mesma
//  compra restaurada em outro aparelho dá a mesma chave, e uma compra só vale
//  para uma conta Aurea.
// =============================================================================

export const PLAY_PACKAGE = "com.aurea.editor";

const b64url = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const b64urlText = (text) => b64url(new TextEncoder().encode(text));
const pemBytes = (pem) => Uint8Array.from(atob(pem.replace(/-----[^-]+-----/g, "").replace(/\s+/g, "")), (c) => c.charCodeAt(0));

export const googleReady = (env) => !!env.GOOGLE_PLAY_SERVICE_ACCOUNT;
export const appleReady = (env) => !!(env.APPLE_IAP_KEY_ID && env.APPLE_IAP_ISSUER_ID && env.APPLE_IAP_PRIVATE_KEY && env.APPLE_BUNDLE_ID);

async function signedJwt(header, claims, key, algorithm) {
  const body = `${b64urlText(JSON.stringify(header))}.${b64urlText(JSON.stringify(claims))}`;
  return `${body}.${b64url(await crypto.subtle.sign(algorithm, key, new TextEncoder().encode(body)))}`;
}

async function googleAccessToken(env) {
  const account = JSON.parse(env.GOOGLE_PLAY_SERVICE_ACCOUNT);
  const key = await crypto.subtle.importKey("pkcs8", pemBytes(account.private_key), { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["sign"]);
  const now = Math.floor(Date.now() / 1000);
  const assertion = await signedJwt({ alg: "RS256", typ: "JWT" }, { iss: account.client_email,
    scope: "https://www.googleapis.com/auth/androidpublisher", aud: "https://oauth2.googleapis.com/token",
    iat: now, exp: now + 600 }, key, "RSASSA-PKCS1-v1_5");
  const r = await fetch("https://oauth2.googleapis.com/token", { method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ grant_type: "urn:ietf:params:oauth:grant-type:jwt-bearer", assertion }) });
  if (!r.ok) throw new Error("google_auth_failed");
  return (await r.json()).access_token;
}

/**
 * Produto único da Play: pago (purchaseState 0) e, se ainda não, RECONHECIDO
 * aqui (a Play estorna em 3 dias uma compra sem acknowledge). Lança se a Play
 * não respondeu (o chamador diz "tente de novo"; nada é gravado).
 */
export async function verifyGooglePurchase(env, token, product) {
  if (!googleReady(env)) return { error: "payments_unavailable" };
  const access = await googleAccessToken(env);
  const base = `https://androidpublisher.googleapis.com/androidpublisher/v3/applications/${PLAY_PACKAGE}`
    + `/purchases/products/${encodeURIComponent(product)}/tokens/${encodeURIComponent(token)}`;
  const r = await fetch(base, { headers: { authorization: `Bearer ${access}` } });
  if (r.status === 404 || r.status === 400 || r.status === 410) return { error: "purchase_invalid" };
  if (!r.ok) throw new Error("google_verify_failed");
  const p = await r.json();
  if (p.purchaseState !== 0) return { error: p.purchaseState === 2 ? "purchase_pending" : "purchase_invalid" };
  if (p.acknowledgementState === 0) {
    const ack = await fetch(`${base}:acknowledge`, { method: "POST", headers: { authorization: `Bearer ${access}`, "content-type": "application/json" }, body: "{}" });
    if (!ack.ok) throw new Error("google_ack_failed");
  }
  return { key: `google:${p.orderId || token}` };
}

async function appleJwt(env) {
  const key = await crypto.subtle.importKey("pkcs8", pemBytes(env.APPLE_IAP_PRIVATE_KEY), { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  const now = Math.floor(Date.now() / 1000);
  return signedJwt({ alg: "ES256", kid: env.APPLE_IAP_KEY_ID, typ: "JWT" }, { iss: env.APPLE_IAP_ISSUER_ID,
    iat: now, exp: now + 600, aud: "appstoreconnect-v1", bid: env.APPLE_BUNDLE_ID }, key, { name: "ECDSA", hash: "SHA-256" });
}

/** O corpo (payload) de um JWS da Apple. A resposta veio por TLS da API com a nossa chave. */
function jwsPayload(signed) {
  const part = String(signed ?? "").split(".")[1] ?? "";
  return JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(part.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0))));
}

/** GET na API do servidor da App Store: produção e, se 404, sandbox. null = não existe em nenhuma. */
async function appleGet(env, path) {
  const jwt = await appleJwt(env);
  for (const host of ["api.storekit.itunes.apple.com", "api.storekit-sandbox.itunes.apple.com"]) {
    const r = await fetch(`https://${host}${path}`, { headers: { authorization: `Bearer ${jwt}` } });
    if (r.status === 404) continue;
    if (r.status === 400) return null;
    if (!r.ok) throw new Error("apple_verify_failed");
    return r.json();
  }
  return null;
}

/** Transação da App Store pela API do servidor (resposta por TLS, com chave nossa). */
export async function verifyApplePurchase(env, transactionId, product) {
  if (!appleReady(env)) return { error: "payments_unavailable" };
  if (!/^\d{1,32}$/.test(transactionId)) return { error: "purchase_invalid" };
  const body = await appleGet(env, `/inApps/v1/transactions/${transactionId}`);
  if (!body) return { error: "purchase_invalid" };
  const info = jwsPayload(body.signedTransactionInfo);
  if (info.productId !== product || info.bundleId !== env.APPLE_BUNDLE_ID || info.revocationDate
    || String(info.transactionId) !== transactionId || info.type !== "Non-Consumable") return { error: "purchase_invalid" };
  return { key: `apple:${info.originalTransactionId || info.transactionId || transactionId}` };
}

// -----------------------------------------------------------------------------
//  ASSINATURAS (16/11/2026): AureaHyper mensal e cada efeito/Texto 3D avulso.
//  Cada conferência devolve {error} ou {key, ref, expiresAt}:
//    key        a compra na loja (uma compra = uma conta Aurea, como acima);
//    ref        o que se guarda para conferir a renovação depois (o purchaseToken
//               da Play, que não muda nas renovações; o originalTransactionId da Apple);
//    expiresAt  segundos Unix em que o período pago termina.
//  Lança quando a loja não respondeu (o chamador não grava nada).
// -----------------------------------------------------------------------------

/** Um período de graça sem data de fim conhecida vale por 1 dia, e o Worker pergunta de novo. */
export const GRACE_FALLBACK_S = 24 * 3600;
const GOOGLE_VALID_STATES = new Set(["SUBSCRIPTION_STATE_ACTIVE", "SUBSCRIPTION_STATE_IN_GRACE_PERIOD",
  // Cancelada = a renovação automática foi desligada; o período já pago continua valendo até expiryTime.
  "SUBSCRIPTION_STATE_CANCELED"]);

/** Assinatura da Play: purchases.subscriptionsv2.get (androidpublisher v3). */
export async function verifyGoogleSubscription(env, token, product) {
  if (!googleReady(env)) return { error: "payments_unavailable" };
  const access = await googleAccessToken(env);
  const url = `https://androidpublisher.googleapis.com/androidpublisher/v3/applications/${PLAY_PACKAGE}`
    + `/purchases/subscriptionsv2/tokens/${encodeURIComponent(token)}`;
  const r = await fetch(url, { headers: { authorization: `Bearer ${access}` } });
  if (r.status === 404 || r.status === 400 || r.status === 410) return { error: "purchase_invalid" };
  if (!r.ok) throw new Error("google_verify_failed");
  const s = await r.json();
  const item = (Array.isArray(s.lineItems) ? s.lineItems : []).find((li) => li?.productId === product);
  if (!item) return { error: "purchase_invalid" };
  const state = s.subscriptionState;
  if (state === "SUBSCRIPTION_STATE_PENDING") return { error: "purchase_pending" };
  const now = Math.floor(Date.now() / 1000);
  let expiresAt = Math.floor(Date.parse(item.expiryTime) / 1000);
  if (!Number.isFinite(expiresAt)) return { error: "purchase_invalid" };
  if (state === "SUBSCRIPTION_STATE_IN_GRACE_PERIOD" && expiresAt <= now) expiresAt = now + GRACE_FALLBACK_S;
  if (!GOOGLE_VALID_STATES.has(state) || expiresAt <= now) return { error: "purchase_expired" };
  // latestOrderId muda a cada renovação ("GPA.x..0", "GPA.x..1"); a base identifica a compra.
  const order = String(s.latestOrderId ?? item.latestSuccessfulOrderId ?? "").split("..")[0];
  return { key: `google-sub:${order || token}`, ref: token, expiresAt };
}

/** Transação de assinatura auto-renovável deste app e deste produto, não revogada? */
function appleSubscriptionInfo(env, info, product) {
  if (info.productId !== product || info.bundleId !== env.APPLE_BUNDLE_ID || info.revocationDate
    || info.type !== "Auto-Renewable Subscription") return null;
  return info;
}

/** Assinatura da App Store pelo id da transação que o app mandou (Transaction.id). */
export async function verifyAppleSubscription(env, transactionId, product) {
  if (!appleReady(env)) return { error: "payments_unavailable" };
  if (!/^\d{1,32}$/.test(transactionId)) return { error: "purchase_invalid" };
  const body = await appleGet(env, `/inApps/v1/transactions/${transactionId}`);
  if (!body) return { error: "purchase_invalid" };
  const info = appleSubscriptionInfo(env, jwsPayload(body.signedTransactionInfo), product);
  if (!info || String(info.transactionId) !== transactionId) return { error: "purchase_invalid" };
  const original = String(info.originalTransactionId || info.transactionId);
  const expiresAt = Math.floor(Number(info.expiresDate) / 1000);
  if (Number.isFinite(expiresAt) && expiresAt > Math.floor(Date.now() / 1000)) {
    return { key: `apple-sub:${original}`, ref: original, expiresAt };
  }
  // A transação mandada já venceu: a assinatura pode ter renovado (outra transação).
  const renewed = await renewAppleSubscription(env, original, product);
  return renewed.error ? renewed : { key: `apple-sub:${original}`, ref: original, expiresAt: renewed.expiresAt };
}

/**
 * Situação atual da assinatura da Apple (Get All Subscription Statuses): status 1
 * = ativa, 4 = período de graça de cobrança; o resto (vencida, nova tentativa de
 * cobrança, revogada) não libera.
 */
export async function renewAppleSubscription(env, originalTransactionId, product) {
  if (!appleReady(env)) return { error: "payments_unavailable" };
  if (!/^\d{1,32}$/.test(String(originalTransactionId))) return { error: "purchase_invalid" };
  const body = await appleGet(env, `/inApps/v1/subscriptions/${originalTransactionId}`);
  if (!body || (body.bundleId && body.bundleId !== env.APPLE_BUNDLE_ID)) return { error: "purchase_invalid" };
  const now = Math.floor(Date.now() / 1000);
  for (const group of Array.isArray(body.data) ? body.data : []) {
    for (const last of Array.isArray(group?.lastTransactions) ? group.lastTransactions : []) {
      if (String(last?.originalTransactionId) !== String(originalTransactionId)) continue;
      const info = appleSubscriptionInfo(env, jwsPayload(last.signedTransactionInfo), product);
      if (!info) return { error: "purchase_invalid" };
      let expiresAt = Math.floor(Number(info.expiresDate) / 1000);
      if (last.status === 4) {
        const renewal = last.signedRenewalInfo ? jwsPayload(last.signedRenewalInfo) : {};
        const grace = Math.floor(Number(renewal.gracePeriodExpiresDate) / 1000);
        expiresAt = Number.isFinite(grace) && grace > now ? grace : Math.max(Number.isFinite(expiresAt) ? expiresAt : 0, now + GRACE_FALLBACK_S);
      }
      if ((last.status === 1 || last.status === 4) && Number.isFinite(expiresAt) && expiresAt > now) return { expiresAt };
      return { error: "purchase_expired" };
    }
  }
  return { error: "purchase_invalid" };
}

/** Confere de novo uma assinatura guardada (renovação). Mesmo contrato das funções acima. */
export async function renewSubscription(env, platform, ref, product) {
  if (platform === "google") return verifyGoogleSubscription(env, ref, product);
  if (platform === "apple") return renewAppleSubscription(env, ref, product);
  return { error: "purchase_invalid" };
}
