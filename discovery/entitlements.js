// =============================================================================
//  Direitos pagos (entitlements) — o servidor diz o que cada conta pode usar.
//
//  Rotas (JSON, cache-control: no-store):
//    GET  /api/entitlements        Bearer <token da sessão>   [?platform=google|apple]
//         → 200 {products: ["aureahyper_lifetime", ...], checkedAt: <s Unix>,
//                purchasable: [...], expires: {<assinatura>: <s Unix>}}
//           `products` só traz o que vale AGORA: assinatura vencida não sai (e,
//           vencida há pouco, é conferida de novo na loja: renovação).
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
import { verifyGooglePurchase, verifyApplePurchase, verifyGoogleSubscription, verifyAppleSubscription,
  renewSubscription, googleReady, appleReady } from "./store_verify.js";

/** Os produtos que o servidor conhece. Outro valor é recusado (400). */
/** AureaOS no AureaMCP: compra ÚNICA (não consumível, vale para sempre na conta). */
export const AUREAOS_PRODUCT = "aurea_aureaos";
/**
 * ASSINATURAS mensais (16/11/2026, decisão do dono; docs/release/assinaturas.md).
 * O AureaHyper libera tudo o que é pago; cada efeito pago e o Texto 3D têm a sua.
 * Quem decide o que cada produto libera é o motor (hyper::has_access, Hyper.cpp):
 * esta lista só diz o que o servidor aceita vender. Mesma lista de
 * hyper::subscription_product_ids (test_hyper.cpp confere a contagem: 12).
 */
export const HYPER_SUBSCRIPTION = "aurea_hyper_mensal";
export const ASSINATURAS = Object.freeze([
  HYPER_SUBSCRIPTION,
  "aurea_fx_cartoon_moblur", "aurea_fx_twitch", "aurea_fx_signal", "aurea_fx_glitchify", "aurea_fx_extrude",
  "aurea_fx_sharpen", "aurea_fx_find_edges", "aurea_fx_wave_warp", "aurea_fx_block_dissolve",
  "aurea_fx_radial_wipe", "aurea_texto3d",
]);
export const PRODUTOS = Object.freeze(["aureahyper_lifetime", AUREAOS_PRODUCT, ...ASSINATURAS]);
/** Os que se compram na loja (POST /api/entitlements/purchase confere com a Play/App Store). */
export const PRODUTOS_DA_LOJA = Object.freeze([AUREAOS_PRODUCT, ...ASSINATURAS]);
/** Assinatura vencida há mais que isto não é mais conferida sozinha (o app manda a compra nova). */
export const RENOVACAO_JANELA_S = 35 * 24 * 3600;
/** Uma assinatura vencida é conferida na loja no máximo uma vez por hora. */
export const RENOVACAO_INTERVALO_S = 3600;

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

/** As linhas da conta. Sem a migração 0012 (colunas de vencimento), lê como antes: nada vence. */
async function linhasDe(env, uid) {
  try {
    const r = await env.AUREA_DB.prepare(
      "SELECT product, source, expires_at, store_ref, store_checked_at FROM entitlements WHERE user_id = ? ORDER BY product")
      .bind(uid).all();
    return r?.results ?? [];
  } catch (e) {
    if (!/no such column/i.test(String(e?.message ?? e))) throw e;
    const r = await env.AUREA_DB.prepare("SELECT product FROM entitlements WHERE user_id = ? ORDER BY product").bind(uid).all();
    return (r?.results ?? []).map((x) => ({ product: x.product, expires_at: null }));
  }
}

/**
 * O que a conta tem AGORA: {products, expires}. `renovar` = uma assinatura que
 * venceu há pouco é conferida de novo na loja (renovação automática); a loja fora
 * do ar não libera nada e não apaga nada (fica para a próxima consulta).
 */
async function direitosDe(env, uid, { renovar = false } = {}) {
  const now = agoraS();
  const products = new Set();
  const expires = {};
  for (const row of await linhasDe(env, uid)) {
    if (!PRODUTOS.includes(row.product)) continue;
    let fim = row.expires_at ?? null;
    if (fim !== null && fim <= now && renovar && row.store_ref && (row.source === "google" || row.source === "apple")
      && now - fim < RENOVACAO_JANELA_S && (row.store_checked_at == null || now - row.store_checked_at >= RENOVACAO_INTERVALO_S)) {
      fim = await renovarAssinatura(env, uid, row, now);
    }
    if (fim !== null && fim <= now) continue;
    products.add(row.product);
    if (fim !== null) expires[row.product] = fim;
  }
  return { products: [...products].sort(), expires };
}

