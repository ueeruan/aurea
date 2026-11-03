package com.aurea.aurea.assistant

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.captions.KeyVault
import com.aurea.aurea.engine.AureaEngine
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/** Uma linha do chat na tela. `tool` = ação feita no editor (aparece como um chip). */
data class ChatLine(val role: Role, val text: String, val tool: String? = null, val failed: Boolean = false) {
    enum class Role { User, Assistant, Tool, Error }
}

/**
 * 0.4.6: o assistente de IA do editor. A conversa vai para o OpenRouter com a
 * chave do usuário; as ferramentas e as regras são do motor
 * (`Engine::assistant_call`). Cada pedido roda até 8 voltas de ferramentas;
 * limite/erro de um modelo gratuito passa para o próximo da lista.
 */
class AssistantChat(
    context: Context,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    /** Ferramentas da interface (o motor devolve {"ui":"open_panel","panel":…}). */
    private val onUi: (panel: String) -> Unit,
    /** O projeto mudou: a interface relê camadas, seleção, cabeçote. */
    private val onModelChanged: () -> Unit,
) {
    private val app = context.applicationContext
    private val vault = KeyVault(app)
    private val prefs = app.getSharedPreferences("aurea.assistant", Context.MODE_PRIVATE)

    var hasKey by mutableStateOf(vault.has(KEY))
        private set
    var models by mutableStateOf<List<AssistantModel>>(emptyList())
        private set
    var model by mutableStateOf(prefs.getString("model", null) ?: OpenRouter.preferred.first())
        private set
    var busy by mutableStateOf(false)
        private set
    var status by mutableStateOf<String?>(null)
    var keyError by mutableStateOf<String?>(null)
        private set
    val lines = mutableStateListOf<ChatLine>()

    private val history = JSONArray()
    private var job: Job? = null
    private val tools by lazy { JSONArray(engine.assistantTools()) }

    private fun systemMessage(): JSONObject {
        val language = app.resources.configuration.locales[0].toLanguageTag()
        return JSONObject().put("role", "system").put("content", engine.assistantSystemPrompt(language, BuildConfig.VERSION_NAME))
    }

    /** Lista viva dos gratuitos com ferramentas (uma vez por abertura). */
    fun loadModels() {
        if (models.isNotEmpty()) return
        scope.launch {
            val list = withContext(Dispatchers.IO) { runCatching { OpenRouter.freeToolModels() }.getOrDefault(emptyList()) }
            models = list
            if (list.isNotEmpty() && list.none { it.id == model }) chooseModel(list.first().id)
        }
    }

    fun chooseModel(id: String) {
        model = id
        prefs.edit().putString("model", id).apply()
    }

    /** Cola a chave: valida no OpenRouter antes de guardar (só no aparelho). */
    fun submitKey(raw: String, invalid: String, offline: String) {
        val key = raw.trim()
        if (key.isEmpty() || busy) return
        busy = true; keyError = null
        scope.launch {
            val result = withContext(Dispatchers.IO) { runCatching { OpenRouter.validate(key) } }
            busy = false
            result.onSuccess { valid ->
                if (valid) { vault.put(KEY, key); hasKey = true; loadModels() } else keyError = invalid
            }.onFailure { keyError = offline }
        }
    }

    fun forgetKey() {
        vault.remove(KEY)
        hasKey = false
        clear()
    }

    fun clear() {
        job?.cancel()
        lines.clear()
        while (history.length() > 0) history.remove(history.length() - 1)
        busy = false; status = null
    }

    fun stop() { job?.cancel(); busy = false; status = null }

    /** Um pedido do usuário: conversa + ferramentas até o modelo responder com texto. */
    fun send(text: String, labels: Labels) {
        val t = text.trim()
        if (t.isEmpty() || busy) return
        val key = vault.get(KEY) ?: run { hasKey = false; return }
        if (history.length() == 0) history.put(systemMessage())
        history.put(JSONObject().put("role", "user").put("content", t))
        lines += ChatLine(ChatLine.Role.User, t)
        busy = true; status = labels.thinking
        job = scope.launch {
            try {
                var rounds = 0
                while (rounds++ < MAX_ROUNDS) {
                    val reply = completeWithFallback(key, labels)
                    history.put(reply)
                    val calls = reply.optJSONArray("tool_calls")
                    val content = reply.optString("content").takeIf { it.isNotBlank() && it != "null" }
                    if (calls == null || calls.length() == 0) {
                        lines += ChatLine(ChatLine.Role.Assistant, content ?: labels.done)
                        break
                    }
                    if (content != null) lines += ChatLine(ChatLine.Role.Assistant, content)
                    for (i in 0 until calls.length()) {
                        val call = calls.getJSONObject(i)
                        val fn = call.optJSONObject("function") ?: continue
                        val name = fn.optString("name")
                        status = labels.tool(name)
                        val args = fn.optString("arguments").ifBlank { "{}" }
                        val result = withContext(Dispatchers.IO) { engine.assistantCall(name, args) }
                        val parsed = runCatching { JSONObject(result) }.getOrNull()
                        val good = parsed?.optBoolean("ok") == true
                        parsed?.optString("ui")?.takeIf { it == "open_panel" }?.let { onUi(parsed.optString("panel")) }
                        if (name != "get_project_state" && name != "list_ae_effects") onModelChanged()
                        lines += ChatLine(ChatLine.Role.Tool, labels.tool(name), tool = name, failed = !good)
                        history.put(JSONObject().put("role", "tool").put("tool_call_id", call.optString("id"))
                            .put("name", name).put("content", result.take(MAX_TOOL_RESULT)))
                    }
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (failure: Exception) {
                val message = (failure as? OpenRouter.Failure)?.let { f ->
                    if (f.code == 401 || f.code == 403) { vault.remove(KEY); hasKey = false; labels.keyRejected } else f.message
                } ?: labels.offline
                lines += ChatLine(ChatLine.Role.Error, message ?: labels.offline)
            } finally {
                busy = false; status = null
            }
        }
    }

    /** Modelo gratuito com limite ou fora do ar: tenta os próximos (até 3), avisando qual ficou. */
    private suspend fun completeWithFallback(key: String, labels: Labels): JSONObject {
        val order = (listOf(model) + models.map { it.id } + OpenRouter.preferred.last()).distinct()
        var last: Exception? = null
        for ((attempt, id) in order.take(4).withIndex()) {
            try {
                val reply = withContext(Dispatchers.IO) { OpenRouter.complete(key, id, history, tools) }
                if (attempt > 0) { chooseModel(id); lines += ChatLine(ChatLine.Role.Tool, labels.switched(models.firstOrNull { it.id == id }?.name ?: id)) }
                return reply
            } catch (f: OpenRouter.Failure) {
                if (f.code == 401 || f.code == 403) throw f
                last = f
            }
        }
        throw last ?: OpenRouter.Failure(500, labels.offline)
    }

    /** Textos da interface (o idioma do app), passados pela tela. */
    class Labels(
        val thinking: String, val done: String, val offline: String, val keyRejected: String,
        val tool: (String) -> String, val switched: (String) -> String,
    )

    private companion object {
        const val KEY = "openrouter_api_key"
        const val MAX_ROUNDS = 8
        const val MAX_TOOL_RESULT = 12_000
    }
}
