// =============================================================================
//  Relatórios de crash — o app manda, o desenvolvedor recebe por e-mail.
//
//  Rotas:
//    POST /api/crash          o app envia UM relatório (JSON, até 256 KiB) → 202
//    GET  /api/crash/list     admin (Bearer CRASH_ADMIN_TOKEN): os mais novos primeiro
//    GET  /api/crash/item?id= admin: o relatório inteiro
//
//  O que o relatório carrega (e SÓ isso): versão/build, sistema, modelo,
//  fabricante, ABI, motivo, etapa (marcador de fase), horário, pilha/tombstone
//  (até 200 KiB), id aleatório da instalação e — se houver sessão — o e-mail da
//  conta, tirado da SESSÃO (não do corpo). Nada de projeto, caminho de mídia ou URI.
//
//  Guardado no KV (`crash:<ordem>-<hash>`, TTL CRASH_TTL_DAYS) com um marcador de
//  deduplicação (`crashvisto:<hash(instalação+id)>`): reenvio do mesmo relatório
//  não vira segundo e-mail. E-mails por dia têm teto (CRASH_EMAIL_DAILY_LIMIT);
//  passou dele, o relatório só fica guardado.
// =============================================================================

import { json, lerJson, dentroDoLimite, lerSessao, normalizarEmail, sha256hex } from "./contas.js";
import { enviarEmail } from "./email.js";

export const CRASH_MAX_BYTES = 256 * 1024;
export const PILHA_MAX_BYTES = 200 * 1024;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const ID_RELATORIO = /^[A-Za-z0-9._:-]{8,100}$/;
const CAMPOS = {
  appVersion: 64, appBuild: 16, os: 32, osVersion: 64, deviceModel: 96,
  manufacturer: 64, abi: 96, reason: 64, phase: 256,
};
const PLATAFORMAS = new Set(["android", "ios"]);

export const LIMITES_CRASH = {
  instalacaoHora: { max: 10, janela: 3600 },
  instalacaoDia: { max: 30, janela: 86400 },
  ipDia: { max: 60, janela: 86400 },
};

const numero = (v, padrao) => {
  const n = Number(v);
  return Number.isFinite(n) && n >= 0 ? n : padrao;
};

/** Tira controle (menos \n e \t) e corta no tamanho. */
function limpo(v, max) {
  if (v === undefined || v === null) return "";
  return String(v).replace(/[\u0000-\u0008\u000B-\u001F\u007F]/g, "").slice(0, max);
}

/** Corta o texto em `max` bytes de UTF-8 sem partir um caractere. */
export function cortarUtf8(texto, max) {
  const bytes = new TextEncoder().encode(texto);
  if (bytes.length <= max) return { texto, cortado: false };
  let fim = max;
  while (fim > 0 && (bytes[fim] & 0xc0) === 0x80) fim--;
  return { texto: new TextDecoder().decode(bytes.subarray(0, fim)), cortado: true };
}

/** Valida e normaliza o corpo do app. {relatorio} ou {erro}. */
export function validarRelatorio(corpo, agoraMs = Date.now()) {
  if (!corpo || typeof corpo !== "object") return { erro: "json_invalido" };
  const reportId = String(corpo.reportId ?? "");
  const installId = String(corpo.installId ?? "").toLowerCase();
  const platform = String(corpo.platform ?? "").toLowerCase();
  if (!ID_RELATORIO.test(reportId)) return { erro: "report_id_invalido" };
  if (!UUID.test(installId)) return { erro: "install_id_invalido" };
  if (!PLATAFORMAS.has(platform)) return { erro: "plataforma_invalida" };
  const timestamp = Number(corpo.timestamp);
  if (!Number.isFinite(timestamp) || timestamp < Date.UTC(2020, 0, 1) || timestamp > agoraMs + 86400_000) {
    return { erro: "timestamp_invalido" };
  }
  if (corpo.stack !== undefined && typeof corpo.stack !== "string") return { erro: "stack_invalida" };
  const r = { reportId, installId, platform, timestamp: Math.floor(timestamp) };
  for (const [campo, max] of Object.entries(CAMPOS)) r[campo] = limpo(corpo[campo], max);
  if (!r.reason) return { erro: "motivo_obrigatorio" };
  const pilha = cortarUtf8(limpo(corpo.stack ?? "", PILHA_MAX_BYTES + 1), PILHA_MAX_BYTES);
  r.stack = pilha.texto;
  r.stackTruncated = pilha.cortado || corpo.stackTruncated === true;
  return { relatorio: r };
}

export function textoDoEmail(r, id) {
  const quando = new Date(r.timestamp).toISOString();
  const cabeca = [
    `Aurea — ${r.reason} (${r.platform})`,
    "",
    `id:           ${id}`,
    `quando:       ${quando}`,
    `app:          ${r.appVersion} (build ${r.appBuild})`,
    `sistema:      ${r.os} ${r.osVersion}`,
    `aparelho:     ${r.manufacturer} ${r.deviceModel}`,
    `ABI:          ${r.abi}`,
    `motivo:       ${r.reason}`,
    `etapa:        ${r.phase || "-"}`,
    `conta:        ${r.email || "-"}`,
    `instalação:   ${r.installId}`,
    `relatório:    ${r.reportId}`,
    r.stackTruncated ? "(pilha cortada em 200 KiB)" : "",
    "",
    "----- pilha / tombstone -----",
  ].join("\n");
  // O corpo leva os primeiros 60 KiB (o Gmail corta mensagem grande); o anexo leva tudo.
  const inicio = cortarUtf8(r.stack || "(sem pilha)", 60 * 1024);
  return {
    texto: cabeca + "\n" + inicio.texto + (inicio.cortado ? "\n\n[... continua no anexo]" : ""),
    anexo: { nome: `crash-${id}.txt`, texto: cabeca + "\n" + (r.stack || "(sem pilha)") },
  };
}

