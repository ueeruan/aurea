package com.aurea.aurea.community

import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.ConsumeParams
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import com.android.billingclient.api.consumePurchase
import com.android.billingclient.api.queryProductDetails
import com.android.billingclient.api.queryPurchasesAsync
import kotlinx.coroutines.CompletableDeferred
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URI
import kotlin.coroutines.resume
import kotlinx.coroutines.suspendCancellableCoroutine

internal data class MuralPost(val id: String, val caption: String, val width: Int, val height: Int, val durationMs: Long,
    val createdAt: Long, val mine: Boolean, val author: CommunityProfile)
internal data class MuralConfig(val posting: Boolean, val productId: String, val maxBytes: Long)
internal data class MuralPage(val items: List<MuralPost>, val cursor: String?)

/** O Mural no servidor (discovery/mural.js). Ver não precisa de conta; publicar, sim. */
internal class MuralApi(private val token: String,
    private val root: String = "https://aurea-ai-discovery.aureaapp.workers.dev/api/mural") {
    companion object {
        private val thumbs = object : LruCache<String, Bitmap>(8 * 1024 * 1024) {
            override fun sizeOf(key: String, value: Bitmap) = value.allocationByteCount
        }
    }
    fun videoUrl(id: String) = "$root/media/$id/video"

    private fun open(path: String, method: String) = (URI(root + path).toURL().openConnection() as HttpURLConnection).apply {
        requestMethod = method; connectTimeout = 15_000; readTimeout = 120_000; instanceFollowRedirects = false
        if (token.isNotEmpty()) setRequestProperty("Authorization", "Bearer $token")
        setRequestProperty("Accept", "application/json")
    }
    private fun answer(c: HttpURLConnection): JSONObject {
        val status = c.responseCode
        val text = (if (status in 200..299) c.inputStream else c.errorStream)?.use { it.readBytes().toString(Charsets.UTF_8) }.orEmpty()
        val value = runCatching { JSONObject(text) }.getOrElse { throw CommunityFailure("community_unavailable") }
        if (status !in 200..299) throw CommunityFailure(value.optString("error", "community_unavailable"))
        return value
    }
    private fun call(path: String, method: String = "GET", body: JSONObject? = null): JSONObject {
        val c = open(path, method)
        try {
            if (body != null) {
                val data = body.toString().toByteArray(Charsets.UTF_8)
                c.doOutput = true; c.setFixedLengthStreamingMode(data.size); c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(data) }
            }
            return answer(c)
        } finally { c.disconnect() }
    }
    fun config(): MuralConfig = call("/config").let {
        MuralConfig(it.optBoolean("posting"), it.optString("productId"), it.optLong("maxBytes"))
    }
    fun feed(cursor: String?): MuralPage {
        val page = call("/feed" + (cursor?.let { "?cursor=" + CommunityApi.query(it) } ?: ""))
        return MuralPage(page.optJSONArray("items").mapObjects { o ->
            MuralPost(o.getString("id"), o.optString("caption"), o.optInt("width"), o.optInt("height"), o.optLong("durationMs"),
                o.optLong("createdAt"), o.optBoolean("mine"), o.getJSONObject("author").profile())
        }, page.nextCursor())
    }
    /** O servidor confere a compra com a Play. Repetir devolve o mesmo crédito. */
    fun credit(purchaseToken: String): String =
        call("/credits", "POST", JSONObject().put("platform", "google").put("token", purchaseToken)).getString("id")
    fun create(credit: String, caption: String, bytes: Long, width: Int, height: Int, durationMs: Long): String =
        call("/posts", "POST", JSONObject().put("creditId", credit).put("caption", caption).put("bytes", bytes)
            .put("width", width).put("height", height).put("durationMs", durationMs)).getString("id")
    fun upload(id: String, kind: String, file: File, onProgress: (Float) -> Unit = {}) {
        val c = open("/posts/$id/$kind", "PUT")
        try {
            val total = file.length()
            c.doOutput = true; c.setFixedLengthStreamingMode(total)
            c.setRequestProperty("Content-Type", if (kind == "video") "video/mp4" else "image/jpeg")
            file.inputStream().use { input -> c.outputStream.use { out ->
                val buffer = ByteArray(64 * 1024); var sent = 0L
                while (true) { val n = input.read(buffer); if (n < 0) break; out.write(buffer, 0, n); sent += n
                    onProgress(sent.toFloat() / total) }
            } }
            answer(c)
        } finally { c.disconnect() }
    }
    fun publish(id: String) { call("/posts/$id/publish", "POST") }
    fun remove(id: String) { call("/posts/$id", "DELETE") }
    fun thumb(id: String): Bitmap? {
        thumbs.get(id)?.let { return it }
        val c = open("/media/$id/thumb", "GET")
        return try {
            if (c.responseCode != 200) null
            else c.inputStream.use { BitmapFactory.decodeStream(it) }?.also { thumbs.put(id, it) }
        } finally { c.disconnect() }
    }
}

/**
 * A compra de uma publicação na Google Play. O produto é consumível: cada post é uma
 * compra nova. Quem decide se pagou é o servidor; aqui só se pega o recibo.
 */
internal class MuralBilling(context: Context, private val productId: String) : PurchasesUpdatedListener {
    private var waiting: CompletableDeferred<Purchase?>? = null
    private val client = BillingClient.newBuilder(context.applicationContext).setListener(this)
        .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build()).build()

    override fun onPurchasesUpdated(result: BillingResult, purchases: MutableList<Purchase>?) {
        val done = waiting ?: return
        waiting = null
        done.complete(if (result.responseCode == BillingClient.BillingResponseCode.OK) purchases?.firstOrNull() else null)
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
    suspend fun details(): ProductDetails? {
        if (!ready()) return null
        val product = QueryProductDetailsParams.Product.newBuilder().setProductId(productId)
            .setProductType(BillingClient.ProductType.INAPP).build()
        val params = QueryProductDetailsParams.newBuilder().setProductList(listOf(product)).build()
        return client.queryProductDetails(params).productDetailsList?.firstOrNull()
    }
    /** Compra paga e ainda não consumida: o app caiu antes de entregar. */
    suspend fun unconsumed(): Purchase? {
        if (!ready()) return null
        val params = QueryPurchasesParams.newBuilder().setProductType(BillingClient.ProductType.INAPP).build()
        return client.queryPurchasesAsync(params).purchasesList.firstOrNull {
            it.purchaseState == Purchase.PurchaseState.PURCHASED && productId in it.products
        }
    }
    suspend fun buy(activity: Activity, details: ProductDetails): Purchase? {
        val done = CompletableDeferred<Purchase?>()
        waiting = done
        val item = BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(details).build()
        val started = client.launchBillingFlow(activity, BillingFlowParams.newBuilder().setProductDetailsParamsList(listOf(item)).build())
        if (started.responseCode != BillingClient.BillingResponseCode.OK) { waiting = null; return null }
        return done.await()
    }
    suspend fun consume(purchase: Purchase) {
        client.consumePurchase(ConsumeParams.newBuilder().setPurchaseToken(purchase.purchaseToken).build())
    }
    fun close() { client.endConnection() }
}
