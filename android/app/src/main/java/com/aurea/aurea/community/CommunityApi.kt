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
    val posts: Int = 0, val followed: Boolean = false)
internal data class CommunityAsset(val id: String, val kind: String, val name: String, val bytes: Long)
internal data class CommunityPost(val id: String, val body: String, val createdAt: Long, val author: CommunityProfile,
    val asset: CommunityAsset?, val likes: Int, val comments: Int, val liked: Boolean)
internal data class CommunityComment(val id: String, val body: String, val createdAt: Long, val author: CommunityProfile)
internal class CommunityFailure(val code: String) : IOException(code)
internal fun JSONObject.profile() = CommunityProfile(getString("id"), optString("username"), optString("name"),
    optString("bio"), optString("avatar").takeIf { it.isNotBlank() && it != "null" }, optString("verification"),
    optInt("followers"), optInt("following"), optInt("posts"), optBoolean("followed"))
internal fun JSONObject.asset() = CommunityAsset(getString("id"), getString("kind"), getString("name"), getLong("bytes"))
internal fun JSONObject.post() = CommunityPost(getString("id"), optString("body"), getLong("createdAt"),
    getJSONObject("author").profile(), optJSONObject("asset")?.asset(), optInt("likes"), optInt("comments"), optBoolean("liked"))
internal fun JSONObject.comment() = CommunityComment(getString("id"), getString("body"), getLong("createdAt"), getJSONObject("author").profile())
internal fun <T> JSONArray?.mapObjects(transform: (JSONObject) -> T): List<T> = if (this == null) emptyList() else (0 until length()).map { transform(getJSONObject(it)) }
internal fun JSONObject.nextCursor(): String? = optString("cursor").takeIf { it.isNotEmpty() && it != "null" }

private fun InputStream.readBounded(limit: Int): ByteArray {
    val out = ByteArrayOutputStream(); val buffer = ByteArray(8192)
    while (out.size() <= limit) { val n = read(buffer, 0, minOf(buffer.size, limit + 1 - out.size())); if (n < 0) break; out.write(buffer, 0, n) }
    return out.toByteArray()
}

internal open class CommunityApi(private val token: String,
    private val root: String = "https://aurea-ai-discovery.aureaapp.workers.dev/api/community") {
    companion object {
        fun query(value: String): String = URLEncoder.encode(value, "UTF-8")
        private val avatars = LruCache<String, Bitmap>(64)
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
            val opts = BitmapFactory.Options().apply { inSampleSize = (maxOf(bounds.outWidth, bounds.outHeight) / 192).coerceAtLeast(1) }
            BitmapFactory.decodeByteArray(data, 0, data.size, opts)?.also { avatars.put(id, it) }
        } finally { c.disconnect() }
    }
}
