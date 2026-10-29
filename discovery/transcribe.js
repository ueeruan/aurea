// =============================================================================
//  Legendas automáticas na nuvem (0.4.1) — Whisper large-v3-turbo no Workers AI.
//
//  Rota (JSON, cache-control: no-store):
//    POST /api/transcribe?lang=<código|auto>   Bearer <token da sessão>
//         corpo: um TRECHO de áudio (WAV 16 kHz mono do app, ≤ 3 MB ≈ 90 s)
//         → 200 {language, words: [{w, s, e}]}   (segundos, relativos ao trecho)
//         → 401 nao_autorizado · 400 corpo_vazio/idioma_invalido · 413 corpo_grande
//         → 429 muitas_tentativas · 503 transcricao_indisponivel (sem o binding AI)
//         → 502 transcricao_falhou (o modelo não respondeu)
//
//  O app corta o áudio nas pausas (Engine::export_speech_chunks), manda trecho a
//  trecho e soma o início de cada um. O áudio NÃO é gravado: vai só para o modelo
//  e a resposta volta. Custo: US$ 0,000513 por minuto (preço público do modelo).
//  Limites (todas as tentativas contam): por conta e por IP, por dia.
// =============================================================================

import { json, lerSessao, dentroDoLimite } from "./contas.js";

export const MODELO = "@cf/openai/whisper-large-v3-turbo";
export const CORPO_MAX = 3 * 1024 * 1024;
export const LIMITES_TRANSCRICAO = {
  conta: { max: 300, janela: 86400 },     // ~ 3 a 4 h de fala por dia
  ip: { max: 1200, janela: 86400 },
};
const IDIOMA = /^[a-z]{2,3}$/;

const ip = (req) => req.headers.get("cf-connecting-ip") ?? "local";

/** Bytes → base64, em blocos (String.fromCharCode com muitos argumentos estoura a pilha). */
export function paraBase64(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

/** As palavras com tempo da resposta do modelo; sem `words`, o texto do segmento repartido. */
export function palavrasDaResposta(r) {
  const out = [];
  for (const seg of Array.isArray(r?.segments) ? r.segments : []) {
    const words = Array.isArray(seg?.words) ? seg.words : [];
    if (words.length) {
      for (const w of words) {
        const t = String(w?.word ?? "").trim();
        if (!t || !Number.isFinite(w.start) || !Number.isFinite(w.end)) continue;
        out.push({ w: t, s: Math.max(0, w.start), e: Math.max(w.start, w.end) });
      }
    } else if (typeof seg?.text === "string" && Number.isFinite(seg.start) && Number.isFinite(seg.end)) {
      const parts = seg.text.trim().split(/\s+/).filter(Boolean);
      const span = Math.max(0, seg.end - seg.start) / Math.max(1, parts.length);
      parts.forEach((t, i) => out.push({ w: t, s: seg.start + i * span, e: seg.start + (i + 1) * span }));
    }
  }
  return out;
}

export async function rotaDeTranscricao(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (rota !== "/api/transcribe") return null;
  if (req.method !== "POST") return json({ error: "metodo_invalido" }, 405);
  if (!env.AI || !env.AUREA_DB) return json({ error: "transcricao_indisponivel" }, 503);
  if (!(await dentroDoLimite(env, "transcribe-ip", ip(req), LIMITES_TRANSCRICAO.ip))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "3600" });
  }
  const sessao = await lerSessao(env, req);
  if (!sessao) return json({ error: "nao_autorizado" }, 401);
  if (!(await dentroDoLimite(env, "transcribe-user", sessao.uid, LIMITES_TRANSCRICAO.conta))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": "3600" });
  }
  const lang = (url.searchParams.get("lang") ?? "auto").toLowerCase();
  if (lang !== "auto" && !IDIOMA.test(lang)) return json({ error: "idioma_invalido" }, 400);
  const declarado = Number(req.headers.get("content-length") ?? 0);
  if (declarado > CORPO_MAX) return json({ error: "corpo_grande" }, 413);
  const bytes = new Uint8Array(await req.arrayBuffer());
  if (bytes.length === 0) return json({ error: "corpo_vazio" }, 400);
  if (bytes.length > CORPO_MAX) return json({ error: "corpo_grande" }, 413);

  const entrada = { audio: paraBase64(bytes), task: "transcribe", vad_filter: true };
  if (lang !== "auto") entrada.language = lang;
  let r;
  try {
    r = await env.AI.run(MODELO, entrada);
  } catch {
    console.error("transcribe: o modelo falhou");
    return json({ error: "transcricao_falhou" }, 502);
  }
  return json({ language: r?.transcription_info?.language ?? (lang === "auto" ? "" : lang), words: palavrasDaResposta(r) });
}