/** Pergunta à loja se a assinatura renovou. Devolve o novo vencimento (ou o antigo). */
async function renovarAssinatura(env, uid, row, now) {
  let checked;
  try {
    checked = await renewSubscription(env, row.source, row.store_ref, row.product);
  } catch {
    return row.expires_at;   // loja fora: tenta de novo na próxima consulta
  }
  // payments_unavailable (sem as chaves) não conta como conferência.
  if (checked.error === "payments_unavailable") return row.expires_at;
  const fim = !checked.error && checked.expiresAt > row.expires_at ? checked.expiresAt : row.expires_at;
  try {
    await env.AUREA_DB.prepare("UPDATE entitlements SET expires_at = ?, store_checked_at = ? WHERE user_id = ? AND product = ? AND store_ref = ?")
      .bind(fim, now, uid, row.product, row.store_ref).run();
  } catch {
    console.error("entitlements: renovação não gravou no D1");
  }
  return fim;
}

async function produtosDe(env, uid) {
  return (await direitosDe(env, uid)).products;
}

async function meusProdutos(req, env) {
  const s = await lerSessao(env, req);
  if (!s) return json({ error: "nao_autorizado" }, 401);
  let direitos;
  try {
    direitos = await direitosDe(env, s.uid, { renovar: true });
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
  return json({ products: direitos.products, checkedAt: agoraS(), purchasable, expires: direitos.expires });
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
 * ASSINATURAS: a linha guarda também o vencimento (expires_at) e o que se manda
 * à loja para conferir a renovação (store_ref); assinatura já vencida = 402
 * purchase_expired. Uma concessão do admin (sem vencimento) nunca é rebaixada
 * para assinatura. A mesma chave AUREAOS_PURCHASES_ENABLED (beta fechado) trava
 * todas as compras.
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
  const assinatura = ASSINATURAS.includes(product);
  let checked;
  try {
    checked = assinatura
      ? (platform === "google" ? await verifyGoogleSubscription(env, token, product) : await verifyAppleSubscription(env, token, product))
      : (platform === "google" ? await verifyGooglePurchase(env, token, product) : await verifyApplePurchase(env, token, product));
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
    const claim = db.prepare("INSERT OR IGNORE INTO entitlement_purchases (product, purchase_hash, user_id, created_at) VALUES (?, ?, ?, ?)")
      .bind(product, purchaseHash, s.uid, agoraS());
    const grant = assinatura
      // Assinatura: vencimento + referência da loja. Uma linha sem vencimento
      // (concessão do admin) fica como está.
      ? db.prepare("INSERT INTO entitlements (user_id, product, granted_at, source, note, expires_at, store_ref, store_checked_at) SELECT ?, ?, ?, ?, ?, ?, ?, ? WHERE EXISTS (SELECT 1 FROM entitlement_purchases WHERE product = ? AND purchase_hash = ? AND user_id = ?) ON CONFLICT(user_id, product) DO UPDATE SET granted_at = excluded.granted_at, source = excluded.source, note = excluded.note, expires_at = excluded.expires_at, store_ref = excluded.store_ref, store_checked_at = excluded.store_checked_at WHERE entitlements.expires_at IS NOT NULL")
        .bind(s.uid, product, agoraS(), platform, marca, checked.expiresAt, checked.ref, agoraS(), product, purchaseHash, s.uid)
      : db.prepare("INSERT INTO entitlements (user_id, product, granted_at, source, note) SELECT ?, ?, ?, ?, ? WHERE EXISTS (SELECT 1 FROM entitlement_purchases WHERE product = ? AND purchase_hash = ? AND user_id = ?) ON CONFLICT(user_id, product) DO UPDATE SET granted_at = excluded.granted_at, source = excluded.source, note = excluded.note")
        .bind(s.uid, product, agoraS(), platform, marca, product, purchaseHash, s.uid);
    await db.batch([claim, grant]);
    const owner = await db.prepare("SELECT user_id FROM entitlement_purchases WHERE product = ? AND purchase_hash = ?")
      .bind(product, purchaseHash).first();
    if (!owner || owner.user_id !== s.uid) return json({ error: "purchase_in_use" }, 409);
  } catch {
    return json({ error: "payments_unavailable" }, 503);
  }
  const direitos = await direitosDe(env, s.uid);
  return json({ ok: true, product, products: direitos.products, expires: direitos.expires });
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
