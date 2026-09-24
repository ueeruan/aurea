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
  async fetch(req, env) {
    const url = new URL(req.url);
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

      let corpo;
      try {
        corpo = await req.json();
      } catch {
        return json({ error: "json_invalido" }, 400);
      }

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
  },
};
