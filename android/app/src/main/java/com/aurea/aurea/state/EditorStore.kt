package com.aurea.aurea.state

import android.app.Application
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.Display
import android.view.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.engine.directBuffer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Destino da tela. É estado, não pilha: o Aurea edita UM projeto por vez. */
enum class Screen { Home, Editor }

/** Um projeto na Home (lido do sidecar `.meta.json`, sem abrir o projeto). */
data class ProjectEntry(
    val path: String,
    val title: String,
    val modifiedMs: Long,
    val width: Int,
    val height: Int,
    val fps: Float,
    val durationFrames: Int,
    val thumbnailPath: String?,
)

/** Estado da composição aberta (espelho do status do motor). */
data class ProjectState(
    val title: String = "",
    val path: String? = null,
    val width: Int = 0,
    val height: Int = 0,
    val fps: Float = 30f,
    val durationFrames: Int = 0,
    val dirty: Boolean = false,
    val canUndo: Boolean = false,
    val canRedo: Boolean = false,
)

/** Ajustes da composição atual (fonte: motor). Fundo em RGBA sRGB (o valor exibido). */
data class CompositionSettings(
    val id: Long,
    val width: Int,
    val height: Int,
    val fps: Double,
    val durationFrames: Int,
    val background: List<Float>,
    /** Teto do aparelho (lado maior × lado menor); 0 = desconhecido. */
    val capLong: Int = 0,
    val capShort: Int = 0,
) {
    /** Cabe no teto com que o motor recusa CompositionSetSize. */
    fun fits(w: Int, h: Int): Boolean =
        capLong <= 0 || (max(w, h) <= capLong && min(w, h) <= capShort)
}

data class PreviewState(
    val width: Int = 0,
    val height: Int = 0,
    val scaleLabel: String = "AUTO",
    val fps: Float = 0f,
)

/**
 * O dono do motor do lado da UI — a ÚNICA fonte de estado das telas.
 *
 * REGRA: a verdade é o motor C++. Este store não guarda cópia editável de
 * nada do projeto: ele LÊ (camadas, detalhe da camada, keyframes, efeitos,
 * parâmetros) quando o motor avisa que o modelo mudou (`modelRevision`) ou o
 * playhead andou, e ESCREVE só por comandos. Uma tela nunca mostra "escala
 * 120" enquanto o motor tem 115.
 *
 * A exceção é estado de APRESENTAÇÃO que o motor não conhece: tela atual,
 * painel aberto, efeito expandido, zoom/rolagem da timeline (esses ficam nas
 * próprias telas).
 *
 * Ordem das camadas: [layers] vem da FRENTE para o FUNDO (`zIndex` 0 = frente),
 * que é a ordem da timeline.
 */

/** `aurea::Errc::UnsupportedFormat` (core/Result.hpp). */
private const val ERRC_UNSUPPORTED_FORMAT = 17L

/** shapeType da camada vetorial no motor (`kShapeVector`, vector/VectorData.hpp). */
const val VECTOR_SHAPE_TYPE = 11

class EditorStore(app: Application) : AndroidViewModel(app) {

    private val engine = AureaEngine.create(app)

    /** Legendas automáticas (transcrição + camadas); ver `CaptionsState`. */
    val captions = com.aurea.aurea.captions.CaptionsState(app, engine, viewModelScope) { refreshNow() }

    /** Export (tela Exportar). O motor renderiza; aqui só acompanha e publica. */
    val exporter = Exporter(app, engine, viewModelScope)
    private val batch = CommandBatch(engine)
    private val main = Handler(Looper.getMainLooper())

    // =========================================================================
    // Estado observável (Compose)
    // =========================================================================
    var screen by mutableStateOf(Screen.Home)
        private set
    var engineReady by mutableStateOf(false)
        private set
    var errorMessage by mutableStateOf<String?>(null)
        private set
    var busyMessage by mutableStateOf<String?>(null)
        private set
    var projects by mutableStateOf<List<ProjectEntry>>(emptyList())
        private set
    var projectsLoaded by mutableStateOf(false)
        private set

    var project by mutableStateOf(ProjectState())
        private set
    /** Playhead em frames da composição. */
    var playhead by mutableIntStateOf(0)
        private set
    var playing by mutableStateOf(false)
        private set
    var preview by mutableStateOf(PreviewState())
        private set

    /** Camadas da frente para o fundo. */
    var layers by mutableStateOf<List<LayerRow>>(emptyList())
        private set
    var selection by mutableStateOf<Set<Long>>(emptySet())
        private set
    /** Detalhe da camada principal da seleção (a primeira escolhida). */
    var detail by mutableStateOf<LayerDetail?>(null)
        private set
    /** Keyframes de TODAS as camadas (losangos da timeline), tempo local. */
    var keyframes by mutableStateOf<Map<Long, List<KeyframeRow>>>(emptyMap())
        private set

    var catalog by mutableStateOf<List<EffectCatalogEntry>>(emptyList())
        private set
    /** Efeitos da camada principal, na ordem da pilha. */
    var effects by mutableStateOf<List<LayerEffect>>(emptyList())
        private set
    /** Parâmetros de cada efeito da camada principal (por effectId). */
    var effectParams by mutableStateOf<Map<Int, List<EffectParam>>>(emptyMap())
        private set

    var perf by mutableStateOf(PerfStats())
        private set
    var uiFps by mutableStateOf(0f)
        private set
    var hudVisible by mutableStateOf(false)
        private set

    /** Muda quando o motor terminou uma miniatura nova (a timeline redesenha). */
    var thumbnailGeneration by mutableIntStateOf(0)
        private set
    val thumbnails = ThumbnailCache(engine)

    /** Camada principal da seleção. */
    val primary: Long? get() = selection.firstOrNull()

    /**
     * Keyframe escolhido (losango tocado na timeline). Estado de APRESENTAÇÃO
     * compartilhado entre a timeline e o editor de curva — o keyframe em si
     * continua no motor; aqui só fica QUAL está escolhido.
     */
    var selectedKeyframe by mutableStateOf<Pair<Long, KeyframeRow>?>(null)
        private set

    /** Aviso curto e não bloqueante (ex.: "Em breve no Aurea novo"). A UI some com ele em ~2 s. */
    var toast by mutableStateOf<String?>(null)
        private set
    private var toastSerial = 0

    fun showToast(message: String) {
        toast = message
        val serial = ++toastSerial
        main.postDelayed({ if (serial == toastSerial) toast = null }, 2200)
    }

    /** Para os recursos que o motor novo ainda não tem: diz, não finge. */
    fun comingSoon(feature: String) = showToast("$feature: em breve no Aurea novo")

    fun selectKeyframe(layer: Long, key: KeyframeRow) {
        selectedKeyframe = layer to key
    }

    fun clearSelectedKeyframe() {
        selectedKeyframe = null
    }

    // =========================================================================
    // Buffers (diretos, reutilizados)
    // =========================================================================
    private val layerBuffer = directBuffer(MAX_LAYERS * PodLayout.LAYER_ROW_BYTES)
    private val nameBlob = directBuffer(32 * 1024)
    private val statusBuffer = directBuffer(PodLayout.STATUS_BYTES)
    private val perfBuffer = directBuffer(PerfStats.BYTES)
    private val detailBuffer = directBuffer(LayerDetail.BYTES)
    private val rowBuffer = directBuffer(64 * EffectParam.ROW_BYTES)
    private val keyBuffer = directBuffer(MAX_KEYS * PodLayout.KEYFRAME_ROW_BYTES)
    private val textBlob = directBuffer(16 * 1024)
    private val status = EngineStatus()

    // =========================================================================
    // Ciclo de vida do motor
    // =========================================================================
    private val lifecycleLock = Any()
    private val lifecycleThread = Executors.newSingleThreadExecutor { r -> Thread(r, "aurea-ciclo") }
    @Volatile private var ready = false
    private var destroyed = false
    private var pendingSurface: Triple<Surface, Int, Int>? = null
    private var statusLoop: RenderLoop? = null
    private var lastRevision = -1
    private var lastPlayhead = -1
    private var lastThumbGen = -1
    private var lastPerfNs = 0L
    private var uiFrames = 0
    private var scrubbing = false

