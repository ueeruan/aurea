// Login com Google pelo Firebase Auth: firebase.js (conferência) e POST /api/auth/firebase.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import worker from "../worker.js";
import { esquecerMemo } from "../contas.js";
import { conferirTokenFirebase, esquecerChaves, JWKS_URL } from "../firebase.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const PROJETO = "aurea-teste";
const AGORA = 1_800_000_000;
const par = await webcrypto.subtle.generateKey(
  { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" }, true, ["sign", "verify"]);
const jwk = { ...(await webcrypto.subtle.exportKey("jwk", par.publicKey)), kid: "k1", alg: "RS256", use: "sig" };
const b64url = (b) => Buffer.from(b).toString("base64url");

async function assinar(corpo, cab = { alg: "RS256", kid: "k1", typ: "JWT" }) {
  const dados = `${b64url(JSON.stringify(cab))}.${b64url(JSON.stringify(corpo))}`;
  const sig = await webcrypto.subtle.sign("RSASSA-PKCS1-v1_5", par.privateKey, new TextEncoder().encode(dados));
  return `${dados}.${b64url(sig)}`;
}
const corpoBom = (extra = {}) => ({
  aud: PROJETO, iss: `https://securetoken.google.com/${PROJETO}`, sub: "uid-123", iat: AGORA - 10, exp: AGORA + 3600,
  auth_time: AGORA - 10, email: "Pessoa@Gmail.com", email_verified: true, firebase: { sign_in_provider: "google.com" }, ...extra,
});
const buscar = async (url) => {
  assert.equal(url, JWKS_URL);
  return new Response(JSON.stringify({ keys: [jwk] }), { headers: { "cache-control": "public, max-age=3600" } });
};
const conferir = (token) => { esquecerChaves(); return conferirTokenFirebase(token, PROJETO, { buscar, agoraS: AGORA }); };

test("token bom do Google passa e devolve uid e e-mail", async () => {
  assert.deepEqual(await conferir(await assinar(corpoBom())), { uid: "uid-123", email: "Pessoa@Gmail.com" });
});

test("recusa projeto errado, vencido, outro provedor, e-mail não verificado e assinatura adulterada", async () => {
  assert.equal((await conferir(await assinar(corpoBom({ aud: "outro" })))).erro, "token_invalido");
  assert.equal((await conferir(await assinar(corpoBom({ exp: AGORA - 1000 })))).erro, "token_vencido");
  assert.equal((await conferir(await assinar(corpoBom({ firebase: { sign_in_provider: "password" } })))).erro, "provedor_invalido");
  assert.equal((await conferir(await assinar(corpoBom({ email_verified: false })))).erro, "email_nao_verificado");
  const t = await assinar(corpoBom());
  const adulterado = t.split(".").map((p, i) => (i === 1 ? b64url(JSON.stringify(corpoBom({ email: "outro@x.com" }))) : p)).join(".");
  assert.equal((await conferir(adulterado)).erro, "token_invalido");
  assert.equal((await conferir(await assinar(corpoBom(), { alg: "none", kid: "k1" }))).erro, "token_invalido");
  assert.equal((await conferirTokenFirebase("x", "")).erro, "firebase_desativado");
});

test("rota: e-mail novo cria conta (uma vez), e-mail existente entra na mesma conta", async () => {
  esquecerMemo();
  esquecerChaves();
  const fetchOriginal = globalThis.fetch;
  globalThis.fetch = buscar;
  try {
    const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), FIREBASE_PROJECT_ID: PROJETO };
    const ctx = { waitUntil: (p) => p };
    // O token conferido usa o relógio real: assina com iat/exp em volta de agora.
    const agora = Math.floor(Date.now() / 1000);
    const token = await assinar(corpoBom({ iat: agora - 5, auth_time: agora - 5, exp: agora + 3600 }));
    const chamar = async (corpo) => {
      const r = await worker.fetch(pedido("/api/auth/firebase", { metodo: "POST", corpo }), env, ctx);
      return { status: r.status, corpo: await r.json() };
    };
    const a = await chamar({ idToken: token });
    assert.equal(a.status, 200);
    assert.equal(a.corpo.email, "pessoa@gmail.com");
    assert.equal(a.corpo.created, true);
    assert.ok(a.corpo.token);
    const b = await chamar({ idToken: token });
    assert.equal(b.status, 200);
    assert.equal(b.corpo.created, false);
    // A conta criada pelo Google não aceita login por senha.
    const senha = await worker.fetch(pedido("/api/auth/login", { metodo: "POST", corpo: { email: "pessoa@gmail.com", password: "qualquer-senha" } }), env, ctx);
    assert.equal(senha.status, 401);
    const ruim = await chamar({ idToken: "a.b.c" });
    assert.equal(ruim.status, 401);
  } finally {
    globalThis.fetch = fetchOriginal;
  }
});
