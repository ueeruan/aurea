package com.aurea.aurea.hyper

import android.app.Activity
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import com.android.billingclient.api.acknowledgePurchase
import com.android.billingclient.api.queryProductDetails
import com.android.billingclient.api.queryPurchasesAsync
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.engine.AureaEngine
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume

// =============================================================================
//  ASSINATURAS (16/11/2026, decisão do dono; docs/release/assinaturas.md).
//
//  1.0.9 (decisão do dono, 25/11/2026): o AureaHyper tem três planos à venda —
//  SEMANAL (R$ 7), MENSAL (R$ 30) e ANUAL (R$ 150) — e todos liberam tudo o que é
//  pago. As assinaturas avulsas (efeitos, Texto 3D) não se vendem mais; quem já
//  tem continua com o acesso e o preço até renovar (coisa da loja). Os preços
//  vivem na Play: a tela mostra o preço localizado da loja (ProductDetails) e,
//  sem ele, o texto padrão. Os ids vêm do motor (hyper::hyper_plans_on_sale para
//  vender, hyper::subscription_product_ids para restaurar).
//
//  Compra: Play Billing SUBS (oferta do plano base) → o token vai ao Worker
//  (POST /api/entitlements/purchase, que confere na Play) → só depois do OK do
//  servidor o app reconhece (acknowledge) a compra. Restaurar: queryPurchasesAsync
//  (SUBS) ao abrir o app/entrar na conta e no botão "Restaurar compras".
//  O par do iOS: AureaSubscriptions.swift (StoreKit 2).
// =============================================================================

/** O item pago que o paywall vende. [product] = assinatura avulsa (null = só o AureaHyper). */
data class PaidItem(val product: String?, val kind: Kind, val subscriptionOnly: Boolean = false) {
    enum class Kind { Effect, Text3D, HyperTools }

    companion object {
        fun effect(typeId: Int): PaidItem =
            PaidItem(runCatching { AureaEngine.nativeEffectProductId(typeId) }.getOrNull(), Kind.Effect)
        fun text3d(): PaidItem = PaidItem(runCatching { AureaEngine.nativeText3dProductId() }.getOrNull(), Kind.Text3D)
        val hyperTools = PaidItem(null, Kind.HyperTools)
        val subscriptionTools = PaidItem(null, Kind.HyperTools, subscriptionOnly = true)
    }
}

object AureaSubscriptions {
    /** O intervalo mínimo entre duas restaurações silenciosas (abrir o app/entrar). */
    private const val SILENT_RESTORE_MS = 10L * 60 * 1000

