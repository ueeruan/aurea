// =============================================================================
//  Geração de vídeo por IA — as rotas do app.
//
//  Aurea (Android/iOS) → este Worker → provedor (8Scale). A chave do provedor
//  mora SÓ aqui, em `env.EIGHTSCALE_API_KEY`; nenhuma resposta a devolve.
//
//  Fluxo (uma geração):
//    1. POST /tickets        valida tudo e emite o ticket (nada é gasto)
//    2. o app mostra o Rewarded com o ticket como Dynamic User ID
//    3. GET  /reward/levelplay  callback ASSINADO do LevelPlay marca o ticket
//    4. POST /generate       reserva no cofre → chama a 8Scale → confirma
//    5. GET  /jobs/{id}      estado real (IN_QUEUE / IN_PROGRESS / ...)
//    6. GET  /jobs/{id}/video   o arquivo, passando por aqui
//
//  Todas as rotas do app pedem o cabeçalho `x-aurea-device` (id aleatório do
//  aparelho): é a identidade para os limites e para "só o dono vê o job".
// =============================================================================

import { EightScaleVideoProvider, ErroDoProvedor } from "./provedores.js";
import { conferirCallback, IPS_DO_LEVELPLAY } from "./levelplay.js";

const PREFIXO = "/api/ai/video";
const APARELHO = /^[A-Za-z0-9-]{16,64}$/;
const ID_NOSSO = /^[A-Za-z0-9_-]{16,64}$/;
const IMAGEM_MAX = 8 * 1024 * 1024;
const VIDEO_MAX = 300 * 1024 * 1024;

function json(corpo, status = 200, extra = {}) {
  return new Response(JSON.stringify(corpo), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", ...extra },
  });
}
const erro = (codigo, status, detalhe = "") => json({ error: codigo, detail: detalhe }, status);

