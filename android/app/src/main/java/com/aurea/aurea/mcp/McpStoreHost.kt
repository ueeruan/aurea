package com.aurea.aurea.mcp

import android.Manifest
import android.app.Application
import android.content.ContentUris
import android.graphics.Bitmap
import android.net.Uri
import android.provider.MediaStore
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import com.aurea.aurea.editor.galleryFull
import com.aurea.aurea.editor.galleryPartial
import com.aurea.aurea.editor.mediaItems
import com.aurea.aurea.state.GalleryPublish
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportFormat
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.state.Screen
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import kotlin.coroutines.resume
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * AureaMCP (Android e PC): as ferramentas de host sobre o [EditorStore]. Tudo
 * que mexe no estado do Compose roda na thread principal; o que espera o
 * resultado (abrir, criar, importar) observa o estado com prazo.
 * Par do iOS: AureaMcpHost em engine/platform/ios/app/AureaMcp.swift.
 */
class McpStoreHost(private val store: EditorStore, private val cacheDir: File) : McpHost {

    /** id = nome do arquivo (sem o caminho do aparelho). */
    private fun idOf(path: String) = File(path).name
    private fun pathOf(id: String): String? = store.projects.firstOrNull { idOf(it.path) == id || it.path == id }?.path

    private suspend fun <T> main(block: suspend () -> T): T = withContext(Dispatchers.Main) { block() }

    private suspend fun ensureProjectsLoaded() = main {
        store.refreshProjects()
        withTimeoutOrNull(5_000) { snapshotFlow { store.projectsLoaded }.first { it } }
    }

    override fun projectTitle(id: String): String? = store.projects.firstOrNull { idOf(it.path) == id }?.title

    override fun currentProject(): Pair<String, Boolean> = store.project.title to (store.screen == Screen.Editor)

    override suspend fun listProjects(query: String?, limit: Int): McpReply {
        ensureProjectsLoaded()
        val q = query?.lowercase()
        val list = main { store.projects }.filter { q == null || it.title.lowercase().contains(q) }.take(limit)
        val open = main { if (store.screen == Screen.Editor) store.project.path else null }
        val arr = JSONArray()
        for (p in list) {
            arr.put(JSONObject().put("id", idOf(p.path)).put("title", p.title).put("width", p.width).put("height", p.height)
                .put("fps", p.fps.toDouble()).put("durationSeconds", if (p.fps > 0) (p.durationFrames / p.fps * 100).roundToInt() / 100.0 else 0.0)
                .put("modified", p.modifiedMs).put("bytes", p.sizeBytes).put("open", p.path == open))
        }
        return McpReply.ok(JSONObject().put("ok", true).put("projects", arr))
    }

    override suspend fun openProject(id: String): McpReply {
        val path = main { ensurePath(id) } ?: return McpReply.fail("No project with id $id (see list_projects).")
        return main {
            if (store.screen == Screen.Editor && store.project.path == path) return@main McpReply.ok("Already open.")
            if (store.screen == Screen.Editor) store.closeProject()
            withTimeoutOrNull(15_000) { snapshotFlow { store.screen }.first { it == Screen.Home } }
            store.errorMessage = null
            store.openProject(path)
            val opened = withTimeoutOrNull(30_000) { snapshotFlow { store.screen == Screen.Editor && store.project.path == path || store.errorMessage != null || store.quarantinePrompt != null }.first { it } }
            when {
                store.quarantinePrompt != null -> McpReply.fail("This project crashed Aurea before; the person must choose Recover in the app.")
                opened == null || store.errorMessage != null -> McpReply.fail(store.errorMessage ?: "Could not open the project.")
                else -> McpReply.ok(JSONObject().put("ok", true).put("title", store.project.title).put("id", id))
            }
        }
    }

    private suspend fun ensurePath(id: String): String? {
        pathOf(id)?.let { return it }
        store.refreshProjects()
        withTimeoutOrNull(3_000) { snapshotFlow { pathOf(id) }.first { it != null } }
        return pathOf(id)
    }

