package com.aurea.aurea.captions
import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

class CaptionPresetStore(private val context: Context) {
    private val vault = KeyVault(context)
    private val root = File(context.filesDir, "caption-presets").apply { mkdirs() }
    private fun directory(mine: Boolean) = File(root, if (mine) "mine" else "downloaded").apply { mkdirs() }
    private fun api(path: String, body: JSONObject? = null, authenticated: Boolean = false): JSONObject {
        var token = ""
        if (authenticated) {
            token = vault.get("caption_community") ?: api("/session", JSONObject()).getString("token").also { vault.put("caption_community", it) }
        }
        val connection = URL("https://aurea-ai-discovery.aureaapp.workers.dev/api/captions$path").openConnection() as HttpURLConnection
        connection.connectTimeout = 15000; connection.readTimeout = 30000
        if (token.isNotEmpty()) connection.setRequestProperty("Authorization", "Bearer $token")
        try {
            if (body != null) {
                val bytes = body.toString().toByteArray(Charsets.UTF_8)
                require(bytes.size <= 132 * 1024) { "Preset muito grande" }
                connection.requestMethod = "POST"; connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json"); connection.setFixedLengthStreamingMode(bytes.size)
                connection.outputStream.use { it.write(bytes) }
            }
            val code = connection.responseCode
            if (code !in 200..299) throw IOException(if (code == 429) "Limite da comunidade atingido. Tente mais tarde." else "Comunidade indisponível (HTTP $code). Seus presets locais continuam disponíveis.")
            val bytes = connection.inputStream.use { it.readBytesLimited(5 * 1024 * 1024) }
            return JSONObject(bytes.toString(Charsets.UTF_8))
        } finally { connection.disconnect() }
    }
    fun local(mine: Boolean, search: String): List<JSONObject> = directory(mine).listFiles().orEmpty().filter { it.extension == "json" && it.length() <= 132 * 1024 }.mapNotNull { runCatching { JSONObject(it.readText()) }.getOrNull() }.filter { it.optString("name").contains(search, true) }.sortedByDescending { it.optLong("created") }
    fun community(search: String, popular: Boolean): List<JSONObject> {
        val result = api("/presets?sort=${if(popular) "popular" else "recent"}&q=${java.net.URLEncoder.encode(search, "UTF-8")}").getJSONArray("items")
        return List(result.length()) { result.getJSONObject(it) }
    }
    fun save(data: String): JSONObject {
        require(data.toByteArray().size <= 128 * 1024)
        val preset = JSONObject(data)
        val entry = JSONObject().put("id", UUID.randomUUID().toString()).put("name", preset.getString("name")).put("created", System.currentTimeMillis()).put("version", 1).put("preset", preset)
        write(entry, true); return entry
    }
    private fun write(entry: JSONObject, mine: Boolean) {
        val id = entry.getString("id"); require(id.matches(Regex("[a-f0-9-]{36}")))
        val target = File(directory(mine), "$id.json"); val temporary = File(directory(mine), "$id.part")
        temporary.writeText(entry.toString()); if (!temporary.renameTo(target)) throw IOException("Não foi possível guardar o preset")
    }
    fun download(entry: JSONObject): JSONObject {
        if (entry.has("preset")) return entry
        val id=entry.getString("id"); require(id.matches(Regex("[a-f0-9-]{36}")))
        val cached=File(directory(false), "$id.json")
        if(cached.exists()) { val old=JSONObject(cached.readText()); if(old.optInt("version")==entry.optInt("version"))return old }
        val received=api("/presets/$id/download", JSONObject(), true)
        val result=JSONObject(entry.toString()).put("preset",received.getJSONObject("preset"));write(result,false);return result
    }
    fun like(entry: JSONObject) { val id=entry.getString("id");require(id.matches(Regex("[a-f0-9-]{36}")));api("/presets/$id/like",JSONObject(),true) }
    fun publish(entry: JSONObject, author: String): JSONObject = api("/presets",JSONObject().put("id",entry.getString("id")).put("author",author).put("preset",entry.getJSONObject("preset")),true)
    private fun java.io.InputStream.readBytesLimited(limit: Int): ByteArray {
        val output=java.io.ByteArrayOutputStream();val bytes=ByteArray(8192)
        while(true){val n=read(bytes);if(n<0)break;if(output.size()+n>limit)throw IOException("Resposta grande demais");output.write(bytes,0,n)}
        return output.toByteArray()
    }
}
