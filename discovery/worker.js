// =============================================================================
//  aurea-ai-discovery — o endereço FIXO que o app consulta.
//
//  Existe para o app não precisar de APK/IPA novo quando o túnel do Colab muda
//  de nome. O que muda é o `endpoint` guardado aqui; o endereço deste Worker é
//  sempre o mesmo (`https://aurea-ai-discovery.aureaapp.workers.dev`).
//
//  Duas rotas:
//    GET  /server   → o documento (público; é o que o app lê)
//    POST /publicar → o Colab escreve o endereço novo (exige segredo)
//
//  O segredo NUNCA vai para o APK nem para o IPA: ele vive só no Colab (como
//  variável de ambiente) e aqui (como secret do Worker).
// =============================================================================

import { rotaDeVideo } from "./ai_video.js";
import { captionRoute } from "./caption_community.js";
export { CaptionCommunity } from "./caption_community.js";
// Contas obrigatórias (/api/auth/*, /api/stats/users) e crash (/api/crash).
import { rotaDeContas, lerJson } from "./contas.js";
import { rotaDeCrash } from "./crash.js";
// "Relatar um problema" (/api/report): o texto que a pessoa escreve no app.
import { rotaDeRelato } from "./relato.js";
import { communityRoute } from "./community.js";

export { CofreDeVideo } from "./cofre.js";

const CHAVE = "atual";


/** O documento que o app lê. Sem nada publicado, é offline — nunca um palpite. */
function offline() {
  return { online: false, endpoint: "", updatedAt: 0 };
}

function json(corpo, status = 200) {
  return new Response(JSON.stringify(corpo), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      // O app relê a cada 20 s: cache aqui só atrasaria a troca de endereço.
      "cache-control": "no-store",
      "x-content-type-options": "nosniff",
      "strict-transport-security": "max-age=31536000",
      "referrer-policy": "no-referrer",
    },
  });
}

/** Comparação em tempo constante: não vaza o segredo pelo tempo da resposta. */
function segredoConfere(recebido, esperado) {
  if (typeof recebido !== "string" || typeof esperado !== "string") return false;
  if (recebido.length !== esperado.length) return false;
  let dif = 0;
  for (let i = 0; i < recebido.length; i++) dif |= recebido.charCodeAt(i) ^ esperado.charCodeAt(i);
  return dif === 0;
}

export default {
  async fetch(req, env, ctx) {
    try {
      const url = new URL(req.url);
      if (url.protocol !== 'https:' && !['127.0.0.1', 'localhost'].includes(url.hostname))
        return json({ error: 'https_obrigatorio' }, 403);
      return await handle(req, env, ctx, url);
    } catch {
      // Never expose SQL, credentials, URLs or provider bodies in error responses/logs.
      console.error('aurea: request failed');
      return json({ error: 'servico_indisponivel' }, 503);
    }
  },
};

async function handle(req, env, ctx, url) {
    const captions = await captionRoute(req, env, url);
    if (captions) return captions;
    const contas = await rotaDeContas(req, env, ctx, url);
    if (contas) return contas;
    const community = await communityRoute(req, env, ctx, url);
    if (community) return community;
    const crash = await rotaDeCrash(req, env, ctx, url);
    if (crash) return crash;
    const relato = await rotaDeRelato(req, env, ctx, url);
    if (relato) return relato;

    // Geração de vídeo paga (8Scale): tudo sob /api/ai/video.
    const video = await rotaDeVideo(req, env, ctx, url);
    if (video) return video;
    const rota = url.pathname.replace(/\/+$/, "") || "/server";

    if (rota === "/server" && (req.method === "GET" || req.method === "HEAD")) {
      const guardado = await env.AUREA_KV.get(CHAVE, "json");
      return json(guardado ?? offline());
    }

    if (rota === "/publicar" && req.method === "POST") {
      const auth = req.headers.get("authorization") ?? "";
      const token = auth.startsWith("Bearer ") ? auth.slice(7) : "";
      if (!env.AUREA_DISCOVERY_SECRET || !segredoConfere(token, env.AUREA_DISCOVERY_SECRET)) {
        return json({ error: "nao_autorizado" }, 401);
      }

      const { valor: corpo, erro } = await lerJson(req, 8192);
      if (erro) return json({ error: erro }, erro === 'corpo_grande' ? 413 : 400);

      const endpoint = String(corpo.endpoint ?? "").trim().replace(/\/+$/, "");
      const online = corpo.online === true;

      // Endereço de servidor de verdade é HTTPS. Sem isto, um `http://` solto
      // entraria no documento e o app recusaria depois, sem saber por quê.
      if (online && !endpoint.startsWith("https://")) {
        return json({ error: "endpoint_invalido", detail: "precisa comecar com https://" }, 400);
      }

      const doc = {
        online,
        endpoint: online ? endpoint : "",
        model: String(corpo.model ?? "MiniMax-H3"),
        gpu: String(corpo.gpu ?? ""),
        capabilities: Array.isArray(corpo.capabilities)
          ? corpo.capabilities.filter((c) => typeof c === "string")
          : ["text_to_video", "image_to_video"],
        updatedAt: Math.floor(Date.now() / 1000),
      };
      await env.AUREA_KV.put(CHAVE, JSON.stringify(doc));
      return json({ ok: true, ...doc });
    }

    return json({ error: "nao_encontrado" }, 404);
}
