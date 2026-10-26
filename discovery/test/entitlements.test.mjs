// Direitos pagos: GET /api/entitlements (sessão) e POST /api/admin/entitlements (segredo).
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";

import worker from "../worker.js";
import { esquecerMemo } from "../contas.js";
import { PRODUTOS, LIMITE_ADMIN } from "../entitlements.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const SEGREDO = "segredo-do-admin-de-teste-0123456789";
const HYPER = "aureahyper_lifetime";

function ambiente(extra = { AUREA_ADMIN_SECRET: SEGREDO }) {
  esquecerMemo();
  return { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), ...extra };
}
const ctx = { waitUntil: (p) => p };
const chamar = async (env, caminho, opcoes) => {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  return { status: r.status, corpo: await r.json(), cabecalhos: r.headers };
};
const cadastro = async (env, email) =>
  (await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email, password: "12345678" } })).corpo;
const meus = (env, token) => chamar(env, "/api/entitlements", { token });
const admin = (env, corpo, token = SEGREDO, ip) =>
  chamar(env, "/api/admin/entitlements", { metodo: "POST", corpo, token, ip });

test("entitlements: lista de produtos conhecidos", () => {
  assert.deepEqual([...PRODUTOS], [HYPER]);
  assert.ok(Object.isFrozen(PRODUTOS));
});

test("entitlements: sem sessao, sessao invalida ou encerrada -> 401 com o mesmo corpo da sessao", async () => {
  const env = ambiente();
  const semToken = await meus(env);
  assert.equal(semToken.status, 401);
  assert.deepEqual(semToken.corpo, { error: "nao_autorizado" });
  const rotaSessao = await chamar(env, "/api/auth/session");
  assert.deepEqual(semToken.corpo, rotaSessao.corpo);
  assert.equal((await meus(env, "A".repeat(43))).status, 401);
  assert.equal((await meus(env, "curto")).status, 401);

  const a = await cadastro(env, "sai@aurea.app");
  assert.equal((await chamar(env, "/api/auth/logout", { metodo: "POST", token: a.token })).status, 200);
  assert.equal((await meus(env, a.token)).status, 401);
});

test("entitlements: sessao valida sem linhas -> products [], checkedAt em segundos, no-store", async () => {
  const env = ambiente();
  const a = await cadastro(env, "novo@aurea.app");
  const antes = Math.floor(Date.now() / 1000);
  const r = await meus(env, a.token);
  assert.equal(r.status, 200);
  assert.deepEqual(r.corpo.products, []);
  assert.ok(Number.isInteger(r.corpo.checkedAt) && r.corpo.checkedAt >= antes && r.corpo.checkedAt <= antes + 5);
  assert.equal(r.cabecalhos.get("cache-control"), "no-store");
  // Outros métodos não existem.
  assert.equal((await chamar(env, "/api/entitlements", { metodo: "POST", token: a.token, corpo: {} })).status, 404);
});

test("admin: sem segredo configurado (ou curto demais) a rota recusa, nunca fica aberta", async () => {
  for (const extra of [{}, { AUREA_ADMIN_SECRET: "" }, { AUREA_ADMIN_SECRET: "curto" }]) {
    const env = ambiente(extra);
    await cadastro(env, "alvo@aurea.app");
    for (const token of ["", "curto", "Bearer", SEGREDO]) {
      const r = await admin(env, { email: "alvo@aurea.app", product: HYPER, action: "grant" }, token);
      assert.equal(r.status, 503);
      assert.equal(r.corpo.error, "admin_desativado");
    }
    assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM entitlements").get().n, 0);
  }
});

test("admin: segredo errado ou ausente -> 401 e nada muda", async () => {
  const env = ambiente();
  await cadastro(env, "alvo@aurea.app");
  const corpo = { email: "alvo@aurea.app", product: HYPER, action: "grant" };
  for (const token of ["", "x", SEGREDO.slice(0, -1) + "X", SEGREDO + "a"]) {
    const r = await admin(env, corpo, token);
    assert.equal(r.status, 401);
    assert.deepEqual(r.corpo, { error: "nao_autorizado" });
  }
  // Token de sessão não serve como segredo de admin.
  const s = await cadastro(env, "outro@aurea.app");
  assert.equal((await admin(env, corpo, s.token)).status, 401);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM entitlements").get().n, 0);
});

test("admin: conceder por e-mail -> a conta passa a ver o produto; revogar -> some", async () => {
  const env = ambiente();
  const a = await cadastro(env, "cliente@aurea.app");
  const b = await cadastro(env, "vizinho@aurea.app");

  const g = await admin(env, { email: "  Cliente@AUREA.app ", product: HYPER, action: "grant", note: "beta tester" });
  assert.equal(g.status, 200);
  assert.deepEqual(g.corpo, { ok: true, email: "cliente@aurea.app", product: HYPER, action: "grant", products: [HYPER] });
  assert.equal(g.cabecalhos.get("cache-control"), "no-store");

  const linha = env.AUREA_DB.sqlite.prepare("SELECT * FROM entitlements").get();
  assert.equal(linha.source, "admin");
  assert.equal(linha.note, "beta tester");
  assert.ok(Math.abs(linha.granted_at - Date.now() / 1000) < 5, "granted_at em segundos Unix");

  assert.deepEqual((await meus(env, a.token)).corpo.products, [HYPER]);
  assert.deepEqual((await meus(env, b.token)).corpo.products, [], "outra conta nao ganha nada");

  // Conceder de novo é idempotente (regrava a linha, sem duplicar).
  const g2 = await admin(env, { email: "cliente@aurea.app", product: HYPER, action: "grant" });
  assert.equal(g2.status, 200);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM entitlements").get().n, 1);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT note FROM entitlements").get().note, null);

  const rv = await admin(env, { email: "cliente@aurea.app", product: HYPER, action: "revoke" });
  assert.equal(rv.status, 200);
  assert.deepEqual(rv.corpo, { ok: true, email: "cliente@aurea.app", product: HYPER, action: "revoke", products: [] });
  assert.deepEqual((await meus(env, a.token)).corpo.products, []);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM entitlements").get().n, 0, "revogar apaga a linha");
  // Revogar o que não existe também dá certo.
  assert.equal((await admin(env, { email: "cliente@aurea.app", product: HYPER, action: "revoke" })).status, 200);
});

