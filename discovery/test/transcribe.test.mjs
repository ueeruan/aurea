// Legendas na nuvem: POST /api/transcribe (Workers AI falso).
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";

import worker from "../worker.js";
import { esquecerMemo } from "../contas.js";
import { palavrasDaResposta, paraBase64, MODELO } from "../transcribe.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const ctx = { waitUntil: (p) => p };
const chamar = async (env, caminho, opcoes = {}) => {
  let req;
  if (opcoes.corpoBruto) {
    const h = { "cf-connecting-ip": "198.51.100.7", "content-type": "audio/wav" };
    if (opcoes.token) h.authorization = `Bearer ${opcoes.token}`;
    req = new Request("https://aurea-ai-discovery.aureaapp.workers.dev" + caminho, { method: "POST", headers: h, body: opcoes.corpoBruto });
  } else {
    req = pedido(caminho, opcoes);
  }
  const r = await worker.fetch(req, env, ctx);
  return { status: r.status, corpo: await r.json() };
};
function ambiente(ai) {
  esquecerMemo();
  return { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), AI: ai };
}
async function entrar(env) {
  const r = await chamar(env, "/api/auth/signup", { metodo: "POST", corpo: { email: "fala@exemplo.com", password: "senha-forte-123" } });
  return r.corpo.token;
}

test("palavras com tempo vêm dos segmentos; sem words, o texto é repartido", () => {
  const r = { segments: [
    { start: 0, end: 1, text: "oi mundo", words: [{ word: " oi", start: 0, end: 0.4 }, { word: "mundo", start: 0.5, end: 1 }] },
    { start: 2, end: 4, text: "tudo bem" },
  ] };
  assert.deepEqual(palavrasDaResposta(r), [
    { w: "oi", s: 0, e: 0.4 }, { w: "mundo", s: 0.5, e: 1 }, { w: "tudo", s: 2, e: 3 }, { w: "bem", s: 3, e: 4 },
  ]);
  assert.equal(paraBase64(new Uint8Array([104, 105])), "aGk=");
});

test("rota: exige conta, manda o áudio em base64 ao modelo e devolve as palavras", async () => {
  const chamadas = [];
  const env = ambiente({ run: async (modelo, entrada) => {
    chamadas.push({ modelo, entrada });
    return { transcription_info: { language: "pt" }, segments: [{ start: 0, end: 1, words: [{ word: "olá", start: 0.1, end: 0.6 }] }] };
  } });
  const sem = await chamar(env, "/api/transcribe?lang=pt", { metodo: "POST", corpoBruto: new Uint8Array([1, 2, 3]) });
  assert.equal(sem.status, 401);
  const token = await entrar(env);
  const ok = await chamar(env, "/api/transcribe?lang=pt", { metodo: "POST", corpoBruto: new Uint8Array([1, 2, 3]), token });
  assert.equal(ok.status, 200);
  assert.deepEqual(ok.corpo, { language: "pt", words: [{ w: "olá", s: 0.1, e: 0.6 }] });
  assert.equal(chamadas[0].modelo, MODELO);
  assert.equal(chamadas[0].entrada.audio, "AQID");
  assert.equal(chamadas[0].entrada.language, "pt");
  const ruim = await chamar(env, "/api/transcribe?lang=português", { metodo: "POST", corpoBruto: new Uint8Array([1]), token });
  assert.equal(ruim.status, 400);
  const semAi = await chamar(ambiente(undefined), "/api/transcribe", { metodo: "POST", corpoBruto: new Uint8Array([1]), token });
  assert.equal(semAi.status, 503);
});
