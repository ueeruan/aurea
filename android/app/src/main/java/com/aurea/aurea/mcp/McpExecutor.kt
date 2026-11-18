package com.aurea.aurea.mcp

import org.json.JSONArray
import org.json.JSONObject

/** O resultado de uma ferramenta para o relay: texto e, opcional, uma imagem JPEG. */
data class McpReply(val ok: Boolean, val text: String, val jpeg: ByteArray? = null) {
    fun toContent(): JSONArray {
        val out = JSONArray()
        if (jpeg != null) out.put(JSONObject().put("type", "image").put("mimeType", "image/jpeg")
            .put("data", java.util.Base64.getEncoder().encodeToString(jpeg)))
        out.put(JSONObject().put("type", "text").put("text", text))
        return out
    }

    companion object {
        fun ok(text: String) = McpReply(true, text)
        fun ok(json: JSONObject) = McpReply(true, json.toString())
        fun fail(text: String) = McpReply(false, text)
    }
}

/**
 * O que o app faz fora do motor (projetos, quadro, exportação, mídia). Android e
 * PC: [McpStoreHost] sobre o EditorStore. O iOS tem o par em Swift (AureaModel).
 * Os esquemas destas ferramentas moram no Worker (discovery/mcp_catalog.js).
 */
interface McpHost {
    suspend fun listProjects(query: String?, limit: Int): McpReply
    suspend fun openProject(id: String): McpReply
    suspend fun createProject(title: String, width: Int, height: Int, fps: Double, background: FloatArray?): McpReply
    suspend fun renameProject(title: String): McpReply
    suspend fun duplicateProject(id: String): McpReply
    suspend fun deleteProject(id: String): McpReply
    suspend fun saveProject(): McpReply
    suspend fun closeProject(): McpReply
    suspend fun renderFrame(seconds: Double?, maxSize: Int): McpReply
    suspend fun exportVideo(args: JSONObject): McpReply
    suspend fun exportStatus(): McpReply
    suspend fun cancelExport(): McpReply
    suspend fun importMediaUrl(url: String, overlay: Boolean): McpReply
    /** Galeria do aparelho (fase D): o mais novo primeiro; kind = all/video/image; eforeId pagina. */
    suspend fun listDeviceMedia(kind: String, limit: Int, beforeId: String?): McpReply
    /** Itens de [listDeviceMedia] (ids image:N / ideo:N), na ordem pedida. */
    suspend fun importDeviceMedia(ids: List<String>, overlay: Boolean): McpReply = importDeviceMedia(ids, overlay, null)
    /**
     * 1.0.2: [trimUs] = o corte [in, out] (µs) de cada VÍDEO; só o trecho (com
     * folga) é copiado para o app e a camada já sai cortada. null = inteiro.
     */
    suspend fun importDeviceMedia(ids: List<String>, overlay: Boolean, trimUs: LongRange?): McpReply
    /** Título do projeto [id] (para a pergunta de confirmação), ou null. */
    fun projectTitle(id: String): String?
    /** O projeto aberto agora: título e se há um aberto (vai no "hello"). */
    fun currentProject(): Pair<String, Boolean>
}

/** Catálogo do lado do app: capacidade de cada ferramenta (o espelho de discovery/mcp_catalog.js). */
object McpCatalog {
    val CAPABILITIES = listOf("read", "edit", "projects", "delete", "media", "device_media", "export")
    /** Ligadas na primeira vez; "Galeria do aparelho" começa desligada. */
    val DEFAULTS = mapOf("read" to true, "edit" to true, "projects" to true, "delete" to true, "media" to true, "device_media" to false, "export" to true)

    /** O espelho de ENGINE_READ em discovery/mcp_catalog.js (as leituras do motor). */
    val ENGINE_READ = setOf("get_project_state", "list_ae_effects", "seek", "playback", "set_view",
        "get_layer_details", "get_keyframes", "list_effect_types", "get_effect", "list_presets", "list_fonts", "get_markers", "get_work_area")