    private val scope by lazy { CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate) }
    private var lastSilentRestore = 0L
    private var lastRestoreSession: String? = null
    private var pricesLoading = false

    /** Preço localizado da Play por produto (só a fase recorrente do plano base). */
    var prices by mutableStateOf<Map<String, String>>(emptyMap())
        private set
    /** O mesmo preço em micros + a moeda (para o "economize N%" do anual). */
    private var priceMicros by mutableStateOf<Map<String, Pair<Long, String>>>(emptyMap())
    /** O produto em compra, ou [RESTORING]; null = nada em andamento. */
    var busy by mutableStateOf<String?>(null)
        private set
    /** O resultado do último passo (texto em R.string). */
    var notice by mutableStateOf<Int?>(null)
        private set

    const val RESTORING = "restore"

    /** O AureaHyper ANUAL, o plano destacado (hyper::kHyperSubscriptionId). */
    fun hyperProduct(): String? = runCatching { AureaEngine.nativeHyperSubscriptionId() }.getOrNull()

    /** Tudo o que o app reconhece, para restaurar (hyper::subscription_product_ids). */
    fun productIds(): List<String> = runCatching { AureaEngine.nativeSubscriptionProductIds()?.toList() }.getOrNull().orEmpty()

    /** 1.0.9: os planos à venda na ordem da tela — semanal, mensal, anual (hyper::hyper_plans_on_sale). */
    fun plansOnSale(): List<String> = runCatching { AureaEngine.nativeHyperPlansOnSale()?.toList() }.getOrNull().orEmpty()

    /**
     * "Economize N%" do anual contra um ano do plano curto mais barato, só com os
     * preços da loja carregados e na mesma moeda (a conta é do motor). 0 = sem selo.
     */
    fun annualSavingsPercent(weekly: String?, monthly: String?, annual: String?): Int {
        val a = annual?.let { priceMicros[it] } ?: return 0
        val w = weekly?.let { priceMicros[it] }?.takeIf { it.second == a.second }
        val m = monthly?.let { priceMicros[it] }?.takeIf { it.second == a.second }
        if (w == null && m == null) return 0
        return runCatching { AureaEngine.nativeHyperAnnualSavingsPercent(w?.first ?: 0L, m?.first ?: 0L, a.first) }.getOrDefault(0)
    }

    fun clearNotice() { notice = null }

    /** Pede à Play os preços (uma vez por processo; repete se a primeira falhou). */
    fun loadPrices(context: Context) {
        if (pricesLoading || prices.isNotEmpty()) return
        val ids = productIds()
        if (ids.isEmpty()) return
        pricesLoading = true
        scope.launch {
            val billing = SubscriptionBilling(context)
            try {
                val details = billing.details(ids)
                val found = details.mapNotNull { d -> SubscriptionBilling.recurringPrice(d)?.let { d.productId to it } }.toMap()
                if (found.isNotEmpty()) {
                    priceMicros = details.mapNotNull { d -> SubscriptionBilling.recurringMicros(d)?.let { d.productId to it } }.toMap()
                    prices = found
                }
            } finally {
                billing.close()
                pricesLoading = false
            }
        }
    }

    /** Assina [product] pela Play e entrega ao servidor. */
    fun buy(activity: Activity, product: String) {
        if (busy != null) return
        val session = ContaViewModel.tokenGuardado(activity) ?: run { notice = R.string.assinatura_entrar; return }
        busy = product
        notice = null
        scope.launch {
            val billing = SubscriptionBilling(activity)
            try {
                // Já assinada nesta conta da loja (o app caiu antes de entregar): só entrega.
                val owned = billing.owned().firstOrNull { product in it.products }
                val purchase = owned ?: run {
                    val details = billing.details(listOf(product)).firstOrNull()
                    val offer = details?.let { SubscriptionBilling.baseOffer(it) }
                    if (details == null || offer == null) { notice = R.string.assinatura_indisponivel; return@launch }
                    when (val outcome = billing.buy(activity, details, offer.offerToken)) {
                        is BillingOutcome.Bought -> outcome.purchase
                        BillingOutcome.AlreadyOwned -> billing.owned().firstOrNull { product in it.products }
                        BillingOutcome.Cancelled -> return@launch
                        BillingOutcome.Failed -> { notice = R.string.assinatura_indisponivel; return@launch }
                    }
                }
                notice = when {
                    purchase == null -> R.string.assinatura_indisponivel
                    purchase.purchaseState != Purchase.PurchaseState.PURCHASED -> R.string.assinatura_pendente
                    else -> deliver(billing, session, purchase)
                }
            } finally {
                billing.close()
                busy = null
            }
        }
    }

    /**
     * Restaura as assinaturas desta conta da loja. [silent] = abertura do app ou
     * login: no máximo a cada 10 min, só manda ao servidor o que ainda não foi
     * reconhecido ou que a conta ainda não tem, e não mostra aviso.
     */
    fun restore(context: Context, silent: Boolean, retry: Boolean = false) {
        if (busy != null) return
        val session = ContaViewModel.tokenGuardado(context) ?: run { if (!silent) notice = R.string.assinatura_entrar; return }
        val now = System.currentTimeMillis()
        if (silent && !retry && lastRestoreSession == session && now - lastSilentRestore < SILENT_RESTORE_MS) return
        if (silent) {
            lastRestoreSession = session
            lastSilentRestore = now
        }
        val ours = productIds().toSet()
        if (ours.isEmpty()) return
        if (!silent) { busy = RESTORING; notice = null }
        scope.launch {
            val billing = SubscriptionBilling(context)
            try {
                val have = HyperEntitlements.ownedProducts()
                val purchases = billing.owned().filter { p -> p.products.any { it in ours } }
                    .filter { p -> !silent || !p.isAcknowledged || p.products.none { it in have } }
                var result: Int? = if (purchases.isEmpty()) R.string.assinatura_nada_restaurar else null
                for (p in purchases) {
                    val r = deliver(billing, session, p)
                    // O pior resultado fica (uma falha não some atrás de um sucesso).
                    if (result == null || result == R.string.assinatura_obrigado) result = r
                }
                if (!silent) notice = if (result == R.string.assinatura_obrigado) R.string.assinatura_restaurada else result
            } finally {
                billing.close()
                if (!silent) busy = null
            }
        }
    }

    /** Entrega ao servidor; com o OK, reconhece na Play. Devolve o aviso para a tela. */
    private suspend fun deliver(billing: SubscriptionBilling, session: String, purchase: Purchase): Int {
        val product = purchase.products.firstOrNull { it in productIds() } ?: return R.string.assinatura_invalida
        val r = withContext(Dispatchers.IO) { ContaApi.purchase(session, product, purchase.purchaseToken) }
        return when (r.status) {
            200 -> {
                val products = ServerEntitlementProvider.stringSet(r.corpo.optJSONArray("products"))
                if (product !in products) return R.string.assinatura_processando
                if (!HyperEntitlements.acceptServerProducts(session, products, ServerEntitlementProvider.expiresOf(r.corpo))) {
                    return R.string.assinatura_entrar
                }
                if (!purchase.isAcknowledged) runCatching { billing.acknowledge(purchase) }
                R.string.assinatura_obrigado
            }
            409 -> R.string.assinatura_outra_conta
            402 -> when (r.corpo.optString("error")) {
                "purchase_pending" -> R.string.assinatura_pendente
                "purchase_expired" -> R.string.assinatura_vencida
                else -> R.string.assinatura_invalida
            }
            401 -> R.string.assinatura_entrar
            else -> R.string.assinatura_indisponivel
        }
    }

}

