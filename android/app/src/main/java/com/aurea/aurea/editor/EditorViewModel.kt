package com.aurea.aurea.editor

import android.app.Application
import android.view.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.directBuffer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Destino da tela.
 *
 * É estado, não uma pilha de navegação: o Aurea edita UM projeto por vez, e
 * "voltar" fecha o projeto. Uma pilha permitiria dois editores abertos, e dois
 * motores não cabem no mesmo processo (ver o comentário da janela nativa no
 * bridge JNI).
 */
enum class Screen { Home, Editor }

/** Uma entrada da lista de projetos da Home. */
data class RecentProject(
    val path: String,
    val title: String,
    val modifiedMs: Long,
    val sizeBytes: Long,
)

/**
 * Estado imutável que a UI desenha.
 *
 * Isto NÃO é o motor. É a cópia do que a tela precisa mostrar, atualizada a
 * cada frame. Manter a UI lendo só este objeto é o que impede uma tela Compose
 * de tocar no motor direto — e é o que torna a tela testável sem aparelho.
 */
data class EditorUiState(
    val screen: Screen = Screen.Home,
    val recentProjects: List<RecentProject> = emptyList(),
    val projectTitle: String = "",
    val playheadFrame: Int = 0,
    val totalFrames: Int = 0,
    val playing: Boolean = false,
    val fps: Float = 60f,
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
    /** Painel inferior aberto: propriedades, efeitos, curvas. */
    val inspectorTab: InspectorTab = InspectorTab.Properties,
    /** Zoom horizontal da timeline, em pixels por frame. */
    val timelineZoom: Float = 4f,
)

enum class InspectorTab { Properties, Effects, Keyframes }

/**
 * O dono do motor do lado da UI.
 *
 * Três responsabilidades, nesta ordem de importância:
 *
 *   1. CICLO DE VIDA. Criar, inicializar, suspender e destruir o motor nos
 *      momentos certos do ciclo do app. Errar aqui vaza GPU ou trava o app em
 *      background.
 *
 *   2. LAÇO DE FRAME. Um `Choreographer` chama `renderFrame` a cada vsync. Não
 *      é um `while(true)` numa corrotina: o Choreographer entrega o timestamp
 *      do display e sincroniza com a taxa real, que pode ser 90 ou 120 Hz.
 *
 *   3. TRADUÇÃO DE GESTO EM COMANDO. Um arrasto na timeline vira `setTransform`
 *      ou `setTimeRange`, dentro de um grupo de desfazer — um gesto, um passo.
 *
 * O que ela NÃO faz: desenhar vídeo. O frame composto vai do motor direto para
 * o `Surface` do `SurfaceView`.
 */
class EditorViewModel(app: Application) : AndroidViewModel(app) {

    private val engine = AureaEngine.create()
    private val batch = CommandBatch(engine)

    var ui by mutableStateOf(EditorUiState())
        private set

    /** Linhas de camada, lidas do motor para um buffer direto. */
    private val layerBuffer = directBuffer(MAX_LAYERS * PodLayout.LAYER_ROW_BYTES)
    private val nameBlob = directBuffer(NAME_BLOB_BYTES)
    private val statusBuffer = directBuffer(PodLayout.STATUS_BYTES)
    private val status = EngineStatus()

    private var surface: Surface? = null
    private var initialized = false
    private var renderLoop: RenderLoop? = null

    /** Projeto aberto na memória. Vazio = Home. */
    private var projectPath: String? = null

    // =========================================================================
    // Ciclo de vida
    // =========================================================================

