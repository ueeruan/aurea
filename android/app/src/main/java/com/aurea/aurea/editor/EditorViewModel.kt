package com.aurea.aurea.editor

import android.app.Application
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.Display
import android.view.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.directBuffer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.Executors

/**
 * Destino da tela. É estado, não pilha: o Aurea edita UM projeto por vez.
 */
enum class Screen { Home, Editor }

data class RecentProject(
    val path: String,
    val title: String,
    val modifiedMs: Long,
    val sizeBytes: Long,
)

/**
 * O que a UI desenha. NÃO é o motor: é a cópia do que a tela precisa mostrar.
 */
data class EditorUiState(
    val screen: Screen = Screen.Home,
    val recentProjects: List<RecentProject> = emptyList(),
    val projectTitle: String = "",
    val playheadFrame: Int = 0,
    val totalFrames: Int = 0,
    val playing: Boolean = false,
    val fps: Float = 30f,
    val compWidth: Int = 0,
    val compHeight: Int = 0,
    val layers: List<LayerRow> = emptyList(),
    val selectedIds: Set<Long> = emptySet(),
    val previewWidth: Int = 0,
    val previewHeight: Int = 0,
    val previewScaleLabel: String = "AUTO",
    val currentFps: Float = 0f,
    val canUndo: Boolean = false,
    val canRedo: Boolean = false,
    val dirty: Boolean = false,
    val recoveryAvailable: Boolean = false,
    val errorMessage: String? = null,
    val engineReady: Boolean = false,
    val importing: Boolean = false,
    val inspectorTab: InspectorTab = InspectorTab.Effects,
    /** Zoom horizontal da timeline, em pixels por frame. */
    val timelineZoom: Float = 4f,
    /** Catálogo de efeitos (tipos disponíveis). */
    val effectCatalog: List<EffectCatalogEntry> = emptyList(),
    /** Efeitos da layer selecionada, na ordem da pilha. */
    val layerEffects: List<LayerEffect> = emptyList(),
    /** Efeito aberto no painel e os seus parâmetros. */
    val openEffectId: Int = -1,
    val effectParams: List<EffectParam> = emptyList(),
    val hudVisible: Boolean = false,
)

enum class InspectorTab { Properties, Effects, Keyframes }

/**
 * O dono do motor do lado da UI.
 *
 *  1. CICLO DE VIDA. O motor sobe UMA vez, sem superfície (GPU, renderer e a
 *     thread de render própria). A superfície do SurfaceView entra e sai
 *     independente disso — rotação, background e split-screen só trocam a
 *     janela, nunca recriam o motor.
 *
 *  2. ESTADO. Um laço no vsync da UI lê o status condensado (uma travessia) e
 *     publica o que mudou. O PREVIEW não depende deste laço: ele roda na
 *     thread de render do motor.
 *
 *  3. GESTOS → COMANDOS, em lotes.
 */
class EditorViewModel(app: Application) : AndroidViewModel(app) {

    private val engine = AureaEngine.create(app)
    private val batch = CommandBatch(engine)
    private val main = Handler(Looper.getMainLooper())

    var ui by mutableStateOf(EditorUiState())
        private set

    /** Métricas do painel DEV. Estado separado: atualizar o HUD não recompõe o editor. */
    var perf by mutableStateOf(PerfStats())
        private set

    /** Frames por segundo do laço da UI (Choreographer), medidos junto com o HUD. */
    var uiFps by mutableStateOf(0f)
        private set
    private var uiFrames = 0

    private val layerBuffer = directBuffer(MAX_LAYERS * PodLayout.LAYER_ROW_BYTES)
    private val nameBlob = directBuffer(NAME_BLOB_BYTES)
    private val statusBuffer = directBuffer(PodLayout.STATUS_BYTES)
    private val perfBuffer = directBuffer(PerfStats.BYTES)
    private val rowBuffer = directBuffer(64 * EffectParam.ROW_BYTES)
    private val textBlob = directBuffer(16 * 1024)
    private val status = EngineStatus()

    // --- Ciclo de vida -------------------------------------------------------
    /** Serializa inicialização, superfície e suspensão. */
    private val lifecycleLock = Any()
    private val lifecycleThread = Executors.newSingleThreadExecutor { r -> Thread(r, "aurea-ciclo") }
    @Volatile private var ready = false
    private var destroyed = false
    private var pendingSurface: Triple<Surface, Int, Int>? = null
    private var statusLoop: RenderLoop? = null
    private var lastPerfNs = 0L
    private var lastLayerSignature = 0L

