// Play Billing no PC: a Google Play não existe no Windows. O cliente responde
// "indisponível", e o Mural mostra que a compra não está disponível aqui.
@file:Suppress("unused", "UNUSED_PARAMETER")

package com.android.billingclient.api

import android.app.Activity
import android.content.Context

class BillingResult(val responseCode: Int = BillingClient.BillingResponseCode.BILLING_UNAVAILABLE, val debugMessage: String = "pc")

fun interface PurchasesUpdatedListener { fun onPurchasesUpdated(result: BillingResult, purchases: MutableList<Purchase>?) }

interface BillingClientStateListener {
    fun onBillingSetupFinished(result: BillingResult)
    fun onBillingServiceDisconnected()
}

class PendingPurchasesParams private constructor() {
    class Builder { fun enableOneTimeProducts() = this; fun enablePrepaidPlans() = this; fun build() = PendingPurchasesParams() }
    companion object { @JvmStatic fun newBuilder() = Builder() }
}

class Purchase private constructor() {
    val purchaseToken: String get() = ""
    val purchaseState: Int get() = PurchaseState.UNSPECIFIED_STATE
    val products: List<String> get() = emptyList()
    val orderId: String? get() = null
    val isAcknowledged: Boolean get() = false
    object PurchaseState { const val UNSPECIFIED_STATE = 0; const val PURCHASED = 1; const val PENDING = 2 }
}

class ProductDetails private constructor() {
    val productId: String get() = ""
    val oneTimePurchaseOfferDetails: OneTimePurchaseOfferDetails? get() = null
    class OneTimePurchaseOfferDetails private constructor() { val formattedPrice: String get() = "" }
}

class ProductDetailsResult(val billingResult: BillingResult, val productDetailsList: List<ProductDetails>?)
class PurchasesResult(val billingResult: BillingResult, val purchasesList: List<Purchase>)
class ConsumeResult(val billingResult: BillingResult, val purchaseToken: String?)

class QueryProductDetailsParams private constructor() {
    class Product private constructor() {
        class Builder { fun setProductId(id: String) = this; fun setProductType(t: String) = this; fun build() = Product() }
        companion object { @JvmStatic fun newBuilder() = Builder() }
    }
    class Builder { fun setProductList(list: List<Product>) = this; fun build() = QueryProductDetailsParams() }
    companion object { @JvmStatic fun newBuilder() = Builder() }
}

class QueryPurchasesParams private constructor() {
    class Builder { fun setProductType(t: String) = this; fun build() = QueryPurchasesParams() }
    companion object { @JvmStatic fun newBuilder() = Builder() }
}

class ConsumeParams private constructor() {
    class Builder { fun setPurchaseToken(t: String) = this; fun build() = ConsumeParams() }
    companion object { @JvmStatic fun newBuilder() = Builder() }
}

class BillingFlowParams private constructor() {
    class ProductDetailsParams private constructor() {
        class Builder { fun setProductDetails(d: ProductDetails) = this; fun setOfferToken(t: String) = this; fun build() = ProductDetailsParams() }
        companion object { @JvmStatic fun newBuilder() = Builder() }
    }
    class Builder { fun setProductDetailsParamsList(l: List<ProductDetailsParams>) = this; fun build() = BillingFlowParams() }
    companion object { @JvmStatic fun newBuilder() = Builder() }
}

class BillingClient private constructor() {
    val isReady: Boolean get() = false
    fun startConnection(listener: BillingClientStateListener) = listener.onBillingSetupFinished(BillingResult())
    fun endConnection() {}
    fun launchBillingFlow(activity: Activity, params: BillingFlowParams): BillingResult = BillingResult()

    class Builder { fun setListener(l: PurchasesUpdatedListener) = this; fun enablePendingPurchases(p: PendingPurchasesParams) = this; fun enablePendingPurchases() = this; fun build() = BillingClient() }
    object BillingResponseCode { const val OK = 0; const val USER_CANCELED = 1; const val SERVICE_UNAVAILABLE = 2; const val BILLING_UNAVAILABLE = 3; const val ITEM_ALREADY_OWNED = 7; const val ERROR = 6 }
    object ProductType { const val INAPP = "inapp"; const val SUBS = "subs" }
    companion object { @JvmStatic fun newBuilder(context: Context) = Builder() }
}

suspend fun BillingClient.queryProductDetails(params: QueryProductDetailsParams) = ProductDetailsResult(BillingResult(), null)
suspend fun BillingClient.queryPurchasesAsync(params: QueryPurchasesParams) = PurchasesResult(BillingResult(), emptyList())
suspend fun BillingClient.consumePurchase(params: ConsumeParams) = ConsumeResult(BillingResult(), null)
