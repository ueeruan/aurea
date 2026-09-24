// =============================================================================
//  Provedores de geração de vídeo.
//
//  A rota do Worker só conhece `VideoGenerationProvider`. Trocar de provedor é
//  escrever outra classe com os mesmos quatro métodos — nada na rota, no cofre
//  ou no app muda. Toda a conversa com a 8Scale mora em
//  `EightScaleVideoProvider`, e só ele lê `env.EIGHTSCALE_API_KEY`.
//
//  Contrato da 8Scale (documentação oficial, Requests API):
//    POST https://8scale.run/{model}          → { requestId, status: "IN_QUEUE" }
//    GET  https://8scale.run/status/{id}      → { requestId, status, output?, executionTimeMs? }
//    POST https://8scale.run/cancel/{id}      → só enquanto IN_QUEUE
//    status ∈ IN_QUEUE | IN_PROGRESS | COMPLETED | FAILED | CANCELLED
//    `output` é uma URL pré-assinada, guardada 24 h depois do fim.
// =============================================================================

/** Erro de provedor já traduzido para um código curto que o app entende. */
export class ErroDoProvedor extends Error {
  constructor(codigo, http, detalhe) {
    super(`${codigo}: ${detalhe}`);
    this.codigo = codigo;
    this.http = http;
    this.detalhe = detalhe;
  }
}

/**
 * A interface. `submit` devolve o id do provedor; `status` devolve o estado
 * normalizado (os cinco da 8Scale) e, no fim, a URL do resultado.
 */
export class VideoGenerationProvider {
  /** @returns {Promise<{requestId: string, status: string}>} */
  async submit(_pedido) { throw new Error("nao implementado"); }
  /** @returns {Promise<{status: string, output: string|null, executionTimeMs: number|null, error: string|null}>} */
  async status(_requestId) { throw new Error("nao implementado"); }
  async cancel(_requestId) { throw new Error("nao implementado"); }
  /** Nome do modelo para o app mostrar. */
  get nome() { return ""; }
}

/**
 * Modelos PERMITIDOS, fixos no servidor. O cliente escolhe o MODO; nunca o id
 * do modelo nem o endpoint — é o que impede alguém de apontar a nossa chave
 * para um modelo mais caro.
 */
const MODELOS_8SCALE = Object.freeze({
  text_to_video: "wan-2.2/14b/text-to-video",
  image_to_video: "wan-2.2/14b/image-to-video",
});

const BASE_8SCALE = "https://8scale.run";
const ID_SEGURO = /^[A-Za-z0-9_-]{4,128}$/;

export class EightScaleVideoProvider extends VideoGenerationProvider {
  constructor(env, { fetch: f, timeoutMs = 25_000 } = {}) {
    super();
    this.chave = env.EIGHTSCALE_API_KEY ?? "";
    // O `fetch` do Worker não pode ser chamado como método de outro objeto
    // ("Illegal invocation"): sempre por uma função solta.
    this.fetch = f ?? ((...a) => globalThis.fetch(...a));
    this.timeoutMs = timeoutMs;
  }

  get nome() { return "Wan 2.2 14B"; }

  static modeloDe(modo) {
    return MODELOS_8SCALE[modo] ?? null;
  }

  async submit(p) {
    const modelo = EightScaleVideoProvider.modeloDe(p.mode);
    if (!modelo) throw new ErroDoProvedor("modo_invalido", 400, p.mode);
    const corpo = {
      prompt: p.prompt,
      negative_prompt: p.negativePrompt || undefined,
      resolution: p.resolution,
      aspect_ratio: p.aspectRatio,
      seconds: p.duration,
    };
    if (p.mode === "image_to_video") corpo.image = p.imageUrl;
    const r = await this._pedir("POST", `/${modelo}`, corpo);
    const requestId = String(r.requestId ?? "");
    if (!ID_SEGURO.test(requestId)) throw new ErroDoProvedor("resposta_invalida", 502, "requestId ausente");
    return { requestId, status: String(r.status ?? "IN_QUEUE"), modelo };
  }

  async status(requestId) {
    if (!ID_SEGURO.test(requestId)) throw new ErroDoProvedor("job_invalido", 400, "requestId");
    const r = await this._pedir("GET", `/status/${encodeURIComponent(requestId)}`);
    const status = String(r.status ?? "");
    if (!["IN_QUEUE", "IN_PROGRESS", "COMPLETED", "FAILED", "CANCELLED"].includes(status)) {
      throw new ErroDoProvedor("resposta_invalida", 502, `status desconhecido: ${status.slice(0, 40)}`);
    }
    const erro = r.error ?? r.message ?? null;
    return {
      status,
      output: typeof r.output === "string" ? r.output : null,
      executionTimeMs: Number.isFinite(r.executionTimeMs) ? r.executionTimeMs : null,
      error: erro == null ? null : String(erro).slice(0, 300),
    };
  }

  async cancel(requestId) {
    if (!ID_SEGURO.test(requestId)) throw new ErroDoProvedor("job_invalido", 400, "requestId");
    await this._pedir("POST", `/cancel/${encodeURIComponent(requestId)}`);
  }

  async _pedir(metodo, caminho, corpo) {
    if (!this.chave) throw new ErroDoProvedor("provedor_nao_configurado", 503, "EIGHTSCALE_API_KEY ausente");
    let resp;
    try {
      resp = await this.fetch(BASE_8SCALE + caminho, {
        method: metodo,
        headers: {
          authorization: `Bearer ${this.chave}`,
          accept: "application/json",
          ...(corpo ? { "content-type": "application/json" } : {}),
        },
        body: corpo ? JSON.stringify(corpo) : undefined,
        signal: AbortSignal.timeout(this.timeoutMs),
      });
    } catch (e) {
      const tempo = e?.name === "TimeoutError" || e?.name === "AbortError";
      throw new ErroDoProvedor(tempo ? "provedor_timeout" : "provedor_indisponivel", 504, String(e?.message ?? e));
    }
    const texto = await resp.text();
    let json = {};
    try { json = texto ? JSON.parse(texto) : {}; } catch { json = {}; }
    if (resp.ok) return json;
    // A mensagem da 8Scale fica no log do Worker; o app recebe só o código.
    const detalhe = String(json.message ?? json.error ?? texto).slice(0, 300);
    throw new ErroDoProvedor(codigoDoHttp(resp.status, detalhe), resp.status, detalhe);
  }
}

/** HTTP da 8Scale → código curto do Aurea. Exportado para teste. */
export function codigoDoHttp(http, detalhe = "") {
  const d = detalhe.toLowerCase();
  if (http === 401 || http === 403) return "provedor_auth";
  if (http === 402 || /insufficient|balance|credit|saldo/.test(d)) return "saldo_insuficiente";
  if (http === 404) return "modelo_indisponivel";
  if (http === 429) return "provedor_ocupado";
  if (/nsfw|safety|moderat|content polic|blocked/.test(d)) return "conteudo_bloqueado";
  if (http === 400 || http === 422) return "pedido_recusado";
  if (http >= 500) return "provedor_indisponivel";
  return "provedor_erro";
}
