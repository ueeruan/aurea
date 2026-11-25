// Assinaturas mensais (16/11/2026): AureaHyper e cada efeito/Texto 3D avulso.
// POST /api/entitlements/purchase confere na Play (subscriptionsv2) e na App Store
// (transactions + subscriptions), grava o vencimento e GET /api/entitlements só
// devolve o que vale agora, conferindo de novo a renovação quando o prazo passa.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";
import { generateKeyPairSync } from "node:crypto";

import worker from "../worker.js";
import { esquecerMemo } from "../contas.js";
import { HYPER_SUBSCRIPTION, HYPER_SEMANAL, HYPER_PLANOS, RENOVACAO_INTERVALO_S } from "../entitlements.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const SEGREDO = "segredo-do-admin-de-teste-0123456789";
const BUNDLE = "com.aurea.aurea.3ATTUN44P8";
const ctx = { waitUntil: (p) => p };
const agora = () => Math.floor(Date.now() / 1000);
const iso = (s) => new Date(s * 1000).toISOString();

function ambiente() {
  esquecerMemo();
  const rsa = generateKeyPairSync("rsa", { modulusLength: 2048 }).privateKey.export({ type: "pkcs8", format: "pem" });
  const ec = generateKeyPairSync("ec", { namedCurve: "prime256v1" }).privateKey.export({ type: "pkcs8", format: "pem" });
  return {
    AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), AUREA_ADMIN_SECRET: SEGREDO,
    AUREAOS_PURCHASES_ENABLED: "true",
    GOOGLE_PLAY_SERVICE_ACCOUNT: JSON.stringify({ client_email: "assinaturas@test", private_key: rsa }),
    APPLE_IAP_KEY_ID: "KEY123", APPLE_IAP_ISSUER_ID: "issuer", APPLE_IAP_PRIVATE_KEY: ec, APPLE_BUNDLE_ID: BUNDLE,
  };
}
const chamar = async (env, caminho, opcoes) => {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  return { status: r.status, corpo: await r.json() };
};
const cadastro = async (env, email) =>
  (await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email, password: "12345678" } })).corpo.token;
const comprar = (env, sessao, platform, product, token) =>
  chamar(env, "/api/entitlements/purchase", { metodo: "POST", corpo: { platform, product, token }, token: sessao });
const meus = (env, sessao, plataforma = "google") => chamar(env, `/api/entitlements?platform=${plataforma}`, { token: sessao });

/** JWS "assinado" da Apple: o Worker só lê o corpo (a resposta vem por TLS com a nossa chave). */
const jws = (obj) => `x.${Buffer.from(JSON.stringify(obj)).toString("base64url")}.y`;

/** fetch falso das duas lojas. `play[token]` e `apple.tx[id]`/`apple.subs[original]` controlam a resposta. */
function lojas() {
  const play = {};
  const apple = { tx: {}, subs: {} };
  const chamadas = [];
  const real = globalThis.fetch;
  globalThis.fetch = async (url) => {
    const text = String(url);
    chamadas.push(text);
    if (text.includes("oauth2.googleapis.com")) return new Response(JSON.stringify({ access_token: "ok" }));
    if (text.includes("/purchases/subscriptionsv2/tokens/")) {
      const token = decodeURIComponent(text.split("/tokens/")[1]);
      const r = play[token];
      if (r === "erro") return new Response("{}", { status: 500 });
      return r ? new Response(JSON.stringify(r)) : new Response("{}", { status: 404 });
    }
    const tx = text.match(/\/inApps\/v1\/transactions\/(\d+)$/);
    if (tx) {
      if (!text.includes("api.storekit-sandbox")) return new Response("{}", { status: 404 });   // só existe no sandbox
      const info = apple.tx[tx[1]];
      return info ? new Response(JSON.stringify({ signedTransactionInfo: jws(info) })) : new Response("{}", { status: 404 });
    }
    const sub = text.match(/\/inApps\/v1\/subscriptions\/(\d+)$/);
    if (sub) {
      const s = apple.subs[sub[1]];
      if (s === "erro") return new Response("{}", { status: 500 });
      if (!s) return new Response("{}", { status: 404 });
      return new Response(JSON.stringify({ bundleId: BUNDLE, data: [{ subscriptionGroupIdentifier: "g", lastTransactions: [{
        originalTransactionId: sub[1], status: s.status, signedTransactionInfo: jws(s.info),
        signedRenewalInfo: s.renewal ? jws(s.renewal) : undefined }] }] }));
    }
    throw new Error(`fetch inesperado: ${text}`);
  };
  return { play, apple, chamadas, restaurar: () => { globalThis.fetch = real; } };
}

