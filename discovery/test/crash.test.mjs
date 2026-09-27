// Crash: validação, deduplicação, limites, e-mail (Resend e Email Workers) e admin.
import { test } from "node:test";
import assert from "node:assert/strict";

import worker from "../worker.js";
import { validarRelatorio, cortarUtf8, PILHA_MAX_BYTES, LIMITES_CRASH } from "../crash.js";
import { provedorDeEmail, montarMime } from "../email.js";
import { esquecerMemo } from "../contas.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const ctx = { waitUntil: (p) => p };
const ADMIN = "token-de-admin-bem-comprido-123";
const INSTALACAO = "3f2b8c1e-9d4a-4e6f-8a7b-1c2d3e4f5a6b";

function relatorio(extra = {}) {
  return {
    reportId: "exit-1790000000000-4242", installId: INSTALACAO, platform: "android",
    appVersion: "2.0.0-beta2", appBuild: "2124", os: "Android", osVersion: "14 (SDK 34)",
    deviceModel: "SM-A515F", manufacturer: "samsung", abi: "arm64-v8a", reason: "CRASH_NATIVE",
    phase: "Aurea build=2124 phase=VIDEO_NATIVE uptimeMs=5000", timestamp: Date.now() - 60_000,
    stack: "signal 11 (SIGSEGV)\n#00 pc 0000000000123456 libaurea.so (aurea::decode+12)", ...extra,
  };
}

function fetchFalso(status = 200) {
  const chamadas = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    chamadas.push({ url: String(url), init, corpo: JSON.parse(init.body) });
    return new Response(JSON.stringify({ id: "re_1" }), { status });
  };
  return { chamadas, restaurar: () => { globalThis.fetch = original; } };
}

async function chamar(env, caminho, opcoes) {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  return { status: r.status, corpo: await r.json() };
}
const enviar = (env, corpo, opcoes = {}) => chamar(env, "/api/crash", { metodo: "POST", corpo, ...opcoes });

test("validacao: ids, plataforma, horario, controle removido e pilha cortada em 200 KiB", () => {
  assert.ok(validarRelatorio(relatorio()).relatorio);
  assert.equal(validarRelatorio(relatorio({ reportId: "x" })).erro, "report_id_invalido");
  assert.equal(validarRelatorio(relatorio({ installId: "nao-uuid" })).erro, "install_id_invalido");
  assert.equal(validarRelatorio(relatorio({ platform: "windows" })).erro, "plataforma_invalida");
  assert.equal(validarRelatorio(relatorio({ timestamp: Date.now() + 3 * 86400_000 })).erro, "timestamp_invalido");
  assert.equal(validarRelatorio(relatorio({ reason: "" })).erro, "motivo_obrigatorio");
  assert.equal(validarRelatorio(relatorio({ stack: 12 })).erro, "stack_invalida");
  const r = validarRelatorio(relatorio({ deviceModel: "SM\u0000-A5\u001b15F", stack: "é".repeat(PILHA_MAX_BYTES) })).relatorio;
  assert.equal(r.deviceModel, "SM-A515F");
  assert.ok(new TextEncoder().encode(r.stack).length <= PILHA_MAX_BYTES);
  assert.equal(r.stackTruncated, true);
  assert.ok(!r.stack.includes("�"), "o corte nao parte um caractere UTF-8");
  assert.deepEqual(cortarUtf8("abc", 10), { texto: "abc", cortado: false });
});

test("provedor: auto prefere o binding da Cloudflare; none desliga; sem nada nao envia", () => {
  assert.equal(provedorDeEmail({}), null);
  assert.equal(provedorDeEmail({ RESEND_API_KEY: "k" }), "resend");
  assert.equal(provedorDeEmail({ RESEND_API_KEY: "k", CRASH_EMAIL: {}, CRASH_EMAIL_FROM: "c@x.com" }), "cloudflare");
  assert.equal(provedorDeEmail({ RESEND_API_KEY: "k", CRASH_EMAIL: {} }), "resend", "sem remetente o binding nao serve");
  assert.equal(provedorDeEmail({ RESEND_API_KEY: "k", CRASH_EMAIL_PROVIDER: "none" }), null);
  assert.equal(provedorDeEmail({ RESEND_API_KEY: "k", CRASH_EMAIL_PROVIDER: "cloudflare" }), null);
});