function autorizadoAdmin(req, env) {
  const segredo = env.CRASH_ADMIN_TOKEN;
  const m = /^Bearer (.+)$/.exec(req.headers.get("authorization") ?? "");
  if (typeof segredo !== "string" || segredo.length < 16 || !m) return false;
  const a = m[1];
  if (a.length !== segredo.length) return false;
  let dif = 0;
  for (let i = 0; i < a.length; i++) dif |= a.charCodeAt(i) ^ segredo.charCodeAt(i);
  return dif === 0;
}

async function receber(req, env) {
  const ip = req.headers.get("cf-connecting-ip") ?? "local";
  if (!(await dentroDoLimite(env, "crash-ip", ip, LIMITES_CRASH.ipDia))) return json({ error: "muitas_tentativas" }, 429);
  const { valor, erro } = await lerJson(req, CRASH_MAX_BYTES);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const { relatorio: r, erro: invalido } = validarRelatorio(valor);
  if (invalido) return json({ error: invalido }, 400);

  const marca = await sha256hex(`${r.installId}|${r.reportId}`);
  const visto = await env.AUREA_KV.get(`crashvisto:${marca}`);
  if (visto) return json({ ok: true, id: visto, duplicate: true }, 200);

  if (!(await dentroDoLimite(env, "crash-inst-h", r.installId, LIMITES_CRASH.instalacaoHora)) ||
      !(await dentroDoLimite(env, "crash-inst-d", r.installId, LIMITES_CRASH.instalacaoDia))) {
    return json({ error: "muitas_tentativas" }, 429);
  }

  // E-mail só da SESSÃO: o corpo não prova nada. Sem sessão, o do corpo entra
  // marcado como "informado pelo app".
  const sessao = await lerSessao(env, req);
  if (sessao) r.email = sessao.email;
  else {
    const informado = normalizarEmail(valor.email);
    r.email = informado ? `${informado} (informado pelo app, sem sessão)` : "";
  }

  const recebido = Date.now();
  const id = `${String(9_999_999_999_999 - recebido).padStart(13, "0")}-${marca.slice(0, 16)}`;
  const ttl = Math.max(1, numero(env.CRASH_TTL_DAYS, 30)) * 86400;
  const pais = typeof req.cf?.country === "string" ? req.cf.country : "";

  // Teto diário de e-mails (aproximado: contador de KV).
  const teto = { max: numero(env.CRASH_EMAIL_DAILY_LIMIT, 50), janela: 86400 };
  let envio = { enviado: false, via: null, erro: "teto_diario" };
  if (await dentroDoLimite(env, "crash-email", "global", teto)) {
    const { texto, anexo } = textoDoEmail(r, id);
    const assunto = `[Aurea] ${r.reason} · ${r.platform} ${r.appBuild} · ${r.manufacturer} ${r.deviceModel}`;
    envio = await enviarEmail(env, { assunto, texto, anexo });
    if (!envio.enviado) console.error("crash: e-mail não saiu", envio.via, envio.erro);
  }

  const registro = { id, recebido, pais, ...r, email_enviado: envio.enviado, email_via: envio.via, email_erro: envio.erro ?? null };
  const meta = {
    t: recebido, p: r.platform, r: r.reason.slice(0, 40), b: r.appBuild,
    m: `${r.manufacturer} ${r.deviceModel}`.slice(0, 60), e: envio.enviado ? 1 : 0,
  };
  await env.AUREA_KV.put(`crash:${id}`, JSON.stringify(registro), { expirationTtl: ttl, metadata: meta });
  await env.AUREA_KV.put(`crashvisto:${marca}`, id, { expirationTtl: ttl });
  return json({ ok: true, id, emailed: envio.enviado }, 202);
}

async function listar(req, env, url) {
  if (!autorizadoAdmin(req, env)) return json({ error: "nao_autorizado" }, 401);
  const cursor = url.searchParams.get("cursor") || undefined;
  const r = await env.AUREA_KV.list({ prefix: "crash:", cursor, limit: 100 });
  return json({
    items: r.keys.map((k) => ({ id: k.name.slice("crash:".length), ...(k.metadata ?? {}) })),
    cursor: r.list_complete ? null : r.cursor,
  });
}

async function item(req, env, url) {
  if (!autorizadoAdmin(req, env)) return json({ error: "nao_autorizado" }, 401);
  const id = url.searchParams.get("id") ?? "";
  if (!/^[0-9]{13}-[0-9a-f]{16}$/.test(id)) return json({ error: "id_invalido" }, 400);
  const r = await env.AUREA_KV.get(`crash:${id}`, "json");
  return r ? json(r) : json({ error: "nao_encontrado" }, 404);
}

export async function rotaDeCrash(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (rota !== "/api/crash" && !rota.startsWith("/api/crash/")) return null;
  if (url.protocol !== "https:" && url.hostname !== "127.0.0.1" && url.hostname !== "localhost") {
    return json({ error: "https_obrigatorio" }, 403);
  }
  if (!env.AUREA_KV) return json({ error: "indisponivel" }, 503);
  if (rota === "/api/crash" && req.method === "POST") return receber(req, env);
  if (rota === "/api/crash/list" && req.method === "GET") return listar(req, env, url);
  if (rota === "/api/crash/item" && req.method === "GET") return item(req, env, url);
  return json({ error: "nao_encontrado" }, 404);
}