const playAtiva = (product, expira, extra = {}) => ({
  subscriptionState: "SUBSCRIPTION_STATE_ACTIVE", latestOrderId: "GPA.3300-1..0",
  lineItems: [{ productId: product, expiryTime: iso(expira) }], ...extra,
});
const appleTx = (id, product, expira, extra = {}) => ({
  transactionId: id, originalTransactionId: "1000", productId: product, bundleId: BUNDLE,
  type: "Auto-Renewable Subscription", expiresDate: expira * 1000, ...extra,
});

test("Play: assinatura ativa libera o produto até o vencimento; a mesma compra não vale em duas contas", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const b = await cadastro(env, "b@aurea.app");
  const loja = lojas();
  try {
    const fim = agora() + 30 * 86400;
    loja.play["tok-twitch"] = playAtiva("aurea_fx_twitch", fim);
    assert.ok((await meus(env, a)).corpo.purchasable.includes("aurea_fx_twitch"));
    const ok = await comprar(env, a, "google", "aurea_fx_twitch", "tok-twitch");
    assert.equal(ok.status, 200);
    assert.deepEqual(ok.corpo.products, ["aurea_fx_twitch"]);
    assert.equal(ok.corpo.expires.aurea_fx_twitch, fim);
    assert.ok(loja.chamadas.some((u) => u.includes("/applications/com.aurea.editor/purchases/subscriptionsv2/tokens/tok-twitch")));
    const linha = env.AUREA_DB.sqlite.prepare("SELECT * FROM entitlements").get();
    assert.equal(linha.source, "google");
    assert.equal(linha.expires_at, fim);
    assert.equal(linha.store_ref, "tok-twitch", "o token fica para conferir a renovação");

    const g = await meus(env, a);
    assert.deepEqual(g.corpo.products, ["aurea_fx_twitch"]);
    assert.deepEqual(g.corpo.expires, { aurea_fx_twitch: fim });
    assert.equal((await comprar(env, a, "google", "aurea_fx_twitch", "tok-twitch")).status, 200, "restaurar na mesma conta");
    // Renovação muda o latestOrderId (..1), mas a compra é a mesma (GPA.3300-1).
    loja.play["tok-twitch-b"] = playAtiva("aurea_fx_twitch", fim, { latestOrderId: "GPA.3300-1..1" });
    assert.equal((await comprar(env, b, "google", "aurea_fx_twitch", "tok-twitch-b")).status, 409);
    assert.deepEqual((await meus(env, b)).corpo.products, []);
  } finally {
    loja.restaurar();
  }
});

test("Play: estados que não liberam, produto errado e beta fechado", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const loja = lojas();
  try {
    const futuro = agora() + 86400;
    loja.play.vencida = { ...playAtiva("aurea_texto3d", agora() - 10), subscriptionState: "SUBSCRIPTION_STATE_EXPIRED" };
    loja.play.pendente = { ...playAtiva("aurea_texto3d", futuro), subscriptionState: "SUBSCRIPTION_STATE_PENDING" };
    loja.play.suspensa = { ...playAtiva("aurea_texto3d", futuro), subscriptionState: "SUBSCRIPTION_STATE_ON_HOLD" };
    loja.play.outra = playAtiva("aurea_fx_signal", futuro);
    loja.play.ativaMasPassou = playAtiva("aurea_texto3d", agora() - 5);
    for (const [token, error] of [["vencida", "purchase_expired"], ["pendente", "purchase_pending"], ["suspensa", "purchase_expired"],
      ["outra", "purchase_invalid"], ["ativaMasPassou", "purchase_expired"], ["nao-existe", "purchase_invalid"]]) {
      const r = await comprar(env, a, "google", "aurea_texto3d", token);
      assert.equal(r.status, 402, token);
      assert.equal(r.corpo.error, error, token);
    }
    assert.equal((await comprar(env, a, "google", "aurea_fx_nao_existe", "x")).status, 400, "produto que o servidor não vende");
    // Cancelada (renovação desligada) ainda vale até o fim do período pago.
    loja.play.cancelada = { ...playAtiva("aurea_texto3d", futuro), subscriptionState: "SUBSCRIPTION_STATE_CANCELED" };
    assert.equal((await comprar(env, a, "google", "aurea_texto3d", "cancelada")).status, 200);
    // Beta fechado: a mesma chave do AureaOS trava as assinaturas.
    env.AUREAOS_PURCHASES_ENABLED = "false";
    loja.play.nova = playAtiva("aurea_hyper_mensal", futuro);
    const fechado = await comprar(env, a, "google", "aurea_hyper_mensal", "nova");
    assert.equal(fechado.status, 503);
    assert.equal(fechado.corpo.error, "payments_unavailable");
    assert.deepEqual((await meus(env, a)).corpo.purchasable, []);
    assert.deepEqual((await meus(env, a)).corpo.products, ["aurea_texto3d"], "o que já foi pago continua");
  } finally {
    loja.restaurar();
  }
});