    /** Scrub em andamento: o playhead da UI segue o dedo, não o status. */
    private var scrubbing = false

    private var projectPath: String? = null

    init {
        lifecycleThread.execute {
            synchronized(lifecycleLock) {
                if (destroyed) return@synchronized
                val dirs = directories()
                val debug = (app.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
                val ok = engine.initialize(displayRefreshRate(), dirs.first, dirs.second, debug)
                ready = ok
                if (ok) {
                    pendingSurface?.let { (s, w, h) -> engine.attachSurface(s, w, h) }
                }
                pendingSurface = null
                main.post {
                    ui = if (ok) {
                        ui.copy(engineReady = true, effectCatalog = readCatalog())
                    } else {
                        ui.copy(errorMessage = "Não foi possível iniciar o motor gráfico (Vulkan) neste aparelho.")
                    }
                    if (ok) startStatusLoop()
                }
            }
        }
    }

    private fun displayRefreshRate(): Float {
        val dm = getApplication<Application>().getSystemService(DisplayManager::class.java)
        return dm?.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate ?: 60f
    }

    private fun directories(): Pair<String, String> {
        val app = getApplication<Application>()
        val cache = File(app.cacheDir, "motor").apply { mkdirs() }
        val docs = File(app.filesDir, "projetos").apply { mkdirs() }
        return cache.absolutePath to docs.absolutePath
    }

    // =========================================================================
    // Superfície (chamadas da thread principal, pelo SurfaceHolder)
    // =========================================================================

    fun attachSurface(surface: Surface, width: Int, height: Int) {
        synchronized(lifecycleLock) {
            if (!ready) {
                pendingSurface = Triple(surface, width, height)
                return
            }
            if (!engine.attachSurface(surface, width, height)) {
                ui = ui.copy(errorMessage = "O preview não conseguiu usar a superfície de vídeo.")
            }
        }
    }

    fun resizeSurface(width: Int, height: Int) {
        synchronized(lifecycleLock) {
            val pending = pendingSurface
            if (!ready && pending != null) {
                pendingSurface = Triple(pending.first, width, height)
                return
            }
            if (ready) engine.resizeSurface(width, height)
        }
    }

    /** Bloqueia até a GPU largar a janela — o Android a destrói em seguida. */
    fun detachSurface() {
        synchronized(lifecycleLock) {
            pendingSurface = null
            if (ready) engine.detachSurface()
        }
    }

    fun onEnterForeground() {
        lifecycleThread.execute { synchronized(lifecycleLock) { if (ready) engine.resume() } }
        if (ready) startStatusLoop()
    }

    fun onEnterBackground() {
        stopStatusLoop()
        // Suspender pausa, devolve os decoders de hardware ao sistema e grava o
        // cache de pipeline antes que o sistema possa matar o processo.
        lifecycleThread.execute { synchronized(lifecycleLock) { if (ready) engine.suspend() } }
    }

    fun shutdown() {
        stopStatusLoop()
        synchronized(lifecycleLock) {
            destroyed = true
            ready = false
            engine.shutdown()
            engine.destroy()
        }
        lifecycleThread.shutdown()
    }

    // =========================================================================
    // Estado
    // =========================================================================

    private fun startStatusLoop() {
        if (statusLoop != null) return
        statusLoop = RenderLoop { frameTimeNanos ->
            if (!ready) return@RenderLoop
            engine.readStatus(statusBuffer)
            status.readFrom(statusBuffer)
            publishStatus()
            uiFrames++
            if (ui.hudVisible && frameTimeNanos - lastPerfNs > 250_000_000L) {
                if (lastPerfNs != 0L) uiFps = uiFrames * 1e9f / (frameTimeNanos - lastPerfNs)
                uiFrames = 0
                lastPerfNs = frameTimeNanos
                engine.readPerf(perfBuffer)
                perf = PerfStats.read(perfBuffer)
            }
        }.also { it.start() }
    }

    private fun stopStatusLoop() {
        statusLoop?.stop()
        statusLoop = null
    }

    private fun publishStatus() {
        // A lista de camadas só é relida quando algo dela muda (contagem,
        // seleção, duração): reler 200 linhas por vsync seria desperdício.
        val signature = status.layerCount.toLong() * 31 + status.duration * 7 + status.assetCount
        val newLayers = if (ui.screen == Screen.Editor && signature != lastLayerSignature) {
            lastLayerSignature = signature
            readLayers()
        } else {
            ui.layers
        }
        val scaleLabel = when {
            status.previewAuto -> "AUTO"
            status.previewDenominator <= 1 -> "FULL"
            else -> "1/${status.previewDenominator}"
        }
        val next = ui.copy(
            playheadFrame = if (scrubbing) ui.playheadFrame else status.playhead.toInt(),
            totalFrames = status.duration.toInt(),
            playing = status.playing,
            fps = if (status.compFps > 0f) status.compFps else ui.fps,
            compWidth = status.compWidth,
            compHeight = status.compHeight,
            layers = newLayers,
            previewWidth = status.previewWidth,
            previewHeight = status.previewHeight,
            previewScaleLabel = scaleLabel,
            currentFps = status.currentFps,
            canUndo = status.canUndo,
            canRedo = status.canRedo,
            dirty = status.dirty,
            recoveryAvailable = status.recoveryAvailable,
        )
        if (next != ui) ui = next
    }

    private fun readLayers(): List<LayerRow> {
        val count = engine.queryLayers(layerBuffer, MAX_LAYERS, nameBlob)
        if (count <= 0) return emptyList()
        return List(count) { i -> LayerRow.read(layerBuffer, i, nameBlob) }
    }

    private fun refreshLayers() {
        lastLayerSignature = -1
        ui = ui.copy(layers = readLayers())
        refreshEffects()
    }

    // =========================================================================
    // Comandos
    // =========================================================================

    private inline fun send(block: CommandBatch.() -> Unit) {
        engine.beginCommandBatch()
        batch.block()
        engine.submitCommands()
    }

    fun createLayer(kind: Int, name: String) {
        send { createLayer(kind, name, 0) }
        refreshLayers()
    }

    fun deleteLayer(layerId: Long) {
        send { deleteLayer(layerId) }
        ui = ui.copy(selectedIds = ui.selectedIds - layerId)
        refreshLayers()
    }

    fun duplicateLayer(layerId: Long) {
        send { duplicateLayer(layerId) }
        refreshLayers()
    }

    fun toggleLayerVisibility(layerId: Long, visible: Boolean) {
        send { setLayerVisible(layerId, visible) }
        refreshLayers()
    }

    fun renameLayer(layerId: Long, name: String) = send { setLayerName(layerId, name) }

    fun beginGesture(label: String) = send { beginUndoGroup(label) }
    fun endGesture() = send { endUndoGroup() }

    fun moveLayerInTime(layerId: Long, startFrame: Int, endFrame: Int) {
        send { setLayerTimeRange(layerId, startFrame, endFrame) }
        refreshLayers()
    }

    fun splitLayerAtPlayhead(layerId: Long) {
        send { splitLayer(layerId, ui.playheadFrame) }
        refreshLayers()
    }

    fun reorderLayer(layerId: Long, newIndex: Int) {
        send { reorderLayer(layerId, newIndex) }
        refreshLayers()
    }

    fun select(layerId: Long, additive: Boolean) {
        val next = if (additive) {
            if (ui.selectedIds.contains(layerId)) ui.selectedIds - layerId else ui.selectedIds + layerId
        } else {
            setOf(layerId)
        }
        ui = ui.copy(selectedIds = next, openEffectId = -1, effectParams = emptyList())
        engine.setSelection(next.toLongArray())
        refreshEffects()
    }

    fun clearSelection() {
        ui = ui.copy(selectedIds = emptySet(), layerEffects = emptyList(), openEffectId = -1, effectParams = emptyList())
        engine.clearSelection()
    }

    // =========================================================================
    // Reprodução
    // =========================================================================

    private fun frameToNs(frame: Int): Long {
        val fps = if (ui.fps > 0f) ui.fps.toDouble() else 30.0
        return (frame / fps * 1_000_000_000.0).toLong()
    }

    fun togglePlayback() = send { togglePlayback() }

    fun seekTo(frame: Int) {
        val clamped = frame.coerceIn(0, maxOf(0, ui.totalFrames - 1))
        send { seek(frameToNs(clamped)) }
        ui = ui.copy(playheadFrame = clamped)
    }

    /** Scrub: início, movimento (coalescido no decoder) e fim (frame exato). */
    fun scrubStart(frame: Int) {
        scrubbing = true
        val clamped = frame.coerceIn(0, maxOf(0, ui.totalFrames - 1))
        send {
            scrubBegin()
            scrub(frameToNs(clamped))
        }
        ui = ui.copy(playheadFrame = clamped)
    }

    fun scrubTo(frame: Int) {
        val clamped = frame.coerceIn(0, maxOf(0, ui.totalFrames - 1))
        if (clamped == ui.playheadFrame) return
        send { scrub(frameToNs(clamped)) }
        ui = ui.copy(playheadFrame = clamped)
    }

    fun scrubEnd() {
        send { scrubEnd() }
        scrubbing = false
        refreshEffectParams()
    }

    fun stepFrames(frames: Int) = send { step(frames) }

    fun setPreviewScale(label: String, numerator: Int, denominator: Int) =
        send { setPreviewScale(label == "AUTO", numerator, denominator) }

    fun undo() {
        send { undo() }
        refreshLayers()
    }

    fun redo() {
        send { redo() }
        refreshLayers()
    }

    fun setTimelineZoom(pixelsPerFrame: Float) {
        ui = ui.copy(timelineZoom = pixelsPerFrame.coerceIn(0.25f, 40f))
    }

    fun setInspectorTab(tab: InspectorTab) {
        ui = ui.copy(inspectorTab = tab)
    }

    fun toggleHud() {
        ui = ui.copy(hudVisible = !ui.hudVisible)
    }

    // =========================================================================
    // Efeitos
    // =========================================================================

    private fun readCatalog(): List<EffectCatalogEntry> {
        val n = engine.queryEffectCatalog(rowBuffer, 64, textBlob)
        return List(maxOf(0, n)) { EffectCatalogEntry.read(rowBuffer, it, textBlob) }
    }

    private fun selectedLayer(): Long? = ui.selectedIds.firstOrNull()

    private fun refreshEffects() {
        val layer = selectedLayer()
        if (layer == null || !ready) {
            ui = ui.copy(layerEffects = emptyList(), openEffectId = -1, effectParams = emptyList())
            return
        }
        val n = engine.queryLayerEffects(layer, rowBuffer, 64, textBlob)
        val effects = List(maxOf(0, n)) { LayerEffect.read(rowBuffer, it, textBlob) }
        val open = if (effects.any { it.effectId == ui.openEffectId }) ui.openEffectId else -1
        ui = ui.copy(layerEffects = effects, openEffectId = open)
        refreshEffectParams()
    }

    private fun refreshEffectParams() {
        val layer = selectedLayer()
        val effect = ui.openEffectId
        if (layer == null || effect < 0) {
            if (ui.effectParams.isNotEmpty()) ui = ui.copy(effectParams = emptyList())
            return
        }
        val n = engine.queryEffectParams(layer, effect, rowBuffer, 64, textBlob)
        ui = ui.copy(effectParams = List(maxOf(0, n)) { EffectParam.read(rowBuffer, it, textBlob) })
    }

    fun addEffect(typeId: Int) {
        val layer = selectedLayer() ?: return
        send { addEffect(layer, typeId) }
        refreshLayers()
        ui.layerEffects.lastOrNull()?.let { openEffect(it.effectId) }
    }

    fun removeEffect(effectId: Int) {
        val layer = selectedLayer() ?: return
        send { removeEffect(layer, effectId) }
        if (ui.openEffectId == effectId) ui = ui.copy(openEffectId = -1)
        refreshLayers()
    }

    fun setEffectEnabled(effectId: Int, enabled: Boolean) {
        val layer = selectedLayer() ?: return
        send { setEffectEnabled(layer, effectId, enabled) }
        refreshEffects()
    }

    fun openEffect(effectId: Int) {
        ui = ui.copy(openEffectId = if (ui.openEffectId == effectId) -1 else effectId)
        refreshEffectParams()
    }

    fun setEffectParam(param: Int, value: Float) {
        val layer = selectedLayer() ?: return
        val effect = ui.openEffectId.takeIf { it >= 0 } ?: return
        send { setEffectParam(layer, effect, param, value) }
        refreshEffectParams()
    }

    fun setEffectVector(param: Int, v: FloatArray) {
        val layer = selectedLayer() ?: return
        val effect = ui.openEffectId.takeIf { it >= 0 } ?: return
        send { setEffectVector(layer, effect, param, v[0], v[1], v[2], v[3]) }
        refreshEffectParams()
    }

    // =========================================================================
    // Importação
    // =========================================================================

    /**
     * Importa um vídeo escolhido no seletor do sistema. O motor recebe a URI
     * `content://` e abre descritores por ela sempre que precisa (decoder novo,
     * volta do segundo plano, projeto reaberto).
     */
    fun importVideo(uri: Uri) {
        val app = getApplication<Application>()
        try {
            app.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: Exception) {
            // Nem todo provedor concede permissão persistente; a da sessão basta.
        }
        val name = displayName(uri) ?: "Vídeo"
        ui = ui.copy(importing = true)
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) { engine.importVideo(uri.toString(), name) }
            ui = ui.copy(importing = false)
            if (id < 0) {
                ui = ui.copy(errorMessage = "Não foi possível importar o vídeo (erro ${-id}).")
                return@launch
            }
            refreshLayers()
            select(id, false)
        }
    }

    private fun displayName(uri: Uri): String? = try {
        getApplication<Application>().contentResolver
            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    } catch (_: Exception) {
        null
    }

    // =========================================================================
    // Projetos
    // =========================================================================

    fun newProject(width: Int, height: Int, fps: Float, title: String) {
        viewModelScope.launch {
            val ok = withContext(Dispatchers.Default) { engine.newProject(width, height, fps, title) }
            if (!ok) {
                ui = ui.copy(errorMessage = "Não foi possível criar o projeto.")
                return@launch
            }
            projectPath = null
            ui = ui.copy(
                screen = Screen.Editor,
                projectTitle = title,
                fps = fps,
                playheadFrame = 0,
                selectedIds = emptySet(),
                layerEffects = emptyList(),
                openEffectId = -1,
                effectParams = emptyList(),
            )
            refreshLayers()
        }
    }

    fun openProject(path: String) {
        viewModelScope.launch {
            val code = withContext(Dispatchers.Default) { engine.loadProject(path) }
            if (code != 0) {
                ui = ui.copy(errorMessage = "Falha ao abrir o projeto (código $code).")
                return@launch
            }
            projectPath = path
            ui = ui.copy(screen = Screen.Editor, projectTitle = File(path).nameWithoutExtension, selectedIds = emptySet())
            refreshLayers()
        }
    }

    fun saveProject() {
        val path = projectPath ?: File(directories().second, "${ui.projectTitle.ifBlank { "Projeto" }}.aurea").absolutePath
        viewModelScope.launch {
            val code = withContext(Dispatchers.IO) { engine.saveProject(path) }
            if (code != 0) {
                ui = ui.copy(errorMessage = "Falha ao salvar (código $code).")
            } else {
                projectPath = path
            }
        }
    }

    fun closeProject() {
        saveIfDirty()
        clearSelection()
        ui = ui.copy(screen = Screen.Home, layers = emptyList())
        projectPath = null
        refreshRecentProjects()
    }

    private fun saveIfDirty() {
        if (!ui.dirty || ui.layers.isEmpty()) return
        val path = projectPath ?: File(directories().second, "${ui.projectTitle.ifBlank { "Projeto" }}.aurea").absolutePath
        engine.saveProject(path)
    }

    fun discardRecovery() {
        viewModelScope.launch(Dispatchers.Default) {
            engine.discardRecovery()
            withContext(Dispatchers.Main) { ui = ui.copy(recoveryAvailable = false) }
        }
    }

    fun dismissError() {
        ui = ui.copy(errorMessage = null)
    }

    fun refreshRecentProjects() {
        viewModelScope.launch(Dispatchers.IO) {
            val dir = File(getApplication<Application>().filesDir, "projetos")
            val list = dir.listFiles { f -> f.extension == "aurea" }
                ?.sortedByDescending { it.lastModified() }
                ?.take(20)
                ?.map { RecentProject(it.absolutePath, it.nameWithoutExtension, it.lastModified(), it.length()) }
                ?: emptyList()
            withContext(Dispatchers.Main) { ui = ui.copy(recentProjects = list) }
        }
    }

    private companion object {
        const val MAX_LAYERS = 512
        const val NAME_BLOB_BYTES = 32 * 1024
    }
}
