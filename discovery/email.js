// =============================================================================
//  E-mail do desenvolvedor — por onde sai o aviso de crash.
//
//  Dois caminhos, escolhidos por `CRASH_EMAIL_PROVIDER` ("auto" por padrão):
//
//    cloudflare  binding `send_email` (Email Workers). Exige Email Routing ativo
//                num domínio da conta, o destino VERIFICADO nele e
//                `CRASH_EMAIL_FROM` com um endereço desse domínio.
//    resend      API da Resend por fetch, com o secret RESEND_API_KEY. Sem
//                domínio próprio, o remetente `onboarding@resend.dev` só entrega
//                para o e-mail dono da conta Resend.
//    none        não envia (o relatório continua guardado e listável).
//    auto        cloudflare se o binding existir; senão resend se houver chave.
//
//  Nenhum segredo sai daqui: a chave só vai no cabeçalho para a Resend.
// =============================================================================

import { EmailMessage } from "cloudflare:email";

export const DESTINO_PADRAO = "ruanpablombl@gmail.com";
const REMETENTE_RESEND = "Aurea Crash <onboarding@resend.dev>";

export function provedorDeEmail(env) {
  const p = String(env.CRASH_EMAIL_PROVIDER ?? "auto").trim().toLowerCase();
  const cf = Boolean(env.CRASH_EMAIL && env.CRASH_EMAIL_FROM);
  const resend = Boolean(env.RESEND_API_KEY);
  if (p === "none") return null;
  if (p === "cloudflare") return cf ? "cloudflare" : null;
  if (p === "resend") return resend ? "resend" : null;
  if (cf) return "cloudflare";
  if (resend) return "resend";
  return null;
}

function base64Utf8(texto) {
  const bytes = new TextEncoder().encode(texto);
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

const quebrar76 = (b) => b.replace(/.{1,76}/g, "$&\r\n").replace(/\r\n$/, "");
const cabecalhoUtf8 = (t) => `=?UTF-8?B?${base64Utf8(t)}?=`;
const semQuebra = (t) => String(t).replace(/[\r\n]+/g, " ").slice(0, 300);
const endereco = (t) => String(t).replace(/[\r\n<>",;]/g, "").trim();

/** MIME cru (RFC 5322) com o texto e o relatório inteiro em anexo. */
export function montarMime({ de, para, assunto, texto, anexo }) {
  const fronteira = "aurea-" + crypto.randomUUID();
  const dominio = endereco(de).split("@")[1] || "aurea.invalid";
  const linhas = [
    `From: Aurea Crash <${endereco(de)}>`,
    `To: <${endereco(para)}>`,
    `Subject: ${cabecalhoUtf8(semQuebra(assunto))}`,
    `Date: ${new Date().toUTCString().replace("GMT", "+0000")}`,
    `Message-ID: <${crypto.randomUUID()}@${dominio}>`,
    "MIME-Version: 1.0",
    `Content-Type: multipart/mixed; boundary="${fronteira}"`,
    "",
    `--${fronteira}`,
    "Content-Type: text/plain; charset=utf-8",
    "Content-Transfer-Encoding: base64",
    "",
    quebrar76(base64Utf8(texto)),
  ];
  if (anexo) {
    const nome = String(anexo.nome).replace(/[^A-Za-z0-9._-]/g, "_");
    linhas.push(
      `--${fronteira}`,
      `Content-Type: text/plain; charset=utf-8; name="${nome}"`,
      `Content-Disposition: attachment; filename="${nome}"`,
      "Content-Transfer-Encoding: base64",
      "",
      quebrar76(base64Utf8(anexo.texto)),
    );
  }
  linhas.push(`--${fronteira}--`, "");
  return linhas.join("\r\n");
}

/**
 * Envia um e-mail ao desenvolvedor. Nunca lança: devolve {enviado, via, erro}.
 * `anexo` = {nome, texto} (opcional).
 */
export async function enviarEmail(env, { assunto, texto, anexo }) {
  const via = provedorDeEmail(env);
  if (!via) return { enviado: false, via: null, erro: "sem_provedor" };
  const para = endereco(env.CRASH_EMAIL_TO || DESTINO_PADRAO);
  try {
    if (via === "cloudflare") {
      const de = endereco(env.CRASH_EMAIL_FROM);
      const bruto = montarMime({ de, para, assunto, texto, anexo });
      await env.CRASH_EMAIL.send(new EmailMessage(de, para, bruto));
      return { enviado: true, via };
    }
    const corpo = {
      from: env.RESEND_FROM || REMETENTE_RESEND,
      to: [para],
      subject: semQuebra(assunto),
      text: texto,
    };
    if (anexo) corpo.attachments = [{ filename: String(anexo.nome), content: base64Utf8(anexo.texto) }];
    const r = await fetch("https://api.resend.com/emails", {
      method: "POST",
      headers: { authorization: `Bearer ${env.RESEND_API_KEY}`, "content-type": "application/json" },
      body: JSON.stringify(corpo),
    });
    if (!r.ok) {
      const detalhe = (await r.text().catch(() => "")).slice(0, 200);
      return { enviado: false, via, erro: `resend_http_${r.status}${detalhe ? ": " + detalhe : ""}` };
    }
    return { enviado: true, via };
  } catch (e) {
    return { enviado: false, via, erro: String(e?.message ?? e).slice(0, 200) };
  }
}