test("renovação: vencida há pouco é conferida de novo na loja (no máximo 1x por hora); loja fora não libera", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const db = env.AUREA_DB.sqlite;
  const loja = lojas();
  try {
    loja.play.tok = playAtiva("aurea_hyper_mensal", agora() + 3600);
    assert.equal((await comprar(env, a, "google", "aurea_hyper_mensal", "tok")).status, 200);
    // O período pago passou (o relógio "andou").
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = ?").run(agora() - 60, agora() - 2 * RENOVACAO_INTERVALO_S);

    // Loja fora: não libera e não grava conferência (tenta de novo depois).
    loja.play.tok = "erro";
    const fora = await meus(env, a);
    assert.equal(fora.status, 200);
    assert.deepEqual(fora.corpo.products, []);

    // A Play renovou: novo vencimento, volta a valer.
    const novoFim = agora() + 30 * 86400;
    loja.play.tok = playAtiva("aurea_hyper_mensal", novoFim, { latestOrderId: "GPA.3300-1..1" });
    const renovada = await meus(env, a);
    assert.deepEqual(renovada.corpo.products, ["aurea_hyper_mensal"]);
    assert.equal(renovada.corpo.expires.aurea_hyper_mensal, novoFim);
    assert.equal(db.prepare("SELECT expires_at FROM entitlements").get().expires_at, novoFim);

    // Venceu de novo e a loja diz que acabou: some, e a loja só é consultada 1x por hora.
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = ?").run(agora() - 60, agora() - 2 * RENOVACAO_INTERVALO_S);
    loja.play.tok = { ...playAtiva("aurea_hyper_mensal", agora() - 60), subscriptionState: "SUBSCRIPTION_STATE_EXPIRED" };
    assert.deepEqual((await meus(env, a)).corpo.products, []);
    const antes = loja.chamadas.length;
    assert.deepEqual((await meus(env, a)).corpo.products, []);
    assert.equal(loja.chamadas.length, antes, "conferida há menos de 1 h: não pergunta de novo");

    // Vencida há muito tempo: não é mais conferida sozinha.
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = NULL").run(agora() - 90 * 86400);
    assert.deepEqual((await meus(env, a)).corpo.products, []);
    assert.equal(loja.chamadas.length, antes);
  } finally {
    loja.restaurar();
  }
});

test("App Store: compra anual aparece na resposta e na verificação seguinte", async () => {
  const env = ambiente();
  const session = await cadastro(env, "hyper@example.com");
  const store = lojas();
  try {
    const expiresAt = agora() + 365 * 86400;
    store.apple.tx["3901"] = appleTx("3901", HYPER_SUBSCRIPTION, expiresAt);
    const bought = await comprar(env, session, "apple", HYPER_SUBSCRIPTION, "3901");
    assert.equal(bought.status, 200);
    assert.deepEqual(bought.corpo.products, [HYPER_SUBSCRIPTION]);
    assert.equal(bought.corpo.expires[HYPER_SUBSCRIPTION], expiresAt);
    const checked = await meus(env, session, "apple");
    assert.equal(checked.status, 200);
    assert.deepEqual(checked.corpo.products, [HYPER_SUBSCRIPTION]);
  } finally {
    store.restaurar();
  }
});

