// Configuração remota assinada: GET /api/app-config e POST /api/admin/app-config.
// Roda com: node --import ./test/cf-stub.mjs --test test/*.test.mjs (em discovery/)
import { test } from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import worker from "../worker.js";
import { esquecerMemo } from "../contas.js";
import { LIMITE_ADMIN_CONFIG, URL_PADRAO, montarPayload } from "../appconfig.js";
import { kvFalso, d1Falso, pedido } from "./falsos.mjs";

const SEGREDO = "segredo-do-admin-de-teste-0123456789";
const par = await webcrypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
const PRIVADA = Buffer.from(await webcrypto.subtle.exportKey("pkcs8", par.privateKey)).toString("base64");

function ambiente(extra = {}) {
  esquecerMemo();
  return { AUREA_KV: kvFalso(), AUREA_DB: d1Falso(), AUREA_ADMIN_SECRET: SEGREDO, AUREA_CONFIG_SIGNING_KEY: PRIVADA, ...extra };
}
const ctx = { waitUntil: (p) => p };
const chamar = async (env, caminho, opcoes) => {
  const r = await worker.fetch(pedido(caminho, opcoes), env, ctx);
  return { status: r.status, corpo: await r.json(), cabecalhos: r.headers };
};
const config = (env, plataforma = "android") => chamar(env, `/api/app-config?platform=${plataforma}&version=2198`);
const admin = (env, corpo, token = SEGREDO, ip) => chamar(env, "/api/admin/app-config", { metodo: "POST", corpo, token, ip });

async function confere(resposta, chave = par.publicKey) {
  const assinatura = Buffer.from(resposta.signature, "base64");
  assert.equal(assinatura.length, 64, "assinatura crua r||s de 64 bytes");
  return webcrypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, chave, assinatura, new TextEncoder().encode(resposta.payload));
}

test("app-config: sem nada guardado -> padroes assinados, validade de 7 dias, no-store", async () => {
  const env = ambiente();
  const antes = Math.floor(Date.now() / 1000);
  const r = await config(env);
  assert.equal(r.status, 200);
  assert.equal(r.cabecalhos.get("cache-control"), "no-store");
  assert.ok(await confere(r.corpo));
  const p = JSON.parse(r.corpo.payload);
  assert.deepEqual(Object.keys(p), ["v", "platform", "minVersionCode", "latestVersionCode", "updateUrl", "hyper", "message",
    "issuedAt", "expiresAt"]);
  assert.equal(p.v, 1);
  assert.equal(p.platform, "android");
  assert.equal(p.minVersionCode, 0);
  assert.equal(p.hyper.mode, "free");
  assert.equal(p.message, null);
  assert.equal(p.updateUrl, URL_PADRAO.android);
  assert.ok(p.issuedAt >= antes && p.issuedAt <= antes + 5);
  assert.equal(p.expiresAt - p.issuedAt, 7 * 86400);
  const ios = JSON.parse((await config(env, "ios")).corpo.payload);
  assert.equal(ios.platform, "ios");
});

test("app-config: a assinatura cobre os bytes exatos (payload ou chave trocados nao conferem)", async () => {
  const env = ambiente();
  const r = (await config(env)).corpo;
  assert.ok(await confere(r));
  const adulterado = { ...r, payload: r.payload.replace('"free"', '"off"') };
  assert.equal(await confere(adulterado), false);
  const outra = await webcrypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
  assert.equal(await confere(r, outra.publicKey), false);
});

test("app-config: plataforma invalida -> 400; sem a chave de assinatura -> 503 (nunca config sem assinatura)", async () => {
  const env = ambiente();
  assert.equal((await chamar(env, "/api/app-config")).status, 400);
  assert.equal((await chamar(env, "/api/app-config?platform=windows")).status, 400);
  const semChave = ambiente({ AUREA_CONFIG_SIGNING_KEY: undefined });
  const r = await config(semChave);
  assert.equal(r.status, 503);
  assert.equal(r.corpo.signature, undefined);
  const chaveRuim = ambiente({ AUREA_CONFIG_SIGNING_KEY: "A".repeat(64) });
  assert.equal((await config(chaveRuim)).status, 503);
  assert.equal((await chamar(env, "/api/app-config?platform=android", { metodo: "POST", corpo: {} })).status, 404);
});

test("app-config admin: segredo ausente, errado ou de outro lugar -> recusado", async () => {
  assert.equal((await admin(ambiente({ AUREA_ADMIN_SECRET: undefined }), { platform: "android", minVersionCode: 1 })).status, 503);
  assert.equal((await admin(ambiente({ AUREA_ADMIN_SECRET: "curto" }), { platform: "android", minVersionCode: 1 })).status, 503);
  const env = ambiente();
  assert.equal((await admin(env, { platform: "android", minVersionCode: 1 }, "x".repeat(SEGREDO.length))).status, 401);
  assert.equal((await admin(env, { platform: "android", minVersionCode: 1 }, null)).status, 401);
  // Nada mudou.
  assert.equal(JSON.parse((await config(env)).corpo.payload).minVersionCode, 0);
});

