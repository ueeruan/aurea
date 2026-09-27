// Contas: hash de senha, cadastro, login, sessão, saída, limites e contagem.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";

import worker from "../worker.js";
import {
  hashSenha, conferirSenha, iguais, normalizarEmail, erroDaSenha, esquecerMemo, PBKDF2_ITERACOES, LIMITES,
} from "../contas.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

function ambiente() {
  esquecerMemo();
  return { AUREA_KV: kvFalso(), AUREA_DB: d1Falso() };
}
const ctx = { waitUntil: (p) => p };
const chamar = async (env, caminho, opcoes) => {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  return { status: r.status, corpo: await r.json(), cabecalhos: r.headers };
};
const cadastro = (env, email, password, ip) => chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email, password }, ip });
const login = (env, email, password, ip) => chamar(env, "/api/auth/login", { metodo: "POST", corpo: { email, password }, ip });

test("hash PBKDF2-SHA256: 100000 iteracoes, sal de 16 bytes aleatorio, confere so a senha certa", async () => {
  const a = await hashSenha("senha-forte-1");
  const b = await hashSenha("senha-forte-1");
  assert.match(a, /^pbkdf2_sha256\$100000\$[A-Za-z0-9+/]{22}==\$[A-Za-z0-9+/]{43}=$/);
  assert.equal(PBKDF2_ITERACOES, 100000);
  assert.notEqual(a, b, "sal aleatorio por usuario");
  assert.ok(!a.includes("senha-forte-1"));
  assert.equal(await conferirSenha("senha-forte-1", a), true);
  assert.equal(await conferirSenha("senha-forte-2", a), false);
  const p = a.split("$");
  p[3] = (p[3][0] === "A" ? "B" : "A") + p[3].slice(1);
  assert.equal(await conferirSenha("senha-forte-1", p.join("$")), false, "hash adulterado");
  assert.equal(await conferirSenha("senha-forte-1", a.replace("$100000$", "$200000$")), false, "acima do teto do Workers");
  assert.equal(await conferirSenha("x", "lixo"), false);
});

test("comparacao em tempo constante compara bytes, nao referencias", () => {
  assert.equal(iguais(new Uint8Array([1, 2, 3]), new Uint8Array([1, 2, 3])), true);
  assert.equal(iguais(new Uint8Array([1, 2, 3]), new Uint8Array([1, 2, 4])), false);
  assert.equal(iguais(new Uint8Array([1, 2]), new Uint8Array([1, 2, 3])), false);
});

test("e-mail normalizado (trim + minusculas) e validado; senha minima de 8", () => {
  assert.equal(normalizarEmail("  Ana.Silva+aurea@Exemplo.COM.br "), "ana.silva+aurea@exemplo.com.br");
  for (const ruim of ["", "ana", "ana@", "@x.com", "a@b", "a@@b.com", "a b@x.com", "a@x.c", ".a@x.com", "a..b@x.com", "a@-x.com", 42, null]) {
    assert.equal(normalizarEmail(ruim), null, String(ruim));
  }
  assert.equal(erroDaSenha("1234567"), "senha_curta");
  assert.equal(erroDaSenha("12345678"), null);
  assert.equal(erroDaSenha("🔒🔒🔒🔒🔒🔒🔒"), "senha_curta", "conta pontos de codigo, nao UTF-16");
  assert.equal(erroDaSenha("x".repeat(129)), "senha_longa");
  assert.equal(erroDaSenha(undefined), "senha_invalida");
});