    override suspend fun createProject(title: String, width: Int, height: Int, fps: Double, background: FloatArray?): McpReply = main {
        if (store.screen == Screen.Editor) {
            store.closeProject()
            withTimeoutOrNull(15_000) { snapshotFlow { store.screen }.first { it == Screen.Home } }
        }
        store.errorMessage = null
        store.newProject(width, height, fps, title, background?.let { floatArrayOf(it[0], it[1], it[2], 1f) })
        val made = withTimeoutOrNull(20_000) { snapshotFlow { (store.screen == Screen.Editor && store.project.title == title) || store.errorMessage != null }.first { it } }
        if (made == null || store.errorMessage != null) McpReply.fail(store.errorMessage ?: "Could not create the project.")
        else McpReply.ok(JSONObject().put("ok", true).put("id", store.project.path?.let(::idOf)).put("title", title).put("width", width).put("height", height).put("fps", fps))
    }

    override suspend fun renameProject(title: String): McpReply = main {
        if (store.screen != Screen.Editor) return@main McpReply.fail("No project is open.")
        store.renameProject(title)
        McpReply.ok("Renamed to \"$title\".")
    }

    override suspend fun duplicateProject(id: String): McpReply {
        val path = main { ensurePath(id) } ?: return McpReply.fail("No project with id $id.")
        return main {
            val before = store.projects.size
            store.duplicateProject(path)
            withTimeoutOrNull(20_000) { snapshotFlow { store.projects.size }.first { it > before } }
            val copy = store.projects.maxByOrNull { it.modifiedMs }
            if (store.projects.size > before && copy != null) McpReply.ok(JSONObject().put("ok", true).put("id", idOf(copy.path)).put("title", copy.title))
            else McpReply.fail("Could not duplicate the project.")
        }
    }

    override suspend fun deleteProject(id: String): McpReply {
        val path = main { ensurePath(id) } ?: return McpReply.fail("No project with id $id.")
        return main {
            if (store.screen == Screen.Editor && store.project.path == path) return@main McpReply.fail("That project is open: close_project first.")
            store.deleteProjects(listOf(path))
            withTimeoutOrNull(10_000) { snapshotFlow { store.projects.none { it.path == path } }.first { it } }
            if (store.projects.none { it.path == path }) McpReply.ok("Deleted.") else McpReply.fail("Could not delete the project.")
        }
    }

    override suspend fun saveProject(): McpReply = main {
        if (store.screen != Screen.Editor || store.project.path == null) return@main McpReply.fail("No project is open.")
        store.errorMessage = null
        val done = withTimeoutOrNull(30_000) { suspendCancellableCoroutine { c -> store.saveProject { if (c.isActive) c.resume(Unit) } } }
        if (done == null || store.errorMessage != null) McpReply.fail(store.errorMessage ?: "Saving did not finish.") else McpReply.ok("Saved.")
    }

    override suspend fun closeProject(): McpReply = main {
        if (store.screen != Screen.Editor) return@main McpReply.ok("No project was open.")
        store.closeProject()
        val home = withTimeoutOrNull(15_000) { snapshotFlow { store.screen }.first { it == Screen.Home } }
        if (home == null) McpReply.fail("Aurea is busy (export or import running); try again later.") else McpReply.ok("Closed.")
    }

    override suspend fun renderFrame(seconds: Double?, maxSize: Int): McpReply {
        val fps = main {
            if (store.screen != Screen.Editor) return@main -1f
            store.project.fps.takeIf { it > 0f } ?: 30f
        }
        if (fps < 0f) return McpReply.fail("No project is open.")
        if (seconds != null) {
            main { store.seek((seconds * fps).roundToInt()) }
            kotlinx.coroutines.delay(120)   // o motor aplica o seek no laço dele
        }
        val bmp: Bitmap = withContext(Dispatchers.IO) { store.captureBitmap(maxSize) } ?: return McpReply.fail("Could not render the frame.")
        val jpeg = try {
            ByteArrayOutputStream().use { out -> bmp.compress(Bitmap.CompressFormat.JPEG, 85, out); out.toByteArray() }
        } finally { bmp.recycle() }
        val at = main { store.playhead / fps }
        return McpReply(true, "Frame at ${(at * 1000).roundToInt() / 1000.0} s (${jpeg.size / 1024} KB).", jpeg)
    }

    private val app: Application get() = store.getApplication()

