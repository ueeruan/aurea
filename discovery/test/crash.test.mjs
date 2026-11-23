// Crash: validação, deduplicação, limites, e-mail (Resend e Email Workers), admin e
// armazenamento no D1 (migração 0014) sem nenhuma gravação no KV.
import { test } from "node:test";
import assert from "node:assert/strict";
import { gunzipSync } from "node:zlib";

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

/** O que ficou no D1: as colunas e o registro descomprimido. */
function guardados(env) {
  return env.AUREA_DB.sqlite.prepare("SELECT * FROM crash_reports ORDER BY id").all()
    .map((l) => ({ ...l, registro: JSON.parse(gunzipSync(l.report_gz).toString("utf8")) }));
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
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), RESEND_API_KEY: "re_segredo", CRASH_EMAIL_TO: "ruanpablombl@gmail.com" };
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

    const linhas = guardados(env);
    assert.equal(linhas.length, 1, "um relatorio, uma linha");
    assert.equal(linhas[0].emailed, 1);
    assert.equal(linhas[0].email_via, "resend");
    assert.equal(linhas[0].email_error, null);
    assert.equal(linhas[0].registro.reportId, "exit-1790000000000-4242");
    assert.ok(!JSON.stringify(linhas[0]).includes("198.51.100.7"), "o IP nao e guardado");
    assert.equal(env.AUREA_KV.escritas, 0, "nada no KV");
  } finally {
    f.restaurar();
  }
});

test("Email Workers: MIME com Message-ID, destino e pilha no corpo", async () => {
  const enviados = [];
  const env = {
    AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), CRASH_EMAIL_FROM: "crash@aurea.app", RESEND_API_KEY: "nao-deve-ser-usada",
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
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), CRASH_ADMIN_TOKEN: ADMIN };
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
  assert.equal((await chamar({ AUREA_DB: d1Falso() }, "/api/crash/list", { token: ADMIN })).status, 401, "sem secret configurado, admin fechado");
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
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), RESEND_API_KEY: "k", CRASH_EMAIL_DAILY_LIMIT: "2" };
  const f = fetchFalso();
  try {
    for (let i = 0; i < LIMITES_CRASH.instalacaoHora.max; i++) {
      assert.equal((await enviar(env, relatorio({ reportId: `rel-limite-${i}` }))).status, 202);
    }
    const r = await enviar(env, relatorio({ reportId: "rel-limite-extra" }));
    assert.equal(r.status, 429);
    assert.equal(f.chamadas.length, 2, "so 2 e-mails no dia; o resto ficou guardado");
    const naoEnviados = guardados(env).filter((x) => !x.emailed);
    assert.equal(naoEnviados.length, LIMITES_CRASH.instalacaoHora.max - 2);
    assert.equal(naoEnviados[0].email_error, "teto_diario");

    const grande = await enviar(env, relatorio({ reportId: "rel-grande-01", installId: "3f2b8c1e-9d4a-4e6f-8a7b-000000000001", stack: "x".repeat(300 * 1024) }));
    assert.equal(grande.status, 413);
  } finally {
    f.restaurar();
  }
});

test("falha do provedor nao perde o relatorio", async () => {
  const env = { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), RESEND_API_KEY: "k" };
  const f = fetchFalso(500);
  try {
    const r = await enviar(env, relatorio({ reportId: "rel-falha-01" }));
    assert.equal(r.status, 202);
    assert.equal(r.corpo.emailed, false);
    const [guardado] = guardados(env);
    assert.equal(guardado.emailed, 0);
    assert.match(guardado.email_error, /^resend_http_500/);
  } finally {
    f.restaurar();
  }
});

