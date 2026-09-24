// Testes das partes puras do backend de vídeo (node --test discovery/test).
// Nada aqui chama a 8Scale: só validação, assinatura e tradução de erros.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";

import { validarPedido, configuracao, tipoDaImagem, saidaAceitavel } from "../ai_video.js";
import { codigoDoHttp, EightScaleVideoProvider } from "../provedores.js";
import { conferirCallback } from "../levelplay.js";

const cfg = configuracao({
  AI_VIDEO_ENABLED: "true", AI_VIDEO_DURATIONS: "5", AI_VIDEO_ASPECTS: "16:9,9:16",
  AI_VIDEO_RESOLUTIONS: "480p", AI_VIDEO_MAX_PROMPT_CHARS: "800", AI_VIDEO_COST_PER_JOB_USD: "0.5",
});
const base = { mode: "text_to_video", prompt: "uma orca saltando", duration: 5, aspectRatio: "16:9", resolution: "480p" };

test("pedido valido passa e sai limpo", () => {
  const r = validarPedido({ ...base, prompt: "  uma\u0007 orca  " }, cfg);
  assert.equal(r.erro, undefined);
  assert.equal(r.pedido.prompt, "uma  orca");
});

test("o cliente nao escolhe nada fora da lista do servidor", () => {
  assert.equal(validarPedido({ ...base, resolution: "720p" }, cfg).erro, "resolucao_invalida");
  assert.equal(validarPedido({ ...base, duration: 10 }, cfg).erro, "duracao_invalida");
  assert.equal(validarPedido({ ...base, aspectRatio: "21:9" }, cfg).erro, "aspecto_invalido");
  assert.equal(validarPedido({ ...base, mode: "wan-2.2/14b/multi-scene" }, cfg).erro, "modo_invalido");
  assert.equal(validarPedido({ ...base, prompt: "x".repeat(801) }, cfg).erro, "prompt_longo");
  assert.equal(validarPedido({ ...base, prompt: " " }, cfg).erro, "prompt_vazio");
  assert.equal(validarPedido({ ...base, imageId: "a".repeat(24) }, cfg).erro, "imagem_proibida");
  assert.equal(validarPedido({ ...base, mode: "image_to_video" }, cfg).erro, "imagem_obrigatoria");
});

test("o modelo e fixo no servidor, por modo", () => {
  assert.equal(EightScaleVideoProvider.modeloDe("text_to_video"), "wan-2.2/14b/text-to-video");
  assert.equal(EightScaleVideoProvider.modeloDe("image_to_video"), "wan-2.2/14b/image-to-video");
  assert.equal(EightScaleVideoProvider.modeloDe("wan-2.2/14b/multi-scene"), null);
});

test("sem custo configurado a geracao fica desligada", () => {
  assert.ok(!Number.isFinite(configuracao({}).custoPorJobUsd));
  assert.equal(configuracao({}).ligado, false);
});

test("imagem e reconhecida pelos bytes, nao pelo content-type", () => {
  assert.equal(tipoDaImagem(new Uint8Array([0x89, 0x50, 0x4e, 0x47, 13, 10, 26, 10, 0])), "image/png");
  assert.equal(tipoDaImagem(new Uint8Array([0xff, 0xd8, 0xff, 0xe0])), "image/jpeg");
  assert.equal(tipoDaImagem(new TextEncoder().encode("<html>nao sou imagem")), null);
});

test("saida so https publica", () => {
  assert.ok(saidaAceitavel("https://cdn.8scale.run/results/aB3cD4eF5gH6i.mp4?sig=1"));
  assert.ok(!saidaAceitavel("http://cdn.8scale.run/x.mp4"));
  assert.ok(!saidaAceitavel("https://127.0.0.1/x.mp4"));
  assert.ok(!saidaAceitavel("https://192.168.0.10/x.mp4"));
  assert.ok(!saidaAceitavel("https://user:pw@cdn.8scale.run/x.mp4"));
  assert.ok(!saidaAceitavel("nao-e-url"));
});

test("erros da 8Scale viram codigos do app", () => {
  assert.equal(codigoDoHttp(401), "provedor_auth");
  assert.equal(codigoDoHttp(403), "provedor_auth");
  assert.equal(codigoDoHttp(402), "saldo_insuficiente");
  assert.equal(codigoDoHttp(400, "Insufficient balance"), "saldo_insuficiente");
  assert.equal(codigoDoHttp(404), "modelo_indisponivel");
  assert.equal(codigoDoHttp(429), "provedor_ocupado");
  assert.equal(codigoDoHttp(400, "prompt blocked by safety filter"), "conteudo_bloqueado");
  assert.equal(codigoDoHttp(422), "pedido_recusado");
  assert.equal(codigoDoHttp(503), "provedor_indisponivel");
});

const md5 = async (s) => createHash("md5").update(s).digest("hex");

test("callback do LevelPlay: assinatura da documentacao", async () => {
  const chave = "chave-privada";
  const p = { userid: "TICKET_abc@x", rewards: "1", eventId: "ev123", timestamp: "202609241455" };
  const sig = await md5(p.timestamp + p.eventId + p.userid + p.rewards + chave);
  const q = new URLSearchParams({ ...p, signature: sig });
  const ok = await conferirCallback(q, chave, { md5 });
  assert.equal(ok.ok, true);
  assert.equal(ok.userId, "TICKET_abc@x"); // decodificado, como a assinatura exige

  const adulterado = new URLSearchParams({ ...p, rewards: "100", signature: sig });
  assert.equal((await conferirCallback(adulterado, chave, { md5 })).erro, "assinatura_invalida");
  assert.equal((await conferirCallback(q, "", { md5 })).erro, "chave_nao_configurada");
  const semAssinatura = new URLSearchParams(p);
  assert.equal((await conferirCallback(semAssinatura, chave, { md5 })).erro, "parametros_faltando");
});

test("callback do LevelPlay: vale a chave do Android OU a do iOS", async () => {
  const p = { userid: "T1", rewards: "1", eventId: "ev9", timestamp: "202609241800" };
  const sigIos = await md5(p.timestamp + p.eventId + p.userid + p.rewards + "090000");
  const q = new URLSearchParams({ ...p, signature: sigIos });
  const r = await conferirCallback(q, ["888888", "090000"], { md5 });
  assert.equal(r.ok, true);
  assert.equal(r.chave, 1);
  assert.equal((await conferirCallback(q, ["888888"], { md5 })).erro, "assinatura_invalida");
});