    override suspend fun exportVideo(args: JSONObject): McpReply {
        // Android 8–9: a galeria só vê a pasta PÚBLICA, e lá só se escreve com
        // WRITE_EXTERNAL_STORAGE — o mesmo pedido da tela Exportar. Negada (ou o app
        // está em segundo plano), o export segue e fica na pasta do app.
        if (main { store.screen == Screen.Editor && !store.exporter.busy } && GalleryPublish.needsLegacyWritePermission(app)) {
            McpPermissions.request(arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE))
        }
        return startExport(args)
    }

    private suspend fun startExport(args: JSONObject): McpReply = main {
        if (store.screen != Screen.Editor) return@main McpReply.fail("No project is open.")
        val exporter = store.exporter
        if (exporter.busy) return@main McpReply.fail("An export is already running (export_status).")
        val p = store.project
        val compW = p.width; val compH = p.height; val compFps = p.fps.toDouble()
        if (compW <= 0 || compH <= 0) return@main McpReply.fail("The composition has no size yet.")
        val format = when (args.optString("format", "video")) { "gif" -> ExportFormat.Gif; "png" -> ExportFormat.Frame; else -> ExportFormat.Video }
        val short = args.optInt("shortSide", 0).takeIf { it > 0 } ?: min(1080, max(720, min(compW, compH)))
        val options = ExportOptions(
            shortSide = short,
            fps = args.optDouble("fps", 0.0).takeIf { !it.isNaN() && it >= 0 } ?: 0.0,
            hevc = args.optString("codec") == "hevc",
            quality = when (args.optString("quality")) { "low" -> 35; "high" -> 85; else -> com.aurea.aurea.state.QUALITY_DEFAULT },
            format = format,
        )
        exporter.reset()
        exporter.plannedSeconds = if (compFps > 0) store.exportDuration(options.trimToContent) / compFps else 0.0
        exporter.start(p.title, compW, compH, compFps, options)
        McpReply.ok(JSONObject().put("ok", true).put("started", true).put("format", format.name.lowercase()).put("shortSide", short)
            .put("hint", "Call export_status every few seconds."))
    }

    override suspend fun exportStatus(): McpReply = main {
        val s = store.exporter.state
        McpReply.ok(JSONObject().put("ok", true).put("phase", s.phase.name.lowercase()).put("framesDone", s.framesDone).put("framesTotal", s.framesTotal)
            .put("progress", (s.fraction * 1000).roundToInt() / 1000.0).put("etaSeconds", s.etaSeconds)
            .put("output", s.outputLabel.takeIf { s.phase == ExportPhase.Done }).put("savedToGallery", s.savedToGallery)
            .put("message", s.message.takeIf { s.phase == ExportPhase.Failed }))
    }

    override suspend fun cancelExport(): McpReply = main {
        if (!store.exporter.busy) return@main McpReply.ok("No export is running.")
        store.exporter.cancel()
        McpReply.ok("Cancelled.")
    }

    override suspend fun importMediaUrl(url: String, overlay: Boolean): McpReply {
        if (main { store.screen } != Screen.Editor) return McpReply.fail("Open or create a project first.")
        val file = try {
            withContext(Dispatchers.IO) {
                // Cancelar a ferramenta (agente ou Parar) para o download no próximo bloco.
                val job = currentCoroutineContext()[Job]
                McpMediaFetch.download(url, File(cacheDir, "mcp-import"), cancelled = { job?.isActive == false })
            }
        } catch (e: McpMediaFetch.Rejected) {
            return McpReply.fail("Download refused: ${e.message}")
        } catch (e: Exception) {
            return McpReply.fail("Download failed: ${e.javaClass.simpleName}")
        }
        return importUris(listOf(Uri.fromFile(file)), overlay) { JSONObject().put("ok", true).put("file", file.name).put("bytes", file.length()) }
    }

    /** Importa e espera o fim (a barra de "importando" some). */
    private suspend fun importUris(uris: List<Uri>, overlay: Boolean, trimUs: LongRange? = null, result: () -> JSONObject): McpReply {
        if (main { store.screen } != Screen.Editor) return McpReply.fail("Open or create a project first.")
        return main {
            store.errorMessage = null
            store.importMediaBatch(uris, asOverlay = overlay, trimUs = trimUs)
            withTimeoutOrNull(1_000) { snapshotFlow { store.busyMessage }.first { it != null } }
            val done = withTimeoutOrNull(300_000) { snapshotFlow { store.busyMessage }.first { it == null } }
            when {
                done == null -> McpReply.fail("Import is still running; check with get_project_state later.")
                store.errorMessage != null -> McpReply.fail(store.errorMessage ?: "Import failed.")
                else -> McpReply.ok(result())
            }
        }
    }

    // --- galeria do aparelho (fase D) ---------------------------------------------------
    // Só com o interruptor "Galeria do aparelho" (o executor confere) E a permissão do
    // sistema (pedida ao ligar o interruptor). Acesso parcial (Android 14 "fotos
    // selecionadas"): o MediaStore só devolve o que a pessoa escolheu — é o que sai.

    private fun galleryAccess(): String? {
        val a = app
        val full = a.galleryFull(true) || a.galleryFull(false)
        return when { full -> "full"; a.galleryPartial() -> "limited"; else -> null }
    }

    override suspend fun listDeviceMedia(kind: String, limit: Int, beforeId: String?): McpReply {
        val access = galleryAccess() ?: return McpReply.fail(NO_GALLERY)
        val all = withContext(Dispatchers.IO) { mediaItems(app) }
            .filter { kind == "all" || (kind == "video") == it.video }
        val start = beforeId?.let { id -> all.indexOfFirst { mediaId(it.video, it.uri) == id }.let { if (it < 0) 0 else it + 1 } } ?: 0
        val page = all.drop(start).take(limit)
        val arr = JSONArray()
        for (m in page) {
            arr.put(JSONObject().put("id", mediaId(m.video, m.uri)).put("name", m.name).put("kind", if (m.video) "video" else "image")
                .put("added", m.dateAdded).put("width", m.width).put("height", m.height).put("bytes", m.sizeBytes)
                .apply { if (m.video) put("durationSeconds", (m.durationMs / 100) / 10.0) })
        }
        return McpReply.ok(JSONObject().put("ok", true).put("access", access).put("items", arr)
            .put("more", start + page.size < all.size).apply { if (access == "limited") put("hint", "Limited access: only the media the person selected for Aurea is visible.") })
    }

    override suspend fun importDeviceMedia(ids: List<String>, overlay: Boolean, trimUs: LongRange?): McpReply {
        galleryAccess() ?: return McpReply.fail(NO_GALLERY)
        val uris = ids.map { id ->
            val kind = id.substringBefore(':')
            val n = id.substringAfter(':').toLong()
            val collection = if (kind == "video") MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI
            ContentUris.withAppendedId(collection, n)
        }
        // Id que o aparelho não mostra (apagado, ou fora do acesso parcial) não entra.
        val visible = withContext(Dispatchers.IO) { mediaItems(app).map { it.uri.toString() }.toSet() }
        val missing = uris.filter { it.toString() !in visible }
        if (missing.isNotEmpty()) return McpReply.fail("Not in the gallery Aurea can see: ${missing.size} of ${uris.size} (list_device_media again).")
        return importUris(uris, overlay, trimUs) {
            JSONObject().put("ok", true).put("imported", uris.size).apply {
                if (trimUs != null) put("trimmed", "videos keep only the trimmed range (plus a small margin) inside Aurea")
            }
        }
    }

    private fun mediaId(video: Boolean, uri: Uri) = (if (video) "video:" else "image:") + ContentUris.parseId(uri)

    private companion object {
        const val NO_GALLERY = "Aurea cannot see the device gallery: ask the person to turn on \"Device gallery\" in Aurea → AureaMCP and allow access."
    }
}

/**
 * Permissão do sistema pedida por uma ferramenta do agente (hoje: gravar na
 * galeria no Android 8–9 ao exportar). A raiz do AureaMCP ([McpPermissionHost])
 * mostra o diálogo do sistema; com o app em segundo plano, o prazo vence e vale
 * "negada" (a ferramenta segue pelo caminho sem a permissão).
 */
object McpPermissions {
    class Request(val permissions: Array<String>, val answer: CompletableDeferred<Map<String, Boolean>>)

    var pending by mutableStateOf<Request?>(null)
        private set

    suspend fun request(permissions: Array<String>, timeoutMs: Long = 60_000): Map<String, Boolean> {
        val r = Request(permissions, CompletableDeferred())
        withContext(Dispatchers.Main) { pending?.answer?.complete(emptyMap()); pending = r }
        val result = withTimeoutOrNull(timeoutMs) { r.answer.await() } ?: emptyMap()
        withContext(Dispatchers.Main) { if (pending === r) pending = null }
        return result
    }

    fun answer(result: Map<String, Boolean>) {
        pending?.answer?.complete(result)
        pending = null
    }
}
