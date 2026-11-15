package com.aurea.aurea.mcp

import android.app.Activity
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.conta.ContaViewModel
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.security.SecureRandom
import java.util.Base64

/**
 * true = o botão do editor abre o chat antigo (0.4.6); false = abre o AureaMCP.
 * Fica true até o Worker com o /mcp estar no ar (sem ele o AureaMCP só mostra
 * "Sem conexão"); depois vira false, e o chat sai quando o dono confirmar.
 */
object AureaMcpFlags {
    @JvmField @Volatile var legacyChat: Boolean = false
}

/**
 * AureaMCP: o app conectado aos agentes de IA da pessoa (Claude, ChatGPT,
 * Codex...) pelo relay do Worker. Ver docs/aureamcp/DESIGN.md.
 *
 * - Conexão: WebSocket em /api/mcp/app/socket com a sessão da conta; reconecta
 *   sozinho (1 s → 60 s), "ping" a cada 25 s, "focus" quando o app volta.
 * - Cada chamada passa pelo [McpExecutor]: permissões, confirmação, motor/host.
 * - Pareamento (código que o Claude.ai/ChatGPT mostra) e tokens pessoais
 *   (Claude Code/Codex) pelas rotas /api/mcp/... com a mesma sessão.
 * Par do iOS: engine/platform/ios/app/AureaMcp.swift.
 */