test("Resend: um e-mail por relatorio para o desenvolvedor, com anexo; reenvio nao duplica", async () => {
  esquecerMemo();
  const env = { AUREA_KV: kvFalso(), RESEND_API_KEY: "re_segredo", CRASH_EMAIL_TO: "ruanpablombl@gmail.com" };
  const f = fetchFalso();
  try {
    const r = await enviar(env, relatorio());
    assert.equal(r.status, 202);
    assert.equal(r.corpo.emailed, true);
    assert.equal(f.chamadas.length, 1);
    const c = f.chamadas[0];
    assert.equal(c.url, "https://api.resend.com/emails");
    assert.equal(c.init.headers.authorization, "Bearer re_segredo");
    assert.deepEqual(c.corpo.to, ["ruanpablombl@gmail.com"]);
    assert.match(c.corpo.subject, /CRASH_NATIVE .* samsung SM-A515F/);
    assert.match(c.corpo.text, /libaurea\.so/);
    assert.match(c.corpo.text, /phase=VIDEO_NATIVE/);
    assert.equal(c.corpo.attachments.length, 1);
    assert.match(Buffer.from(c.corpo.attachments[0].content, "base64").toString("utf8"), /SIGSEGV/);

    const de_novo = await enviar(env, relatorio());
    assert.equal(de_novo.status, 200);
    assert.equal(de_novo.corpo.duplicate, true);
    assert.equal(de_novo.corpo.id, r.corpo.id);
    assert.equal(f.chamadas.length, 1, "o mesmo relatorio nao vira segundo e-mail");

    const guardado = JSON.parse([...env.AUREA_KV.mapa].find(([k]) => k.startsWith("crash:"))[1].valor);
    assert.equal(guardado.email_enviado, true);
    assert.equal(guardado.email_via, "resend");
    assert.ok(!JSON.stringify(guardado).includes("198.51.100.7"), "o IP nao e guardado");
  } finally {
    f.restaurar();
  }
});