    /** As ferramentas de host que ESTE app executa (o Worker só anuncia estas). */
    val HOST = mapOf(
        "list_projects" to "read", "open_project" to "projects", "create_project" to "projects",
        "rename_project" to "projects", "duplicate_project" to "projects", "delete_project" to "delete",
        "save_project" to "edit", "close_project" to "projects", "render_frame" to "read",
        "export_video" to "export", "export_status" to "export", "cancel_export" to "export",
        "import_media_url" to "media", "list_device_media" to "device_media", "import_device_media" to "device_media",
    )
    /** Sempre perguntam à pessoa, mesmo com a permissão ligada. */
    val CONFIRM = setOf("delete_project")

    fun capabilityOf(tool: String): String = HOST[tool] ?: if (tool in ENGINE_READ) "read" else "edit"
    fun isHost(tool: String) = tool in HOST
}

/**
 * Executa uma chamada vinda do relay: permissão → confirmação → motor ou host.
 * Sem tipos do Android (testável na JVM): o motor entra como função.
 */
class McpExecutor(
    /** O motor (Engine::assistant_call); quem passa garante a thread (o app usa IO). */
    private val engineCall: suspend (name: String, args: String) -> String,
    private val host: McpHost,
    private val allowed: (capability: String) -> Boolean,
    private val askEveryEdit: () -> Boolean,
    /** Pergunta à pessoa (diálogo no app). true = aprovou. */
    private val confirm: suspend (tool: String, agent: String, detail: String) -> Boolean,
    /** Ferramentas da interface (o motor devolve {"ui":"open_panel","panel":…}). */
    private val onUi: (panel: String) -> Unit = {},
    /** O projeto mudou: a interface relê camadas, seleção, cabeçote. */
    private val onModelChanged: () -> Unit = {},
) {
    suspend fun run(tool: String, args: JSONObject, agent: String): McpReply {
        val capability = McpCatalog.capabilityOf(tool)
        if (!allowed(capability)) return McpReply.fail("The person turned off the \"$capability\" permission for AureaMCP in the Aurea app.")
        val mustAsk = tool in McpCatalog.CONFIRM || (askEveryEdit() && capability != "read" && tool != "export_status")
        if (mustAsk) {
            val detail = when (tool) {
                "delete_project" -> host.projectTitle(args.optString("id")) ?: args.optString("id")
                "run_ae_script" -> args.optString("code").take(400)
                else -> args.toString().take(400)
            }
            if (!confirm(tool, agent, detail)) return McpReply.fail("The person did not approve \"$tool\" in the Aurea app.")
        }
        return if (McpCatalog.isHost(tool)) runHost(tool, args) else runEngine(tool, args)
    }

    private suspend fun runEngine(tool: String, args: JSONObject): McpReply {
        val result = engineCall(tool, args.toString())
        val parsed = runCatching { JSONObject(result) }.getOrNull()
        val ok = parsed?.optBoolean("ok") == true
        if (ok && parsed?.optString("ui") == "open_panel") onUi(parsed.optString("panel"))
        if (McpCatalog.capabilityOf(tool) != "read" || tool == "seek" || tool == "set_view") onModelChanged()
        return McpReply(ok, result)
    }

    private suspend fun runHost(tool: String, a: JSONObject): McpReply = when (tool) {
        "list_projects" -> host.listProjects(a.optString("query").ifBlank { null }, a.optInt("limit", 50).coerceIn(1, 200))
        "open_project" -> a.requiredId()?.let { host.openProject(it) } ?: missing("id")
        "create_project" -> {
            val w = a.optInt("width"); val h = a.optInt("height"); val fps = a.optDouble("fps", 30.0)
            val title = a.optString("title").trim().take(120)
            when {
                title.isEmpty() -> missing("title")
                w !in 16..8192 || h !in 16..8192 -> McpReply.fail("width and height must be 16..8192")
                fps.isNaN() || fps < 1 || fps > 120 -> McpReply.fail("fps must be 1..120")
                else -> host.createProject(title, w, h, fps, a.optJSONArray("background")?.toRgb())
            }
        }
        "rename_project" -> a.optString("title").trim().take(120).takeIf { it.isNotEmpty() }?.let { host.renameProject(it) } ?: missing("title")
        "duplicate_project" -> a.requiredId()?.let { host.duplicateProject(it) } ?: missing("id")
        "delete_project" -> a.requiredId()?.let { host.deleteProject(it) } ?: missing("id")
        "save_project" -> host.saveProject()
        "close_project" -> host.closeProject()
        "render_frame" -> {
            val s = if (a.has("seconds")) a.optDouble("seconds") else null
            if (s != null && (s.isNaN() || s < 0)) McpReply.fail("seconds must be >= 0")
            else host.renderFrame(s, a.optInt("maxSize", 768).coerceIn(128, 1280))
        }
        "export_video" -> host.exportVideo(a)
        "export_status" -> host.exportStatus()
        "cancel_export" -> host.cancelExport()
        "import_media_url" -> {
            val url = a.optString("url")
            if (!url.startsWith("https://") || url.length > 2048) McpReply.fail("url must be https://") else host.importMediaUrl(url, a.optBoolean("overlay", false))
        }
        "list_device_media" -> {
            val kind = a.optString("kind", "all").ifBlank { "all" }
            if (kind !in setOf("all", "video", "image")) McpReply.fail("kind must be all, video or image")
            else host.listDeviceMedia(kind, a.optInt("limit", 30).coerceIn(1, 100), a.optString("beforeId").takeIf { MEDIA_ID.matches(it) })
        }
        "import_device_media" -> {
            val arr = a.optJSONArray("ids")
            val ids = (0 until (arr?.length() ?: 0)).map { arr!!.optString(it) }
            val hasTrim = a.has("trimInSeconds") || a.has("trimOutSeconds")
            val trim = if (hasTrim) a.trimUs() else null
            when {
                ids.isEmpty() || ids.size > 20 -> McpReply.fail("ids must be 1..20 ids from list_device_media")
                ids.any { !MEDIA_ID.matches(it) } -> McpReply.fail("unknown id (use the ids from list_device_media)")
                hasTrim && trim == null -> McpReply.fail("trimInSeconds/trimOutSeconds: need 0 <= trimInSeconds < trimOutSeconds <= 86400")
                trim != null -> host.importDeviceMedia(ids.distinct(), a.optBoolean("overlay", false), trim)
                else -> host.importDeviceMedia(ids.distinct(), a.optBoolean("overlay", false))
            }
        }
        else -> McpReply.fail("Unknown tool: $tool")
    }

    private companion object { val MEDIA_ID = Regex("^(image|video):[0-9]{1,19}$") }

    /** O corte do `import_device_media` em µs (só um lado = do começo / até o fim); null = inválido. */
    private fun JSONObject.trimUs(): LongRange? {
        val maxSeconds = 86_400.0
        val tin = if (has("trimInSeconds")) optDouble("trimInSeconds", Double.NaN) else 0.0
        val tout = if (has("trimOutSeconds")) optDouble("trimOutSeconds", Double.NaN) else maxSeconds
        if (!tin.isFinite() || !tout.isFinite() || tin < 0.0 || tout <= tin || tout > maxSeconds) return null
        return Math.round(tin * 1e6)..Math.round(tout * 1e6)
    }

    private fun JSONObject.requiredId(): String? = optString("id").takeIf { it.isNotBlank() && it.length <= 300 }
    private fun missing(field: String) = McpReply.fail("Missing $field.")
    private fun JSONArray.toRgb(): FloatArray? =
        if (length() != 3) null else FloatArray(3) { optDouble(it, 0.0).toFloat().coerceIn(0f, 1f) }
}
