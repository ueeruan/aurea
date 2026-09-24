// =============================================================================
//  Verificação server-side da recompensa do LevelPlay (server-to-server callback).
//
//  Documentação oficial (Unity LevelPlay, "Server-to-server callback settings"
//  e "event handlers"):
//    - placeholders obrigatórios: [USER_ID], [REWARDS], [EVENT_ID]
//    - chegam também `timestamp` (YYYYMMDDHHMM) e `signature`
//    - signature = md5(timestamp + eventId + userId + rewards + privateKey),
//      com o userId DECODIFICADO da URL
//    - a resposta precisa ser HTTP 200 contendo "<EVENT_ID>:OK", senão o
//      LevelPlay reenvia (20 vezes em 24 h, depois 1 por dia por 7 dias)
//
//  O [USER_ID] é o Dynamic User ID que o app define ANTES de mostrar o anúncio
//  (`LevelPlay.setDynamicUserId`) — e o app define o TICKET da geração. É o que
//  amarra "este anúncio foi assistido" a "esta geração", sem confiar em nenhum
//  boolean que o app mande.
// =============================================================================

/** md5 em hex. No Worker, `crypto.subtle` aceita MD5; o teste injeta outro. */
export async function md5Hex(texto) {
  const bytes = new TextEncoder().encode(texto);
  const dig = await crypto.subtle.digest("MD5", bytes);
  return [...new Uint8Array(dig)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function iguaisEmTempoConstante(a, b) {
  if (typeof a !== "string" || typeof b !== "string" || a.length !== b.length) return false;
  let dif = 0;
  for (let i = 0; i < a.length; i++) dif |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return dif === 0;
}

/**
 * IPs de onde o LevelPlay (ironSource) faz o callback, pela documentação
 * oficial ("Server-to-server callback event handlers").
 */
export const IPS_DO_LEVELPLAY = Object.freeze([
  "79.125.5.179", "79.125.26.193", "79.125.117.130", "176.34.224.39", "176.34.224.41", "176.34.224.49",
  "34.194.180.125", "34.196.56.165", "34.196.251.81", "34.196.253.23", "54.88.253.218", "54.209.185.78",
]);

/**
 * Confere com QUALQUER uma das chaves (Android e iOS são apps separados no
 * LevelPlay, cada um com a sua). `params` é o URLSearchParams da requisição,
 * que já decodifica o userId, como a assinatura exige.
 */
export async function conferirCallback(params, chaves, { md5 = md5Hex } = {}) {
  const lista = (Array.isArray(chaves) ? chaves : [chaves]).filter(Boolean);
  if (!lista.length) return { ok: false, erro: "chave_nao_configurada" };
  const userId = params.get("userid") ?? params.get("userId") ?? params.get("USER_ID") ?? "";
  const eventId = params.get("eventId") ?? params.get("eventid") ?? "";
  const rewards = params.get("rewards") ?? "";
  const timestamp = params.get("timestamp") ?? "";
  const assinatura = (params.get("signature") ?? "").toLowerCase();
  if (!userId || !eventId || !timestamp || !assinatura) return { ok: false, erro: "parametros_faltando", eventId };
  for (let i = 0; i < lista.length; i++) {
    const esperada = await md5(timestamp + eventId + userId + rewards + lista[i]);
    if (iguaisEmTempoConstante(assinatura, esperada)) return { ok: true, userId, eventId, rewards, chave: i };
  }
  return { ok: false, erro: "assinatura_invalida", eventId };
}