test("Email Workers: MIME com Message-ID, destino e pilha no corpo", async () => {
  const enviados = [];
  const env = {
    AUREA_KV: kvFalso(), CRASH_EMAIL_FROM: "crash@aurea.app", RESEND_API_KEY: "nao-deve-ser-usada",
    CRASH_EMAIL: { send: async (m) => { enviados.push(m); } },
  };
  const f = fetchFalso();
  try {
    const r = await enviar(env, relatorio({ reportId: "exit-cf-00000001" }));
    assert.equal(r.status, 202);
    assert.equal(f.chamadas.length, 0);
  } finally {
    f.restaurar();
  }
  assert.equal(enviados.length, 1);
  const m = enviados[0];
  assert.equal(m.from, "crash@aurea.app");
  assert.equal(m.to, "ruanpablombl@gmail.com");
  assert.match(m.raw, /^Message-ID: <[0-9a-f-]+@aurea\.app>\r$/m);
  assert.match(m.raw, /^To: <ruanpablombl@gmail\.com>\r$/m);
  assert.match(m.raw, /^Subject: =\?UTF-8\?B\?/m);
  const partes = m.raw.split(/\r\n\r\n/);
  const corpo = Buffer.from(partes[2].split("\r\n--")[0].replace(/\r\n/g, ""), "base64").toString("utf8");
  assert.match(corpo, /aurea::decode/);
  assert.match(m.raw, /Content-Disposition: attachment; filename="crash-/);
});

test("MIME: cabecalho nao aceita quebra de linha injetada no assunto", () => {
  const bruto = montarMime({ de: "a@x.com", para: "b@y.com", assunto: "oi\r\nBcc: vitima@z.com", texto: "t" });
  assert.ok(!/^Bcc:/m.test(bruto));
});

test("sem provedor o relatorio fica guardado e o admin lista e le; sem token de admin, nada", async () => {
  const env = { AUREA_KV: kvFalso(), CRASH_ADMIN_TOKEN: ADMIN };
  const r = await enviar(env, relatorio({ reportId: "java-guardado-01" }));
  assert.equal(r.status, 202);
  assert.equal(r.corpo.emailed, false);
  assert.equal((await chamar(env, "/api/crash/list")).status, 401);
  assert.equal((await chamar(env, "/api/crash/list", { token: "errado-errado-errado-errado-1" })).status, 401);
  const lista = await chamar(env, "/api/crash/list", { token: ADMIN });
  assert.equal(lista.status, 200);
  assert.equal(lista.corpo.items.length, 1);
  assert.equal(lista.corpo.items[0].r, "CRASH_NATIVE");
  assert.equal(lista.corpo.items[0].e, 0);
  const item = await chamar(env, `/api/crash/item?id=${lista.corpo.items[0].id}`, { token: ADMIN });
  assert.equal(item.corpo.reportId, "java-guardado-01");
  assert.equal(item.corpo.email_erro, "sem_provedor");
  assert.equal((await chamar({ AUREA_KV: kvFalso() }, "/api/crash/list", { token: ADMIN })).status, 401, "sem secret configurado, admin fechado");
});

test("e-mail da conta vem da SESSAO; o do corpo sem sessao entra marcado", async () => {
  esquecerMemo();
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), CRASH_ADMIN_TOKEN: ADMIN };
  const cad = await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email: "dona@aurea.app", password: "12345678" } });
  await enviar(env, relatorio({ reportId: "com-sessao-001", email: "outra@pessoa.com" }), { token: cad.corpo.token });
  await enviar(env, relatorio({ reportId: "sem-sessao-001", email: "outra@pessoa.com" }));
  const lista = await chamar(env, "/api/crash/list", { token: ADMIN });
  const itens = await Promise.all(lista.corpo.items.map((i) => chamar(env, `/api/crash/item?id=${i.id}`, { token: ADMIN })));
  const porId = Object.fromEntries(itens.map((i) => [i.corpo.reportId, i.corpo.email]));
  assert.equal(porId["com-sessao-001"], "dona@aurea.app");
  assert.equal(porId["sem-sessao-001"], "outra@pessoa.com (informado pelo app, sem sessão)");
});

test("limites: por instalacao, tamanho do corpo e teto diario de e-mails", async () => {
  const env = { AUREA_KV: kvFalso(), RESEND_API_KEY: "k", CRASH_EMAIL_DAILY_LIMIT: "2" };
  const f = fetchFalso();
  try {
    for (let i = 0; i < LIMITES_CRASH.instalacaoHora.max; i++) {
      assert.equal((await enviar(env, relatorio({ reportId: `rel-limite-${i}` }))).status, 202);
    }
    const r = await enviar(env, relatorio({ reportId: "rel-limite-extra" }));
    assert.equal(r.status, 429);
    assert.equal(f.chamadas.length, 2, "so 2 e-mails no dia; o resto ficou guardado");
    const naoEnviados = [...env.AUREA_KV.mapa].filter(([k]) => k.startsWith("crash:")).map(([, v]) => JSON.parse(v.valor))
      .filter((x) => !x.email_enviado);
    assert.equal(naoEnviados.length, LIMITES_CRASH.instalacaoHora.max - 2);
    assert.equal(naoEnviados[0].email_erro, "teto_diario");

    const grande = await enviar(env, relatorio({ reportId: "rel-grande-01", installId: "3f2b8c1e-9d4a-4e6f-8a7b-000000000001", stack: "x".repeat(300 * 1024) }));
    assert.equal(grande.status, 413);
  } finally {
    f.restaurar();
  }
});

test("falha do provedor nao perde o relatorio", async () => {
  const env = { AUREA_KV: kvFalso(), RESEND_API_KEY: "k" };
  const f = fetchFalso(500);
  try {
    const r = await enviar(env, relatorio({ reportId: "rel-falha-01" }));
    assert.equal(r.status, 202);
    assert.equal(r.corpo.emailed, false);
    const guardado = JSON.parse([...env.AUREA_KV.mapa].find(([k]) => k.startsWith("crash:"))[1].valor);
    assert.match(guardado.email_erro, /^resend_http_500/);
  } finally {
    f.restaurar();
  }
});