test("D1: grava sem KV, pilha de 200 KiB comprimida, lista pagina dos mais novos e item le tudo", async () => {
  const env = { AUREA_DB: d1Falso(), CRASH_ADMIN_TOKEN: ADMIN };
  const pilha = "Thread 0 Crashed:\n" + "0   Aurea  0x0000000100a1b2c3 aurea::render + 64\n".repeat(4000);
  const r = await enviar(env, relatorio({ reportId: "ios-grande-0001", platform: "ios", stack: pilha }));
  assert.equal(r.status, 202, "sem KV nenhum o crash continua sendo aceito");
  const [linha] = guardados(env);
  assert.ok(linha.report_gz.length < 20 * 1024, `gzip pequeno (${linha.report_gz.length} bytes)`);
  const item = await chamar(env, `/api/crash/item?id=${r.corpo.id}`, { token: ADMIN });
  assert.equal(item.status, 200);
  assert.equal(item.corpo.stack, validarRelatorio(relatorio({ stack: pilha })).relatorio.stack);
  assert.equal(item.corpo.platform, "ios");
  assert.equal(item.corpo.email_enviado, false);

  // 150 relatórios de instalações diferentes: 100 na primeira página, 51 na segunda.
  for (let i = 0; i < 150; i++) {
    const inst = `3f2b8c1e-9d4a-4e6f-8a7b-${String(i).padStart(12, "0")}`;
    const ok = await enviar(env, relatorio({ reportId: `pagina-${String(i).padStart(4, "0")}`, installId: inst }), { ip: `203.0.113.${i % 250}` });
    assert.equal(ok.status, 202);
  }
  const p1 = await chamar(env, "/api/crash/list", { token: ADMIN });
  assert.equal(p1.corpo.items.length, 100);
  assert.ok(p1.corpo.cursor);
  const t = p1.corpo.items.map((x) => x.t);
  assert.deepEqual(t, [...t].sort((a, b) => b - a), "os mais novos primeiro");
  const p2 = await chamar(env, `/api/crash/list?cursor=${p1.corpo.cursor}`, { token: ADMIN });
  assert.equal(p2.corpo.items.length, 51);
  assert.equal(p2.corpo.cursor, null);
  const ids = new Set([...p1.corpo.items, ...p2.corpo.items].map((x) => x.id));
  assert.equal(ids.size, 151, "nenhum repetido entre as paginas");
  assert.deepEqual(Object.keys(p1.corpo.items[0]).sort(), ["b", "e", "id", "m", "p", "r", "t"], "mesmo formato da lista do KV");
  assert.equal((await chamar(env, "/api/crash/list?cursor=x'--", { token: ADMIN })).status, 400);
  assert.equal((await chamar(env, "/api/crash/item?id=0000000000000-0000000000000000", { token: ADMIN })).status, 404);
});

test("D1: vencido some da lista e do item na hora e e apagado no proximo POST; reenvio simultaneo nao duplica", async () => {
  const env = { AUREA_DB: d1Falso(), CRASH_ADMIN_TOKEN: ADMIN, CRASH_TTL_DAYS: "30" };
  const velho = await enviar(env, relatorio({ reportId: "velho-000001" }));
  assert.equal(velho.status, 202);
  const [linha] = guardados(env);
  assert.equal(linha.expires_at - linha.received_at, 30 * 86400_000);
  env.AUREA_DB.sqlite.prepare("UPDATE crash_reports SET expires_at = ?").run(Date.now() - 1);
  assert.equal((await chamar(env, "/api/crash/list", { token: ADMIN })).corpo.items.length, 0);
  assert.equal((await chamar(env, `/api/crash/item?id=${velho.corpo.id}`, { token: ADMIN })).status, 404);

  const [a, b] = await Promise.all([
    enviar(env, relatorio({ reportId: "junto-000001" })),
    enviar(env, relatorio({ reportId: "junto-000001" })),
  ]);
  assert.deepEqual([a.status, b.status].sort(), [200, 202]);
  assert.equal(a.corpo.id, b.corpo.id);
  const restantes = guardados(env).map((x) => x.registro.reportId);
  assert.deepEqual(restantes, ["junto-000001"], "o vencido foi apagado e o reenvio nao duplicou");
});

test("D1 fora do ar: 503, sem e-mail", async () => {
  const quebrado = d1Falso();
  const prepareOriginal = quebrado.prepare;
  quebrado.prepare = (sql) => (/crash_reports/.test(sql) && /INSERT/.test(sql)
    ? { bind: () => ({ run: async () => { throw new Error("D1_ERROR: network"); } }) }
    : prepareOriginal(sql));
  const env = { AUREA_DB: quebrado, RESEND_API_KEY: "k" };
  const f = fetchFalso();
  try {
    const r = await enviar(env, relatorio({ reportId: "d1-fora-0001" }));
    assert.equal(r.status, 503);
    assert.equal(f.chamadas.length, 0);
  } finally {
    f.restaurar();
  }
});
