// =============================================================================
//  Login com Google pelo Firebase Auth (0.4.1) — a CONFERÊNCIA do token.
//
//  O app entra com o Google no Firebase Auth e manda o ID token do Firebase
//  para POST /api/auth/firebase (contas.js). Aqui o token é conferido como a
//  documentação do Firebase manda ("Verify ID tokens using a third-party JWT
//  library"), sem SDK de admin:
//    · cabeçalho alg RS256 e kid de uma das chaves públicas do securetoken;
//    · assinatura RSASSA-PKCS1-v1_5/SHA-256 com essa chave (JWK);
//    · aud = o id do projeto Firebase (env FIREBASE_PROJECT_ID);
//    · iss = https://securetoken.google.com/<projeto>;
//    · exp no futuro, iat e auth_time no passado (folga de 5 min no relógio);
//    · sub não vazio; e-mail presente e verificado; provedor google.com.
//  As chaves vêm de JWKS_URL e ficam na memória do isolate pelo max-age.
// =============================================================================

export const JWKS_URL = "https://www.googleapis.com/service_accounts/v1/jwk/securetoken@system.gserviceaccount.com";
const FOLGA_S = 300;

let cache = { chaves: null, ate: 0 };
export function esquecerChaves() { cache = { chaves: null, ate: 0 }; }

function deB64url(s) {
  if (typeof s !== "string" || !/^[A-Za-z0-9_-]*$/.test(s)) return null;
  const b = s.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((s.length + 3) % 4);
  try {
    return Uint8Array.from(atob(b), (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
}

function deJsonB64url(s) {
  const bytes = deB64url(s);
  if (!bytes) return null;
  try {
    return JSON.parse(new TextDecoder().decode(bytes));
  } catch {
    return null;
  }
}

async function chavesPublicas(buscar) {
  const agora = Date.now();
  if (cache.chaves && agora < cache.ate) return cache.chaves;
  const r = await buscar(JWKS_URL);
  if (!r.ok) throw new Error("jwks_indisponivel");
  const corpo = await r.json();
  const m = /max-age=(\d+)/.exec(r.headers.get("cache-control") ?? "");
  const ttl = Math.min(Number(m?.[1] ?? 3600), 6 * 3600);
  cache = { chaves: Array.isArray(corpo?.keys) ? corpo.keys : [], ate: agora + ttl * 1000 };
  return cache.chaves;
}

/**
 * Confere o ID token do Firebase. Devolve {uid, email} ou {erro} (nunca lança).
 * [buscar] e [agoraS] só mudam nos testes.
 */
export async function conferirTokenFirebase(token, projeto, { buscar = fetch, agoraS = Math.floor(Date.now() / 1000) } = {}) {
  if (!projeto) return { erro: "firebase_desativado" };
  if (typeof token !== "string" || token.length > 8192) return { erro: "token_invalido" };
  const partes = token.split(".");
  if (partes.length !== 3) return { erro: "token_invalido" };
  const cab = deJsonB64url(partes[0]);
  const corpo = deJsonB64url(partes[1]);
  const assinatura = deB64url(partes[2]);
  if (!cab || !corpo || !assinatura || cab.alg !== "RS256" || typeof cab.kid !== "string") return { erro: "token_invalido" };

  let chaves;
  try {
    chaves = await chavesPublicas(buscar);
  } catch {
    return { erro: "firebase_indisponivel" };
  }
  const jwk = chaves.find((k) => k?.kid === cab.kid);
  if (!jwk) return { erro: "token_invalido" };
  let valida = false;
  try {
    const chave = await crypto.subtle.importKey("jwk", { kty: "RSA", n: jwk.n, e: jwk.e, alg: "RS256", ext: true },
      { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" }, false, ["verify"]);
    valida = await crypto.subtle.verify("RSASSA-PKCS1-v1_5", chave, assinatura,
      new TextEncoder().encode(`${partes[0]}.${partes[1]}`));
  } catch {
    valida = false;
  }
  if (!valida) return { erro: "token_invalido" };

  if (corpo.aud !== projeto || corpo.iss !== `https://securetoken.google.com/${projeto}`) return { erro: "token_invalido" };
  if (!Number.isFinite(corpo.exp) || corpo.exp <= agoraS - FOLGA_S) return { erro: "token_vencido" };
  if (!Number.isFinite(corpo.iat) || corpo.iat > agoraS + FOLGA_S) return { erro: "token_invalido" };
  if (Number.isFinite(corpo.auth_time) && corpo.auth_time > agoraS + FOLGA_S) return { erro: "token_invalido" };
  if (typeof corpo.sub !== "string" || corpo.sub.length === 0 || corpo.sub.length > 128) return { erro: "token_invalido" };
  if (corpo.firebase?.sign_in_provider !== "google.com") return { erro: "provedor_invalido" };
  if (typeof corpo.email !== "string" || corpo.email_verified !== true) return { erro: "email_nao_verificado" };
  return { uid: corpo.sub, email: corpo.email };
}