function idAleatorio() {
  const b = new Uint8Array(18);
  crypto.getRandomValues(b);
  return btoa(String.fromCharCode(...b)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

const lista = (v, padrao) => String(v ?? padrao).split(",").map((s) => s.trim()).filter(Boolean);
const numero = (v, padrao) => {
  const n = Number(v);
  return Number.isFinite(n) && n >= 0 ? n : padrao;
};

/** Tudo o que é configurável, lido de `env` (vars do wrangler.toml). */
export function configuracao(env) {
  return {
    ligado: String(env.AI_VIDEO_ENABLED ?? "false").toLowerCase() === "true",
    exigirRecompensa: String(env.AI_VIDEO_REQUIRE_REWARD_SSV ?? "true").toLowerCase() !== "false",
    custoPorJobUsd: numero(env.AI_VIDEO_COST_PER_JOB_USD, NaN),
    modos: ["text_to_video", ...(String(env.AI_VIDEO_I2V_ENABLED ?? "true") === "true" ? ["image_to_video"] : [])],
    duracoes: lista(env.AI_VIDEO_DURATIONS, "5").map(Number).filter((n) => Number.isInteger(n) && n > 0),
    aspectos: lista(env.AI_VIDEO_ASPECTS, "16:9"),
    resolucoes: lista(env.AI_VIDEO_RESOLUTIONS, "480p"),
    promptMax: Math.min(numero(env.AI_VIDEO_MAX_PROMPT_CHARS, 800), 4000),
    limites: {
      orcamentoDiarioUsd: numero(env.AI_VIDEO_MAX_DAILY_COST_USD, 0),
      jobsGlobaisPorDia: numero(env.AI_VIDEO_GLOBAL_DAILY_LIMIT, 0),
      jobsPorAparelhoPorDia: numero(env.AI_VIDEO_DEVICE_DAILY_LIMIT, 0),
      jobsPorIpPorDia: numero(env.AI_VIDEO_IP_DAILY_LIMIT, 0),
      simultaneosGlobais: numero(env.AI_VIDEO_GLOBAL_CONCURRENT, 1),
      simultaneosPorAparelho: numero(env.AI_VIDEO_DEVICE_CONCURRENT, 1),
      ticketsPorHora: numero(env.AI_VIDEO_TICKETS_PER_HOUR, 20),
    },
  };
}

/**
 * Validação rígida do pedido. Só entra o que está na lista do servidor: o app
 * não escolhe modelo, endpoint, resolução ou duração fora dela.
 */
export function validarPedido(c, cfg) {
  if (!c || typeof c !== "object") return { erro: "pedido_invalido" };
  const mode = String(c.mode ?? "");
  if (!cfg.modos.includes(mode)) return { erro: "modo_invalido" };
  // Controle e invisíveis fora: o prompt vai para um modelo pago, não é lugar de lixo.
  const prompt = String(c.prompt ?? "").replace(/[\u0000-\u0008\u000B-\u001F\u007F]/g, " ").trim();
  if (prompt.length < 3) return { erro: "prompt_vazio" };
  if (prompt.length > cfg.promptMax) return { erro: "prompt_longo" };
  const negativePrompt = String(c.negativePrompt ?? "").replace(/[\u0000-\u001F\u007F]/g, " ").trim().slice(0, 500);
  const duration = Number(c.duration);
  if (!cfg.duracoes.includes(duration)) return { erro: "duracao_invalida" };
  const aspectRatio = String(c.aspectRatio ?? "");
  if (!cfg.aspectos.includes(aspectRatio)) return { erro: "aspecto_invalido" };
  const resolution = String(c.resolution ?? "");
  if (!cfg.resolucoes.includes(resolution)) return { erro: "resolucao_invalida" };
  const imageId = c.imageId == null || c.imageId === "" ? null : String(c.imageId);
  if (mode === "image_to_video" && (!imageId || !ID_NOSSO.test(imageId))) return { erro: "imagem_obrigatoria" };
  if (mode === "text_to_video" && imageId) return { erro: "imagem_proibida" };
  return { pedido: { mode, prompt, negativePrompt, duration, aspectRatio, resolution, imageId } };
}

/** Os primeiros bytes dizem se é imagem de verdade (o Content-Type é do cliente). */
export function tipoDaImagem(b) {
  if (b.length > 8 && b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4e && b[3] === 0x47) return "image/png";
  if (b.length > 3 && b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff) return "image/jpeg";
  if (b.length > 12 && String.fromCharCode(...b.slice(0, 4)) === "RIFF" && String.fromCharCode(...b.slice(8, 12)) === "WEBP") return "image/webp";
  return null;
}

function tokenConfere(recebido, esperado) {
  if (typeof recebido !== "string" || typeof esperado !== "string" || !esperado) return false;
  if (recebido.length !== esperado.length) return false;
  let d = 0;
  for (let i = 0; i < recebido.length; i++) d |= recebido.charCodeAt(i) ^ esperado.charCodeAt(i);
  return d === 0;
}

/** Estado do provedor → o que o app mostra. Sem porcentagem: a 8Scale não dá. */
function paraApp(j, reenvio) {
  const mapa = {
    IN_QUEUE: ["queued", "Enviando…"],
    IN_PROGRESS: ["generating", "Gerando vídeo…"],
    COMPLETED: ["completed", "Finalizando…"],
    FAILED: ["failed", "Falhou"],
    CANCELLED: ["cancelled", "Cancelado"],
  };
  const [status, stage] = mapa[j.estado] ?? ["queued", "Enviando…"];
  return {
    jobId: j.jobId,
    status,
    stage,
    elapsedSeconds: Math.round(((j.fim ?? Date.now()) - j.criado) / 100) / 10,
    error: j.estado === "FAILED" ? (j.erro === "resultado_invalido" ? "resultado_invalido" : "geracao_falhou")
      : j.estado === "CANCELLED" ? "cancelado" : null,
    retryWithoutAd: Boolean(reenvio),
    result: j.estado === "COMPLETED" && j.saida
      ? { videoUrl: `${PREFIXO}/jobs/${j.jobId}/video`, executionMs: j.execucaoMs }
      : null,
  };
}

/** Só HTTPS público: a URL vem de fora, e é o Worker que vai buscá-la. */
export function saidaAceitavel(url) {
  let u;
  try { u = new URL(url); } catch { return false; }
  if (u.protocol !== "https:" || u.username || u.password) return false;
  const h = u.hostname;
  if (h === "localhost" || /^(10|127|169\.254|192\.168)\./.test(h) || /^172\.(1[6-9]|2\d|3[01])\./.test(h)) return false;
  if (/^\[/.test(h) || /^\d+\.\d+\.\d+\.\d+$/.test(h)) return false; // IP cru não é CDN
  return true;
}

/** As chaves do callback S2S (Android e iOS; a antiga, única, também vale). */
export function chavesDoLevelPlay(env) {
  return [env.LEVELPLAY_S2S_PRIVATE_KEY_ANDROID, env.LEVELPLAY_S2S_PRIVATE_KEY_IOS, env.LEVELPLAY_S2S_PRIVATE_KEY]
    .filter((k) => typeof k === "string" && k.length > 0);
}

export async function rotaDeVideo(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (!rota.startsWith(PREFIXO)) return null;
  const sub = rota.slice(PREFIXO.length) || "/";
  const cfg = configuracao(env);
  const cofre = env.COFRE_DE_VIDEO.get(env.COFRE_DE_VIDEO.idFromName("global"));
  const agora = Date.now();
  const ip = req.headers.get("cf-connecting-ip") ?? "0.0.0.0";
  const admin = tokenConfere((req.headers.get("authorization") ?? "").replace(/^Bearer /, ""), env.AI_VIDEO_ADMIN_TOKEN ?? "");

  // --- público --------------------------------------------------------------
  if (sub === "/config" && req.method === "GET") {
    return json({
      enabled: cfg.ligado,
      provider: "8scale",
      model: "Wan 2.2 14B",
      modes: cfg.modos,
      durations: cfg.duracoes,
      aspectRatios: cfg.aspectos,
      resolutions: cfg.resolucoes,
      promptMaxChars: cfg.promptMax,
      requiresReward: true,
    });
  }

  // Callback do LevelPlay: é o LevelPlay que chama, não o app.
  if (sub === "/reward/levelplay" && req.method === "GET") {
    // Só os servidores do LevelPlay (lista da documentação). Desligável por var,
    // caso eles mudem de IP: aí a assinatura continua sendo a barreira.
    if (String(env.AI_VIDEO_LEVELPLAY_IP_CHECK ?? "true") !== "false" && !IPS_DO_LEVELPLAY.includes(ip)) {
      console.warn(`[ai-video] callback levelplay de IP fora da lista: ${ip}`);
      return new Response("erro:ip", { status: 403 });
    }
    const r = await conferirCallback(url.searchParams, chavesDoLevelPlay(env));
    if (!r.ok) {
      console.warn(`[ai-video] callback levelplay recusado: ${r.erro}`);
      return new Response(`erro:${r.erro}`, { status: r.erro === "chave_nao_configurada" ? 503 : 400 });
    }
    const res = await cofre.registrarRecompensa({ ticketId: r.userId, eventId: r.eventId, agora });
    console.log(`[ai-video] recompensa S2S valida (${r.chave === 0 ? "android" : r.chave === 1 ? "ios" : "chave " + r.chave})${res.ignorado ? "" : " -> ticket liberado"}`);
    if (res.ignorado) console.warn(`[ai-video] recompensa ignorada: ${res.ignorado}`);
    return new Response(`${r.eventId}:OK`, { status: 200, headers: { "content-type": "text/plain" } });
  }

  // A imagem de partida do I2V: a 8Scale busca por URL. Id impossível de adivinhar, vida de 1 h.
  const img = sub.match(/^\/images\/([A-Za-z0-9_-]{16,64})$/);
  if (img && req.method === "GET") {
    const { value, metadata } = await env.AUREA_KV.getWithMetadata(`img:${img[1]}`, "arrayBuffer");
    if (!value) return erro("imagem_expirada", 404);
    return new Response(value, { headers: { "content-type": metadata?.tipo ?? "application/octet-stream", "cache-control": "private, max-age=600" } });
  }

  // --- administração ----------------------------------------------------------
  if (sub === "/admin/status" && req.method === "GET") {
    if (!admin) return erro("nao_autorizado", 401);
    return json({ config: { ...cfg, limites: cfg.limites }, hoje: await cofre.painel({ agora }) });
  }

  // --- o app ------------------------------------------------------------------
  const aparelho = req.headers.get("x-aurea-device") ?? "";
  if (!APARELHO.test(aparelho)) return erro("aparelho_invalido", 400, "x-aurea-device");

  if (sub === "/images" && req.method === "POST") {
    if (!cfg.ligado) return erro("ia_desligada", 503);
    if (!cfg.modos.includes("image_to_video")) return erro("modo_invalido", 400);
    const tamanho = Number(req.headers.get("content-length") ?? "0");
    if (tamanho > IMAGEM_MAX) return erro("imagem_grande", 413);
    const bytes = new Uint8Array(await req.arrayBuffer());
    if (bytes.length === 0 || bytes.length > IMAGEM_MAX) return erro("imagem_grande", 413);
    const tipo = tipoDaImagem(bytes);
    if (!tipo) return erro("imagem_invalida", 415, "so png, jpeg ou webp");
    const id = idAleatorio();
    await env.AUREA_KV.put(`img:${id}`, bytes, { expirationTtl: 3600, metadata: { tipo, aparelho } });
    return json({ imageId: id });
  }

  if (sub === "/tickets" && req.method === "POST") {
    if (!cfg.ligado) return erro("ia_desligada", 503);
    if (!Number.isFinite(cfg.custoPorJobUsd)) return erro("ia_nao_configurada", 503, "AI_VIDEO_COST_PER_JOB_USD");
    let corpo;
    try { corpo = await req.json(); } catch { return erro("json_invalido", 400); }
    const v = validarPedido(corpo, cfg);
    if (v.erro) return erro(v.erro, 400);
    if (v.pedido.imageId) {
      const meta = await env.AUREA_KV.getWithMetadata(`img:${v.pedido.imageId}`);
      if (!meta.value || meta.metadata?.aparelho !== aparelho) return erro("imagem_expirada", 400);
    }
    const id = idAleatorio();
    const r = await cofre.emitirTicket({ id, aparelho, ip, pedido: v.pedido, limites: cfg.limites, agora });
    if (!r.ok) return erro(r.erro, r.erro === "muitos_pedidos" ? 429 : 429);
    return json({ ticket: id, expiresAt: r.expira, requiresReward: cfg.exigirRecompensa && !admin });
  }

  if (sub === "/generate" && req.method === "POST") {
    // Kill switch: com a IA desligada, NENHUM POST pago chega à 8Scale.
    if (!cfg.ligado) return erro("ia_desligada", 503);
    if (!Number.isFinite(cfg.custoPorJobUsd)) return erro("ia_nao_configurada", 503, "AI_VIDEO_COST_PER_JOB_USD");
    if (cfg.exigirRecompensa && chavesDoLevelPlay(env).length === 0 && !admin) return erro("recompensa_nao_configurada", 503);
    let corpo;
    try { corpo = await req.json(); } catch { return erro("json_invalido", 400); }
    const ticketId = String(corpo?.ticket ?? "");
    if (!ID_NOSSO.test(ticketId)) return erro("ticket_invalido", 400);

    const reserva = await cofre.reservar({
      ticketId, aparelho, ip, admin,
      exigirRecompensa: cfg.exigirRecompensa,
      custoUsd: cfg.custoPorJobUsd,
      limites: cfg.limites,
      agora,
    });
    if (reserva.jaExiste) return json({ jobId: reserva.jobId, status: "queued", repeated: true }, 202);
    if (!reserva.ok) {
      const http = { recompensa_pendente: 409, em_andamento: 409, ticket_usado: 409, orcamento_diario: 429 }[reserva.erro]
        ?? (reserva.erro.startsWith("limite") || reserva.erro.endsWith("ocupado") || reserva.erro === "job_em_andamento" ? 429 : 400);
      return erro(reserva.erro, http);
    }

    const p = reserva.pedido;
    const provedor = new EightScaleVideoProvider(env);
    try {
      const enviado = await provedor.submit({
        ...p,
        imageUrl: p.imageId ? `${url.origin}${PREFIXO}/images/${p.imageId}` : undefined,
      });
      const jobId = idAleatorio();
      await cofre.confirmar({ ticketId, jobId, requestId: enviado.requestId, modelo: enviado.modelo, agora: Date.now() });
      console.log(`[ai-video] job ${jobId} aceito (${enviado.modelo}, ${p.resolution}, ${p.duration}s)`);
      return json({ jobId, status: "queued" }, 202);
    } catch (e) {
      // Não aceito = não cobrado: a reserva volta e o ticket segue valendo.
      await cofre.liberar({ ticketId, agora: Date.now() });
      const pe = e instanceof ErroDoProvedor ? e : new ErroDoProvedor("provedor_erro", 502, String(e));
      console.warn(`[ai-video] envio recusado: ${pe.codigo} (${pe.http}) ${pe.detalhe}`);
      const http = pe.codigo === "pedido_recusado" || pe.codigo === "conteudo_bloqueado" ? 422
        : pe.codigo === "provedor_ocupado" ? 429 : 502;
      return erro(pe.codigo, http);
    }
  }

  const rj = sub.match(/^\/jobs\/([A-Za-z0-9_-]{16,64})(\/video|\/cancel)?$/);
  if (rj) {
    const jobId = rj[1];
    let j = await cofre.job({ jobId });
    if (!j || j.aparelho !== aparelho) return erro("job_nao_encontrado", 404);
    const provedor = new EightScaleVideoProvider(env);

    if (!rj[2] && req.method === "GET") {
      if (!j.fim) {
        try {
          const s = await provedor.status(j.requestId);
          let saida = s.output;
          let falha = s.error;
          let estado = s.status;
          if (estado === "COMPLETED" && (!saida || !saidaAceitavel(saida))) {
            estado = "FAILED";
            falha = "resultado_invalido";
            saida = null;
          }
          j = await cofre.atualizarJob({ jobId, estado, saida, execucaoMs: s.executionTimeMs, erro: falha, agora: Date.now() });
        } catch (e) {
          const pe = e instanceof ErroDoProvedor ? e : new ErroDoProvedor("provedor_erro", 502, String(e));
          console.warn(`[ai-video] status ${jobId}: ${pe.codigo} ${pe.detalhe}`);
          // Consulta que falhou não muda o job: o app tenta de novo.
          return erro(pe.codigo, pe.http >= 500 || pe.http === 0 ? 502 : pe.http);
        }
      }
      return json(paraApp(j, j.reenvioDisponivel));
    }

    if (rj[2] === "/cancel" && req.method === "POST") {
      if (j.fim) return json(paraApp(j, j.reenvioDisponivel));
      try { await provedor.cancel(j.requestId); } catch (e) {
        // Em andamento não cancela na 8Scale: o app segue acompanhando.
        return erro("nao_cancelavel", 409, String(e?.codigo ?? ""));
      }
      j = await cofre.atualizarJob({ jobId, estado: "CANCELLED", agora: Date.now() });
      return json(paraApp(j, j.reenvioDisponivel));
    }

    if (rj[2] === "/video" && req.method === "GET") {
      if (j.estado !== "COMPLETED" || !j.saida) return erro("video_indisponivel", 409);
      let resp;
      try {
        // SEM a nossa chave: a URL já vem pré-assinada.
        resp = await fetch(j.saida, { signal: AbortSignal.timeout(120_000) });
      } catch (e) {
        return erro("download_falhou", 502, String(e?.message ?? e));
      }
      if (resp.status === 403 || resp.status === 404 || resp.status === 410) return erro("resultado_expirado", 410);
      if (!resp.ok || !resp.body) return erro("download_falhou", 502, `HTTP ${resp.status}`);
      const tipo = (resp.headers.get("content-type") ?? "").toLowerCase();
      if (tipo && !/^(video\/|application\/octet-stream|binary\/octet-stream)/.test(tipo)) {
        return erro("resultado_nao_e_video", 502, tipo.slice(0, 60));
      }
      const tamanho = Number(resp.headers.get("content-length") ?? "0");
      if (tamanho > VIDEO_MAX) return erro("resultado_grande", 502);
      const h = { "content-type": tipo.startsWith("video/") ? tipo : "video/mp4", "cache-control": "no-store" };
      if (tamanho) h["content-length"] = String(tamanho);
      return new Response(resp.body, { status: 200, headers: h });
    }
  }

  return erro("nao_encontrado", 404);
}
