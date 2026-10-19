package com.aurea.aurea.community

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import org.json.JSONObject
import org.json.JSONArray
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder

internal data class CommunityProfile(val id: String, val username: String, val name: String, val bio: String = "",
    val avatar: String? = null, val verification: String = "", val followers: Int = 0, val following: Int = 0,
    val posts: Int = 0, val followed: Boolean = false, val blocked: Boolean = false)
internal data class CommunityAsset(val id: String, val kind: String, val name: String, val bytes: Long)
internal data class CommunityPost(val id: String, val body: String, val createdAt: Long, val author: CommunityProfile,
    val asset: CommunityAsset?, val likes: Int, val comments: Int, val liked: Boolean)
internal data class CommunityComment(val id: String, val body: String, val createdAt: Long, val author: CommunityProfile)
internal class CommunityFailure(val code: String) : IOException(code)
internal fun JSONObject.profile() = CommunityProfile(getString("id"), optString("username"), optString("name"),
    optString("bio"), optString("avatar").takeIf { it.isNotBlank() && it != "null" }, optString("verification"),
    optInt("followers"), optInt("following"), optInt("posts"), optBoolean("followed"), optBoolean("blocked"))
internal fun JSONObject.asset() = CommunityAsset(getString("id"), getString("kind"), getString("name"), getLong("bytes"))
internal fun JSONObject.post() = CommunityPost(getString("id"), optString("body"), getLong("createdAt"),
    getJSONObject("author").profile(), optJSONObject("asset")?.asset(), optInt("likes"), optInt("comments"), optBoolean("liked"))
internal fun JSONObject.comment() = CommunityComment(getString("id"), getString("body"), getLong("createdAt"), getJSONObject("author").profile())
internal fun <T> JSONArray?.mapObjects(transform: (JSONObject) -> T): List<T> = if (this == null) emptyList() else (0 until length()).map { transform(getJSONObject(it)) }
internal fun JSONObject.nextCursor(): String? = optString("cursor").takeIf { it.isNotEmpty() && it != "null" }

// Denúncias (Google Play: conteúdo gerado pelo usuário e por IA). Os motivos e os
// alvos são os mesmos que o servidor aceita; o que não passa aqui ele recusa.
internal val reportReasons = listOf("spam", "nudity", "violence", "hate", "harassment", "illegal", "copyright", "other")
internal const val REPORT_DETAILS_MAX = 500
private val reportKinds = setOf("post", "comment", "user", "ai_video")
/** Post, comentário e perfil são UUID; o vídeo de IA é o id opaco do job. */
internal fun isReportTarget(kind: String, id: String): Boolean =
    kind in reportKinds && id.matches(if (kind == "ai_video") Regex("[A-Za-z0-9_.:-]{1,80}") else Regex("[0-9a-f-]{36}"))
/** Corta em [REPORT_DETAILS_MAX] sem partir um emoji (par substituto) ao meio. */
internal fun String.clipReportDetails(): String =
    if (length <= REPORT_DETAILS_MAX) this else take(if (this[REPORT_DETAILS_MAX - 1].isHighSurrogate()) REPORT_DETAILS_MAX - 1 else REPORT_DETAILS_MAX)
/** Corpo comum às denúncias da comunidade e dos presets de legenda; detalhe vazio não vai. */
internal fun reportBody(reason: String, details: String): JSONObject {
    require(reason in reportReasons) { "invalid_report" }
    return JSONObject().put("reason", reason).apply { details.trim().clipReportDetails().takeIf { it.isNotEmpty() }?.let { put("details", it) } }
}

private fun InputStream.readBounded(limit: Int): ByteArray {
    val out = ByteArrayOutputStream(); val buffer = ByteArray(8192)
    while (out.size() <= limit) { val n = read(buffer, 0, minOf(buffer.size, limit + 1 - out.size())); if (n < 0) break; out.write(buffer, 0, n) }
    return out.toByteArray()
}