    init {
        lifecycleThread.execute {
            synchronized(lifecycleLock) {
                if (destroyed) return@synchronized
                val dirs = directories()
                val debug = (app.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
                val ok = engine.initialize(displayRefreshRate(), dirs.cache.absolutePath, dirs.projects.absolutePath, debug)
                ready = ok
                if (ok) pendingSurface?.let { (s, w, h) -> engine.attachSurface(s, w, h) }
                pendingSurface = null
                main.post {
                    if (ok) {
                        startThermalWatch()
                        engineReady = true
                        catalog = readCatalog()
                        startStatusLoop()
                    } else {
                        errorMessage = "Não foi possível iniciar o motor gráfico (Vulkan) neste aparelho."
                    }
                }
            }
        }
        refreshProjects()
    }

    private data class Dirs(val cache: File, val projects: File, val thumbs: File)

    private fun directories(): Dirs {
        val app = getApplication<Application>()
        val cache = File(app.cacheDir, "motor").apply { mkdirs() }
        val projects = File(app.filesDir, "projetos").apply { mkdirs() }
        val thumbs = File(projects, ".miniaturas").apply { mkdirs() }
        return Dirs(cache, projects, thumbs)
    }

    private fun displayRefreshRate(): Float {
        val dm = getApplication<Application>().getSystemService(DisplayManager::class.java)
        return dm?.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate ?: 60f
    }

    /** SurfaceHolder.surfaceCreated. */
    fun attachSurface(surface: Surface, width: Int, height: Int) {
        synchronized(lifecycleLock) {
            if (!ready) {
                pendingSurface = Triple(surface, width, height)
                return
            }
            if (!engine.attachSurface(surface, width, height)) {
                errorMessage = "O preview não conseguiu usar a superfície de vídeo."
            }
        }
    }

    /** SurfaceHolder.surfaceChanged. */
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

    /** SurfaceHolder.surfaceDestroyed. Bloqueia até a GPU largar a janela. */
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
        saveIfDirty()
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

    // --- Temperatura do aparelho -----------------------------------------------------------
    /** PowerManager (Android 10+): sob calor o preview reduz o que é caro; o export não muda. */
    private var thermalListener: Any? = null

    private fun startThermalWatch() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) return
        val pm = getApplication<Application>().getSystemService(android.content.Context.POWER_SERVICE) as? android.os.PowerManager ?: return
        engine.setThermal(pm.currentThermalStatus)
        val l = android.os.PowerManager.OnThermalStatusChangedListener { status -> engine.setThermal(status) }
        pm.addThermalStatusListener(l)
        thermalListener = l
    }

    private fun stopThermalWatch() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) return
        val l = thermalListener as? android.os.PowerManager.OnThermalStatusChangedListener ?: return
        val pm = getApplication<Application>().getSystemService(android.content.Context.POWER_SERVICE) as? android.os.PowerManager ?: return
        pm.removeThermalStatusListener(l)
        thermalListener = null
    }

    override fun onCleared() {
        stopThermalWatch()
        saveIfDirty()
        shutdown()
        super.onCleared()
    }

    // =========================================================================
    // Laço de estado (vsync da UI)
    // =========================================================================
    private fun startStatusLoop() {
        if (statusLoop != null) return
        statusLoop = RenderLoop { frameTimeNanos ->
            if (!ready) return@RenderLoop
            engine.readStatus(statusBuffer)
            status.readFrom(statusBuffer)
            publish()
            uiFrames++
            if (hudVisible && frameTimeNanos - lastPerfNs > 250_000_000L) {
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

    private fun publish() {
        val p = ProjectState(
            title = project.title,
            path = project.path,
            width = status.compWidth,
            height = status.compHeight,
            fps = if (status.compFps > 0f) status.compFps else project.fps,
            durationFrames = status.duration.toInt(),
            dirty = status.dirty,
            canUndo = status.canUndo,
            canRedo = status.canRedo,
        )
        if (p != project) project = p
        if (!scrubbing && status.playhead.toInt() != playhead) playhead = status.playhead.toInt()
        if (status.playing != playing) playing = status.playing
        val scale = when {
            status.previewAuto -> "AUTO"
            status.previewDenominator <= 1 -> "FULL"
            else -> "1/${status.previewDenominator}"
        }
        val pv = PreviewState(status.previewWidth, status.previewHeight, scale, status.currentFps)
        if (pv != preview) preview = pv
        if (status.thumbnailGeneration != lastThumbGen) {
            lastThumbGen = status.thumbnailGeneration
            thumbnailGeneration = lastThumbGen
        }
        if (screen != Screen.Editor) return
        if (status.modelRevision != lastRevision) {
            lastRevision = status.modelRevision
            lastPlayhead = playhead
            lastModelChangeNs = System.nanoTime()
            refreshModel()
        } else if (playhead != lastPlayhead) {
            // Só o detalhe depende do playhead (valor animado, keyframe aqui).
            lastPlayhead = playhead
            refreshDetail()
            refreshEffectParams()
        }
        autosaveIfIdle()
    }

    // --- Autosave -------------------------------------------------------------
    //
    //  A A.01 só gravava ao ir para segundo plano; um processo morto em
    //  primeiro plano (queda, reinstalação, o sistema matando) perdia tudo
    //  desde a última gravação. O journal do motor ainda não está ligado, então
    //  o store grava o projeto quando ele está SUJO e PARADO: nenhum gesto
    //  aberto, sem tocar, sem scrub, [AUTOSAVE_IDLE_NS] depois da última
    //  mudança. Sem miniatura (é a parte cara); ela sai ao fechar e ao ir
    //  para segundo plano.
    private var lastModelChangeNs = 0L
    private var autosaving = false

    private fun autosaveIfIdle() {
        if (!status.dirty || autosaving || playing || scrubbing || gestureDepth > 0) return
        if (System.nanoTime() - lastModelChangeNs < AUTOSAVE_IDLE_NS) return
        val path = project.path ?: return
        if (layers.isEmpty()) return
        autosaving = true
        viewModelScope.launch {
            withContext(Dispatchers.IO) { saveBlocking(path, withThumbnail = false) }
            autosaving = false
        }
    }

    /** Relê tudo o que depende do modelo. Barato: dezenas de linhas POD. */
    private fun refreshModel() {
        layers = readLayers()
        refreshMarkers()
        editMode = engine.editMode()
        precompDepth = engine.precompDepth()
        run {
            val out = FloatArray(3)
            if (engine.queryEnvironment(out)) environment = out.toList()
        }
        compositionName = if (precompDepth > 0) engine.compositionName() else ""
        val mb = engine.motionBlurState()
        compMotionBlur = mb > 0f
        shutterAngle = if (mb != 0f) kotlin.math.abs(mb) - 1f else 180f
        selectCreatedAfter?.let { before ->
            val created = layers.map { it.id }.filter { it !in before }
            if (created.isNotEmpty()) {
                selectCreatedAfter = null
                selection = LinkedHashSet(created)
                engine.setSelection(selection.toLongArray())
            }
        }
        val alive = layers.map { it.id }.toSet()
        if (!alive.containsAll(selection)) {
            selection = selection.filter { it in alive }.toCollection(LinkedHashSet())
            engine.setSelection(selection.toLongArray())
        }
        keyframes = layers.associate { it.id to readKeyframes(it.id) }
        refreshComposition()
        refreshDetail()
        refreshEffects()
    }

    private fun readLayers(): List<LayerRow> {
        val n = engine.queryLayers(layerBuffer, MAX_LAYERS, nameBlob)
        return List(max(0, n)) { LayerRow.read(layerBuffer, it, nameBlob) }
    }

    private fun readKeyframes(layer: Long): List<KeyframeRow> {
        val n = engine.queryKeyframes(layer, keyBuffer, MAX_KEYS)
        return List(max(0, n)) { KeyframeRow.read(keyBuffer, it) }
    }

    private fun refreshDetail() {
        val id = primary
        detail = if (id != null && engine.queryLayerDetail(id, detailBuffer)) LayerDetail.read(detailBuffer) else null
        gizmo = if (id != null) {
            val out = FloatArray(8)
            if (engine.queryGizmo(id, GIZMO_LENGTH, out)) out else null
        } else {
            null
        }
        echo = if (id != null) {
            val out = FloatArray(4)
            if (engine.queryEcho(id, out)) out.toList() else null
        } else {
            null
        }
        particles = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Particles.kind) {
            val out = FloatArray(8)
            if (engine.queryParticles(id, out)) out.toList() else null
        } else {
            null
        }
        timeRemap = if (id != null && detail?.timeRemap == true) {
            val out = FloatArray(5 + 7 * 64)
            val n = engine.queryTimeRemap(id, out)
            if (n >= 5) out.copyOf(n) else null
        } else {
            null
        }
        text3d = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Model3D.kind) {
            val f = FloatArray(5)
            engine.queryText3d(id, f)?.let { Text3DInfo(it, f[0], f[1].toInt(), floatArrayOf(f[2], f[3], f[4], 1f)) }
        } else {
            null
        }
        if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Text.kind) {
            refreshTextFont()
            val st = FloatArray(18)
            textStyle = if (engine.queryTextStyle(id, st)) st else null
            textAnimators = engine.queryTextAnimators(id)?.let { a -> List(a.size / 40) { i -> a.copyOfRange(i * 40, i * 40 + 40) } } ?: emptyList()
        } else {
            textFont = null
            textStyle = null
            textAnimators = emptyList()
        }
        textDetail = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Text.kind) {
            engine.queryText(id, textFloats)?.let { com.aurea.aurea.engine.TextDetail.of(it, textFloats) }
        } else {
            null
        }
        textPath = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Text.kind) engine.queryTextPath(id) else null
        refreshVector()
    }

    // --- Camada vetorial (Fase 7D) -----------------------------------------------
    /** Ferramenta do palco: 0 normal, 1 pontos (editar caminho), 2 mão livre. */
    var vectorTool by mutableIntStateOf(0)
        private set
    /** Documento da camada vetorial principal (nulo = não é vetorial). */
    var vectorDoc by mutableStateOf<com.aurea.aurea.engine.VectorDoc?>(null)
        private set
    var vectorGroup by mutableIntStateOf(0)
        private set
    var vectorPath by mutableIntStateOf(0)
        private set
    /** Caminho escolhido no cabeçote (com a afim para a composição). */
    var vectorPathAt by mutableStateOf<com.aurea.aurea.engine.VPathAt?>(null)
        private set
    /** Valores animáveis do grupo no cabeçote + bits animados + bits com keyframe. */
    var vectorParams by mutableStateOf<FloatArray?>(null)
        private set
    /** Ponto escolhido no modo de pontos (−1 = nenhum). */
    var vectorPoint by mutableIntStateOf(-1)
    /** Texto no caminho da camada de texto: [guia, bits da margem, perpendicular, invertido]. */
    var textPath by mutableStateOf<LongArray?>(null)
        private set
    /** Camada que recebe os traços seguintes da mão livre (0 = cria uma). */
    private var freehandLayer = 0L

    val isVectorLayer: Boolean
        get() = detail?.let { it.kind == com.aurea.aurea.ui.theme.LayerType.Shape.kind && (it.shapeTypePoints and 0xFFFF) == VECTOR_SHAPE_TYPE } == true

    private fun refreshVector() {
        val id = primary
        if (id == null || !isVectorLayer) {
            vectorDoc = null
            vectorPathAt = null
            vectorParams = null
            if (vectorTool == 1) vectorTool = 0
            return
        }
        val doc = com.aurea.aurea.engine.VectorDoc.decode(engine.vectorDocument(id), engine.vectorGroupNames(id))
        vectorDoc = doc
        val groups = doc?.groups?.size ?: 0
        if (vectorGroup >= groups) vectorGroup = max(0, groups - 1)
        val paths = doc?.groups?.getOrNull(vectorGroup)?.paths?.size ?: 0
        if (vectorPath >= paths) vectorPath = max(0, paths - 1)
        vectorPathAt = if (paths > 0) com.aurea.aurea.engine.VPathAt.of(engine.vectorPathAt(id, vectorGroup, vectorPath)) else null
        val n = vectorPathAt?.path?.v?.size ?: 0
        if (vectorPoint >= n) vectorPoint = -1
        vectorParams = if (groups > 0) engine.queryVectorParams(id, vectorGroup) else null
    }

    /** "Desenho vetorial" (preset 0, entra no modo de pontos) e formas paramétricas (1..4). */
    fun addVectorLayer(preset: Int): Long {
        val id = engine.addVectorLayer(preset)
        if (id < 0) {
            errorMessage = "Não foi possível criar a camada vetorial (erro ${-id})."
            return -1
        }
        refreshNow()
        vectorGroup = 0
        vectorPath = 0
        vectorPoint = -1
        select(id)
        if (preset == 0) vectorTool = 1
        return id
    }

    fun chooseVectorTool(tool: Int) {
        if (tool == 2 && vectorTool != 2) freehandLayer = 0L
        vectorTool = if (tool == 1 && !isVectorLayer) 0 else tool
        vectorPoint = -1
    }

    fun selectVectorPath(group: Int, path: Int) {
        vectorGroup = max(0, group)
        vectorPath = max(0, path)
        vectorPoint = -1
        refreshVector()
    }

    /** Muda uma cópia do documento e devolve ao motor. `continuing` = meio de um arrasto (mesmo passo de desfazer). */
    fun editVectorDoc(continuing: Boolean = false, change: (com.aurea.aurea.engine.VectorDoc) -> Unit) {
        val id = primary ?: return
        val doc = vectorDoc?.copyDeep() ?: return
        change(doc)
        engine.setVectorDocument(id, doc.encode(), doc.names(), continuing)
        refreshVector()
    }

    /** Edita o grupo escolhido. */
    fun editVectorGroup(continuing: Boolean = false, change: (com.aurea.aurea.engine.VGroup) -> Unit) {
        val g = vectorGroup
        editVectorDoc(continuing) { d -> d.groups.getOrNull(g)?.let(change) }
    }

    /** Forma do caminho escolhido (com keyframes de forma: grava no cabeçote). */
    fun setVectorPathShape(path: com.aurea.aurea.engine.VBezier, continuing: Boolean) {
        val id = primary ?: return
        engine.setVectorPath(id, vectorGroup, vectorPath, path.toArray(), continuing)
        refreshVector()
    }

    fun toggleVectorPathKey() {
        val id = primary ?: return
        engine.toggleVectorPathKey(id, vectorGroup, vectorPath)
        refreshNow()
    }

    fun setVectorParam(param: Int, value: Float, continuing: Boolean) {
        val id = primary ?: return
        engine.setVectorParam(id, vectorGroup, param, value, continuing)
        refreshVector()
    }

    fun toggleVectorParamKey(param: Int) {
        val id = primary ?: return
        engine.toggleVectorParamKey(id, vectorGroup, param)
        refreshNow()
    }

    fun addVectorGroup(kind: Int) {
        val id = primary ?: return
        val g = engine.addVectorGroup(id, kind)
        if (g >= 0) {
            vectorGroup = g
            vectorPath = 0
            vectorPoint = -1
            if (kind == 0) vectorTool = 1
        }
        refreshNow()
    }

    fun removeVectorGroup(group: Int) {
        val id = primary ?: return
        engine.removeVectorGroup(id, group)
        refreshNow()
    }

    fun addVectorPath(kind: Int) {
        val id = primary ?: return
        val p = engine.addVectorPath(id, vectorGroup, kind, null)
        if (p >= 0) {
            vectorPath = p
            vectorPoint = -1
            if (kind == 0) vectorTool = 1
        }
        refreshNow()
    }

    fun removeVectorPath(path: Int) {
        val id = primary ?: return
        engine.removeVectorPath(id, vectorGroup, path)
        refreshNow()
    }

    fun makeVectorPathEditable() {
        val id = primary ?: return
        engine.makeVectorPathEditable(id, vectorGroup, vectorPath)
        refreshNow()
    }

    /** Traço do dedo (px da composição) → caminho suave; os traços seguintes entram na mesma camada. */
    fun commitFreehand(xy: FloatArray) {
        if (xy.size < 8) return
        val target = if (freehandLayer != 0L && layers.any { it.id == freehandLayer }) freehandLayer else 0L
        val id = engine.addFreehandPath(target, xy, 1.5f)
        if (id < 0) {
            errorMessage = "Não foi possível criar o desenho (erro ${-id})."
            return
        }
        freehandLayer = id
        refreshNow()
        select(id)
    }

    /** SVG pelo seletor de documentos do sistema. */
    fun importSvg(uri: Uri) {
        viewModelScope.launch {
            val bytes = withContext(Dispatchers.IO) {
                runCatching { getApplication<Application>().contentResolver.openInputStream(uri)?.use { it.readBytes() } }.getOrNull()
            }
            if (bytes == null || bytes.isEmpty()) {
                errorMessage = "Não foi possível ler o SVG."
                return@launch
            }
            val name = displayName(uri)?.substringBeforeLast('.') ?: "SVG"
            val id = engine.importSvg(bytes, name)
            if (id < 0) {
                errorMessage = if (-id == ERRC_UNSUPPORTED_FORMAT) "SVG sem formas suportadas." else "Não foi possível importar o SVG (erro ${-id})."
                return@launch
            }
            refreshNow()
            select(id)
        }
    }

    /** Texto no caminho: camada-guia vetorial (0 = desliga). */
    fun setTextPath(pathLayer: Long, offset: Float, perpendicular: Boolean, reverse: Boolean) {
        val id = primary ?: return
        if (!engine.setTextPath(id, pathLayer, offset, perpendicular, reverse)) {
            showToast("A guia precisa ser uma camada vetorial")
        }
        refreshNow()
    }

    /** Receita do texto 3D da camada principal (nulo = não é texto 3D). */
    var text3d by mutableStateOf<Text3DInfo?>(null)
        private set

    fun addText3D(): Long {
        val id = engine.addText3d("Texto", 0.25f, 1, 1f, 1f, 1f)
        if (id < 0) {
            errorMessage = "Não foi possível criar o texto 3D (erro ${-id})."
            return -1
        }
        refreshNow()
        select(id)
        return id
    }

    /** Aplica a receita (a malha é gerada de novo). Digitação agrupada como no texto 2D. */
    fun setText3D(info: Text3DInfo, typing: Boolean = false, lazy: Boolean = false) {
        val id = primary ?: return
        text3d = info
        if (lazy) {
            // Arrasto (cor, profundidade): a malha acompanha em passos curtos.
            typingHandler.removeCallbacks(applyText3d)
            typingHandler.postDelayed(applyText3d, 90)
            return
        }
        if (typing) {
            if (!textEditing) {
                textEditing = true
                beginGesture("editar texto 3D")
            }
            typingHandler.removeCallbacks(closeTyping)
            typingHandler.postDelayed(closeTyping, 1000)
            // A malha segue a digitação com um respiro (não uma malha por tecla).
            typingHandler.removeCallbacks(applyText3d)
            typingHandler.postDelayed(applyText3d, 250)
            return
        }
        pushText3D(id, info)
    }

    private val applyText3d = Runnable { primary?.let { id -> text3d?.let { pushText3D(id, it) } } }

    private fun pushText3D(id: Long, info: Text3DInfo) {
        if (info.content.isBlank()) return
        engine.setText3d(id, info.content, info.depth, info.alignment, info.color[0], info.color[1], info.color[2])
        refreshNow()
    }

    /** Texto da camada principal (quando é texto). */
    var textDetail by mutableStateOf<com.aurea.aurea.engine.TextDetail?>(null)
        private set
    private val textFloats = FloatArray(13)

    private fun readCatalog(): List<EffectCatalogEntry> {
        val n = engine.queryEffectCatalog(rowBuffer, 64, textBlob)
        return List(max(0, n)) { EffectCatalogEntry.read(rowBuffer, it, textBlob) }
    }

    private fun refreshEffects() {
        val id = primary
        if (id == null) {
            effects = emptyList()
            effectParams = emptyMap()
            return
        }
        val n = engine.queryLayerEffects(id, rowBuffer, 64, textBlob)
        effects = List(max(0, n)) { LayerEffect.read(rowBuffer, it, textBlob) }
        refreshEffectParams()
    }

    private fun refreshEffectParams() {
        val id = primary ?: return
        effectParams = effects.associate { e ->
            val n = engine.queryEffectParams(id, e.effectId, rowBuffer, 64, textBlob)
            e.effectId to List(max(0, n)) { EffectParam.read(rowBuffer, it, textBlob) }
        }
    }

    /** Força a releitura na mesma volta (depois de um comando local). */
    private fun refreshNow() {
        engine.readStatus(statusBuffer)
        status.readFrom(statusBuffer)
        lastRevision = -1
        publish()
    }

    // =========================================================================
    // Comandos
    // =========================================================================
    private inline fun send(block: CommandBatch.() -> Unit) {
        engine.beginCommandBatch()
        batch.block()
        engine.submitCommands()
    }

    /**
     * Um gesto contínuo (arrasto, slider) = UM passo de desfazer. Abra no
     * começo do gesto e feche no fim; tudo que for enviado no meio desfaz junto.
     */
    fun beginGesture(label: String) {
        gestureDepth++
        send { beginUndoGroup(label) }
    }
    fun endGesture() {
        gestureDepth = max(0, gestureDepth - 1)
        send { endUndoGroup() }
        refreshNow()
    }

    /** Gestos abertos (arrasto, slider, folha de cor): o autosave espera fechar. */
    private var gestureDepth = 0

    /** Várias ações como UM passo de desfazer (ex.: apagar 3 camadas). */
    private inline fun group(label: String, block: CommandBatch.() -> Unit) {
        engine.beginCommandBatch()
        batch.beginUndoGroup(label)
        batch.block()
        batch.endUndoGroup()
        engine.submitCommands()
        refreshNow()
    }

    fun undo() {
        send { undo() }
        refreshNow()
    }

    fun redo() {
        send { redo() }
        refreshNow()
    }

    // --- Seleção -------------------------------------------------------------
    fun select(layer: Long, additive: Boolean = false) {
        selection = if (additive) {
            if (layer in selection) selection - layer else LinkedHashSet(selection).apply { add(layer) }
        } else {
            linkedSetOf(layer)
        }
        engine.setSelection(selection.toLongArray())
        refreshDetail()
        refreshEffects()
    }

    fun selectAll() {
        selection = layers.map { it.id }.toCollection(LinkedHashSet())
        engine.setSelection(selection.toLongArray())
        refreshDetail()
        refreshEffects()
    }

    fun clearSelection() {
        if (selection.isEmpty()) return
        selection = emptySet()
        engine.clearSelection()
        detail = null
        effects = emptyList()
        effectParams = emptyMap()
    }

    /** Seleção vizinha na timeline (setas ‹ › do clipe no modo compacto). */
    fun selectNeighbor(delta: Int) {
        val current = primary ?: return
        val i = layers.indexOfFirst { it.id == current }
        if (i < 0) return
        layers.getOrNull(i + delta)?.let { select(it.id) }
    }

    // --- Camadas ------------------------------------------------------------
    fun deleteLayers(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        if (editMode) {
            // Modo Edição: some e o buraco fecha (um passo de desfazer).
            engine.rippleDelete(ids.toLongArray())
            refreshNow()
        } else {
            group("apagar") { ids.forEach { deleteLayer(it) } }
        }
        clearSelection()
    }

    fun duplicateLayers(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        selectCreatedAfter = layers.map { it.id }.toSet()
        group("duplicar") { ids.forEach { duplicateLayer(it) } }
    }

    /**
     * Ids que existiam antes de um comando que CRIA camadas (duplicar). O
     * comando é assíncrono; quando o modelo novo chega, as camadas que
     * surgiram viram a seleção — a cópia fica escolhida, como na A.01
     * (`duplicateLayer` selecionava a cópia). Sem isto a lixeira logo depois
     * apagava o ORIGINAL.
     */
    private var selectCreatedAfter: Set<Long>? = null

    fun renameLayer(layer: Long, name: String) {
        send { setLayerName(layer, name) }
        refreshNow()
    }

    fun setLayerVisible(layer: Long, visible: Boolean) {
        send { setLayerVisible(layer, visible) }
        refreshNow()
    }

    fun setLayerLocked(layer: Long, locked: Boolean) {
        send { setLayerLocked(layer, locked) }
        refreshNow()
    }

    /**
     * Modo de mesclagem da camada principal (`aurea::BlendMode`: 0 Normal,
     * 1 Add…). Um comando = um passo de desfazer. O painel Mesclagem só oferece
     * os modos que o renderer já desenha.
     */
    fun setBlendMode(mode: Int, layer: Long? = primary) {
        val id = layer ?: return
        send { setLayerBlendMode(id, mode) }
        refreshNow()
    }

    /** Divide as camadas escolhidas no playhead (as que o cobrem). */
    fun splitAtPlayhead(ids: Collection<Long> = selection) {
        val t = playhead
        val targets = layers.filter { it.id in ids && t > it.startFrame && t < it.endFrame }
        if (targets.isEmpty()) return
        group("dividir") { targets.forEach { splitLayer(it.id, t) } }
    }

    /**
     * Move camadas no tempo (arrasto do corpo do clipe). O conteúdo anda junto:
     * o deslocamento interno não muda. Use dentro de [beginGesture]/[endGesture].
     */
    fun moveLayers(ids: Collection<Long>, deltaFrames: Int) {
        if (deltaFrames == 0) return
        val rows = layers.filter { it.id in ids }
        val minStart = rows.minOfOrNull { it.startFrame } ?: return
        val d = max(deltaFrames, -minStart)   // nada antes do instante 0
        if (d == 0) return
        send { rows.forEach { setLayerTimeRange(it.id, it.startFrame + d, it.endFrame + d) } }
        refreshNow()
    }

    /**
     * Trim do INÍCIO para `newStart`: o conteúdo fica parado no tempo e só a
     * borda anda (o deslocamento interno compensa). Vídeo não passa do começo
     * da mídia; nenhuma camada fica com menos de 1 frame.
     */
    fun trimStart(layer: Long, newStart: Int) {
        val d = detailOf(layer) ?: return
        var start = min(newStart, d.endFrame - 1)
        var offset = d.offsetFrames + (start - d.startFrame)
        if (d.sourceFrames > 0 && offset < 0) {
            start -= offset
            offset = 0
        }
        start = max(0, start)
        if (start == d.startFrame) return
        send { setLayerTimeRange(layer, start, d.endFrame, offset) }
        refreshNow()
    }

    /** Trim do FIM para `newEnd`. Vídeo não passa do fim da mídia. */
    fun trimEnd(layer: Long, newEnd: Int) {
        val d = detailOf(layer) ?: return
        var end = max(newEnd, d.startFrame + 1)
        if (d.sourceFrames > 0) end = min(end, d.startFrame - d.offsetFrames + d.sourceFrames)
        if (end == d.endFrame) return
        send { setLayerTimeRange(layer, d.startFrame, end) }
        refreshNow()
    }

    private fun detailOf(layer: Long): LayerDetail? =
        if (engine.queryLayerDetail(layer, detailBuffer)) LayerDetail.read(detailBuffer) else null

    /**
     * Detalhe de QUALQUER camada no playhead (transform avaliado, tamanho da
     * mídia). Para o hit-test do palco e afins; leitura síncrona e barata.
     */
    fun queryDetail(layer: Long): LayerDetail? = detailOf(layer)

    /**
     * Reordena na vertical. `displayIndex` é a posição na timeline (0 = topo,
     * a camada da frente).
     */
    fun reorderLayer(layer: Long, displayIndex: Int) {
        val n = layers.size
        if (n == 0) return
        val clamped = displayIndex.coerceIn(0, n - 1)
        send { reorderLayer(layer, n - 1 - clamped) }
        refreshNow()
    }

    // --- Transform (com semântica de keyframe) -------------------------------
    /**
     * Muda uma propriedade de transform da camada principal. Se a propriedade
     * está ANIMADA, cria/atualiza o keyframe no playhead (como no After
     * Effects); senão muda o valor fixo. Rotação em graus; escala em fração
     * (1 = 100 %); opacidade 0..1; posição/âncora em px da composição.
     */
    fun setTransform(property: Int, value: Float, layer: Long? = primary) {
        val id = layer ?: return
        val d = if (id == primary) detail else detailOf(id)
        d ?: return
        if (d.isAnimated(property)) {
            send { insertKeyframe(id, property, NO_EFFECT, 0, d.localPlayhead, value) }
        } else {
            send {
                when (property) {
                    TrackProperty.POSITION_X -> setPosition(id, value, d.position[1], d.position[2])
                    TrackProperty.POSITION_Y -> setPosition(id, d.position[0], value, d.position[2])
                    TrackProperty.POSITION_Z -> setPosition(id, d.position[0], d.position[1], value)
                    TrackProperty.SCALE_X -> setScale(id, value, d.scale[1], d.scale[2])
                    TrackProperty.SCALE_Y -> setScale(id, d.scale[0], value, d.scale[2])
                    TrackProperty.ROTATION_X -> setRotation(id, value, d.rotation[1], d.rotation[2])
                    TrackProperty.ROTATION_Y -> setRotation(id, d.rotation[0], value, d.rotation[2])
                    TrackProperty.ROTATION_Z -> setRotation(id, d.rotation[0], d.rotation[1], value)
                    TrackProperty.ANCHOR_X -> setAnchor(id, value, d.anchor[1], d.anchor[2])
                    TrackProperty.ANCHOR_Y -> setAnchor(id, d.anchor[0], value, d.anchor[2])
                    TrackProperty.OPACITY -> setOpacity(id, value)
                    else -> {}
                }
            }
        }
        refreshNow()
    }

    /** Duas propriedades de uma vez (arrastar a camada no palco: X e Y). */
    fun setTransform2(pa: Int, va: Float, pb: Int, vb: Float, layer: Long? = primary) {
        val id = layer ?: return
        val d = (if (id == primary) detail else detailOf(id)) ?: return
        val animated = d.isAnimated(pa) || d.isAnimated(pb)
        send {
            if (animated) {
                insertKeyframe(id, pa, NO_EFFECT, 0, d.localPlayhead, va)
                insertKeyframe(id, pb, NO_EFFECT, 0, d.localPlayhead, vb)
            } else if (pa == TrackProperty.POSITION_X && pb == TrackProperty.POSITION_Y) {
                setPosition(id, va, vb, d.position[2])
            } else if (pa == TrackProperty.SCALE_X && pb == TrackProperty.SCALE_Y) {
                setScale(id, va, vb, d.scale[2])
            } else if (pa == TrackProperty.ANCHOR_X && pb == TrackProperty.ANCHOR_Y) {
                setAnchor(id, va, vb, d.anchor[2])
            }
        }
        refreshNow()
    }

    /**
     * Losango de keyframe de um grupo de propriedades (ex.: posição = X+Y).
     * Keyframe no playhead em todas → apaga; senão → cria com o valor atual.
     */
    fun toggleTransformKeyframe(properties: IntArray, layer: Long? = primary) {
        val id = layer ?: return
        val d = (if (id == primary) detail else detailOf(id)) ?: return
        val allHere = properties.all { d.hasKeyAtPlayhead(it) }
        group(if (allHere) "remover keyframe" else "adicionar keyframe") {
            properties.forEach { p ->
                if (allHere) deleteKeyframe(id, p, NO_EFFECT, 0, d.localPlayhead)
                else insertKeyframe(id, p, NO_EFFECT, 0, d.localPlayhead, transformValue(d, p))
            }
        }
    }

    private fun transformValue(d: LayerDetail, p: Int): Float = when (p) {
        TrackProperty.POSITION_X -> d.position[0]
        TrackProperty.POSITION_Y -> d.position[1]
        TrackProperty.POSITION_Z -> d.position[2]
        TrackProperty.SCALE_X -> d.scale[0]
        TrackProperty.SCALE_Y -> d.scale[1]
        TrackProperty.SCALE_Z -> d.scale[2]
        TrackProperty.ROTATION_X -> d.rotation[0]
        TrackProperty.ROTATION_Y -> d.rotation[1]
        TrackProperty.ROTATION_Z -> d.rotation[2]
        TrackProperty.ANCHOR_X -> d.anchor[0]
        TrackProperty.ANCHOR_Y -> d.anchor[1]
        TrackProperty.ANCHOR_Z -> d.anchor[2]
        TrackProperty.OPACITY -> d.opacity
        TrackProperty.SKEW_X -> d.skew[0]
        TrackProperty.SKEW_Y -> d.skew[1]
        else -> 0f
    }

    // --- Keyframes (qualquer trilha) ------------------------------------------
    /** Move um keyframe (tempo LOCAL da camada). */
    fun moveKeyframe(layer: Long, key: KeyframeRow, toLocalFrame: Int) {
        if (toLocalFrame == key.time) return
        send { moveKeyframe(layer, key.property, key.effectIndex, key.paramIndex, key.time, toLocalFrame) }
        refreshNow()
    }

    fun deleteKeyframe(layer: Long, key: KeyframeRow) {
        send { deleteKeyframe(layer, key.property, key.effectIndex, key.paramIndex, key.time) }
        refreshNow()
    }

    /** Interpolação/easing de um keyframe (`interp` = `aurea::Interpolation`). */
    fun setKeyframeEasing(layer: Long, key: KeyframeRow, interp: Int, bx1: Float, by1: Float, bx2: Float, by2: Float) {
        send { setKeyframeInterpolation(layer, key.property, key.effectIndex, key.paramIndex, key.time, interp, bx1, by1, bx2, by2) }
        refreshNow()
    }

    // --- Efeitos -------------------------------------------------------------
    /** Adiciona o efeito em TODAS as camadas escolhidas. */
    fun addEffect(typeId: Int, ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        group("adicionar efeito") { ids.forEach { addEffect(it, typeId) } }
    }

    fun removeEffect(effectId: Int) {
        val id = primary ?: return
        send { removeEffect(id, effectId) }
        refreshNow()
    }

    fun setEffectEnabled(effectId: Int, enabled: Boolean) {
        val id = primary ?: return
        send { setEffectEnabled(id, effectId, enabled) }
        refreshNow()
    }

    fun reorderEffect(effectId: Int, newIndex: Int) {
        val id = primary ?: return
        send { reorderEffect(id, effectId, newIndex) }
        refreshNow()
    }

    /**
     * Parâmetro escalar de efeito. Animado → keyframe no playhead.
     * `component` só importa para tipos de vários componentes.
     */
    fun setEffectParam(effectId: Int, param: EffectParam, value: Float, component: Int = 0) {
        val id = primary ?: return
        val d = detail ?: return
        send {
            if (param.animated) {
                insertKeyframe(id, TrackProperty.EFFECT_PARAM, effectId, param.index * 4 + component, d.localPlayhead, value)
            } else if (ParamType.componentCount(param.type) > 1) {
                val v = param.value.copyOf()
                v[component] = value
                setEffectVector(id, effectId, param.index, v[0], v[1], v[2], v[3])
            } else {
                setEffectParam(id, effectId, param.index, value)
            }
        }
        refreshNow()
    }

    /** Losango de keyframe de um parâmetro de efeito (todos os componentes). */
    fun toggleEffectKeyframe(effectId: Int, param: EffectParam) {
        val id = primary ?: return
        val d = detail ?: return
        val comps = max(1, ParamType.componentCount(param.type))
        val here = (keyframes[id] ?: emptyList()).filter {
            it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effectId &&
                it.paramIndex / 4 == param.index && it.time == d.localPlayhead
        }
        group(if (here.isNotEmpty()) "remover keyframe" else "adicionar keyframe") {
            if (here.isNotEmpty()) {
                here.forEach { deleteKeyframe(id, it.property, it.effectIndex, it.paramIndex, it.time) }
            } else {
                for (c in 0 until comps) {
                    insertKeyframe(id, TrackProperty.EFFECT_PARAM, effectId, param.index * 4 + c, d.localPlayhead, param.value[c])
                }
            }
        }
    }

    // --- Reprodução ------------------------------------------------------------
    private fun frameToNs(frame: Int): Long {
        val fps = if (project.fps > 0f) project.fps.toDouble() else 30.0
        return (frame / fps * 1_000_000_000.0).toLong()
    }

    private fun clampFrame(frame: Int) = frame.coerceIn(0, max(0, project.durationFrames - 1))

    fun play() = send { play() }
    fun pause() = send { pause() }
    fun togglePlayback() = send { togglePlayback() }

    fun seek(frame: Int) {
        val f = clampFrame(frame)
        send { seek(frameToNs(f)) }
        playhead = f
    }

    /** Scrub: o playhead da UI segue o dedo; o decoder coalesce os pedidos. */
    fun scrubStart(frame: Int) {
        scrubbing = true
        val f = clampFrame(frame)
        send { scrubBegin(); scrub(frameToNs(f)) }
        playhead = f
    }

    fun scrubTo(frame: Int) {
        val f = clampFrame(frame)
        if (f == playhead) return
        send { scrub(frameToNs(f)) }
        playhead = f
    }

    fun scrubEnd() {
        send { scrubEnd() }
        scrubbing = false
        refreshDetail()
        refreshEffectParams()
    }

    fun step(frames: Int) = send { step(frames) }

    /** Vai ao keyframe anterior/seguinte da camada principal (A.01: |◀ ▶|). */
    fun stepToKeyframe(direction: Int): Boolean {
        val id = primary ?: return false
        val d = detail ?: return false
        val times = (keyframes[id] ?: emptyList()).map { d.timelineFrame(it.time) }.distinct().sorted()
        val target = if (direction > 0) times.firstOrNull { it > playhead } else times.lastOrNull { it < playhead }
        target ?: return false
        seek(target)
        return true
    }

    /**
     * Reprodução em loop. O status do motor não traz esse flag, então o store
     * guarda o último pedido (o motor nasce desligado) — é o que a casca pinta
     * no play e marca no menu da linha do tempo.
     */
    var looping by mutableStateOf(false)
        private set

    fun setLoop(loop: Boolean) {
        looping = loop
        send { setLoop(loop) }
    }

    fun setPreviewScale(automatic: Boolean, numerator: Int = 1, denominator: Int = 1) =
        send { setPreviewScale(automatic, numerator, denominator) }

    fun toggleHud() {
        hudVisible = !hudVisible
    }

    // --- Composição (⚙ Projeto) ---------------------------------------------
    /** Ajustes da composição atual, relidos do motor a cada mudança do modelo. */
    var composition by mutableStateOf<CompositionSettings?>(null)
        private set
    private val compBuffer = DoubleArray(10)

    private fun refreshComposition() {
        val id = engine.queryComposition(compBuffer)
        composition = if (id == 0L) null else CompositionSettings(
            id = id,
            width = compBuffer[0].toInt(),
            height = compBuffer[1].toInt(),
            fps = compBuffer[2],
            durationFrames = compBuffer[3].toInt(),
            background = listOf(compBuffer[4].toFloat(), compBuffer[5].toFloat(), compBuffer[6].toFloat(), compBuffer[7].toFloat()),
            capLong = compBuffer[8].toInt(),
            capShort = compBuffer[9].toInt(),
        )
    }

    /** Tamanho da composição (px). O motor recusa acima do teto do aparelho. */
    fun setCompositionSize(width: Int, height: Int) {
        val c = composition ?: return
        send { setCompositionSize(c.id, width and 1.inv(), height and 1.inv()) }
        refreshNow()
    }

    fun setCompositionFps(fps: Double) {
        val c = composition ?: return
        send { setCompositionFps(c.id, fps) }
        refreshNow()
    }

    fun setCompositionDuration(frames: Int) {
        val c = composition ?: return
        send { setCompositionDuration(c.id, max(1, frames)) }
        refreshNow()
    }

    /** Fundo em RGBA sRGB — o motor guarda a cor como exibida e lineariza ao compor. */
    fun setCompositionBackground(r: Float, g: Float, b: Float, a: Float = 1f) {
        val c = composition ?: return
        send { setCompositionBackground(c.id, r, g, b, a) }
        refreshNow()
    }

    // =========================================================================
    // Importação
    // =========================================================================
    /** Vídeo do seletor do sistema. O motor guarda a URI e abre descritores por ela. */
    fun importVideo(uri: Uri) {
        takePermission(uri)
        val name = displayName(uri) ?: "Vídeo"
        busyMessage = "Importando vídeo…"
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) { engine.importVideo(uri.toString(), name) }
            busyMessage = null
            if (id < 0) {
                errorMessage = "Não foi possível importar o vídeo (erro ${-id})."
                return@launch
            }
            refreshNow()
            select(id)
        }
    }

    /**
     * Arquivo de áudio (ou o som de um vídeo, pelo mesmo caminho): o motor
     * sonda a trilha e cria a camada de áudio no topo. Sem trilha legível, o
     * erro diz isso — nada de camada muda fingindo que importou.
     */
    fun importAudio(uri: Uri) {
        takePermission(uri)
        val name = displayName(uri)?.substringBeforeLast('.') ?: "Áudio"
        busyMessage = "Importando áudio…"
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) { engine.importAudio(uri.toString(), name) }
            busyMessage = null
            if (id < 0) {
                errorMessage = if (-id == ERRC_UNSUPPORTED_FORMAT) {
                    "Esse arquivo não tem som que este aparelho consiga ler."
                } else {
                    "Não foi possível importar o áudio (erro ${-id})."
                }
                return@launch
            }
            refreshNow()
            select(id)
        }
    }

    /** "Extrair o áudio": o som vira camada própria e o vídeo fica mudo (um passo de desfazer). */
    fun extractAudio(layer: Long? = primary) {
        val id = layer ?: return
        val created = engine.extractAudio(id)
        if (created < 0) {
            errorMessage = "Este vídeo não tem som para extrair."
            return
        }
        refreshNow()
        select(created)
        showToast("Áudio extraído · o vídeo ficou mudo")
    }

    /** Waveform de uma camada (ver `Engine::query_waveform`). 0 = sem som / motor não pronto. */
    fun queryWaveform(layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: java.nio.ByteBuffer): Int =
        if (engineReady) engine.queryWaveform(layer, startFrame, framesPerBucket, count, out) else 0

    /** A janela do editor voltou a aparecer: o preview é reapresentado. */
    fun invalidatePreview() {
        if (engineReady) engine.invalidate()
    }

    // --- Texto ------------------------------------------------------------------
    /** Botão "Texto": camada nova no centro, já escolhida. */
    fun addText(): Long {
        val id = engine.addText("Texto")
        if (id < 0) {
            errorMessage = "Não foi possível criar o texto (erro ${-id})."
            return -1
        }
        refreshNow()
        select(id)
        return id
    }

    /**
     * Digitação: um passo de desfazer por "rajada" (fecha depois de 1 s sem
     * tecla), não um por letra.
     */
    var textEditing = false
        private set
    private val typingHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private val closeTyping = Runnable {
        if (textEditing) {
            textEditing = false
            endGesture()
        }
    }

    fun setTextContent(content: String) {
        val id = primary ?: return
        if (!textEditing) {
            textEditing = true
            beginGesture("editar texto")
        }
        typingHandler.removeCallbacks(closeTyping)
        typingHandler.postDelayed(closeTyping, 1000)
        send { setTextContent(id, content) }
        refreshDetail()
    }

    fun setTextSize(size: Float) {
        val id = primary ?: return
        send { setTextSize(id, size.coerceIn(1f, 2000f)) }
        refreshDetail()
    }

    fun setTextColor(r: Float, g: Float, b: Float, a: Float) {
        val id = primary ?: return
        send { setTextColor(id, r, g, b, a) }
        refreshDetail()
    }

    fun setTextAlignment(alignment: Int) {
        val id = primary ?: return
        send { setTextAlignment(id, alignment) }
        refreshNow()
    }

    fun setTextStrokeWidth(width: Float) {
        val id = primary ?: return
        send { setTextStrokeWidth(id, width.coerceIn(0f, 200f)) }
        refreshDetail()
    }

    fun setTextStrokeColor(r: Float, g: Float, b: Float, a: Float) {
        val id = primary ?: return
        send { setTextStrokeColor(id, r, g, b, a) }
        refreshDetail()
    }

    // --- Gizmo 3D ---------------------------------------------------------------------------
    /** Setas da camada 3D escolhida: origem e pontas X, Y, Z em px da composição. */
    var gizmo by mutableStateOf<FloatArray?>(null)
        private set

    /** Arrasto numa seta: anda `amount` unidades do mundo no eixo (0 X, 1 Y, 2 Z). */
    fun gizmoDrag(axis: Int, amount: Float) {
        val id = primary ?: return
        val d = detail ?: return
        val out = FloatArray(3)
        if (!engine.gizmoMoveLocal(id, axis, amount, out)) return
        val animated = d.isAnimated(TrackProperty.POSITION_X) || d.isAnimated(TrackProperty.POSITION_Y) || d.isAnimated(TrackProperty.POSITION_Z)
        send {
            if (animated) {
                insertKeyframe(id, TrackProperty.POSITION_X, NO_EFFECT, 0, d.localPlayhead, out[0])
                insertKeyframe(id, TrackProperty.POSITION_Y, NO_EFFECT, 0, d.localPlayhead, out[1])
                insertKeyframe(id, TrackProperty.POSITION_Z, NO_EFFECT, 0, d.localPlayhead, out[2])
            } else {
                setPosition(id, out[0], out[1], out[2])
            }
        }
        refreshNow()
    }

    // --- Ambiente 3D (HDRI) --------------------------------------------------------------
    /** {tem HDRI, intensidade, giro°}. */
    var environment by mutableStateOf(listOf(0f, 1f, 0f))
        private set

    fun importHdri(uri: Uri) {
        val name = displayName(uri) ?: "ambiente.hdr"
        if (!name.lowercase().endsWith(".hdr")) {
            errorMessage = "Use um HDRI .hdr (Radiance)."
            return
        }
        busyMessage = "Carregando o HDRI…"
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) {
                val file = copyModelToSandbox(uri, "hdr") ?: return@withContext -1_000L
                engine.importHdri(file.absolutePath)
            }
            busyMessage = null
            if (id < 0) errorMessage = "Não deu para ler esse HDRI (${if (id == -1_000L) "arquivo" else "erro ${-id}"})."
            else showToast("HDRI aplicado aos modelos 3D")
            refreshNow()
        }
    }

    fun clearHdri() {
        engine.clearHdri()
        refreshNow()
    }

    fun setEnvironment(intensity: Float, rotation: Float) {
        engine.setEnvironment(intensity, rotation)
        refreshNow()
    }

    // --- Pré-composição (grupo) ----------------------------------------------------------
    /** 0 = composição principal; > 0 = dentro de uma pré-composição. */
    var precompDepth by mutableStateOf(0)
        private set
    var compositionName by mutableStateOf("")
        private set

    /** "Agrupar": as camadas viram uma pré-composição (mesmo visual, tempos iguais). */
    fun precompose(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val id = engine.precompose(ids.toLongArray())
        if (id < 0) {
            errorMessage = "Não foi possível agrupar (erro ${-id})."
            return
        }
        refreshNow()
        select(id)
        showToast("Agrupado · toque em Editar o grupo para mexer dentro")
    }

    /** "Desagrupar": as camadas voltam para cá, no mesmo lugar e tempo da tela. */
    fun ungroupPrecomp(layer: Long) {
        val why = engine.ungroupPrecomp(layer)
        if (why != null) {
            errorMessage = "Não dá para desagrupar: $why (o resultado mudaria)."
            return
        }
        selection = LinkedHashSet()
        refreshNow()
        showToast("Desagrupado")
    }

    fun openPrecomp(layer: Long) {
        if (!engine.openPrecomp(layer)) return
        selection = LinkedHashSet()
        refreshNow()
    }

    fun closePrecomp() {
        if (!engine.closePrecomp()) return
        selection = LinkedHashSet()
        refreshNow()
    }

    // --- Rastreio de ponto / estabilização ---------------------------------------------------
    /** Esperando o toque no palco: null = não; senão, se é para estabilizar. */
    var pointPick by mutableStateOf<Boolean?>(null)
        private set
    var tracking by mutableStateOf(false)
        private set
    private val trackHandler = android.os.Handler(android.os.Looper.getMainLooper())

    fun beginPointPick(stabilize: Boolean) {
        val row = layers.firstOrNull { it.id == primary }
        if (row == null || row.kind != com.aurea.aurea.ui.theme.LayerType.Video.kind) {
            showToast("Escolha uma camada de vídeo")
            return
        }
        pointPick = stabilize
        showToast("Toque no ponto a seguir (um detalhe com contraste)")
    }

    fun cancelPointPick() { pointPick = null; pickCursor = null }

    /** Mira do rastreio de ponto (px da composição): segue o dedo; no rastreio, fica no ponto. */
    var pickCursor by mutableStateOf<androidx.compose.ui.geometry.Offset?>(null)
    /** Tamanho do bloco e da janela de busca do rastreio, em px da composição (desenho da mira). */
    var pickBoxes by mutableStateOf(floatArrayOf(17f, 65f))

    /** Toque em (x, y) — px da camada, no primeiro quadro dela. */
    fun finishPointPick(x: Float, y: Float) {
        val stabilize = pointPick ?: return
        val id = primary ?: return
        pointPick = null
        if (tracking) { pickCursor = null; return }
        tracking = true
        showToast(if (stabilize) "Estabilizando…" else "Rastreando o ponto…")
        lifecycleThread.execute {
            val tracked = IntArray(1)
            val r = synchronized(lifecycleLock) { if (ready) engine.trackPoint(id, x, y, stabilize, tracked) else -1L }
            trackHandler.post {
                tracking = false
                pickCursor = null
                refreshNow()
                when {
                    r >= 0 && !stabilize -> { select(r); showToast("Rastreio: ${tracked[0]} quadros · o Nulo segue o ponto") }
                    r >= 0 -> showToast("Estabilizado em ${tracked[0]} quadros")
                    else -> showToast("Não deu para seguir este ponto (${tracked[0]} quadros). Tente um detalhe com mais contraste.")
                }
            }
        }
    }

    // --- Eco e RGB no tempo -------------------------------------------------------------------
    /** {cópias, atraso, queda, atraso RGB} da camada escolhida. */
    var echo by mutableStateOf<List<Float>?>(null)
        private set

    fun setEcho(count: Int, delay: Float, decay: Float) {
        val id = primary ?: return
        engine.setEcho(id, count, delay, decay)
        refreshNow()
    }

    fun setRgbTime(delay: Float) {
        val id = primary ?: return
        engine.setRgbTime(id, delay)
        refreshNow()
    }

    // --- Transições ------------------------------------------------------------------------
    fun setTransition(out: Boolean, type: Int, frames: Int) {
        val id = primary ?: return
        engine.setTransition(id, out, type, frames)
        refreshNow()
    }

    // --- Partículas ------------------------------------------------------------------------
    /** Parâmetros da camada de partículas escolhida (8, ver Engine::query_particles). */
    var particles by mutableStateOf<List<Float>?>(null)
        private set

    fun addParticles(preset: Int = 0): Long {
        val id = engine.addParticles(preset)
        if (id < 0) {
            errorMessage = "Não foi possível criar as partículas (erro ${-id})."
            return -1
        }
        refreshNow()
        select(id)
        return id
    }

    fun applyParticlePreset(preset: Int) {
        val id = primary ?: return
        engine.applyParticlePreset(id, preset)
        refreshNow()
    }

    fun setParticleParam(param: Int, value: Float) {
        val id = primary ?: return
        engine.setParticleParam(id, param, value)
        refreshDetail()
    }

    // --- Remapeamento de tempo -----------------------------------------------------------
    /** Rampa pronta (0 linear, 1 suave, 2 herói, 3 acelerar, 4 desacelerar); −1 = sem rampa. */
    fun applySpeedRamp(preset: Int) {
        val id = primary ?: return
        if (preset < 0) engine.setTimeRemap(id, false) else engine.applySpeedRamp(id, preset)
        refreshNow()
    }

    // --- Desfoque de movimento ---------------------------------------------------------
    var compMotionBlur by mutableStateOf(false)
        private set
    var shutterAngle by mutableStateOf(180f)
        private set

    fun setLayerMotionBlur(layer: Long, on: Boolean) {
        engine.setMotionBlur(layer, on)
        refreshNow()
        showToast(if (on) "Desfoque de movimento ligado" else "Desfoque de movimento desligado")
    }

    // --- Rastreio de câmera 3D ------------------------------------------------------------
    /** Estado da análise (ver `camera_track_status`) e a mensagem do motor. */
    data class CameraTrackUi(
        val state: Int, val progress: Float, val frames: Int, val solved: Int, val tracks: Int, val points: Int,
        val errorPx: Float, val confidence: Float, val fovDeg: Float, val rotationOnly: Boolean, val cached: Boolean, val message: String,
    )
    var cameraTrack by mutableStateOf<CameraTrackUi?>(null)
        private set
    private var cameraTrackPoll: kotlinx.coroutines.Job? = null

    private fun readCameraTrack(): CameraTrackUi {
        val f = FloatArray(11)
        val msg = engine.cameraTrackStatus(f) ?: ""
        return CameraTrackUi(f[0].toInt(), f[1], f[2].toInt(), f[3].toInt(), f[4].toInt(), f[5].toInt(), f[6], f[7], f[8],
            f[9] > 0.5f, f[10] > 0.5f, msg)
    }

    /** Analisa a câmera do vídeo escolhido em segundo plano (0 rápido, 1 equilibrado, 2 alta). */
    fun startCameraTrack(mode: Int) {
        val id = primary ?: return
        if (!engine.startCameraTrack(id, mode)) {
            errorMessage = "Não foi possível analisar esta camada (precisa ser um vídeo com pelo menos 10 quadros)."
            return
        }
        cameraTrackPoll?.cancel()
        cameraTrackPoll = viewModelScope.launch {
            while (true) {
                val st = readCameraTrack()
                cameraTrack = st
                if (st.state != 1) break
                kotlinx.coroutines.delay(150)
            }
        }
    }

    /** Pontos seguidos no quadro do cabeçote (x, y, estado)×n, px da composição; nulo = nada a mostrar. */
    var cameraFeatures by mutableStateOf<FloatArray?>(null)
        private set
    private val featureBuf = FloatArray(3 * 1500)

    /** Chamado a cada atualização: mostra os pontos enquanto o painel de rastreio está aberto. */
    fun refreshCameraFeatures(show: Boolean) {
        val st = cameraTrack
        if (!show || st == null || st.state != 2) {
            if (cameraFeatures != null) cameraFeatures = null
            return
        }
        val n = engine.cameraTrackFeatures(playhead.toLong(), featureBuf)
        cameraFeatures = if (n > 0) featureBuf.copyOf(n * 3) else null
    }

    fun cancelCameraTrack() {
        engine.cancelCameraTrack()
        cameraTrack = readCameraTrack()
    }

    /** Cria a câmera rastreada e o Nulo de referência da cena. */
    fun applyCameraTrack() {
        val id = engine.applyCameraTrack()
        if (id < 0) {
            errorMessage = "Não foi possível criar a câmera (erro ${-id})."
            return
        }
        refreshNow()
        select(id)
        showToast("Câmera rastreada criada · ligue modelos 3D ao Nulo da cena")
    }

    /** Curva de tempo da camada principal (ver `query_time_remap`); nulo = desligada. */
    var timeRemap by mutableStateOf<FloatArray?>(null)
        private set

    /** Ponto novo na curva de tempo (no valor que ela já tem). Devolve o índice. */
    fun remapInsert(localFrame: Long): Int {
        val id = primary ?: return -1
        val i = engine.editTimeRemapKey(id, -1, localFrame, 0f, -1)
        refreshNow()
        return i
    }

    fun remapMove(index: Int, localFrame: Long, sourceFrame: Float) {
        val id = primary ?: return
        engine.editTimeRemapKey(id, index, localFrame, sourceFrame, -1)
        refreshDetail()
    }

    fun remapInterp(index: Int, interp: Int) {
        val id = primary ?: return
        val q = timeRemap ?: return
        if (index < 0 || index >= q[0].toInt()) return
        engine.editTimeRemapKey(id, index, q[5 + index * 7].toLong(), q[5 + index * 7 + 1], interp)
        refreshNow()
    }

    fun remapRemove(index: Int): Boolean {
        val id = primary ?: return false
        val ok = engine.removeTimeRemapKey(id, index)
        if (!ok) showToast("A curva precisa de pelo menos dois pontos")
        refreshNow()
        return ok
    }

    // --- Fontes ---------------------------------------------------------------------------
    /** Uma fonte (arquivo) do aparelho ou importada. */
    data class FontItem(val family: String, val style: String, val weight: Int, val italic: Boolean, val path: String, val imported: Boolean)

    var fonts by mutableStateOf<List<FontItem>>(emptyList())
        private set
    /** Fonte da camada de texto escolhida (família vazia = padrão do aparelho). */
    var textFont by mutableStateOf<FontItem?>(null)
        private set

    private fun parseFont(line: String): FontItem? {
        val p = line.split('\t')
        if (p.size < 6) return null
        return FontItem(p[0], p[1], p[2].toIntOrNull() ?: 400, p[3] == "1", p[4], p[5] == "1")
    }

    /** Lista as fontes (a varredura lê só os cabeçalhos; fora da thread de UI). */
    fun loadFonts() {
        if (fonts.isNotEmpty()) return
        viewModelScope.launch {
            val list = withContext(Dispatchers.IO) { engine.listFonts()?.lineSequence()?.mapNotNull { parseFont(it) }?.toList() ?: emptyList() }
            fonts = list
        }
    }

    /** Estilo de parágrafo da camada de texto (ver `set_text_style`: 18 valores). */
    var textStyle by mutableStateOf<FloatArray?>(null)
        private set

    fun setTextStyleValue(index: Int, value: Float) {
        val id = primary ?: return
        val v = (textStyle ?: return).copyOf()
        v[index] = value
        textStyle = v
        engine.setTextStyle(id, v)
        refreshDetail()
    }

    /** Animadores da camada de texto (40 floats cada; ver `query_text_animators`). */
    var textAnimators by mutableStateOf<List<FloatArray>>(emptyList())
        private set

    fun applyTextPreset(preset: Int) {
        val id = primary ?: return
        engine.applyTextPreset(id, preset)
        refreshDetail()
    }

    fun addTextAnimator(props: Int) {
        val id = primary ?: return
        engine.addTextAnimator(id, props)
        refreshDetail()
    }

    fun removeTextAnimator(index: Int) {
        val id = primary ?: return
        engine.removeTextAnimator(id, index)
        refreshDetail()
    }

    /** Ajuste não animável (0..6, cores 28..35) do animador. */
    fun setTextAnimatorValues(index: Int, values: Map<Int, Float>) {
        val id = primary ?: return
        val v = textAnimators.getOrNull(index)?.copyOf() ?: return
        values.forEach { (k, x) -> v[k] = x }
        engine.setTextAnimator(id, index, v)
        refreshDetail()
    }

    fun setTextAnimParam(index: Int, param: Int, value: Float) {
        val id = primary ?: return
        engine.setTextAnimParam(id, index, param, value)
        refreshDetail()
    }

    fun toggleTextAnimKey(index: Int, param: Int) {
        val id = primary ?: return
        engine.toggleTextAnimKey(id, index, param)
        refreshDetail()
    }

    fun setTextStyleValues(values: Map<Int, Float>) {
        val id = primary ?: return
        val v = (textStyle ?: return).copyOf()
        values.forEach { (k, x) -> v[k] = x }
        textStyle = v
        engine.setTextStyle(id, v)
        refreshDetail()
    }

    /** Trecho [start, end) do texto (índices do Kotlin, UTF-16) → caracteres do motor. */
    private fun cpRange(start: Int, end: Int): Pair<Int, Int>? {
        val c = textDetail?.content ?: return null
        val s = start.coerceIn(0, c.length)
        val e = end.coerceIn(0, c.length)
        if (e <= s) return null
        return c.codePointCount(0, s) to c.codePointCount(0, e)
    }

    fun setTextSpan(start: Int, end: Int, color: FloatArray?, weight: Int, scale: Float) {
        val id = primary ?: return
        val (s, e) = cpRange(start, end) ?: return
        engine.setTextSpan(id, s, e, color != null, color?.get(0) ?: 1f, color?.get(1) ?: 1f, color?.get(2) ?: 1f, weight, scale)
        refreshDetail()
    }

    fun clearTextSpans(start: Int, end: Int) {
        val id = primary ?: return
        val (s, e) = cpRange(start, end) ?: return
        engine.clearTextSpans(id, s, e)
        refreshDetail()
    }

    fun refreshTextFont() {
        val id = primary ?: run { textFont = null; return }
        val s = engine.textFont(id) ?: run { textFont = null; return }
        val p = s.split('\t')
        textFont = if (p.size >= 4) FontItem(p[0], "", p[1].toIntOrNull() ?: 400, p[2] == "1", p[3], p[3].isNotEmpty()) else null
    }

    fun applyTextFont(item: FontItem?) {
        val id = primary ?: return
        if (item == null) engine.setTextFont(id, "", 400, false, "")
        else engine.setTextFont(id, item.family, item.weight, item.italic, if (item.imported) item.path else "")
        refreshTextFont()
        refreshNow()
    }

    /** TTF/OTF do seletor do sistema: copiado para o projeto, registrado e aplicado. */
    fun importFont(uri: Uri) {
        val name = displayName(uri) ?: "fonte.ttf"
        val ext = name.substringAfterLast('.', "ttf").lowercase()
        if (ext != "ttf" && ext != "otf") {
            errorMessage = "Use uma fonte TTF ou OTF."
            return
        }
        viewModelScope.launch {
            val line = withContext(Dispatchers.IO) {
                val file = copyToDir(uri, "fontes", ext) ?: return@withContext null
                engine.importFont(file.absolutePath)
            }
            val item = line?.let { parseFont(it) }
            if (item == null) {
                errorMessage = "Não deu para ler essa fonte."
                return@launch
            }
            fonts = (fonts.filterNot { it.path == item.path } + item).sortedWith(compareBy({ it.family }, { it.italic }, { it.weight }))
            applyTextFont(item)
            showToast("Fonte ${item.family} importada")
        }
    }

    private fun copyToDir(uri: Uri, sub: String, ext: String): File? = try {
        val dir = File(File(getApplication<Application>().filesDir, "projetos"), sub).apply { mkdirs() }
        val tmp = File(dir, "importando.$ext")
        val digest = java.security.MessageDigest.getInstance("SHA-1")
        getApplication<Application>().contentResolver.openInputStream(uri)?.use { input ->
            FileOutputStream(tmp).use { out ->
                val buf = ByteArray(1 shl 16)
                while (true) {
                    val n = input.read(buf)
                    if (n <= 0) break
                    digest.update(buf, 0, n)
                    out.write(buf, 0, n)
                }
            }
        } ?: throw IllegalStateException("sem stream")
        val dst = File(dir, digest.digest().joinToString("") { "%02x".format(it) } + ".$ext")
        if (dst.exists()) tmp.delete() else tmp.renameTo(dst)
        dst
    } catch (e: Exception) {
        null
    }

    /** Câmera lenta sem "degraus": 0 repete, 1 mistura os quadros vizinhos, 2 movimento de pixels. */
    fun setFrameBlend(mode: Int) {
        val id = primary ?: return
        engine.setFrameBlend(id, mode)
        refreshNow()
    }

    /** Desfoque pelo movimento do próprio vídeo (optical flow). */
    fun setVectorBlur(layer: Long, on: Boolean) {
        engine.setVectorBlur(layer, if (on) 1f else 0f)
        refreshNow()
        showToast(if (on) "Desfoque do movimento do vídeo ligado" else "Desfoque do movimento do vídeo desligado")
    }

    fun setCompositionMotionBlur(on: Boolean) {
        engine.setCompositionMotionBlur(on)
        refreshNow()
    }

    fun changeShutterAngle(degrees: Float) {
        engine.setShutterAngle(degrees)
        refreshNow()
    }

    // --- Copiar e colar ---------------------------------------------------------------
    /** O que há para colar (bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes). */
    var clipboard by mutableStateOf(0)
        private set

    private fun afterClipboard() { clipboard = engine.clipboardState() }

    fun copyLayers(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.copyLayers(ids.toLongArray())
        afterClipboard()
        showToast(if (n == 1) "Camada copiada" else "$n camadas copiadas")
    }

    fun pasteLayers() {
        // As coladas ficam escolhidas (o mesmo caminho do duplicar).
        selectCreatedAfter = layers.map { it.id }.toSet()
        val n = engine.pasteLayers(playhead.toLong())
        if (n <= 0) selectCreatedAfter = null
        refreshNow()
        showToast(
            when {
                n <= 0 -> "Nada para colar (a mídia não existe neste projeto)"
                n == 1 -> "Camada colada no cabeçote"
                else -> "$n camadas coladas no cabeçote"
            },
        )
    }

    fun copyStyle() {
        val id = primary ?: return
        if (engine.copyStyle(id)) showToast("Estilo copiado")
        afterClipboard()
    }

    fun pasteStyle(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteStyle(ids.toLongArray())
        refreshNow()
        if (n > 0) showToast("Estilo colado")
    }

    fun copyEffects() {
        val id = primary ?: return
        val n = engine.copyEffects(id)
        afterClipboard()
        showToast(if (n > 0) "$n efeito(s) copiado(s)" else "Esta camada não tem efeitos")
    }

    fun pasteEffects(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteEffects(ids.toLongArray())
        refreshNow()
        if (n > 0) showToast("Efeitos colados")
    }

    fun copyKeyframes() {
        val id = primary ?: return
        val n = engine.copyKeyframes(id, playhead.toLong())
        afterClipboard()
        showToast(if (n > 0) "$n keyframe(s) copiado(s)" else "Nenhum keyframe no cabeçote")
    }

    fun pasteKeyframes(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteKeyframes(ids.toLongArray(), playhead.toLong())
        refreshNow()
        showToast(if (n > 0) "$n keyframe(s) colado(s) no cabeçote" else "Nenhuma propriedade compatível")
    }

    // --- Modo Edição (timeline magnética) -------------------------------------------
    /** Aparar empurra/puxa as seguintes; excluir fecha o buraco. */
    var editMode by mutableStateOf(false)
        private set

    fun toggleEditMode() {
        engine.setEditMode(!editMode)
        refreshNow()
        showToast(if (editMode) "Modo Edição: a timeline fecha os espaços sozinha" else "Modo Composição: camadas livres no tempo")
    }

    fun removeGaps() {
        val removed = engine.removeGaps()
        refreshNow()
        val fps = project.fps.takeIf { it > 0f } ?: 30f
        showToast(if (removed > 0) "Espaços vazios removidos (${"%.1f".format(removed / fps)} s)" else "Não há espaços vazios")
    }

    fun trimProjectAtPlayhead() {
        if (engine.trimComposition(playhead.toLong())) {
            refreshNow()
            showToast("Projeto aparado no cabeçote")
        }
    }

    /**
     * Aparar o começo por um INCREMENTO (arrasto no modo Edição: a camada não
     * sai do lugar, então o alvo absoluto não serve — conta o que já aparou).
     */
    fun trimStartBy(layer: Long, delta: Int) {
        val d = detailOf(layer) ?: return
        var start = min(d.startFrame + delta, d.endFrame - 1)
        var offset = d.offsetFrames + (start - d.startFrame)
        if (d.sourceFrames > 0 && offset < 0) {
            start -= offset
            offset = 0
        }
        start = max(0, start)
        if (start == d.startFrame) return
        send { setLayerTimeRange(layer, start, d.endFrame, offset) }
        refreshNow()
    }

    // --- Marcas e batidas ----------------------------------------------------------
    /** Marcas da composição, em ordem de frame. */
    class Markers(val frames: IntArray, val kinds: IntArray, val colors: IntArray) {
        val size: Int get() = frames.size
    }
    var markers by mutableStateOf(Markers(IntArray(0), IntArray(0), IntArray(0)))
        private set
    private var markerBuf = LongArray(3 * 256)

    private fun refreshMarkers() {
        var total = engine.queryMarkers(markerBuf)
        if (total * 3 > markerBuf.size) {
            markerBuf = LongArray(total * 3)
            total = engine.queryMarkers(markerBuf)
        }
        val n = minOf(total, markerBuf.size / 3)
        val f = IntArray(n) { markerBuf[it * 3].toInt() }
        val k = IntArray(n) { markerBuf[it * 3 + 2].toInt() }
        val c = IntArray(n) { markerBuf[it * 3 + 1].toInt() }
        val cur = markers
        if (f.contentEquals(cur.frames) && k.contentEquals(cur.kinds) && c.contentEquals(cur.colors)) return
        markers = Markers(f, k, c)
    }

    /** Cabeçote na próxima marca (volta à primeira depois da última). */
    fun seekToNextMarker() {
        val f = markers.frames
        if (f.isEmpty()) return
        val next = f.firstOrNull { it > playhead } ?: f.first()
        seek(next)
    }

    /** "Marcas": marca (ou desmarca) o frame do cabeçote. */
    fun toggleMarker() {
        val on = engine.toggleMarker(playhead.toLong())
        refreshNow()
        showToast(if (on) "Marca adicionada" else "Marca removida")
    }

    var detectingBeats by mutableStateOf(false)
        private set
    private val beatHandler = android.os.Handler(android.os.Looper.getMainLooper())

    /**
     * "Detectar batidas" no som da camada escolhida. Decodifica o áudio fora da
     * UI (thread do ciclo de vida: o motor não fecha no meio).
     */
    fun detectBeats() {
        val id = primary
        val row = layers.firstOrNull { it.id == id }
        if (id == null || row == null || (row.kind != com.aurea.aurea.ui.theme.LayerType.Audio.kind && row.kind != com.aurea.aurea.ui.theme.LayerType.Video.kind)) {
            showToast("Escolha uma camada de áudio ou vídeo com som")
            return
        }
        if (detectingBeats) return
        detectingBeats = true
        showToast("Detectando batidas…")
        lifecycleThread.execute {
            val bpm = DoubleArray(1)
            val n = synchronized(lifecycleLock) { if (ready) engine.detectBeats(id, bpm) else -1L }
            beatHandler.post {
                detectingBeats = false
                refreshNow()
                when {
                    n > 0 -> showToast("$n batidas · ${bpm[0].roundToInt()} BPM")
                    n == 0L -> showToast("Nenhuma batida clara neste som")
                    else -> showToast("Não foi possível analisar o som (erro ${-n})")
                }
            }
        }
    }

    // --- Nulo e parentesco --------------------------------------------------------
    fun addNull(threeD: Boolean) {
        val id = engine.addNull(threeD)
        if (id < 0) {
            errorMessage = "Não foi possível criar o nulo (erro ${-id})."
            return
        }
        refreshNow()
        select(id)
    }

    /**
     * Liga (ou solta, `parent` = 0) o pai da camada. O motor compensa: a camada
     * fica onde está na tela e passa a seguir o pai dali em diante.
     */
    fun setParent(layer: Long, parent: Long) {
        send { setLayerParent(layer, parent) }
        refreshNow()
    }

    /**
     * Vincular em lote: a ÚLTIMA camada escolhida vira o pai das outras (como
     * arrastar o pick whip de várias camadas para uma). Um passo de desfazer.
     */
    fun parentSelectionToLast() {
        val ids = selection.toList()
        if (ids.size < 2) return
        val parent = ids.last()
        val children = ids.dropLast(1).filter { c -> parentCandidates(c).any { it.id == parent } }
        if (children.isEmpty()) {
            showToast("Essas camadas já formam uma cadeia — nada a vincular")
            return
        }
        beginGesture("vincular camadas")
        children.forEach { c -> send { setLayerParent(c, parent) } }
        endGesture()
        refreshNow()
        showToast("${children.size} camada(s) seguindo a última escolhida")
    }

    /** Vários filhos para o mesmo pai (0 = soltar), num passo de desfazer. */
    fun setParentMany(ids: Collection<Long>, parent: Long) {
        val children = ids.filter { it != parent && (parent == 0L || parentCandidates(it).any { c -> c.id == parent }) }
        if (children.isEmpty()) return
        beginGesture(if (parent == 0L) "soltar camadas" else "vincular camadas")
        children.forEach { c -> send { setLayerParent(c, parent) } }
        endGesture()
        refreshNow()
        if (parent != 0L) showToast("${children.size} camada(s) seguindo o pai escolhido")
    }

    /** Pais possíveis para todas: nenhuma das escolhidas nem descendente delas. */
    fun parentCandidatesForAll(ids: Collection<Long>): List<LayerRow> {
        if (ids.isEmpty()) return emptyList()
        var set: Set<Long>? = null
        for (id in ids) {
            val c = parentCandidates(id).map { it.id }.toSet()
            set = set?.intersect(c) ?: c
        }
        val ok = (set ?: emptySet()) - ids.toSet()
        return layers.filter { it.id in ok }
    }

    /** Pais possíveis: toda camada que não é ela nem descendente dela. */
    fun parentCandidates(layer: Long): List<LayerRow> {
        val rows = layers
        val self = rows.indexOfFirst { it.id == layer }
        if (self < 0) return emptyList()
        return rows.filterIndexed { i, _ ->
            var cur = i
            var steps = 0
            while (cur >= 0 && cur < rows.size && steps < 64) {
                if (cur == self) return@filterIndexed false
                cur = rows[cur].parentIndex
                steps++
            }
            true
        }
    }

    // --- Forma ------------------------------------------------------------------
    /** Ladrilho `preset` da aba Forma: nova camada no centro, já escolhida. */
    fun addShape(preset: Int) {
        val id = engine.addShape(preset)
        if (id < 0) {
            errorMessage = "Não foi possível criar a forma (erro ${-id})."
            return
        }
        refreshNow()
        select(id)
    }

    fun setShapeFill(r: Float, g: Float, b: Float, a: Float) {
        val id = primary ?: return
        send { setShapeFill(id, r, g, b, a) }
        refreshDetail()
    }

    fun setShapeStroke(r: Float, g: Float, b: Float, a: Float) {
        val id = primary ?: return
        send { setShapeStroke(id, r, g, b, a) }
        refreshDetail()
    }

    fun setShapeParam(param: Int, value: Float) {
        val id = primary ?: return
        send { setShapeParam(id, param, value) }
        refreshDetail()
    }

    // --- Tempo do clipe --------------------------------------------------------
    fun setLayerSpeed(speed: Float) {
        val id = primary ?: return
        send { setLayerSpeed(id, speed.coerceIn(0.05f, 16f)) }
        refreshNow()
    }

    fun setLayerReversed(reversed: Boolean) {
        val id = primary ?: return
        send { setLayerReversed(id, reversed) }
        refreshNow()
    }

    /** Congela o quadro do cabeçote por 3 s (o resto do clipe anda). */
    fun freezeFrame(layer: Long? = primary) {
        val id = layer ?: return
        val fps = project.fps.takeIf { it > 0f } ?: 30f
        val created = engine.freezeFrame(id, playhead, (fps * 3f).toInt())
        if (created < 0) {
            errorMessage = "Posicione o cabeçote sobre o vídeo para congelar o quadro."
            return
        }
        refreshNow()
        select(created)
        showToast("Quadro congelado por 3 s")
    }

    // --- Som da camada principal ---------------------------------------------
    fun setAudioMuted(muted: Boolean) {
        val id = primary ?: return
        send { setAudioMuted(id, muted) }
        refreshNow()
    }

    fun setAudioSolo(solo: Boolean) {
        val id = primary ?: return
        send { setAudioSolo(id, solo) }
        refreshNow()
    }

    /** Volume linear (1 = 100%). Animado: grava/atualiza o keyframe no playhead. */
    fun setAudioVolume(volume: Float) {
        val id = primary ?: return
        val d = detail ?: return
        val v = volume.coerceIn(0f, 2f)
        if (d.volumeAnimated) {
            send { insertKeyframe(id, TrackProperty.AUDIO_VOLUME, -1, 0, d.localPlayhead, v) }
        } else {
            send { setAudioVolume(id, v) }
        }
        refreshDetail()
    }

    /** Liga/desliga keyframe de volume no playhead. */
    fun toggleVolumeKeyframe() {
        val id = primary ?: return
        val d = detail ?: return
        val at = keyframes[id].orEmpty().any { it.property == TrackProperty.AUDIO_VOLUME && it.time == d.localPlayhead }
        if (at) {
            send { deleteKeyframe(id, TrackProperty.AUDIO_VOLUME, -1, 0, d.localPlayhead) }
        } else {
            send { insertKeyframe(id, TrackProperty.AUDIO_VOLUME, -1, 0, d.localPlayhead, d.audioVolume) }
        }
        refreshNow()
    }

    /** Ganho do clipe (linear). */
    fun setAudioGain(gain: Float) {
        val id = primary ?: return
        send { setAudioGain(id, gain.coerceIn(0f, 4f)) }
        refreshDetail()
    }

    fun setAudioPan(pan: Float) {
        val id = primary ?: return
        send { setAudioPan(id, pan.coerceIn(-1f, 1f)) }
        refreshDetail()
    }

    fun setAudioFade(fadeIn: Boolean, frames: Int) {
        val id = primary ?: return
        val f = max(0, frames)
        send { if (fadeIn) setAudioFadeIn(id, f) else setAudioFadeOut(id, f) }
        refreshDetail()
    }

    /**
     * Modelo 3D (glTF/GLB, FBX ou OBJ) do seletor de arquivos. O arquivo é COPIADO para o
     * sandbox do app (`files/modelos/<hash>.glb`): a permissão de uma URI
     * `content://` pode sumir, e o projeto guarda só o caminho relativo — o
     * mesmo .aurea abre no Android e no iOS. Dois imports do mesmo arquivo
     * reaproveitam a cópia (hash do conteúdo).
     *
     * O motor valida de verdade (buffers, acessores, texturas): falha mostra o
     * MOTIVO, nunca "importado" com a tela preta.
     */
    fun importModel(uri: Uri) {
        val name = displayName(uri) ?: "Modelo 3D"
        val ext = name.substringAfterLast('.', "").lowercase()
        if (ext !in setOf("glb", "gltf", "fbx", "obj")) {
            errorMessage = "Esse arquivo não é um modelo 3D suportado (.glb, .gltf, .fbx ou .obj)."
            return
        }
        busyMessage = "Importando modelo 3D…"
        viewModelScope.launch {
            val poll = launch {
                while (true) {
                    kotlinx.coroutines.delay(150)
                    val p = engine.importModelProgress()
                    val phase = when (p / 1000) {
                        1 -> "Lendo o arquivo"
                        2 -> "Geometria"
                        3 -> "Texturas"
                        4 -> "Otimizando"
                        5, 6 -> "Preparando"
                        else -> "Importando"
                    }
                    busyMessage = "$phase… ${(p % 1000) / 10}%"
                }
            }
            val detail = arrayOfNulls<String>(1)
            val id = withContext(Dispatchers.IO) {
                val file = copyModelToSandbox(uri, ext) ?: return@withContext -1_000L
                engine.importModel(file.absolutePath, name.substringBeforeLast('.'), detail)
            }
            poll.cancel()
            busyMessage = null
            when {
                id == -1_000L -> errorMessage = "Não consegui ler esse arquivo."
                id < 0 -> errorMessage = "Não deu para importar o modelo: ${detail[0]?.trim()?.ifBlank { null } ?: "erro ${-id}"}."
                else -> {
                    refreshNow()
                    select(id)
                    val warnings = detail[0]?.lines()?.filter { it.isNotBlank() }.orEmpty()
                    if (warnings.isNotEmpty()) showToast("Modelo importado · ${warnings.size} aviso(s): ${warnings.first()}")
                }
            }
        }
    }

    private fun copyModelToSandbox(uri: Uri, ext: String): File? = try {
        val dir = File(getApplication<Application>().filesDir, "modelos").apply { mkdirs() }
        val tmp = File(dir, "importando.$ext")
        val digest = java.security.MessageDigest.getInstance("SHA-1")
        getApplication<Application>().contentResolver.openInputStream(uri)?.use { input ->
            FileOutputStream(tmp).use { out ->
                val buf = ByteArray(1 shl 16)
                while (true) {
                    val n = input.read(buf)
                    if (n <= 0) break
                    digest.update(buf, 0, n)
                    out.write(buf, 0, n)
                }
            }
        } ?: throw IllegalStateException("sem stream")
        val hash = digest.digest().joinToString("") { "%02x".format(it) }
        val dst = File(dir, "$hash.$ext")
        if (dst.exists()) tmp.delete() else tmp.renameTo(dst)
        dst
    } catch (e: Exception) {
        null
    }

    /** Imagem do seletor do sistema: decodificada aqui (RGBA8) e entregue ao motor. */
    fun importImage(uri: Uri) {
        takePermission(uri)
        val name = displayName(uri) ?: "Imagem"
        busyMessage = "Importando imagem…"
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) {
                val bmp = AureaEngine.decodeBitmapRgba(getApplication(), uri) ?: return@withContext -1L
                val buf = directBuffer(bmp.width * bmp.height * 4)
                bmp.copyPixelsToBuffer(buf)
                buf.rewind()
                // A URI vai junto: ao reabrir o projeto o motor pede a imagem de novo.
                val r = engine.importImage(buf, bmp.width, bmp.height, name, uri.toString())
                bmp.recycle()
                r
            }
            busyMessage = null
            if (id < 0) {
                errorMessage = "Não foi possível importar a imagem."
                return@launch
            }
            refreshNow()
            select(id)
        }
    }

    private fun takePermission(uri: Uri) {
        try {
            getApplication<Application>().contentResolver
                .takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: Exception) {
            // Nem todo provedor concede permissão persistente; a da sessão basta.
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
                errorMessage = "Não foi possível criar o projeto."
                return@launch
            }
            val path = File(directories().projects, "${uniqueName(title)}.aurea").absolutePath
            project = ProjectState(title = title, path = path, width = width, height = height, fps = fps)
            enterEditor()
        }
    }

    fun openProject(path: String) {
        viewModelScope.launch {
            val code = withContext(Dispatchers.Default) { engine.loadProject(path) }
            if (code != 0) {
                errorMessage = "Falha ao abrir o projeto (código $code)."
                return@launch
            }
            val meta = readMeta(File(path))
            project = ProjectState(title = meta?.title ?: File(path).nameWithoutExtension, path = path)
            enterEditor()
        }
    }

    private fun enterEditor() {
        selection = emptySet()
        detail = null
        effects = emptyList()
        effectParams = emptyMap()
        thumbnails.clear()
        screen = Screen.Editor
        refreshNow()
    }

    fun saveProject(onDone: (() -> Unit)? = null) {
        val path = project.path ?: return
        viewModelScope.launch {
            val code = withContext(Dispatchers.IO) { saveBlocking(path) }
            if (code != 0) errorMessage = "Falha ao salvar (código $code)."
            onDone?.invoke()
        }
    }

    /** Salva projeto, miniatura e o sidecar que a Home lê. */
    private fun saveBlocking(path: String, withThumbnail: Boolean = true): Int {
        val code = engine.saveProject(path)
        if (code != 0) return code
        val file = File(path)
        val thumbFile = File(directories().thumbs, file.nameWithoutExtension + ".jpg")
        if (withThumbnail || !thumbFile.exists()) writeThumbnail(thumbFile)
        val meta = JSONObject()
            .put("title", project.title)
            .put("width", project.width)
            .put("height", project.height)
            .put("fps", project.fps.toDouble())
            .put("durationFrames", project.durationFrames)
            .put("thumbnail", thumbFile.absolutePath)
        File(path + META_SUFFIX).writeText(meta.toString())
        return 0
    }

    private fun writeThumbnail(target: File) {
        val bmp = captureBitmap(THUMB_MAX) ?: return
        FileOutputStream(target).use { bmp.compress(Bitmap.CompressFormat.JPEG, 85, it) }
        bmp.recycle()
    }

    /** O quadro do cabeçote renderizado pelo motor (lado maior ≤ `maxDim`). */
    fun captureBitmap(maxDim: Int): Bitmap? {
        val buf = directBuffer(maxDim * maxDim * 4)
        val size = IntArray(2)
        val bytes = engine.captureFrame(maxDim, buf, size)
        if (bytes <= 0 || size[0] <= 0 || size[1] <= 0) return null
        buf.rewind()
        return Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888).also { it.copyPixelsFromBuffer(buf) }
    }

    private fun saveIfDirty() {
        if (screen != Screen.Editor || !project.dirty) return
        val path = project.path ?: return
        saveBlocking(path)
    }

    /** Sai do editor: salva (se mudou) e volta à Home. */
    fun closeProject() {
        if (exporter.busy) {
            showToast("Aguarde a exportação terminar")
            return
        }
        val path = project.path
        stopScrubIfNeeded()
        if (playing) pause()
        viewModelScope.launch {
            if (path != null && (project.dirty || !File(path).exists()) && layers.isNotEmpty()) {
                withContext(Dispatchers.IO) { saveBlocking(path) }
            }
            clearSelection()
            layers = emptyList()
            keyframes = emptyMap()
            screen = Screen.Home
            refreshProjects()
        }
    }

    private fun stopScrubIfNeeded() {
        if (scrubbing) scrubEnd()
    }

    fun renameProject(title: String) {
        if (title.isBlank()) return
        project = project.copy(title = title.trim())
    }

    /**
     * Renomeia um projeto da Home SEM abri-lo: o título mora só no sidecar
     * `.meta.json` (o arquivo `.aurea` e a miniatura não mudam de nome), então
     * basta reescrever o campo e reler a lista. Sem sidecar, nasce um só com o título.
     */
    fun renameProjectFile(path: String, title: String) {
        val clean = title.trim()
        if (clean.isEmpty()) return
        viewModelScope.launch(Dispatchers.IO) {
            val metaFile = File(path + META_SUFFIX)
            val j = try {
                if (metaFile.exists()) JSONObject(metaFile.readText()) else JSONObject()
            } catch (_: Exception) {
                JSONObject()
            }
            metaFile.writeText(j.put("title", clean).toString())
            withContext(Dispatchers.Main) { refreshProjects() }
        }
    }

    fun deleteProjects(paths: Collection<String>) {
        viewModelScope.launch(Dispatchers.IO) {
            paths.forEach { p ->
                val f = File(p)
                File(directories().thumbs, f.nameWithoutExtension + ".jpg").delete()
                File(p + META_SUFFIX).delete()
                f.delete()
            }
            withContext(Dispatchers.Main) { refreshProjects() }
        }
    }

    fun duplicateProject(path: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val src = File(path)
            val meta = readMeta(src)
            val title = (meta?.title ?: src.nameWithoutExtension) + " (cópia)"
            val dst = File(directories().projects, "${uniqueName(title)}.aurea")
            src.copyTo(dst)
            meta?.thumbnailPath?.let { t ->
                val tf = File(t)
                if (tf.exists()) tf.copyTo(File(directories().thumbs, dst.nameWithoutExtension + ".jpg"), overwrite = true)
            }
            File(dst.absolutePath + META_SUFFIX).writeText(
                JSONObject()
                    .put("title", title)
                    .put("width", meta?.width ?: 0)
                    .put("height", meta?.height ?: 0)
                    .put("fps", (meta?.fps ?: 30f).toDouble())
                    .put("durationFrames", meta?.durationFrames ?: 0)
                    .put("thumbnail", File(directories().thumbs, dst.nameWithoutExtension + ".jpg").absolutePath)
                    .toString(),
            )
            withContext(Dispatchers.Main) { refreshProjects() }
        }
    }

    fun refreshProjects() {
        viewModelScope.launch(Dispatchers.IO) {
            val dir = directories().projects
            val list = dir.listFiles { f -> f.extension == "aurea" }
                ?.mapNotNull { f -> readMeta(f) }
                ?.sortedByDescending { it.modifiedMs }
                ?: emptyList()
            withContext(Dispatchers.Main) {
                projects = list
                projectsLoaded = true
            }
        }
    }

    private fun readMeta(file: File): ProjectEntry? {
        if (!file.exists()) return null
        val metaFile = File(file.absolutePath + META_SUFFIX)
        val j = try {
            if (metaFile.exists()) JSONObject(metaFile.readText()) else null
        } catch (_: Exception) {
            null
        }
        val thumb = j?.optString("thumbnail")?.takeIf { it.isNotEmpty() && File(it).exists() }
        return ProjectEntry(
            path = file.absolutePath,
            title = j?.optString("title")?.takeIf { it.isNotEmpty() } ?: file.nameWithoutExtension,
            modifiedMs = file.lastModified(),
            width = j?.optInt("width") ?: 0,
            height = j?.optInt("height") ?: 0,
            fps = (j?.optDouble("fps") ?: 30.0).toFloat(),
            durationFrames = j?.optInt("durationFrames") ?: 0,
            thumbnailPath = thumb,
        )
    }

    private fun uniqueName(title: String): String {
        val base = title.replace(Regex("[^\\p{L}\\p{N} _-]"), "").trim().ifEmpty { "Projeto" }
        val dir = directories().projects
        var name = base
        var i = 2
        while (File(dir, "$name.aurea").exists()) name = "$base ($i)".also { i++ }
        return name
    }

    /**
     * "Limpar cache". Onde fica cada cache do app (e nada fora daqui):
     *  - `cacheDir/motor/` — cache de pipeline do Vulkan (regenerável; o motor
     *    regrava no próximo segundo plano);
     *  - `filesDir/projetos/.miniaturas/` — miniaturas dos projetos da Home
     *    (só as ÓRFÃS, de projetos apagados, saem);
     *  - miniaturas da timeline e frames decodificados vivem só na memória.
     * Devolve os bytes liberados.
     */
    fun clearCache(): Long {
        val dirs = directories()
        var freed = 0L
        dirs.cache.listFiles()?.forEach { f ->
            freed += f.walkBottomUp().filter { it.isFile }.sumOf { it.length() }
            f.deleteRecursively()
        }
        val alive = dirs.projects.listFiles { f -> f.extension == "aurea" }
            ?.map { it.nameWithoutExtension }?.toSet() ?: emptySet()
        dirs.thumbs.listFiles()?.forEach { t ->
            if (t.nameWithoutExtension !in alive) {
                freed += t.length()
                t.delete()
            }
        }
        thumbnails.clear()
        showToast("Cache limpo: ${"%.1f".format(freed / (1024.0 * 1024.0))} MB")
        return freed
    }

    fun dismissError() {
        errorMessage = null
    }

    fun showError(message: String) {
        errorMessage = message
    }

    companion object {
        const val MAX_LAYERS = 512
        const val MAX_KEYS = 4096
        const val THUMB_MAX = 512
        /** Silêncio depois da última mudança antes do autosave. */
        const val AUTOSAVE_IDLE_NS = 3_000_000_000L
        const val META_SUFFIX = ".meta.json"
        /** `kInvalidIndex` do C++: keyframe que não é de efeito. */
        const val NO_EFFECT = -1
    }
}

