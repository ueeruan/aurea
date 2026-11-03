package com.aurea.aurea.assistant

import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Um modelo gratuito do OpenRouter que aceita ferramentas (tool calling). */
data class AssistantModel(val id: String, val name: String, val context: Int, val vision: Boolean)

/**
 * 0.4.6: cliente mínimo do OpenRouter (API compatível com a da OpenAI).
 * A chave é do usuário (colada no app, guardada no KeyVault); nada passa por
 * servidor do Aurea. Só modelos GRATUITOS com ferramentas: a lista vem viva de
 * `/api/v1/models` (os gratuitos mudam toda semana) e os melhores vão primeiro.
 */
object OpenRouter {
    private const val BASE = "https://openrouter.ai/api/v1"

    /** Ordem de preferência (os mais capazes para agir no editor); o resto vem depois, pelo contexto. */
    val preferred = listOf(
        "thinkingmachines/inkling:free",
        "nvidia/nemotron-3-ultra-550b-a55b:free",
        "google/gemma-4-31b-it:free",
        "nvidia/nemotron-3.5-super-vl:free",
        "dots-studio/dots-3-note-preview:free",
        "openrouter/free",
    )

    class Failure(val code: Int, message: String) : IOException(message)

    private fun request(method: String, path: String, key: String?, body: JSONObject? = null, timeoutMs: Int = 90_000): JSONObject {
        val c = (URL(BASE + path).openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = 15_000
            readTimeout = timeoutMs
            setRequestProperty("Accept", "application/json")
            // Identificação do app para o OpenRouter (ranking de apps); não leva dado do usuário.
            setRequestProperty("HTTP-Referer", "https://aurea.app")
            setRequestProperty("X-Title", "AUREA")
            if (key != null) setRequestProperty("Authorization", "Bearer $key")
            if (body != null) { doOutput = true; setRequestProperty("Content-Type", "application/json") }
        }
        try {
            if (body != null) c.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            val text = (if (code in 200..299) c.inputStream else c.errorStream)?.use { it.readBytes().toString(Charsets.UTF_8) }.orEmpty()
            if (code !in 200..299) {
                val msg = runCatching { JSONObject(text).optJSONObject("error")?.optString("message") }.getOrNull()
                throw Failure(code, msg?.takeIf { it.isNotBlank() } ?: "HTTP $code")
            }
            return JSONObject(text)
        } finally { c.disconnect() }
    }

    /** A chave vale? (GET /key responde 401 para chave errada). */
    fun validate(key: String): Boolean = runCatching { request("GET", "/key", key, timeoutMs = 20_000); true }.getOrElse {
        if (it is Failure && (it.code == 401 || it.code == 403)) false else throw it
    }

    /** Os gratuitos com ferramentas, os preferidos primeiro. Modelos "stealth" ficam de fora (guardam os prompts). */
    fun freeToolModels(): List<AssistantModel> {
        val data = request("GET", "/models", null, timeoutMs = 30_000).optJSONArray("data") ?: return emptyList()
        val out = ArrayList<AssistantModel>()
        for (i in 0 until data.length()) {
            val m = data.optJSONObject(i) ?: continue
            val id = m.optString("id")
            val pricing = m.optJSONObject("pricing")
            val free = id.endsWith(":free") || id == "openrouter/free" ||
                (pricing?.optString("prompt") == "0" && pricing.optString("completion") == "0")
            val params = m.optJSONArray("supported_parameters")
            val tools = params != null && (0 until params.length()).any { params.optString(it) == "tools" }
            val output = m.optJSONObject("architecture")?.optJSONArray("output_modalities")
            val textOut = output == null || (0 until output.length()).any { output.optString(it) == "text" }
            if (!free || !tools || !textOut || id.startsWith("stealth/")) continue
            val input = m.optJSONObject("architecture")?.optJSONArray("input_modalities")
            val vision = input != null && (0 until input.length()).any { input.optString(it) == "image" }
            out += AssistantModel(id, m.optString("name", id).removeSuffix(" (free)"), m.optInt("context_length"), vision)
        }
        return out.sortedWith(compareBy<AssistantModel> { preferred.indexOf(it.id).let { p -> if (p < 0) Int.MAX_VALUE else p } }.thenByDescending { it.context })
    }

    /** Uma volta do chat: devolve a mensagem do assistente (texto e/ou tool_calls). */
    fun complete(key: String, model: String, messages: JSONArray, tools: JSONArray): JSONObject {
        val body = JSONObject().put("model", model).put("messages", messages).put("tools", tools)
            .put("tool_choice", "auto").put("temperature", 0.3)
        val r = request("POST", "/chat/completions", key, body)
        r.optJSONObject("error")?.let { throw Failure(it.optInt("code", 500), it.optString("message", "erro")) }
        return r.optJSONArray("choices")?.optJSONObject(0)?.optJSONObject("message") ?: throw Failure(500, "resposta vazia")
    }
}