    /**
     * Chamado quando o `SurfaceView` entrega a superfície.
     *
     * A inicialização do motor é PESADA — detecção de codecs, sondagem de GPU,
     * criação do dispositivo — e leva dezenas de milissegundos. Ela roda em
     * `Dispatchers.Default`, nunca na thread principal: na principal, o
     * resultado seria um congelamento visível ao abrir o app.
     */
    fun attachSurface(newSurface: Surface, width: Int, height: Int, refreshRate: Float) {
        surface = newSurface
        if (initialized) {
            engine.resizeSurface(width, height)
            return
        }

        viewModelScope.launch {
            val ok = withContext(Dispatchers.Default) {
                val dirs = cacheDirectories()
                engine.initialize(
                    newSurface, width, height, refreshRate,
                    dirs.first, dirs.second,
                )
            }
            if (!ok) {
                ui = ui.copy(errorMessage = "Não foi possível iniciar o motor gráfico neste aparelho.")
                return@launch
            }
            initialized = true
            ui = ui.copy(engineReady = true)
            startRenderLoop()
        }
    }

    fun detachSurface() {
        renderLoop?.stop()
        renderLoop = null
    }

    /**
     * A superfície mudou de tamanho: rotação, split-screen, janela redimensionada.
     *
     * Rotear isto para o motor é o que evita o preview esticar. O Vulkan precisa
     * recriar o swapchain, e sem o aviso ele continuaria desenhando no tamanho
     * antigo — a imagem apareceria esticada ou cortada.
     */
    fun resizeSurface(width: Int, height: Int) {
        if (!initialized) return
        viewModelScope.launch(Dispatchers.Default) {
            engine.resizeSurface(width, height)
        }
    }

    fun onEnterForeground() {
        if (initialized && renderLoop == null) startRenderLoop()
    }

    fun onEnterBackground() {
        renderLoop?.stop()
        renderLoop = null
        if (initialized) {
            // O sistema pode matar o processo a qualquer momento. Suspender
            // libera GPU e cache descartável ANTES disso — e o projeto continua
            // em memória, então voltar não perde trabalho.
            viewModelScope.launch(Dispatchers.Default) { engine.suspend() }
        }
    }

    fun shutdown() {
        renderLoop?.stop()
        renderLoop = null
        if (initialized) {
            initialized = false
            engine.shutdown()
        }
    }

    private fun cacheDirectories(): Pair<String, String> {
        val app = getApplication<Application>()
        val cache = File(app.cacheDir, "motor").apply { mkdirs() }
        val docs = File(app.filesDir, "projetos").apply { mkdirs() }
        return cache.absolutePath to docs.absolutePath
    }

    // =========================================================================
    // Laço de frame
    // =========================================================================

    /**
     * O laço de frame.
     *
     * Usa `Choreographer` em vez de uma corrotina com `delay`: o Choreographer
     * entrega o instante do vsync e sincroniza com a taxa REAL do display —
     * a 120 Hz o `delay(16)` perderia metade dos frames ou os desalinharia.
     *
     * O trabalho por frame é: enviar os comandos pendentes, desenhar, ler o
     * status. Três travessias de bridge, independente de quantas camadas o
     * usuário está mexendo.
     */
    private fun startRenderLoop() {
        if (renderLoop != null) return
        renderLoop = RenderLoop { frameTimeNanos ->
            if (!initialized) return@RenderLoop

            engine.submitCommands()
            engine.renderFrame(frameTimeNanos)
            engine.readStatus(statusBuffer)
            status.readFrom(statusBuffer)

            publishStatus()
        }.also { it.start() }
    }

