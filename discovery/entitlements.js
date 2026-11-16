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

import { json, lerJson, lerSessao, normalizarEmail, dentroDoLimite, sha256hex } from "./contas.js";
import { verifyGooglePurchase, verifyApplePurchase, googleReady, appleReady } from "./store_verify.js";

/** Os produtos que o servidor conhece. Outro valor é recusado (400). */
/** AureaOS no AureaMCP: compra ÚNICA (não consumível, vale para sempre na conta). */
export const AUREAOS_PRODUCT = "aurea_aureaos";
export const PRODUTOS = Object.freeze(["aureahyper_lifetime", AUREAOS_PRODUCT]);
/** Os que se compram na loja (POST /api/entitlements/purchase confere com a Play/App Store). */
export const PRODUTOS_DA_LOJA = Object.freeze([AUREAOS_PRODUCT]);

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
  // `purchasable`: o que dá para comprar agora (a loja do aparelho confere; sem as
  // chaves da loja no Worker, nada — o app mostra "indisponível" em vez de cobrar).
  const platform = new URL(req.url).searchParams.get("platform");
  const ready = env.AUREAOS_PURCHASES_ENABLED === "true" && (platform === "google" ? googleReady(env) : platform === "apple" ? appleReady(env)
    : googleReady(env) || appleReady(env));
  const purchasable = ready ? [...PRODUTOS_DA_LOJA] : [];
  return json({ products, checkedAt: agoraS(), purchasable });
}

/** A conta tem o produto? true/false; null = não deu para ler (D1 fora): quem chama não libera. */
export async function temProduto(env, uid, product) {
  try {
    const r = await env.AUREA_DB.prepare("SELECT 1 AS ok FROM entitlements WHERE user_id = ? AND product = ?").bind(uid, product).first();
    return !!r;
  } catch {
    return null;
  }
}

export const LIMITE_COMPRA = { max: 20, janela: 3600 };

/**
 * POST /api/entitlements/purchase {platform: "google"|"apple", product, token}
 * (sessão da conta). token = purchaseToken da Play ou o id da transação da Apple.
 * A loja confere; a compra vira a linha de entitlements (source = a loja; a
 * nota guarda o hash da compra). Repetir (restaurar) devolve 200; a mesma
 * compra em OUTRA conta = 409.
 */
async function comprar(req, env) {
  const s = await lerSessao(env, req);
  if (!s) return json({ error: "nao_autorizado" }, 401);
  if (!(await dentroDoLimite(env, "entitlements-compra", s.uid, LIMITE_COMPRA))) {
    return json({ error: "muitas_tentativas" }, 429, { "retry-after": String(LIMITE_COMPRA.janela) });
  }
  const { valor, erro } = await lerJson(req, 8192);
  if (erro) return json({ error: erro }, erro === "corpo_grande" ? 413 : 400);
  const platform = valor?.platform;
  const product = valor?.product;
  const token = typeof valor?.token === "string" ? valor.token.trim() : "";
  if (platform !== "google" && platform !== "apple") return json({ error: "plataforma_invalida" }, 400);
  if (typeof product !== "string" || !PRODUTOS_DA_LOJA.includes(product)) return json({ error: "produto_desconhecido" }, 400);
  if (!token || token.length > 4096) return json({ error: "purchase_invalid" }, 400);
  if (env.AUREAOS_PURCHASES_ENABLED !== "true") return json({ error: "payments_unavailable" }, 503);
  let checked;
  try {
    checked = platform === "google" ? await verifyGooglePurchase(env, token, product) : await verifyApplePurchase(env, token, product);
  } catch {
    return json({ error: "payments_unavailable" }, 503);
  }
  if (checked.error) return json({ error: checked.error }, checked.error === "payments_unavailable" ? 503 : 402);
  const purchaseHash = await sha256hex(checked.key);
  const marca = `purchase:${purchaseHash}`;
  const db = env.AUREA_DB;
  try {
    // Claim and grant run in one transaction. Two accounts restoring the same
    // receipt concurrently cannot both obtain the entitlement.
    await db.batch([
      db.prepare("INSERT OR IGNORE INTO entitlement_purchases (product, purchase_hash, user_id, created_at) VALUES (?, ?, ?, ?)")
        .bind(product, purchaseHash, s.uid, agoraS()),
      db.prepare("INSERT INTO entitlements (user_id, product, granted_at, source, note) SELECT ?, ?, ?, ?, ? WHERE EXISTS (SELECT 1 FROM entitlement_purchases WHERE product = ? AND purchase_hash = ? AND user_id = ?) ON CONFLICT(user_id, product) DO UPDATE SET granted_at = excluded.granted_at, source = excluded.source, note = excluded.note")
        .bind(s.uid, product, agoraS(), platform, marca, product, purchaseHash, s.uid),
    ]);
    const owner = await db.prepare("SELECT user_id FROM entitlement_purchases WHERE product = ? AND purchase_hash = ?")
      .bind(product, purchaseHash).first();
    if (!owner || owner.user_id !== s.uid) return json({ error: "purchase_in_use" }, 409);
  } catch {
    return json({ error: "payments_unavailable" }, 503);
  }
  return json({ ok: true, product, products: await produtosDe(env, s.uid) });
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
  if (rota !== "/api/entitlements" && rota !== "/api/admin/entitlements" && rota !== "/api/entitlements/purchase") return null;
  if (!env.AUREA_DB) return json({ error: "contas_indisponiveis" }, 503);
  if (rota === "/api/entitlements" && req.method === "GET") return meusProdutos(req, env);
  if (rota === "/api/entitlements/purchase" && req.method === "POST") return comprar(req, env);
  if (rota === "/api/admin/entitlements" && req.method === "POST") return administrar(req, env);
  return json({ error: "nao_encontrado" }, 404);
}
