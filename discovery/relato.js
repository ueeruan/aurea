// =============================================================================
//  "Relatar um problema" — o que a PESSOA escreve no app (não é crash).
//
//  Rota:
//    POST /api/report   o app manda UM relato (JSON, até 32 KiB) → 202
//
//  Campos: o que você fez, o que esperava / o que aconteceu (obrigatório),
//  passos para repetir, e a ficha do aparelho (versão/build, sistema, modelo,
//  fabricante, ABI, idioma). O e-mail vem da SESSÃO quando há; sem sessão, o
//  do corpo entra marcado "informado pelo app".
//
//  Guardado no D1 (tabela user_reports, migração 0003) — o KV grátis só grava
//  1 000 vezes por dia na conta inteira e acaba. Reenvio do mesmo relato
//  (mesma instalação + reportId) não duplica nem manda segundo e-mail.
//  Limites (D1): por instalação e por IP; e-mails por dia com teto.
// =============================================================================

import { json, lerJson, dentroDoLimite, lerSessao, normalizarEmail, sha256hex } from "./contas.js";
import { enviarEmail } from "./email.js";

export const RELATO_MAX_BYTES = 32 * 1024;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const ID_RELATO = /^[A-Za-z0-9._:-]{8,100}$/;
const PLATAFORMAS = new Set(["android", "ios"]);
const FICHA = {
  appVersion: 64, appBuild: 16, os: 32, osVersion: 64, deviceModel: 96,
  manufacturer: 64, abi: 96, locale: 32,
};
export const TEXTOS = { whatDid: 2000, whatHappened: 4000, steps: 4000 };

export const LIMITES_RELATO = {
  instalacaoHora: { max: 5, janela: 3600 },
  instalacaoDia: { max: 20, janela: 86400 },
  ipDia: { max: 40, janela: 86400 },
};

/** Tira controle (menos \n e \t), apara e corta no tamanho. */
function limpo(v, max) {
  if (v === undefined || v === null) return "";
  return String(v).replace(/[\u0000-\u0008\u000B-\u001F\u007F]/g, "").trim().slice(0, max);
}

/** Valida e normaliza o corpo do app. {relato} ou {erro}. */
export function validarRelato(corpo) {
  if (!corpo || typeof corpo !== "object") return { erro: "json_invalido" };
  const reportId = String(corpo.reportId ?? "");
  const installId = String(corpo.installId ?? "").toLowerCase();
  const platform = String(corpo.platform ?? "").toLowerCase();
  if (!ID_RELATO.test(reportId)) return { erro: "report_id_invalido" };
  if (!UUID.test(installId)) return { erro: "install_id_invalido" };
  if (!PLATAFORMAS.has(platform)) return { erro: "plataforma_invalida" };
  for (const campo of Object.keys(TEXTOS)) {
    if (corpo[campo] !== undefined && typeof corpo[campo] !== "string") return { erro: "texto_invalido" };
  }
  const r = { reportId, installId, platform };
  for (const [campo, max] of Object.entries(FICHA)) r[campo] = limpo(corpo[campo], max);
  for (const [campo, max] of Object.entries(TEXTOS)) r[campo] = limpo(corpo[campo], max);
  if (r.whatHappened.length < 3) return { erro: "descricao_obrigatoria" };
  return { relato: r };
}

export function textoDoRelato(r, id, email) {
  return [
    `Aurea — relato de problema (${r.platform})`,
    "",
    `id:           ${id}`,
    `app:          ${r.appVersion} (build ${r.appBuild})`,
    `sistema:      ${r.os} ${r.osVersion}`,
    `aparelho:     ${r.manufacturer} ${r.deviceModel}`,
    `ABI:          ${r.abi || "-"}`,
    `idioma:       ${r.locale || "-"}`,
    `conta:        ${email || "-"}`,
    `instalação:   ${r.installId}`,
    "",
    "----- o que fez -----",
    r.whatDid || "-",
    "",
    "----- o que esperava / o que aconteceu -----",
    r.whatHappened,
    "",
    "----- passos para repetir -----",
    r.steps || "-",
  ].join("\n");
}