test("App Store: assinatura auto-renovável, bundle, revogação e renovação pelo status", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const b = await cadastro(env, "b@aurea.app");
  const db = env.AUREA_DB.sqlite;
  const loja = lojas();
  try {
    const fim = agora() + 30 * 86400;
    loja.apple.tx["2001"] = appleTx("2001", "aurea_fx_wave_warp", fim);
    loja.apple.tx["2002"] = appleTx("2002", "aurea_fx_wave_warp", fim, { bundleId: "com.outro.app" });
    loja.apple.tx["2003"] = appleTx("2003", "aurea_fx_wave_warp", fim, { revocationDate: Date.now() });
    loja.apple.tx["2004"] = appleTx("2004", "aurea_fx_wave_warp", fim, { type: "Non-Consumable" });
    loja.apple.tx["2005"] = appleTx("2005", "aurea_fx_sharpen", fim);
    for (const id of ["2002", "2003", "2004", "2005", "9999"]) {
      assert.equal((await comprar(env, a, "apple", "aurea_fx_wave_warp", id)).status, 402, id);
    }
    assert.equal((await comprar(env, a, "apple", "aurea_fx_wave_warp", "nao-numero")).status, 402);
    const ok = await comprar(env, a, "apple", "aurea_fx_wave_warp", "2001");
    assert.equal(ok.status, 200);
    assert.deepEqual(ok.corpo.products, ["aurea_fx_wave_warp"]);
    assert.equal(ok.corpo.expires.aurea_fx_wave_warp, fim);
    assert.equal(db.prepare("SELECT store_ref FROM entitlements").get().store_ref, "1000", "guarda o originalTransactionId");
    // A mesma assinatura (mesmo originalTransactionId, outra transação) em outra conta: 409.
    loja.apple.tx["2006"] = appleTx("2006", "aurea_fx_wave_warp", fim);
    assert.equal((await comprar(env, b, "apple", "aurea_fx_wave_warp", "2006")).status, 409);

    // A transação mandada venceu, mas a assinatura renovou: o status (1 = ativa) diz até quando.
    const renovado = agora() + 60 * 86400;
    loja.apple.tx["2010"] = appleTx("2010", "aurea_texto3d", agora() - 100, { originalTransactionId: "3000" });
    loja.apple.subs["3000"] = { status: 1, info: appleTx("2011", "aurea_texto3d", renovado, { originalTransactionId: "3000" }) };
    const r = await comprar(env, a, "apple", "aurea_texto3d", "2010");
    assert.equal(r.status, 200);
    assert.equal(r.corpo.expires.aurea_texto3d, renovado);
    loja.apple.subs["3000"] = { status: 2, info: appleTx("2011", "aurea_texto3d", agora() - 100, { originalTransactionId: "3000" }) };
    loja.apple.tx["2012"] = appleTx("2012", "aurea_texto3d", agora() - 100, { originalTransactionId: "3000" });
    assert.equal((await comprar(env, a, "apple", "aurea_texto3d", "2012")).corpo.error, "purchase_expired");

    // Renovação no GET: período de graça de cobrança (status 4) vale até o fim da graça.
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = NULL WHERE product = 'aurea_fx_wave_warp'").run(agora() - 30);
    const graca = agora() + 6 * 86400;
    loja.apple.subs["1000"] = { status: 4, info: appleTx("2013", "aurea_fx_wave_warp", agora() - 30), renewal: { gracePeriodExpiresDate: graca * 1000 } };
    const g = await meus(env, a, "apple");
    assert.ok(g.corpo.products.includes("aurea_fx_wave_warp"));
    assert.equal(g.corpo.expires.aurea_fx_wave_warp, graca);
    // Revogada (reembolso): some depois do vencimento.
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = NULL WHERE product = 'aurea_fx_wave_warp'").run(agora() - 30);
    loja.apple.subs["1000"] = { status: 5, info: appleTx("2013", "aurea_fx_wave_warp", agora() + 86400, { revocationDate: Date.now() }) };
    assert.ok(!(await meus(env, a, "apple")).corpo.products.includes("aurea_fx_wave_warp"));
  } finally {
    loja.restaurar();
  }
});