/**
 * Miniaturas da timeline: pequenas imagens (não é o preview) pedidas ao motor,
 * que as decodifica em baixa prioridade. `get` devolve null enquanto a
 * miniatura não chegou; a UI redesenha quando `thumbnailGeneration` muda.
 */
class ThumbnailCache(private val engine: AureaEngine) {
    // Chave composta: o id da camada é `geração << 32 | índice`, e o antigo
    // `layer * 31 + frame` colidia entre camadas vizinhas (camada 0 no frame 31
    // = camada 1 no frame 0 → miniatura de outra camada).
    private data class Key(val layer: Long, val frame: Int, val height: Int)

    private val cache = object : LinkedHashMap<Key, Bitmap>(256, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Key, Bitmap>?) = size > 400
    }
    private val buffer: ByteBuffer = directBuffer(512 * 512 * 4)
    private val width = IntArray(1)

    /** Quem chama arredonda o frame para a grade de 250 ms do motor (a timeline pede um frame por balde). */
    fun get(layer: Long, timelineFrame: Int, heightPx: Int): Bitmap? {
        val key = Key(layer, timelineFrame, heightPx)
        cache[key]?.let { return it }
        buffer.clear()
        val bytes = engine.queryThumbnail(layer, timelineFrame, heightPx, buffer, width)
        if (bytes <= 0 || width[0] <= 0) return null
        buffer.rewind()
        val bmp = Bitmap.createBitmap(width[0], heightPx, Bitmap.Config.ARGB_8888)
        bmp.copyPixelsFromBuffer(buffer)
        cache[key] = bmp
        return bmp
    }

    fun clear() = cache.clear()
}

/** Comprimento das setas do gizmo 3D, em unidades do mundo (px da composição no plano Z = 0). */
const val GIZMO_LENGTH = 320f

/** Receita do texto 3D: texto, profundidade (em alturas de letra), alinhamento e cor sRGB. */
data class Text3DInfo(val content: String, val depth: Float, val alignment: Int, val color: FloatArray)