async function receber(req, env) {
  const ip = req.headers.get("cf-connecting-ip") ?? "local";
  if (!(await dentroDoLimite(env, "relato-ip", ip, LIMITES_RELATO.ipDia))) return json({ error: "muitas_tentativas" }, 429);
  const { valor, erro } = await lerJson(req, RELATO_MAX_BYTES);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const { relato: r, erro: invalido } = validarRelato(valor);
  if (invalido) return json({ error: invalido }, 400);

  const marca = await sha256hex(`${r.installId}|${r.reportId}`);
  const visto = await env.AUREA_DB.prepare("SELECT id FROM user_reports WHERE dedupe = ?").bind(marca).first();
  if (visto) return json({ ok: true, id: visto.id, duplicate: true }, 200);

  if (!(await dentroDoLimite(env, "relato-inst-h", r.installId, LIMITES_RELATO.instalacaoHora)) ||
      !(await dentroDoLimite(env, "relato-inst-d", r.installId, LIMITES_RELATO.instalacaoDia))) {
    return json({ error: "muitas_tentativas" }, 429);
  }

  const sessao = await lerSessao(env, req);
  let email = "";
  if (sessao) email = sessao.email;
  else {
    const informado = normalizarEmail(valor.email);
    email = informado ? `${informado} (informado pelo app, sem sessão)` : "";
  }

  const recebido = Date.now();
  const id = `${String(9_999_999_999_999 - recebido).padStart(13, "0")}-${marca.slice(0, 16)}`;
  const pais = typeof req.cf?.country === "string" ? req.cf.country : "";

  // Grava ANTES do e-mail: o relato não se perde se o e-mail falhar.
  try {
    await env.AUREA_DB.prepare(
      "INSERT INTO user_reports (id, dedupe, received_at, platform, install_id, email, country, app_version, app_build, " +
      "os, os_version, device_model, manufacturer, abi, locale, what_did, what_happened, steps, emailed) " +
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)")
      .bind(id, marca, recebido, r.platform, r.installId, email, pais, r.appVersion, r.appBuild,
        r.os, r.osVersion, r.deviceModel, r.manufacturer, r.abi, r.locale, r.whatDid, r.whatHappened, r.steps)
      .run();
  } catch (e) {
    // Corrida com um reenvio simultâneo: o UNIQUE de `dedupe` segura.
    if (/UNIQUE/i.test(String(e?.message ?? e))) return json({ ok: true, id, duplicate: true }, 200);
    console.error("relato: D1 indisponível", String(e?.message ?? e).slice(0, 200));
    return json({ error: "indisponivel" }, 503);
  }

  const teto = { max: Number(env.REPORT_EMAIL_DAILY_LIMIT) > 0 ? Number(env.REPORT_EMAIL_DAILY_LIMIT) : 50, janela: 86400 };
  let envio = { enviado: false, via: null, erro: "teto_diario" };
  if (await dentroDoLimite(env, "relato-email", "global", teto)) {
    const primeiraLinha = r.whatHappened.split("\n")[0].slice(0, 60);
    const assunto = `[Aurea] Relato · ${r.platform} ${r.appBuild} · ${primeiraLinha}`;
    envio = await enviarEmail(env, { assunto, texto: textoDoRelato(r, id, email) });
    if (envio.enviado) {
      await env.AUREA_DB.prepare("UPDATE user_reports SET emailed = 1 WHERE id = ?").bind(id).run().catch(() => {});
    } else {
      console.error("relato: e-mail não saiu", envio.via, envio.erro);
    }
  }
  return json({ ok: true, id, emailed: envio.enviado }, 202);
}

export async function rotaDeRelato(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (rota !== "/api/report") return null;
  if (url.protocol !== "https:" && url.hostname !== "127.0.0.1" && url.hostname !== "localhost") {
    return json({ error: "https_obrigatorio" }, 403);
  }
  if (!env.AUREA_DB) return json({ error: "indisponivel" }, 503);
  if (req.method === "POST") return receber(req, env);
  return json({ error: "nao_encontrado" }, 404);
}
