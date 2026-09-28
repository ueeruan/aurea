// "Relatar um problema": validação, gravação no D1 (não no KV), e-mail, deduplicação e limites.
import { test } from "node:test";
import assert from "node:assert/strict";

import worker from "../worker.js";
import { validarRelato, textoDoRelato, LIMITES_RELATO } from "../relato.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const ctx = { waitUntil: (p) => p };
const INSTALACAO = "3f2b8c1e-9d4a-4e6f-8a7b-1c2d3e4f5a6b";

function relato(extra = {}) {
  return {
    reportId: "report-1790000000000-77", installId: INSTALACAO, platform: "android",
    appVersion: "2.0.0-beta2", appBuild: "2126", os: "Android", osVersion: "14 (SDK 34)",
    deviceModel: "SM-A515F", manufacturer: "samsung", abi: "arm64-v8a", locale: "pt-BR",
    whatDid: "Importei um video 4K", whatHappened: "Esperava a previa; a tela ficou preta",
    steps: "1. Novo projeto\n2. Importar video", ...extra,
  };
}

function fetchFalso(status = 200) {
  const chamadas = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    chamadas.push({ url: String(url), corpo: JSON.parse(init.body) });
    return new Response(JSON.stringify({ id: "re_1" }), { status });
  };
  return { chamadas, restaurar: () => { globalThis.fetch = original; } };
}

async function enviar(env, corpo, opcoes = {}) {
  const r = await worker.fetch(pedido("/api/report", { metodo: "POST", corpo, ...opcoes }), env, ctx);
  return { status: r.status, corpo: await r.json() };
}

test("validacao: ids, plataforma, descricao obrigatoria, controle removido, textos cortados", () => {
  assert.ok(validarRelato(relato()).relato);
  assert.equal(validarRelato(relato({ reportId: "x" })).erro, "report_id_invalido");
  assert.equal(validarRelato(relato({ installId: "nao" })).erro, "install_id_invalido");
  assert.equal(validarRelato(relato({ platform: "web" })).erro, "plataforma_invalida");
  assert.equal(validarRelato(relato({ whatHappened: "  " })).erro, "descricao_obrigatoria");
  assert.equal(validarRelato(relato({ steps: 12 })).erro, "texto_invalido");
  const r = validarRelato(relato({ deviceModel: "SM\u0000-A5\u001b15F", steps: "a".repeat(9000) })).relato;
  assert.equal(r.deviceModel, "SM-A515F");
  assert.equal(r.steps.length, 4000);
  const texto = textoDoRelato(r, "id-1", "a@b.com");
  assert.match(texto, /o que fez[\s\S]*Importei/);
  assert.match(texto, /passos para repetir/);
});

test("grava no D1 (nunca no KV), manda um e-mail e reenvio nao duplica", async () => {
  const kv = kvFalso();
  const db = d1Falso();
  const env = { AUREA_KV: kv, AUREA_DB: db, RESEND_API_KEY: "re_segredo", CRASH_EMAIL_TO: "dev@exemplo.com" };
  const f = fetchFalso();
  try {
    const r = await enviar(env, relato());
    assert.equal(r.status, 202);
    assert.equal(r.corpo.emailed, true);
    assert.equal(f.chamadas.length, 1);
    assert.match(f.chamadas[0].corpo.subject, /Relato .* android 2126/);
    assert.match(f.chamadas[0].corpo.text, /tela ficou preta/);
    assert.equal(kv.escritas, 0, "nada vai para o KV");
    const linha = db.sqlite.prepare("SELECT * FROM user_reports").get();
    assert.equal(linha.what_did, "Importei um video 4K");
    assert.equal(linha.emailed, 1);
    assert.equal(linha.email, "");

    const de_novo = await enviar(env, relato());
    assert.equal(de_novo.status, 200);
    assert.equal(de_novo.corpo.duplicate, true);
    assert.equal(f.chamadas.length, 1);
    assert.equal(db.sqlite.prepare("SELECT COUNT(*) AS n FROM user_reports").get().n, 1);
  } finally {
    f.restaurar();
  }
});

test("sem provedor de e-mail o relato fica guardado; corpo ruim e limite respondem certo", async () => {
  const db = d1Falso();
  const env = { AUREA_KV: kvFalso(), AUREA_DB: db };
  const r = await enviar(env, relato({ reportId: "report-sem-email-1" }));
  assert.equal(r.status, 202);
  assert.equal(r.corpo.emailed, false);
  assert.equal(db.sqlite.prepare("SELECT emailed FROM user_reports").get().emailed, 0);

  assert.equal((await enviar(env, relato({ whatHappened: "" }))).status, 400);
  assert.equal((await enviar(env, "{nao json")).status, 400);

  let ultimo = 0;
  for (let i = 0; i < LIMITES_RELATO.instalacaoHora.max + 1; i++) {
    ultimo = (await enviar(env, relato({ reportId: `report-limite-${i}-xx` }))).status;
  }
  assert.equal(ultimo, 429);
});

test("sem D1 a rota responde indisponivel; GET nao existe", async () => {
  const env = { AUREA_KV: kvFalso() };
  assert.equal((await enviar(env, relato())).status, 503);
  const r = await worker.fetch(pedido("/api/report"), { AUREA_KV: kvFalso(), AUREA_DB: d1Falso() }, ctx);
  assert.equal(r.status, 404);
});
