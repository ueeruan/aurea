// =============================================================================
//  Direitos pagos (entitlements) — o servidor diz o que cada conta pode usar.
//
//  Rotas (JSON, cache-control: no-store):
//    GET  /api/entitlements        Bearer <token da sessão>
//         → 200 {products: ["aureahyper_lifetime", ...], checkedAt: <s Unix>}
//         → 401 {error: "nao_autorizado"}  (sem sessão, inválida ou vencida)
//    POST /api/admin/entitlements  Bearer <AUREA_ADMIN_SECRET>
//         {email, product, action: "grant"|"revoke", note?: ≤200 caracteres}
//         → 200 {ok, email, product, action, products}
//         → 400 email/produto/ação/nota inválidos · 401 segredo errado
//         → 404 conta_nao_encontrada · 429 tentativas demais
//         → 503 admin_desativado (secret não configurado: nunca fica aberto)
//
//  Ainda não há cobrança: o admin concede e revoga por e-mail. Conceder grava
//  (ou regrava) a linha com granted_at = agora e source = "admin"; revogar
//  APAGA a linha (migração 0007). Só produtos de PRODUTOS saem na resposta.
//
//  O segredo vive só aqui (`wrangler secret put AUREA_ADMIN_SECRET`) e na
//  máquina do admin. Nunca vai para o APK/IPA nem para o log.
// =============================================================================

import { json, lerJson, lerSessao, normalizarEmail, dentroDoLimite } from "./contas.js";

/** Os produtos que o servidor conhece. Outro valor é recusado (400). */
export const PRODUTOS = Object.freeze(["aureahyper_lifetime"]);

const CORPO_ADMIN_MAX = 1024;
const NOTA_MAX = 200;
const SEGREDO_MIN = 16;
// Conta TODAS as tentativas (como o login): o admin usa isto à mão.
export const LIMITE_ADMIN = { max: 30, janela: 900 };

const agoraS = () => Math.floor(Date.now() / 1000);
const ip = (req) => req.headers.get("cf-connecting-ip") ?? "local";

/** Comparação em tempo constante (mesma de `segredoConfere` no worker.js). */
function segredoConfere(recebido, esperado) {
  if (typeof recebido !== "string" || typeof esperado !== "string") return false;
  if (recebido.length !== esperado.length) return false;
  let dif = 0;
  for (let i = 0; i < recebido.length; i++) dif |= recebido.charCodeAt(i) ^ esperado.charCodeAt(i);
  return dif === 0;
}

async function produtosDe(env, uid) {
  const r = await env.AUREA_DB.prepare("SELECT product FROM entitlements WHERE user_id = ? ORDER BY product")
    .bind(uid).all();
  return (r?.results ?? []).map((x) => x.product).filter((p) => PRODUTOS.includes(p)).sort();
}

async function meusProdutos(req, env) {
  const s = await lerSessao(env, req);
  if (!s) return json({ error: "nao_autorizado" }, 401);
  let products;
  try {
    products = await produtosDe(env, s.uid);
  } catch {
    // Sem a migração 0007 (ou D1 fora): o app não pode entender isso como "não tem".
    console.error("entitlements: leitura falhou no D1");
    return json({ error: "entitlements_indisponiveis" }, 503);
  }
  return json({ products, checkedAt: agoraS() });
}

async function administrar(req, env) {
  const segredo = env.AUREA_ADMIN_SECRET;
  if (typeof segredo !== "string" || segredo.length < SEGREDO_MIN) {
    return json({ error: "admin_desativado" }, 503);
  }
  if (!(await dentroDoLimite(env, "admin-entitlements-ip", ip(req), LIMITE_ADMIN))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": String(LIMITE_ADMIN.janela) });
  }
  const auth = req.headers.get("authorization") ?? "";
  const token = auth.startsWith("Bearer ") ? auth.slice(7) : "";
  if (!segredoConfere(token, segredo)) return json({ error: "nao_autorizado" }, 401);

  const { valor, erro } = await lerJson(req, CORPO_ADMIN_MAX);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const email = normalizarEmail(valor.email);
  if (!email) return json({ error: "email_invalido" }, 400);
  const product = valor.product;
  if (typeof product !== "string" || !PRODUTOS.includes(product)) return json({ error: "produto_desconhecido" }, 400);
  const action = valor.action;
  if (action !== "grant" && action !== "revoke") return json({ error: "acao_invalida" }, 400);
  let note = null;
  if (valor.note !== undefined && valor.note !== null) {
    if (typeof valor.note !== "string" || [...valor.note].length > NOTA_MAX) return json({ error: "nota_invalida" }, 400);
    note = valor.note.trim() || null;
  }

  const db = env.AUREA_DB;
  const conta = await db.prepare("SELECT id FROM users WHERE email = ?").bind(email).first();
  if (!conta) return json({ error: "conta_nao_encontrada" }, 404);

  if (action === "grant") {
    await db.prepare(
      "INSERT OR REPLACE INTO entitlements (user_id, product, granted_at, source, note) VALUES (?, ?, ?, 'admin', ?)")
      .bind(conta.id, product, agoraS(), note).run();
  } else {
    await db.prepare("DELETE FROM entitlements WHERE user_id = ? AND product = ?").bind(conta.id, product).run();
  }
  return json({ ok: true, email, product, action, products: await produtosDe(env, conta.id) });
}

/** Devolve a resposta quando a rota é de entitlements; null para o Worker seguir. */
export async function rotaDeEntitlements(req, env, ctx, url) {
  const rota = url.pathname.replace(/\/+$/, "");
  if (rota !== "/api/entitlements" && rota !== "/api/admin/entitlements") return null;
  if (!env.AUREA_DB) return json({ error: "contas_indisponiveis" }, 503);
  if (rota === "/api/entitlements" && req.method === "GET") return meusProdutos(req, env);
  if (rota === "/api/admin/entitlements" && req.method === "POST") return administrar(req, env);
  return json({ error: "nao_encontrado" }, 404);
}