/** O fim de um fluxo de compra da Play. */
internal sealed interface BillingOutcome {
    data class Bought(val purchase: Purchase?) : BillingOutcome
    data object AlreadyOwned : BillingOutcome
    data object Cancelled : BillingOutcome
    data object Failed : BillingOutcome
}

/** Um cliente da Play para assinaturas (SUBS), aberto por operação (como MuralBilling). */
internal class SubscriptionBilling(context: Context) : PurchasesUpdatedListener {
    private var waiting: CompletableDeferred<BillingOutcome>? = null
    private val client = BillingClient.newBuilder(context.applicationContext).setListener(this)
        .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build()).build()

    override fun onPurchasesUpdated(result: BillingResult, purchases: MutableList<Purchase>?) {
        val done = waiting ?: return
        waiting = null
        done.complete(when (result.responseCode) {
            BillingClient.BillingResponseCode.OK -> BillingOutcome.Bought(purchases?.firstOrNull())
            BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED -> BillingOutcome.AlreadyOwned
            BillingClient.BillingResponseCode.USER_CANCELED -> BillingOutcome.Cancelled
            else -> BillingOutcome.Failed
        })
    }

    private suspend fun ready(): Boolean {
        if (client.isReady) return true
        return suspendCancellableCoroutine { go ->
            client.startConnection(object : BillingClientStateListener {
                override fun onBillingSetupFinished(result: BillingResult) {
                    if (go.isActive) go.resume(result.responseCode == BillingClient.BillingResponseCode.OK)
                }
                override fun onBillingServiceDisconnected() {}
            })
        }
    }

    suspend fun details(ids: List<String>): List<ProductDetails> {
        if (ids.isEmpty() || !runCatching { ready() }.getOrDefault(false)) return emptyList()
        val products = ids.map {
            QueryProductDetailsParams.Product.newBuilder().setProductId(it).setProductType(BillingClient.ProductType.SUBS).build()
        }
        val params = QueryProductDetailsParams.newBuilder().setProductList(products).build()
        return runCatching { client.queryProductDetails(params).productDetailsList }.getOrNull().orEmpty()
    }

    /** Assinaturas pagas desta conta da loja (inclui as ainda não reconhecidas). */
    suspend fun owned(): List<Purchase> {
        if (!runCatching { ready() }.getOrDefault(false)) return emptyList()
        val params = QueryPurchasesParams.newBuilder().setProductType(BillingClient.ProductType.SUBS).build()
        return runCatching { client.queryPurchasesAsync(params).purchasesList }.getOrNull().orEmpty()
            .filter { it.purchaseState == Purchase.PurchaseState.PURCHASED }
    }

    suspend fun buy(activity: Activity, details: ProductDetails, offerToken: String): BillingOutcome {
        if (!runCatching { ready() }.getOrDefault(false)) return BillingOutcome.Failed
        val done = CompletableDeferred<BillingOutcome>()
        waiting = done
        val item = BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(details).setOfferToken(offerToken).build()
        val started = client.launchBillingFlow(activity, BillingFlowParams.newBuilder().setProductDetailsParamsList(listOf(item)).build())
        if (started.responseCode != BillingClient.BillingResponseCode.OK) {
            waiting = null
            return if (started.responseCode == BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED) BillingOutcome.AlreadyOwned
            else BillingOutcome.Failed
        }
        return done.await()
    }

    suspend fun acknowledge(purchase: Purchase) {
        client.acknowledgePurchase(AcknowledgePurchaseParams.newBuilder().setPurchaseToken(purchase.purchaseToken).build())
    }

    fun close() = client.endConnection()

    companion object {
        /**
         * A oferta do plano base (sem oferta promocional). Cada plano do AureaHyper é
         * um produto próprio na Play; o plano base dele se chama como o fim do id
         * ("aurea_hyper_semanal" → "semanal", "mensal", "anual"). Sem esse nome,
         * qualquer plano base; senão a primeira oferta.
         */
        fun baseOffer(details: ProductDetails): ProductDetails.SubscriptionOfferDetails? {
            val offers = details.subscriptionOfferDetails.orEmpty()
            val named = details.productId.substringAfterLast('_')
            return offers.firstOrNull { it.offerId == null && it.basePlanId == named }
                ?: offers.firstOrNull { it.offerId == null } ?: offers.firstOrNull()
        }

        /** O preço que se repete a cada período (a última fase do plano base), já localizado pela Play. */
        fun recurringPrice(details: ProductDetails): String? =
            baseOffer(details)?.pricingPhases?.pricingPhaseList?.lastOrNull()?.formattedPrice?.ifBlank { null }

        /** O mesmo preço em micros + o código da moeda. */
        fun recurringMicros(details: ProductDetails): Pair<Long, String>? =
            baseOffer(details)?.pricingPhases?.pricingPhaseList?.lastOrNull()
                ?.takeIf { it.priceAmountMicros > 0 }?.let { it.priceAmountMicros to it.priceCurrencyCode }
    }
}