test("cadastro: 201 com token de 32 bytes; D1 guarda so o hash; KV guarda so o hash do token", async () => {
  const env = ambiente();
  const r = await cadastro(env, " Pessoa@Aurea.app ", "minha senha secreta");
  assert.equal(r.status, 201);
  assert.equal(r.corpo.email, "pessoa@aurea.app");
  assert.match(r.corpo.token, /^[A-Za-z0-9_-]{43}$/);
  assert.equal(r.corpo.users, 1);
  assert.equal(r.cabecalhos.get("cache-control"), "no-store");

  const linha = env.AUREA_DB.sqlite.prepare("SELECT * FROM users").get();
  assert.equal(linha.email, "pessoa@aurea.app");
  assert.match(linha.password_hash, /^pbkdf2_sha256\$100000\$/);
  assert.ok(!JSON.stringify(linha).includes("minha senha secreta"));
  for (const [chave, v] of env.AUREA_KV.mapa) {
    assert.ok(!chave.includes(r.corpo.token) && !v.valor.includes(r.corpo.token), "token cru nunca e gravado");
    assert.ok(!v.valor.includes("minha senha secreta"));
    assert.ok(!chave.includes("198.51.100.7") && !chave.includes("pessoa@aurea.app"), "IP e e-mail so em hash nas chaves");
  }
  const sessoes = [...env.AUREA_KV.mapa.keys()].filter((k) => k.startsWith("sess:"));
  assert.equal(sessoes.length, 1);
  const ttl = (env.AUREA_KV.mapa.get(sessoes[0]).expira - Date.now()) / 86400_000;
  assert.ok(ttl > 179 && ttl <= 180, "sessao de 180 dias");
});

test("e-mail repetido (outra caixa/espacos) e recusado e nao conta duas vezes", async () => {
  const env = ambiente();
  assert.equal((await cadastro(env, "dup@aurea.app", "12345678")).status, 201);
  const r = await cadastro(env, "  DUP@aurea.APP", "outra-senha", "203.0.113.9");
  assert.equal(r.status, 409);
  assert.equal(r.corpo.error, "email_em_uso");
  esquecerMemo();
  assert.equal((await chamar(env, "/api/stats/users")).corpo.count, 1);
});

test("login: senha certa entra; senha errada e e-mail inexistente recebem a MESMA resposta e o mesmo trabalho", async () => {
  const env = ambiente();
  await cadastro(env, "login@aurea.app", "senha-certa-123");
  const ok = await login(env, "LOGIN@aurea.app", "senha-certa-123");
  assert.equal(ok.status, 200);
  assert.equal(ok.corpo.email, "login@aurea.app");
  assert.match(ok.corpo.token, /^[A-Za-z0-9_-]{43}$/);

  const original = crypto.subtle.deriveBits.bind(crypto.subtle);
  let derivacoes = 0;
  crypto.subtle.deriveBits = (...a) => { derivacoes++; return original(...a); };
  try {
    const errada = await login(env, "login@aurea.app", "senha-errada-123", "203.0.113.20");
    const inexistente = await login(env, "ninguem@aurea.app", "senha-errada-123", "203.0.113.21");
    assert.equal(errada.status, 401);
    assert.deepEqual(errada.corpo, inexistente.corpo);
    assert.equal(inexistente.status, 401);
    assert.deepEqual(errada.corpo, { error: "credenciais_invalidas" });
    assert.equal(derivacoes, 2, "o e-mail inexistente tambem roda o PBKDF2");
  } finally {
    crypto.subtle.deriveBits = original;
  }
  const linha = env.AUREA_DB.sqlite.prepare("SELECT last_login_at FROM users").get();
  assert.ok(linha.last_login_at > 0);
});

test("sessao: valida com o token, recusa token ruim, morre no logout", async () => {
  const env = ambiente();
  const { corpo } = await cadastro(env, "sessao@aurea.app", "12345678");
  const valida = await chamar(env, "/api/auth/session", { token: corpo.token });
  assert.equal(valida.status, 200);
  assert.equal(valida.corpo.email, "sessao@aurea.app");
  assert.equal((await chamar(env, "/api/auth/session", { token: "x".repeat(43) })).status, 401);
  assert.equal((await chamar(env, "/api/auth/session")).status, 401);
  assert.equal((await chamar(env, "/api/auth/logout", { metodo: "POST", token: corpo.token })).corpo.ok, true);
  assert.equal((await chamar(env, "/api/auth/session", { token: corpo.token })).status, 401);
  // Logout de novo (ou sem token) nao e erro.
  assert.equal((await chamar(env, "/api/auth/logout", { metodo: "POST" })).status, 200);
});