test("app-config admin: forca atualizacao, troca o modo do Hyper e o aviso; o GET assina o novo valor", async () => {
  const env = ambiente();
  const r = await admin(env, { platform: "android", minVersionCode: 2200, updateUrl: "https://example.com/aurea.apk",
    hyperMode: "entitled", message: { "pt-BR": "Atualize o Aurea", en: "Update Aurea" }, ttlDays: 3 });
  assert.equal(r.status, 200);
  assert.equal(r.corpo.config.minVersionCode, 2200);
  assert.equal(r.corpo.config.latestVersionCode, 2200, "latest nunca abaixo do minimo");
  const c = (await config(env)).corpo;
  assert.ok(await confere(c));
  const p = JSON.parse(c.payload);
  assert.equal(p.minVersionCode, 2200);
  assert.equal(p.hyper.mode, "entitled");
  assert.equal(p.updateUrl, "https://example.com/aurea.apk");
  assert.deepEqual(p.message, { "pt-BR": "Atualize o Aurea", en: "Update Aurea" });
  assert.equal(p.expiresAt - p.issuedAt, 3 * 86400);
  // A outra plataforma não muda.
  assert.equal(JSON.parse((await config(env, "ios")).corpo.payload).minVersionCode, 0);
  // Atualização parcial preserva o resto; message null apaga o aviso.
  await admin(env, { platform: "android", hyperMode: "off", message: null });
  const q = JSON.parse((await config(env)).corpo.payload);
  assert.equal(q.minVersionCode, 2200);
  assert.equal(q.hyper.mode, "off");
  assert.equal(q.message, null);
  await admin(env, { platform: "android", hyperMode: "free", minVersionCode: 0 });
  assert.equal(JSON.parse((await config(env)).corpo.payload).hyper.mode, "free");
});

test("app-config admin: entradas invalidas -> 400 sem gravar", async () => {
  const env = ambiente();
  const casos = [
    [{ platform: "web", minVersionCode: 1 }, "plataforma_invalida"],
    [{ platform: "android", minVersionCode: -1 }, "versao_invalida"],
    [{ platform: "android", minVersionCode: 1.5 }, "versao_invalida"],
    [{ platform: "android", latestVersionCode: "10" }, "versao_invalida"],
    [{ platform: "android", hyperMode: "pago" }, "modo_invalido"],
    [{ platform: "android", updateUrl: "http://inseguro.example.com" }, "url_invalida"],
    [{ platform: "android", updateUrl: "javascript:alert(1)" }, "url_invalida"],
    [{ platform: "android", message: { fr: "Bonjour" } }, "mensagem_invalida"],
    [{ platform: "android", message: { en: "x".repeat(501) } }, "mensagem_invalida"],
    [{ platform: "android", message: "texto" }, "mensagem_invalida"],
    [{ platform: "android", ttlDays: 0 }, "validade_invalida"],
    [{ platform: "android", ttlDays: 31 }, "validade_invalida"],
  ];
  for (const [corpo, erro] of casos) {
    const r = await admin(env, corpo);
    assert.equal(r.status, 400, JSON.stringify(corpo));
    assert.equal(r.corpo.error, erro);
  }
  assert.equal(env.AUREA_KV.escritas, 0);
  const malformado = await chamar(env, "/api/admin/app-config", { metodo: "POST", corpo: "{nao json", token: SEGREDO });
  assert.equal(malformado.status, 400);
});

test("app-config admin: limite por IP conta todas as tentativas", async () => {
  const env = ambiente();
  for (let i = 0; i < LIMITE_ADMIN_CONFIG.max; i++) await admin(env, { platform: "android" }, "errado-" + "x".repeat(30), "203.0.113.9");
  const r = await admin(env, { platform: "android", minVersionCode: 5 }, SEGREDO, "203.0.113.9");
  assert.equal(r.status, 429);
  assert.equal((await admin(env, { platform: "android", minVersionCode: 5 }, SEGREDO, "203.0.113.10")).status, 200);
});

test("app-config: payload canonico estavel (mesmas entradas -> mesmos bytes)", () => {
  const a = montarPayload("ios", { minVersionCode: 3, hyperMode: "entitled", message: { en: "hi", "pt-BR": "oi" } }, 1000);
  const b = montarPayload("ios", { message: { "pt-BR": "oi", en: "hi" }, hyperMode: "entitled", minVersionCode: 3 }, 1000);
  assert.equal(a, b);
  assert.equal(JSON.parse(a).message["pt-BR"], "oi");
  assert.equal(Object.keys(JSON.parse(a).message)[0], "pt-BR");
});