internal open class CommunityApi(private val token: String,
    private val root: String = "https://aurea-ai-discovery.aureaapp.workers.dev/api/community") {
    companion object : com.aurea.aurea.engine.TrimmableImageCache {
        init { com.aurea.aurea.engine.UiImageCaches.register(this) }
        override fun releaseImages() = avatars.evictAll()
        fun query(value: String): String = URLEncoder.encode(value, "UTF-8")
        private val avatars = object : LruCache<String, Bitmap>(4 * 1024 * 1024) {
            override fun sizeOf(key: String, value: Bitmap) = value.allocationByteCount
        }
    }
    private fun open(path: String, method: String): HttpURLConnection = (URI(root + path).toURL().openConnection() as HttpURLConnection).apply {
        requestMethod = method; connectTimeout = 15_000; readTimeout = 60_000; instanceFollowRedirects = false
        setRequestProperty("Authorization", "Bearer $token"); setRequestProperty("Accept", "application/json")
    }
    private fun response(c: HttpURLConnection): JSONObject {
        val status = c.responseCode
        val stream = if (status in 200..299) c.inputStream else c.errorStream
        val data = stream?.use { it.readBounded(1024 * 1024) } ?: byteArrayOf()
        if (data.size > 1024 * 1024) throw CommunityFailure("invalid_response")
        val value = runCatching { JSONObject(data.toString(Charsets.UTF_8)) }.getOrElse { throw CommunityFailure("community_unavailable") }
        if (status !in 200..299) throw CommunityFailure(value.optString("error", "community_unavailable"))
        return value
    }
    open fun request(path: String, method: String = "GET", body: JSONObject? = null): JSONObject {
        val c = open(path, method)
        try {
            if (body != null) {
                val data = body.toString().toByteArray(Charsets.UTF_8)
                c.doOutput = true; c.setFixedLengthStreamingMode(data.size); c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(data) }
            }
            return response(c)
        } finally { c.disconnect() }
    }
    /** 201 (nova) e 200 (já denunciado por esta conta) são sucesso para quem denuncia. */
    fun report(kind: String, id: String, reason: String, details: String = "") {
        if (!isReportTarget(kind, id)) throw CommunityFailure("invalid_report")
        request("/reports", "POST", reportBody(reason, details).put("kind", kind).put("id", id))
    }
    /** Bloquear também desfaz o seguir nos dois sentidos; o servidor esconde o resto. */
    fun block(id: String, blocked: Boolean): CommunityProfile =
        request("/profiles/$id/block", if (blocked) "PUT" else "DELETE").getJSONObject("profile").profile()
    fun upload(file: File, kind: String, name: String = file.name): CommunityAsset {
        val max = if (kind == "avatar") 512L * 1024 else if (kind == "preset") 1024L * 1024 else 50L * 1024 * 1024
        if (file.length() !in 1..max) throw CommunityFailure("file_too_large")
        val c = open("/assets?kind=$kind&name=${query(name)}", "POST")
        try {
            c.doOutput = true; c.setFixedLengthStreamingMode(file.length()); c.setRequestProperty("Content-Type", "application/octet-stream")
            file.inputStream().use { input -> c.outputStream.use { input.copyTo(it) } }
            return response(c).asset()
        } finally { c.disconnect() }
    }
    fun download(asset: CommunityAsset, target: File) {
        if (asset.bytes !in 1..50L * 1024 * 1024 || !asset.id.matches(Regex("[a-f0-9-]{36}"))) throw CommunityFailure("invalid_file")
        val c = open("/assets/${asset.id}", "GET")
        try {
            if (c.responseCode != 200) throw CommunityFailure("community_unavailable")
            c.inputStream.use { input -> target.outputStream().use { out ->
                val buf = ByteArray(64 * 1024); var total = 0L
                while (true) { val n = input.read(buf); if (n < 0) break; total += n
                    if (total > asset.bytes) throw CommunityFailure("invalid_file"); out.write(buf, 0, n) }
                if (total != asset.bytes) throw CommunityFailure("invalid_file")
            } }
        } catch (e: Exception) { target.delete(); throw e } finally { c.disconnect() }
    }
    fun avatar(id: String): Bitmap? {
        avatars.get(id)?.let { return it }
        if (!id.matches(Regex("[a-f0-9-]{36}"))) return null
        val c = open("/assets/$id", "GET")
        return try {
            if (c.responseCode != 200) return null
            val data = c.inputStream.use { it.readBounded(512 * 1024) }
            if (data.size > 512 * 1024) return null
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(data, 0, data.size, bounds)
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
            val sample = com.aurea.aurea.engine.ImageMemoryPolicy.sampleSize(bounds.outWidth, bounds.outHeight, 384, 384L * 384) ?: return null
            val opts = BitmapFactory.Options().apply { inSampleSize = sample }
            BitmapFactory.decodeByteArray(data, 0, data.size, opts)?.also { avatars.put(id, it) }
        } catch (_: OutOfMemoryError) {
            avatars.evictAll(); null
        } finally { c.disconnect() }
    }
}
