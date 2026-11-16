// =============================================================================
//  Conferência de compras NÃO consumíveis na loja (Google Play / App Store),
//  para os direitos de entitlements.js (hoje: aurea_aureaos, o AureaOS no
//  AureaMCP). O app só traz o recibo; quem diz se pagou é a loja.
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

/** Transação da App Store pela API do servidor (resposta por TLS, com chave nossa). */
export async function verifyApplePurchase(env, transactionId, product) {
  if (!appleReady(env)) return { error: "payments_unavailable" };
  if (!/^\d{1,32}$/.test(transactionId)) return { error: "purchase_invalid" };
  const key = await crypto.subtle.importKey("pkcs8", pemBytes(env.APPLE_IAP_PRIVATE_KEY), { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  const now = Math.floor(Date.now() / 1000);
  const jwt = await signedJwt({ alg: "ES256", kid: env.APPLE_IAP_KEY_ID, typ: "JWT" }, { iss: env.APPLE_IAP_ISSUER_ID,
    iat: now, exp: now + 600, aud: "appstoreconnect-v1", bid: env.APPLE_BUNDLE_ID }, key, { name: "ECDSA", hash: "SHA-256" });
  for (const host of ["api.storekit.itunes.apple.com", "api.storekit-sandbox.itunes.apple.com"]) {
    const r = await fetch(`https://${host}/inApps/v1/transactions/${transactionId}`, { headers: { authorization: `Bearer ${jwt}` } });
    if (r.status === 404) continue;
    if (!r.ok) throw new Error("apple_verify_failed");
    const signed = (await r.json()).signedTransactionInfo ?? "";
    const part = signed.split(".")[1] ?? "";
    const info = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(part.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0))));
    if (info.productId !== product || info.bundleId !== env.APPLE_BUNDLE_ID || info.revocationDate
      || String(info.transactionId) !== transactionId || info.type !== "Non-Consumable") return { error: "purchase_invalid" };
    return { key: `apple:${info.originalTransactionId || info.transactionId || transactionId}` };
  }
  return { error: "purchase_invalid" };
}