test("admin: produto desconhecido, acao, e-mail e nota invalidos -> 400; e-mail sem conta -> 404", async () => {
  const env = ambiente();
  await cadastro(env, "alvo@aurea.app");
  const base = { email: "alvo@aurea.app", product: HYPER, action: "grant" };
  const casos = [
    [{ ...base, product: "aureahyper_mensal" }, 400, "produto_desconhecido"],
    [{ ...base, product: undefined }, 400, "produto_desconhecido"],
    [{ ...base, product: ["aureahyper_lifetime"] }, 400, "produto_desconhecido"],
    [{ ...base, action: "delete" }, 400, "acao_invalida"],
    [{ ...base, action: undefined }, 400, "acao_invalida"],
    [{ ...base, email: "nao-e-email" }, 400, "email_invalido"],
    [{ ...base, note: 5 }, 400, "nota_invalida"],
    [{ ...base, note: "x".repeat(201) }, 400, "nota_invalida"],
    [{ ...base, email: "ninguem@aurea.app" }, 404, "conta_nao_encontrada"],
  ];
  for (const [corpo, status, error] of casos) {
    const r = await admin(env, corpo);
    assert.equal(r.status, status, JSON.stringify(corpo));
    assert.equal(r.corpo.error, error);
  }
  assert.equal((await admin(env, "{nao json")).status, 400);
  assert.equal((await admin(env, { ...base, note: "x".repeat(2000) })).status, 413, "corpo acima de 1 KiB");
  assert.equal((await admin(env, { ...base, note: "x".repeat(200) })).status, 200, "200 caracteres cabem");
  assert.equal((await chamar(env, "/api/admin/entitlements", { token: SEGREDO })).status, 404, "GET nao existe");
});

test("admin: tentativas por IP tem teto (contam todas, como o login)", async () => {
  const env = ambiente();
  await cadastro(env, "alvo@aurea.app");
  const corpo = { email: "alvo@aurea.app", product: HYPER, action: "grant" };
  for (let i = 0; i < LIMITE_ADMIN.max; i++) assert.equal((await admin(env, corpo, "errado", "203.0.113.9")).status, 401);
  const bloqueado = await admin(env, corpo, SEGREDO, "203.0.113.9");
  assert.equal(bloqueado.status, 429);
  assert.ok(Number(bloqueado.cabecalhos.get("retry-after")) > 0);
  // Outro IP segue livre.
  assert.equal((await admin(env, corpo, SEGREDO, "203.0.113.10")).status, 200);
});

test("entitlements: produto que o servidor nao conhece nunca sai na resposta", async () => {
  const env = ambiente();
  const a = await cadastro(env, "alvo@aurea.app");
  const uid = env.AUREA_DB.sqlite.prepare("SELECT id FROM users WHERE email = 'alvo@aurea.app'").get().id;
  env.AUREA_DB.sqlite.prepare("INSERT INTO entitlements (user_id, product, granted_at, source) VALUES (?, 'legado_x', 1, 'admin')").run(uid);
  assert.deepEqual((await meus(env, a.token)).corpo.products, []);
  await admin(env, { email: "alvo@aurea.app", product: HYPER, action: "grant" });
  assert.deepEqual((await meus(env, a.token)).corpo.products, [HYPER]);
});

test("excluir conta apaga os direitos dela e nao os de outra conta", async () => {
  const env = ambiente();
  const a = await cadastro(env, "sai@aurea.app");
  await cadastro(env, "fica@aurea.app");
  for (const email of ["sai@aurea.app", "fica@aurea.app"]) {
    assert.equal((await admin(env, { email, product: HYPER, action: "grant" })).status, 200);
  }
  const db = env.AUREA_DB.sqlite;
  const uidA = db.prepare("SELECT id FROM users WHERE email = 'sai@aurea.app'").get().id;

  const r = await chamar(env, "/api/auth/delete", { metodo: "POST", corpo: { confirm: true }, token: a.token });
  assert.equal(r.status, 200);
  assert.equal(db.prepare("SELECT COUNT(*) n FROM entitlements WHERE user_id = ?").get(uidA).n, 0);
  assert.equal(db.prepare("SELECT COUNT(*) n FROM entitlements").get().n, 1, "a outra conta mantem o direito");

  // O mesmo e-mail recadastrado não herda o produto.
  const novo = await cadastro(env, "sai@aurea.app");
  assert.deepEqual((await meus(env, novo.token)).corpo.products, []);
});

test("excluir conta continua funcionando num banco sem a migracao 0007", async () => {
  const env = ambiente();
  const a = await cadastro(env, "sai@aurea.app");
  env.AUREA_DB.sqlite.exec("DROP TABLE entitlements");
  const r = await chamar(env, "/api/auth/delete", { metodo: "POST", corpo: { confirm: true }, token: a.token });
  assert.equal(r.status, 200);
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) n FROM users").get().n, 0);
  // E a leitura dos direitos falha fechada (503), nunca como "nao tem".
  const b = await cadastro(env, "outro@aurea.app");
  const g = await meus(env, b.token);
  assert.equal(g.status, 503);
  assert.equal(g.corpo.error, "entitlements_indisponiveis");
});