test("concessão do admin (sem vencimento) nunca é rebaixada por uma assinatura; o legado vitalício segue", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const admin = (corpo) => chamar(env, "/api/admin/entitlements", { metodo: "POST", corpo, token: SEGREDO });
  assert.equal((await admin({ email: "a@aurea.app", product: "aureahyper_lifetime", action: "grant" })).status, 200);
  assert.equal((await admin({ email: "a@aurea.app", product: "aurea_texto3d", action: "grant", note: "cortesia" })).status, 200);
  const loja = lojas();
  try {
    loja.play.tok = playAtiva("aurea_texto3d", agora() + 3600);
    const r = await comprar(env, a, "google", "aurea_texto3d", "tok");
    assert.equal(r.status, 200);
    const linha = env.AUREA_DB.sqlite.prepare("SELECT * FROM entitlements WHERE product = 'aurea_texto3d'").get();
    assert.equal(linha.expires_at, null, "a concessão sem vencimento ficou");
    assert.equal(linha.source, "admin");
    const g = await meus(env, a);
    assert.deepEqual(g.corpo.products, ["aurea_texto3d", "aureahyper_lifetime"]);
    assert.deepEqual(g.corpo.expires, {}, "nada aqui vence");
  } finally {
    loja.restaurar();
  }
});

test("sem a migração 0012 a leitura continua (nada vence) e a compra de assinatura falha fechada", async () => {
  const env = ambiente();
  const a = await cadastro(env, "a@aurea.app");
  const db = env.AUREA_DB.sqlite;
  db.exec("DROP INDEX entitlements_expires; ALTER TABLE entitlements DROP COLUMN expires_at; ALTER TABLE entitlements DROP COLUMN store_ref; ALTER TABLE entitlements DROP COLUMN store_checked_at;");
  assert.equal((await chamar(env, "/api/admin/entitlements", { metodo: "POST", corpo: { email: "a@aurea.app", product: "aureahyper_lifetime", action: "grant" }, token: SEGREDO })).status, 200);
  assert.deepEqual((await meus(env, a)).corpo.products, ["aureahyper_lifetime"]);
  const loja = lojas();
  try {
    loja.play.tok = playAtiva("aurea_hyper_mensal", agora() + 3600);
    const r = await comprar(env, a, "google", "aurea_hyper_mensal", "tok");
    assert.equal(r.status, 503);
    assert.deepEqual((await meus(env, a)).corpo.products, ["aureahyper_lifetime"]);
  } finally {
    loja.restaurar();
  }
});

// 1.0.9: os três planos do AureaHyper (semanal, mensal, anual) se vendem e liberam
// tudo; o semanal vence em 7 dias e renova como os outros (o vencimento vem da loja).
test("1.0.9: AureaHyper semanal na Play e na App Store, com renovação semanal", async () => {
  const env = ambiente();
  const a = await cadastro(env, "semanal@aurea.app");
  const b = await cadastro(env, "semanal-apple@aurea.app");
  const db = env.AUREA_DB.sqlite;
  const loja = lojas();
  try {
    const venda = (await meus(env, a)).corpo.purchasable;
    for (const plano of HYPER_PLANOS) assert.ok(venda.includes(plano), plano);
    const semana = agora() + 7 * 86400;
    loja.play.sem = playAtiva(HYPER_SEMANAL, semana);
    const ok = await comprar(env, a, "google", HYPER_SEMANAL, "sem");
    assert.equal(ok.status, 200);
    assert.deepEqual(ok.corpo.products, [HYPER_SEMANAL]);
    assert.equal(ok.corpo.expires[HYPER_SEMANAL], semana);
    // A semana passou e a Play renovou por mais 7 dias.
    db.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = ?").run(agora() - 60, agora() - 2 * RENOVACAO_INTERVALO_S);
    const proxima = agora() + 7 * 86400;
    loja.play.sem = playAtiva(HYPER_SEMANAL, proxima, { latestOrderId: "GPA.3300-1..1" });
    const renovada = await meus(env, a);
    assert.deepEqual(renovada.corpo.products, [HYPER_SEMANAL]);
    assert.equal(renovada.corpo.expires[HYPER_SEMANAL], proxima);

    loja.apple.tx["4901"] = appleTx("4901", HYPER_SEMANAL, semana);
    const apple = await comprar(env, b, "apple", HYPER_SEMANAL, "4901");
    assert.equal(apple.status, 200);
    assert.deepEqual(apple.corpo.products, [HYPER_SEMANAL]);
  } finally {
    loja.restaurar();
  }
});