class AureaMcp(
    context: Context,
    private val scope: CoroutineScope,
    engineTools: () -> String,
    engineCall: (String, String) -> String,
    private val host: McpHost,
    onUi: (String) -> Unit,
    onModelChanged: () -> Unit,
) {
    enum class Status { Off, NoAccount, Connecting, Online, Offline }
    data class Agent(val name: String, val version: String, val seen: Long, val tokenId: String?)
    data class LogLine(val at: Long, val agent: String, val tool: String?, val ok: Boolean, val ms: Long, val event: String?)
    data class Grant(val id: String, val kind: String, val name: String, val createdAt: Long, val lastUsedAt: Long?, val expiresAt: Long?)
    data class NewToken(val token: String, val url: String)
    data class PairRequest(val code: String, val client: String, val redirectHost: String)
    class Confirm(val tool: String, val agent: String, val detail: String, val answer: CompletableDeferred<Boolean>)

    private val app = context.applicationContext
    private val prefs = app.getSharedPreferences("aurea.mcp", Context.MODE_PRIVATE)
    private val engineToolsJson = engineTools

    var enabled by mutableStateOf(prefs.getBoolean(KEY_ENABLED, false))
        private set

    init { if (enabled) active = true }
    var status by mutableStateOf(if (enabled) Status.Connecting else Status.Off)
        private set
    var paused by mutableStateOf(false)
        private set
    var agents by mutableStateOf<List<Agent>>(emptyList())
        private set
    var grants by mutableStateOf<List<Grant>>(emptyList())
        private set
    var confirm by mutableStateOf<Confirm?>(null)
        private set
    var askEveryEdit by mutableStateOf(prefs.getBoolean(KEY_ASK, false))
        private set
    /** Ação de rede em andamento (pareamento, token): a tela mostra o progresso. */
    var busy by mutableStateOf(false)
        private set
    val log = mutableStateListOf<LogLine>()
    val permissions = mutableStateMapOf<String, Boolean>().apply {
        for (c in McpCatalog.CAPABILITIES) put(c, prefs.getBoolean(KEY_PERM + c, McpCatalog.DEFAULTS[c] ?: true))
    }

    private val deviceId: String = prefs.getString(KEY_DEVICE, null) ?: ByteArray(16).also { SecureRandom().nextBytes(it) }
        .let { Base64.getUrlEncoder().withoutPadding().encodeToString(it) }.also { prefs.edit().putString(KEY_DEVICE, it).apply() }

    private var loop: Job? = null
    @Volatile private var socket: McpSocket? = null
    private val running = HashMap<String, Job>()

    private val executor = McpExecutor(
        engineCall = { name, args -> withContext(Dispatchers.IO) { engineCall(name, args) } },
        host = host,
        allowed = { cap -> permissions[cap] == true },
        askEveryEdit = { askEveryEdit },
        confirm = ::askPerson,
        onUi = onUi,
        onModelChanged = onModelChanged,
    )

    // --- liga / desliga ------------------------------------------------------------

    fun changeEnabled(on: Boolean) {
        enabled = on
        active = on
        prefs.edit().putBoolean(KEY_ENABLED, on).apply()
        if (on) start() else stop()
    }

    /** Chamado na abertura do app e ao voltar ao primeiro plano. */
    fun start() {
        if (!enabled) { status = Status.Off; return }
        if (loop?.isActive == true) { send(JSONObject().put("t", "focus")); return }
        loop = scope.launch { connectLoop() }
    }

    fun stop() {
        loop?.cancel(); loop = null
        socket?.close(); socket = null
        running.values.forEach { it.cancel() }; running.clear()
        confirm?.answer?.complete(false)
        status = Status.Off
    }

    fun setPermission(capability: String, on: Boolean) {
        permissions[capability] = on
        prefs.edit().putBoolean(KEY_PERM + capability, on).apply()
        send(JSONObject().put("t", "state").put("permissions", permissionsJson()))
    }

    fun changeAskEveryEdit(on: Boolean) {
        askEveryEdit = on
        prefs.edit().putBoolean(KEY_ASK, on).apply()
    }

    /** Stop: nenhum agente age até [resume]; as chamadas em andamento caem. */
    fun pause() { paused = true; running.values.forEach { it.cancel() }; send(JSONObject().put("t", "pause")) }
    fun resume() { paused = false; send(JSONObject().put("t", "resume")) }

    /** O projeto aberto mudou (o relay mostra no aurea_status). */
    fun projectChanged() {
        val (title, open) = host.currentProject()
        send(JSONObject().put("t", "state").put("project", JSONObject().put("title", title).put("open", open)))
    }

    fun answerConfirm(approved: Boolean) {
        confirm?.answer?.complete(approved)
        confirm = null
    }

    private suspend fun askPerson(tool: String, agent: String, detail: String): Boolean {
        val c = Confirm(tool, agent, detail, CompletableDeferred())
        withContext(Dispatchers.Main) { confirm?.answer?.complete(false); confirm = c }
        val ok = withTimeoutOrNull(CONFIRM_MS) { c.answer.await() } ?: false
        withContext(Dispatchers.Main) { if (confirm === c) confirm = null }
        return ok
    }

    private fun permissionsJson() = JSONObject().apply { for ((k, v) in permissions) put(k, v) }

    // --- conexão ---------------------------------------------------------------------

    private suspend fun connectLoop() {
        var backoff = 1_000L
        while (scope.isActive && enabled) {
            val token = ContaViewModel.tokenGuardado(app)
            if (token == null) { status = Status.NoAccount; delay(5_000); continue }
            status = Status.Connecting
            val s = McpSocket(URI("$WSS/api/mcp/app/socket?device=$deviceId"), mapOf("Authorization" to "Bearer $token", "User-Agent" to "Aurea/${BuildConfig.VERSION_NAME}"))
            var pinger: Job? = null
            try {
                withContext(Dispatchers.IO) { s.connect() }
                socket = s
                status = Status.Online
                backoff = 1_000L
                send(hello())
                pinger = scope.launch { while (isActive) { delay(25_000); runCatching { withContext(Dispatchers.IO) { s.sendText("ping") } } } }
                withContext(Dispatchers.IO) { s.readLoop { text -> scope.launch(Dispatchers.Main) { onMessage(text) } } }
            } catch (c: CancellationException) {
                s.close(); throw c
            } catch (e: McpSocket.HandshakeFailure) {
                if (e.status == 401) status = Status.NoAccount
            } catch (_: Exception) {
                // rede caiu: tenta de novo
            } finally {
                pinger?.cancel()
                if (socket === s) socket = null
            }
            if (!enabled) break
            if (status != Status.NoAccount) status = Status.Offline
            delay(backoff)
            backoff = (backoff * 2).coerceAtMost(60_000L)
        }
    }

    private fun hello(): JSONObject {
        val (title, open) = host.currentProject()
        val pc = com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe
        val name = runCatching { if (pc) "PC" else listOf(android.os.Build.MANUFACTURER, android.os.Build.MODEL).joinToString(" ").trim() }.getOrNull().orEmpty().ifBlank { "Aurea" }
        return JSONObject().put("t", "hello")
            .put("device", JSONObject().put("name", name.take(60)).put("platform", if (pc) "pc" else "android").put("app", BuildConfig.VERSION_NAME))
            .put("engineTools", runCatching { JSONArray(engineToolsJson()) }.getOrDefault(JSONArray()))
            .put("hostTools", JSONArray(McpCatalog.HOST.keys.toList()))
            .put("permissions", permissionsJson())
            .put("project", JSONObject().put("title", title).put("open", open))
    }

    private fun send(msg: JSONObject) {
        val s = socket ?: return
        val text = msg.toString()
        scope.launch(Dispatchers.IO) { runCatching { s.sendText(text) } }
    }

    private fun onMessage(text: String) {
        if (text == "pong") return
        val m = runCatching { JSONObject(text) }.getOrNull() ?: return
        when (m.optString("t")) {
            "welcome" -> {
                paused = m.optBoolean("paused"); agents = parseAgents(m.optJSONArray("agents"))
                m.optJSONArray("log")?.let { arr -> log.clear(); for (i in 0 until arr.length()) parseLog(arr.optJSONObject(i))?.let(log::add) }
            }
            "agents" -> agents = parseAgents(m.optJSONArray("agents"))
            "paused" -> paused = m.optBoolean("paused")
            "log" -> parseLog(m)?.let { log.add(it); if (log.size > LOG_MAX) log.removeRange(0, log.size - LOG_MAX) }
            "cancel" -> running.remove(m.optString("id"))?.cancel()
            "call" -> handleCall(m)
        }
    }

    private fun handleCall(m: JSONObject) {
        val id = m.optString("id").takeIf { it.isNotEmpty() && it.length <= 64 } ?: return
        val tool = m.optString("tool")
        val args = m.optJSONObject("args") ?: JSONObject()
        val agent = m.optString("agent").ifBlank { "agent" }.take(80)
        running[id] = scope.launch {
            val reply = try {
                if (paused) McpReply.fail("The person paused AureaMCP in the app.") else executor.run(tool, args, agent)
            } catch (c: CancellationException) {
                McpReply.fail("Cancelled.")
            } catch (e: Exception) {
                McpReply.fail("Aurea failed: ${e.javaClass.simpleName}")
            }
            running.remove(id)
            val out = JSONObject().put("t", "result").put("id", id).put("ok", reply.ok).put("content", reply.toContent())
            if (!reply.ok) out.put("error", reply.text.take(2000))
            send(out)
        }
    }

    private fun parseAgents(arr: JSONArray?): List<Agent> = buildList {
        if (arr == null) return@buildList
        for (i in 0 until arr.length()) arr.optJSONObject(i)?.let { add(Agent(it.optString("name"), it.optString("version"), it.optLong("seen"), it.optString("tokenId").ifBlank { null })) }
    }

    private fun parseLog(o: JSONObject?): LogLine? = o?.let {
        LogLine(it.optLong("at"), it.optString("agent"), it.optString("tool").ifBlank { null }, it.optBoolean("ok", true), it.optLong("ms"), it.optString("event").ifBlank { null })
    }

    // --- pareamento e tokens (HTTP com a sessão da conta) ----------------------------------

    /** Lê quem pediu o código (nome do cliente e o domínio de retorno). null = código inválido/vencido. */
    fun lookupPair(code: String, onResult: (PairRequest?, String?) -> Unit) = http("POST", "/api/mcp/pair/lookup", JSONObject().put("code", code)) { s, b ->
        if (s == 200) onResult(PairRequest(code, b.optString("client"), b.optString("redirectHost")), null) else onResult(null, b.optString("error").ifBlank { "offline" })
    }

    fun approvePair(code: String, allow: Boolean, onDone: (Boolean) -> Unit) = http("POST", "/api/mcp/pair/approve", JSONObject().put("code", code).put("allow", allow)) { s, _ ->
        onDone(s == 200); refreshGrants()
    }

    fun createToken(name: String, onResult: (NewToken?) -> Unit) = http("POST", "/api/mcp/tokens", JSONObject().put("name", name)) { s, b ->
        onResult(if (s == 201) NewToken(b.optString("token"), b.optString("url")) else null); refreshGrants()
    }

    fun revoke(id: String?) = http("POST", "/api/mcp/tokens/revoke", if (id == null) JSONObject().put("all", true) else JSONObject().put("id", id)) { _, _ -> refreshGrants() }

    // --- AureaOS (pago: aurea_aureaos) --------------------------------------------------
    // O Worker é quem libera o conteúdo do AureaOS para os agentes; aqui só se mostra o
    // estado e se compra/restaura pela Play (compra única, não consumível; o servidor
    // confere com a Play e reconhece a compra).

    /** null = ainda não perguntou; o servidor diz. */
    var aureaosActive by mutableStateOf<Boolean?>(null)
        private set
    /** O Worker tem as chaves da loja (sem elas não se cobra). */
    var aureaosPurchasable by mutableStateOf(false)
        private set
    var aureaosPrice by mutableStateOf<String?>(null)
        private set
    /** Mensagem do último passo da compra (chave de texto, a tela traduz). */
    var aureaosNotice by mutableStateOf<String?>(null)
        private set
    var aureaosBusy by mutableStateOf(false)
        private set

    fun refreshAureaos() = http("GET", "/api/entitlements?platform=google", null) { s, b ->
        if (s != 200) return@http
        val products = b.optJSONArray("products") ?: JSONArray()
        aureaosActive = (0 until products.length()).any { products.optString(it) == AUREAOS_PRODUCT }
        val buy = b.optJSONArray("purchasable") ?: JSONArray()
        aureaosPurchasable = (0 until buy.length()).any { buy.optString(it) == AUREAOS_PRODUCT }
        if (aureaosPurchasable && aureaosActive == false && aureaosPrice == null) scope.launch {
            val billing = com.aurea.aurea.community.MuralBilling(app, AUREAOS_PRODUCT)
            aureaosPrice = runCatching { billing.details()?.oneTimePurchaseOfferDetails?.formattedPrice }.getOrNull()
            billing.close()
        }
    }

    /** Compra na Play e entrega ao servidor. `restore` = só procura uma compra que já existe. */
    fun buyAureaos(activity: Activity?, restore: Boolean) {
        if (aureaosBusy) return
        aureaosBusy = true; aureaosNotice = null
        scope.launch {
            val billing = com.aurea.aurea.community.MuralBilling(app, AUREAOS_PRODUCT)
            try {
                val owned = runCatching { billing.unconsumed() }.getOrNull()
                val purchase = owned ?: if (restore || activity == null) null else {
                    val details = runCatching { billing.details() }.getOrNull()
                    if (details == null) { aureaosNotice = "mcp_aureaos_unavailable"; return@launch }
                    runCatching { billing.buy(activity, details) }.getOrNull()
                }
                when {
                    purchase == null -> aureaosNotice = if (restore) "mcp_aureaos_nothing_to_restore" else null
                    purchase.purchaseState != com.android.billingclient.api.Purchase.PurchaseState.PURCHASED -> aureaosNotice = "mcp_aureaos_pending"
                    else -> deliverAureaos(purchase.purchaseToken)
                }
            } finally {
                billing.close()
                aureaosBusy = false
            }
        }
    }

    private suspend fun deliverAureaos(token: String) {
        val done = CompletableDeferred<Int>()
        http("POST", "/api/entitlements/purchase", JSONObject().put("platform", "google").put("product", AUREAOS_PRODUCT).put("token", token)) { s, _ -> done.complete(s) }
        aureaosNotice = when (done.await()) {
            200 -> { aureaosActive = true; "mcp_aureaos_thanks" }
            409 -> "mcp_aureaos_other_account"
            402 -> "mcp_aureaos_invalid"
            else -> "mcp_aureaos_unavailable"
        }
        refreshAureaos()
    }

    fun refreshGrants() = http("GET", "/api/mcp/tokens", null) { s, b ->
        if (s != 200) return@http
        val arr = b.optJSONArray("tokens") ?: JSONArray()
        grants = buildList {
            for (i in 0 until arr.length()) arr.optJSONObject(i)?.let {
                add(Grant(it.optString("id"), it.optString("kind"), it.optString("name"), it.optLong("createdAt"),
                    it.optLong("lastUsedAt").takeIf { v -> v > 0 }, it.optLong("expiresAt").takeIf { v -> v > 0 }))
            }
        }
    }

    private fun http(method: String, path: String, body: JSONObject?, done: (Int, JSONObject) -> Unit) {
        val token = ContaViewModel.tokenGuardado(app) ?: run { status = if (enabled) Status.NoAccount else status; done(401, JSONObject()); return }
        busy = true
        scope.launch {
            val (code, json) = withContext(Dispatchers.IO) {
                runCatching {
                    val c = (URL(ContaApi.BASE + path).openConnection() as HttpURLConnection).apply {
                        requestMethod = method; connectTimeout = 12_000; readTimeout = 20_000; useCaches = false; instanceFollowRedirects = false
                        setRequestProperty("Accept", "application/json"); setRequestProperty("Authorization", "Bearer $token")
                    }
                    try {
                        if (body != null) {
                            val bytes = body.toString().toByteArray(Charsets.UTF_8)
                            c.doOutput = true; c.setRequestProperty("Content-Type", "application/json; charset=utf-8"); c.setFixedLengthStreamingMode(bytes.size)
                            c.outputStream.use { it.write(bytes) }
                        }
                        val status = c.responseCode
                        val text = (if (status in 200..299) c.inputStream else c.errorStream)?.use { it.readBytes().take(64 * 1024).toByteArray().toString(Charsets.UTF_8) }.orEmpty()
                        status to (runCatching { JSONObject(text) }.getOrNull() ?: JSONObject())
                    } finally { c.disconnect() }
                }.getOrElse { 0 to JSONObject() }
            }
            busy = false
            done(code, json)
        }
    }

    companion object {
        /** Há uma instância ligada: a raiz do app mostra as confirmações e segue o ciclo de vida. */
        var active by mutableStateOf(false)
            private set

        const val AUREAOS_PRODUCT = "aurea_aureaos"

        /** Código de pareamento que chegou pelo link aurea://mcp/pair (a tela abre e preenche). */
        var pendingPairCode by mutableStateOf<String?>(null)
        /** O link pediu a tela do AureaMCP (Home ou editor). */
        var deepLinkOpen by mutableStateOf(false)

        /**
         * `aurea://mcp/pair?code=ABCD-2345` → "ABCD-2345" (maiúsculas, com hífen).
         * Só o esquema/host/caminho certos e um código de 8 símbolos do alfabeto do
         * pareamento (sem 0/O/1/I); o resto é null (link estranho não abre nada).
         */
        fun pairCodeFromLink(link: String?): String? {
            val u = runCatching { URI(link ?: return null) }.getOrNull() ?: return null
            if (!u.scheme.equals("aurea", true) || !u.host.equals("mcp", true) || u.path?.trimEnd('/') != "/pair") return null
            val raw = u.rawQuery?.split('&')?.firstOrNull { it.startsWith("code=") }?.substringAfter("code=") ?: return null
            val code = java.net.URLDecoder.decode(raw, "UTF-8").uppercase().filter { it.isLetterOrDigit() }
            if (code.length != 8 || code.any { it !in PAIR_ALPHABET }) return null
            return code.take(4) + "-" + code.drop(4)
        }
        private const val PAIR_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

        /** O link chegou (MainActivity): guarda o código e pede a tela. */
        fun openPairLink(link: String?): Boolean {
            val code = pairCodeFromLink(link) ?: return false
            pendingPairCode = code
            deepLinkOpen = true
            return true
        }

        /** Ligado nos ajustes (sem criar a instância). */
        fun enabledIn(context: Context): Boolean =
            context.applicationContext.getSharedPreferences("aurea.mcp", Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

        private const val WSS = "wss://aurea-ai-discovery.aureaapp.workers.dev"
        /** O endereço que o agente usa (Claude.ai/ChatGPT: conector remoto; Claude Code/Codex: com o token). */
        const val SERVER_URL = "https://aurea-ai-discovery.aureaapp.workers.dev/mcp"
        private const val KEY_ENABLED = "enabled"
        private const val KEY_ASK = "ask_every_edit"
        private const val KEY_PERM = "perm."
        private const val KEY_DEVICE = "device"
        private const val CONFIRM_MS = 85_000L
        private const val LOG_MAX = 200

        /** Comandos prontos para colar (Claude Code e Codex). */
        fun claudeCodeCommand(token: String) = "claude mcp add --transport http aurea $SERVER_URL --header \"Authorization: Bearer $token\""
        fun codexConfig(token: String) = "[mcp_servers.aurea]\nurl = \"$SERVER_URL\"\nbearer_token_env_var = \"AUREA_MCP_TOKEN\"\n\n# AUREA_MCP_TOKEN=$token"
    }
}