test("sessao de usuario apagado deixa de valer", async () => {
  const env = ambiente();
  const { corpo } = await cadastro(env, "apagado@aurea.app", "12345678");
  env.AUREA_DB.sqlite.exec("DELETE FROM users");
  assert.equal((await chamar(env, "/api/auth/session", { token: corpo.token })).status, 401);
});

test("limite de login por e-mail vale de qualquer IP; por IP no cadastro", async () => {
  const env = ambiente();
  await cadastro(env, "alvo@aurea.app", "senha-certa-123", "192.0.2.1");
  for (let i = 0; i < LIMITES.loginEmail.max; i++) {
    const r = await login(env, "alvo@aurea.app", "chute-" + i + "xxxx", `192.0.2.${10 + i}`);
    assert.equal(r.status, 401);
  }
  const bloqueado = await login(env, "alvo@aurea.app", "senha-certa-123", "192.0.2.99");
  assert.equal(bloqueado.status, 429);
  assert.equal(bloqueado.corpo.error, "muitas_tentativas");
  assert.ok(Number(bloqueado.cabecalhos.get("retry-after")) > 0);

  const env2 = ambiente();
  for (let i = 0; i < LIMITES.cadastroIp.max; i++) {
    assert.equal((await cadastro(env2, `p${i}@aurea.app`, "12345678", "192.0.2.50")).status, 201);
  }
  assert.equal((await cadastro(env2, "extra@aurea.app", "12345678", "192.0.2.50")).status, 429);
  assert.equal((await cadastro(env2, "extra@aurea.app", "12345678", "192.0.2.51")).status, 201);
});

test("cadastros simultaneos: a contagem e exata e o e-mail repetido entra uma vez so", async () => {
  const env = ambiente();
  const emails = Array.from({ length: 10 }, (_, i) => `c${i}@aurea.app`);
  const pedidos = [...emails, "c3@aurea.app", "C7@AUREA.APP"].map((e, i) => cadastro(env, e, "12345678", `198.18.0.${i}`));
  const respostas = await Promise.all(pedidos);
  assert.equal(respostas.filter((r) => r.status === 201).length, 10);
  assert.equal(respostas.filter((r) => r.status === 409).length, 2);
  esquecerMemo();
  const stats = await chamar(env, "/api/stats/users");
  assert.equal(stats.status, 200);
  assert.deepEqual(stats.corpo, { count: 10 });
  assert.equal(stats.cabecalhos.get("cache-control"), "public, max-age=60");
  assert.equal(env.AUREA_DB.sqlite.prepare("SELECT COUNT(*) AS n FROM users").get().n, 10);
});

test("entrada ruim: formato, tamanho, tipo e HTTPS", async () => {
  const env = ambiente();
  assert.equal((await cadastro(env, "nao-e-email", "12345678")).corpo.error, "email_invalido");
  assert.equal((await cadastro(env, "ok@aurea.app", "1234567")).corpo.error, "senha_curta");
  const tipo = await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: "email=a", cabecalhos: { "content-type": "text/plain" } });
  assert.equal(tipo.status, 400);
  const grande = await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email: "g@aurea.app", password: "x".repeat(5000) } });
  assert.equal(grande.status, 413);
  const quebrado = await chamar(env, "/api/auth/login", { metodo: "POST", corpo: "{nao json" });
  assert.equal(quebrado.corpo.error, "json_invalido");
  const http = await worker.fetch(new Request("http://aurea-ai-discovery.aureaapp.workers.dev/api/stats/users"), env, ctx);
  assert.equal(http.status, 403);
});

test("sem D1 configurado as contas respondem 503 e o resto do Worker segue", async () => {
  const env = { AUREA_KV: kvFalso() };
  const r = await chamar(env, "/api/auth/login", { metodo: "POST", corpo: { email: "a@aurea.app", password: "12345678" } });
  assert.equal(r.status, 503);
  assert.equal(r.corpo.error, "contas_indisponiveis");
  const discovery = await chamar(env, "/server");
  assert.equal(discovery.status, 200);
  assert.equal(typeof discovery.corpo.online, "boolean");
});