    /**
     * Publica o status do motor no estado da UI.
     *
     * A lista de camadas só é relida quando ela MUDA (contagem ou seleção
     * diferentes) ou quando o playhead pode ter alterado a ordem de desenho.
     * Reler 200 linhas a 60 Hz seria 12 mil leituras por segundo para desenhar
     * a mesma lista — e é o tipo de desperdício que faz a UI engasgar sem
     * motivo visível.
     */
    private fun publishStatus() {
        // A lista só é relida quando a contagem muda. Reler 200 linhas a 60 Hz
        // seriam 12 mil leituras por segundo para desenhar a mesma lista.
        val layersChanged = status.layerCount != ui.layers.size &&
            status.layerCount > 0 && ui.screen == Screen.Editor
        val newLayers = if (layersChanged) readLayers() else ui.layers

        val scaleLabel = if (status.previewAuto) {
            "AUTO"
        } else if (status.previewDenominator <= 1) {
            "FULL"
        } else {
            "1/${status.previewDenominator}"
        }

        ui = ui.copy(
            playheadFrame = status.playhead.toInt(),
            totalFrames = status.duration.toInt(),
            playing = status.playing,
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
    }

    /**
     * Lê a lista de camadas do motor para o buffer direto e a converte em
     * objetos para o Compose.
     *
     * A conversão em `List<LayerRow>` aloca, e alocar por frame é justamente o
     * que o buffer direto evita. Ela só acontece quando a CONTAGEM muda ou
     * quando um gesto acabou de alterar a lista — não a cada vsync.
     */
    private fun readLayers(): List<LayerRow> {
        val count = engine.queryLayers(layerBuffer, MAX_LAYERS, nameBlob)
        if (count <= 0) return emptyList()
        return List(count) { i -> LayerRow.read(layerBuffer, i, nameBlob) }
    }

    // =========================================================================
    // Comandos vindos da UI
    // =========================================================================

    fun createLayer(kind: Int, name: String) {
        engine.beginCommandBatch()
        batch.createLayer(kind, name, 0)
        engine.submitCommands()
        refreshLayers()
    }

    fun deleteLayer(layerId: Long) {
        engine.beginCommandBatch()
        batch.deleteLayer(layerId)
        engine.submitCommands()
        refreshLayers()
    }

    fun duplicateLayer(layerId: Long) {
        engine.beginCommandBatch()
        batch.duplicateLayer(layerId)
        engine.submitCommands()
        refreshLayers()
    }

    fun toggleLayerVisibility(layerId: Long, visible: Boolean) {
        engine.beginCommandBatch()
        batch.setLayerVisible(layerId, visible)
        engine.submitCommands()
    }

    fun renameLayer(layerId: Long, name: String) {
        engine.beginCommandBatch()
        batch.setLayerName(layerId, name)
        engine.submitCommands()
    }

    /**
     * Um arrasto na timeline, em uma frase: abre o grupo de desfazer, emite o
     * comando, fecha no fim do gesto.
     *
     * O grupo é o que faz o gesto inteiro desfazer de uma vez. Sem ele, desfazer
     * um arrasto de dois segundos exigiria dezenas de toques em "desfazer" — e o
     * usuário leria isso como "o desfazer está quebrado".
     */
    fun beginGesture(label: String) {
        engine.beginCommandBatch()
        batch.beginUndoGroup(label)
        engine.submitCommands()
    }

    fun endGesture() {
        engine.beginCommandBatch()
        batch.endUndoGroup()
        engine.submitCommands()
    }

    fun moveLayerInTime(layerId: Long, startFrame: Int, endFrame: Int) {
        engine.beginCommandBatch()
        batch.setLayerTimeRange(layerId, startFrame, endFrame)
        engine.submitCommands()
    }

    fun transformLayer(
        layerId: Long,
        x: Float, y: Float, opacity: Float,
        scaleX: Float, scaleY: Float, rotationZ: Float,
    ) {
        engine.beginCommandBatch()
        batch.setTransform(
            layerId,
            x, y, 0f,
            scaleX, scaleY, 1f,
            0f, 0f, rotationZ,
            0f, 0f, 0f,
            opacity,
        )
        engine.submitCommands()
    }

    fun splitLayerAtPlayhead(layerId: Long) {
        engine.beginCommandBatch()
        batch.splitLayer(layerId, ui.playheadFrame)
        engine.submitCommands()
        refreshLayers()
    }

    fun reorderLayer(layerId: Long, newIndex: Int) {
        engine.beginCommandBatch()
        batch.reorderLayer(layerId, newIndex)
        engine.submitCommands()
        refreshLayers()
    }

    fun select(layerId: Long, additive: Boolean) {
        val next = if (additive) {
            if (ui.selectedIds.contains(layerId)) ui.selectedIds - layerId
            else ui.selectedIds + layerId
        } else {
            setOf(layerId)
        }
        ui = ui.copy(selectedIds = next)
        engine.setSelection(next.toLongArray())
    }

    fun clearSelection() {
        ui = ui.copy(selectedIds = emptySet())
        engine.clearSelection()
    }

    // =========================================================================
    // Reprodução
    // =========================================================================

    fun togglePlayback() {
        engine.beginCommandBatch()
        if (ui.playing) batch.pause() else batch.play()
        engine.submitCommands()
    }

    fun seekTo(frame: Int) {
        val clamped = frame.coerceIn(0, ui.totalFrames)
        val fps = ui.fps.toDouble()
        val timeNs = (clamped / fps * 1_000_000_000.0).toLong()
        engine.beginCommandBatch()
        batch.seek(timeNs)
        engine.submitCommands()
        ui = ui.copy(playheadFrame = clamped)
    }

    fun setPreviewScale(label: String, numerator: Int, denominator: Int) {
        engine.beginCommandBatch()
        batch.setPreviewScale(label == "AUTO", numerator, denominator)
        engine.submitCommands()
    }

    fun undo() {
        engine.beginCommandBatch()
        batch.undo()
        engine.submitCommands()
        refreshLayers()
    }

    fun redo() {
        engine.beginCommandBatch()
        batch.redo()
        engine.submitCommands()
        refreshLayers()
    }

    fun setTimelineZoom(pixelsPerFrame: Float) {
        ui = ui.copy(timelineZoom = pixelsPerFrame.coerceIn(0.25f, 40f))
    }

    fun setInspectorTab(tab: InspectorTab) {
        ui = ui.copy(inspectorTab = tab)
    }

    // =========================================================================
    // Projetos
    // =========================================================================

    /** Relê a lista depois de uma operação que a mudou. */
    private fun refreshLayers() {
        ui = ui.copy(layers = readLayers())
    }

    fun newProject(width: Int, height: Int, fps: Float, title: String) {
        viewModelScope.launch {
            val ok = withContext(Dispatchers.Default) {
                engine.newProject(width, height, fps, title)
            }
            if (!ok) {
                ui = ui.copy(errorMessage = "Não foi possível criar o projeto.")
                return@launch
            }
            ui = ui.copy(
                screen = Screen.Editor,
                projectTitle = title,
                fps = fps,
                playheadFrame = 0,
                totalFrames = (fps * 10f).toInt(),
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
            ui = ui.copy(
                screen = Screen.Editor,
                projectTitle = File(path).nameWithoutExtension,
            )
            refreshLayers()
        }
    }

    fun closeProject() {
        clearSelection()
        ui = ui.copy(screen = Screen.Home, layers = emptyList())
        projectPath = null
        refreshRecentProjects()
    }

    /**
     * Descarta a recuperação da sessão anterior.
     *
     * É uma ação destrutiva — o journal é apagado —, então ela só acontece
     * quando o usuário pede explicitamente. Oferecer "Descartar" ao lado de
     * "Recuperar" sem confirmação some com o trabalho de quem toca errado.
     */
    fun discardRecovery() {
        viewModelScope.launch(Dispatchers.Default) {
            engine.discardRecovery()
            ui = ui.copy(recoveryAvailable = false)
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
                ?.map {
                    RecentProject(
                        path = it.absolutePath,
                        title = it.nameWithoutExtension,
                        modifiedMs = it.lastModified(),
                        sizeBytes = it.length(),
                    )
                }
                ?: emptyList()
            ui = ui.copy(recentProjects = list)
        }
    }

    private companion object {
        const val MAX_LAYERS = 512
        const val NAME_BLOB_BYTES = 32 * 1024
    }
}
