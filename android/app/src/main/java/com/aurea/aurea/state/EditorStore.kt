package com.aurea.aurea.state

import android.app.Application
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.R
import androidx.annotation.StringRes
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.display.DisplayManager
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.util.Log
import android.view.Display
import android.view.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.effects.EffectPreviewStore
import com.aurea.aurea.effects.EffectPrefs
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.ExpressionDiag
import com.aurea.aurea.engine.ExpressionInfo
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.engine.ExpressionRow
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackKey
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
/** `aurea::Errc::IoError` / `StorageFull` (TEMP_STORAGE_FULL). */
private const val ERRC_IO = 10
private const val ERRC_STORAGE_FULL = 28

private const val TAG = "AureaStore"

/** O Application, para o texto do catálogo fora do Compose (mensagens do motor). */
private lateinit var storeApp: Application

/** Texto do catálogo no idioma do app (AppText respeita a escolha em Ajustes). */
private fun appText(@StringRes id: Int, vararg args: Any): String =
    if (args.isEmpty()) AppText.get(storeApp, id) else AppText.get(storeApp, id, *args)

/**
 * Mensagem para humanos de um `aurea::Errc` (core/Result.hpp; os números têm
 * static_assert lá). Os códigos padronizados da Fase 8 (§115) — GPU sem
 * memória, decoder, encoder, mídia/projeto corrompido, armazenamento cheio —
 * dizem o que aconteceu e o que fazer; nenhum mostra só "erro 28".
 */
fun humanError(code: Int): String = when (code) {
    0 -> appText(R.string.msg_concluido)
    3 -> appText(R.string.msg_arquivo_nao_encontrado)
    6, 24 -> appText(R.string.msg_recurso_nao_suportado_neste_aparelho)
    8, 9 -> appText(R.string.msg_memoria_insuficiente_feche_outros_apps_e)
    10 -> appText(R.string.msg_erro_ao_ler_ou_gravar_o)
    11, 13 -> appText(R.string.msg_o_arquivo_esta_danificado)
    12 -> appText(R.string.msg_este_projeto_foi_salvo_por_uma)
    14 -> appText(R.string.msg_nao_foi_possivel_decodificar_a_midia)
    15 -> appText(R.string.msg_falha_ao_codificar_o_video)
    16 -> appText(R.string.msg_codec_de_video_nao_suportado_por)
    17 -> appText(R.string.msg_formato_de_arquivo_nao_suportado)
    18 -> appText(R.string.msg_a_midia_original_nao_esta_mais)
    19 -> appText(R.string.msg_a_gpu_foi_reiniciada_tente_de)
    20 -> appText(R.string.msg_memoria_de_video_gpu_insuficiente_baixe)
    25 -> appText(R.string.msg_cancelado)
    28 -> appText(R.string.msg_sem_espaco_no_aparelho_libere_espaco)
    29 -> appText(R.string.msg_o_arquivo_de_midia_esta_danificado)
    30 -> appText(R.string.msg_o_projeto_esta_danificado_e_nao)
    31 -> appText(R.string.msg_este_aparelho_nao_tem_codificador_para)
    else -> appText(R.string.msg_erro_inesperado_codigo, code)
}

/** Grava texto atomicamente: temporário → sync → rename (o sidecar nunca fica pela metade). */
internal fun writeTextAtomic(target: File, text: String) {
    val tmp = File(target.path + ".tmp")
    try {
        FileOutputStream(tmp).use { out ->
            out.write(text.toByteArray(Charsets.UTF_8))
            out.fd.sync()
        }
        // Files.move atômico (API 26+): substitui o existente numa operação só.
        java.nio.file.Files.move(
            tmp.toPath(), target.toPath(),
            java.nio.file.StandardCopyOption.ATOMIC_MOVE, java.nio.file.StandardCopyOption.REPLACE_EXISTING,
        )
    } finally {
        tmp.delete()
    }
}
/** Floats do cabeçalho de cada máscara em Engine::query_masks (kMaskHeaderFloats). */
private const val MASK_HEADER = 12

/** shapeType da camada vetorial no motor (`kShapeVector`, vector/VectorData.hpp). */
const val VECTOR_SHAPE_TYPE = 11

class EditorStore(app: Application) : AndroidViewModel(app) {
    init { storeApp = app }

    private val engine = AureaEngine.create(app)

    /**
     * Legendas automáticas (transcrição + camadas); ver `CaptionsState`.
     * Criadas no primeiro uso (8I): a abertura não lê o cofre da chave.
     */
    val captions by lazy { com.aurea.aurea.captions.CaptionsState(app, engine, viewModelScope) { refreshNow() } }

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

    /**
     * Favoritos e recentes do catálogo de efeitos: preferência do APARELHO, não
     * do projeto. Vive aqui porque sobrevive à saída do editor — quem favorita
     * um efeito o encontra marcado na próxima vez que abrir o navegador.
     */
    val effectPrefs = EffectPrefs(getApplication())
    /**
     * As prévias dos efeitos (§13–§15). Nulo até o motor subir — o navegador
     * desenha a cartela genérica enquanto isso, em vez de esperar.
     */
    var effectPreviews by mutableStateOf<EffectPreviewStore?>(null)
        private set

    /**
     * A prévia de UM efeito: o efeito de verdade rodando sobre a cartela de
     * demonstração do motor, em RGBA8. Devolve nulo quando não dá para
     * pré-visualizar — e aí o cartão mostra a cartela genérica.
     *
     * Cada chamada traz o SEU buffer. Hoje as prévias passam por uma fila de
     * um só (EffectPreviewStore), mas um buffer compartilhado aqui voltaria a
     * ser corrida silenciosa no dia em que a fila crescer — a prévia de um
     * efeito apareceria no cartão de outro.
     */
    private fun renderEffectPreview(typeId: Int, width: Int, height: Int): ImageBitmap? {
        ensureEffectPreviewPhoto()
        val pixels = directBuffer(width * height * 4)
        val dims = IntArray(2)
        if (!engine.renderEffectPreview(typeId, width, height, pixels, dims)) return null
        if (dims[0] <= 0 || dims[1] <= 0) return null
        pixels.rewind()
        val bmp = Bitmap.createBitmap(dims[0], dims[1], Bitmap.Config.ARGB_8888)
        bmp.copyPixelsFromBuffer(pixels)
        return bmp.asImageBitmap()
    }
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
    /** Memória do processo e do sistema, lida pela HUD (só com ela aberta). */
    var appMemory by mutableStateOf(AppMemory())
        private set

    /**
     * RAM vista pelo app (Fase 8A, HUD). Tudo leitura barata: heap nativo e
     * Java do processo, e o MemoryInfo do sistema (uma chamada ao
     * ActivityManager por segundo). PSS/RSS não entram: getProcessMemoryInfo é
     * limitado pelo sistema e Debug.getPss custa milissegundos.
     */
    data class AppMemory(
        val nativeHeapBytes: Long = 0,
        val javaHeapBytes: Long = 0,
        val systemAvailBytes: Long = 0,
        val systemTotalBytes: Long = 0,
        val lowMemory: Boolean = false,
        /** Quadros da UI (Choreographer) com intervalo > 1,5 vsync na janela. */
        val uiSlowFrames: Int = 0,
        val uiWorstFrameMs: Float = 0f,
    )

    /** Muda quando o motor terminou uma miniatura nova (a timeline redesenha). */
    var thumbnailGeneration by mutableIntStateOf(0)
        private set
    val thumbnails = ThumbnailCache(engine)

    /** Arquivos regeneráveis do app (Ajustes › Armazenamento, Fase 8B §49–52). */
    val storage = CacheStorage(getApplication())

    /** Camada principal da seleção. */
    val primary: Long? get() = selection.firstOrNull()

    /**
     * Keyframe escolhido (losango tocado na timeline). Estado de APRESENTAÇÃO
     * compartilhado entre a timeline e o editor de curva — o keyframe em si
     * continua no motor; aqui só fica QUAL está escolhido.
     */
    var selectedKeyframe by mutableStateOf<Pair<Long, KeyframeRow>?>(null)
        private set

    /** Aviso curto e não bloqueante (ex.: "Salvo na galeria"). A UI some com ele em ~2 s. */
    var toast by mutableStateOf<String?>(null)
        private set
    private var toastSerial = 0

    fun showToast(message: String) {
        toast = message
        val serial = ++toastSerial
        main.postDelayed({ if (serial == toastSerial) toast = null }, 2200)
    }

    fun selectKeyframe(layer: Long, key: KeyframeRow) {
        selectedKeyframe = layer to key
    }

    fun clearSelectedKeyframe() {
        selectedKeyframe = null
    }

    // =========================================================================
    // Buffers (diretos, reutilizados)
    // =========================================================================
    // Camadas e keyframes CRESCEM com o projeto (fase 8D): o teto fixo de 512
    // camadas e 4096 keyframes por camada cortava a timeline em silêncio — 5000
    // palavras de legenda dão ~1400 camadas.
    private var layerCapacity = 256
    private var layerBuffer = directBuffer(layerCapacity * PodLayout.LAYER_ROW_BYTES)
    private var nameBlob = directBuffer(32 * 1024)
    private var keyIndexCapacity = 256
    private var keyIndex = directBuffer(keyIndexCapacity * KeyframeSnapshot.INDEX_BYTES)
    private var allKeysCapacity = 4096
    private var allKeys = directBuffer(allKeysCapacity * PodLayout.KEYFRAME_ROW_BYTES)
    private val keySnapshot = KeyframeSnapshot()
    private val statusBuffer = directBuffer(PodLayout.STATUS_BYTES)
    private val perfBuffer = directBuffer(PerfStats.BYTES)
    private val detailBuffer = directBuffer(LayerDetail.BYTES)
    private val rowBuffer = directBuffer(64 * EffectParam.ROW_BYTES)
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
    private var lastUiFrameNs = 0L
    private var uiSlowFrames = 0
    private var uiWorstFrameNs = 0L
    private var lastSystemMemNs = 0L
    private var systemMem: android.app.ActivityManager.MemoryInfo? = null
    private val vsyncNs: Long by lazy { (1e9f / displayRefreshRate().coerceAtLeast(30f)).toLong() }
    private var scrubbing = false

    init {
        lifecycleThread.execute {
            synchronized(lifecycleLock) {
                if (destroyed) return@synchronized
                val dirs = directories()
                val debug = (app.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
                // Sondagem do aparelho: medida UMA vez (primeira abertura ou SO
                // novo) e guardada; o motor decide orçamento, workers, teto de
                // textura/preview/export a partir dela e da GPU real.
                val probe = runCatching { com.aurea.aurea.engine.DeviceProfile.probe(app) }.getOrNull()
                val ok = engine.initialize(
                    displayRefreshRate(), dirs.cache.absolutePath, dirs.projects.absolutePath, debug,
                    probe?.memory, probe?.codecs,
                )
                ready = ok
                if (ok) pendingSurface?.let { (s, w, h) -> engine.attachSurface(s, w, h) }
                pendingSurface = null
                main.post {
                    if (ok) {
                        deviceReport = engine.deviceReport()
                        deviceName = engine.deviceSummary()
                        startThermalWatch()
                        engineReady = true
                        catalog = readCatalog()
                        // Abertura (Fase 8I §59–63): a foto das prévias NÃO é
                        // decodificada aqui (era JPEG + cópia de 1,6 MB no main
                        // thread em toda abertura); sobe na primeira prévia.
                        effectPreviews = EffectPreviewStore(dirs.cache, installStamp(), ::renderEffectPreview)
                        startStatusLoop()
                    } else {
                        errorMessage = appText(R.string.msg_nao_foi_possivel_iniciar_o_motor)
                    }
                }
            }
        }
        refreshProjects()
        // Limpeza automática (§51): tipo acima do teto perde os mais antigos;
        // sobra de export/legenda de uma sessão que morreu (crash, force kill)
        // sai aqui. Fora da main thread; nunca toca em projeto.
        viewModelScope.launch(Dispatchers.IO) { runCatching { storage.enforceLimits(exporting = false) } }
    }

    /**
     * A foto das prévias de efeito (assets/previa_efeitos.jpg): cada efeito é
     * mostrado aplicado sobre ela, não sobre uma cartela de teste. Carregada
     * UMA vez, na primeira prévia pedida (fila de render das prévias, fora do
     * main thread) — quem nunca abre o navegador de efeitos não paga nada.
     */
    @Volatile private var previewPhotoLoaded = false

    private fun ensureEffectPreviewPhoto() {
        if (previewPhotoLoaded) return
        previewPhotoLoaded = true
        runCatching {
            getApplication<Application>().assets.open("previa_efeitos.jpg").use { input ->
                val bmp = android.graphics.BitmapFactory.decodeStream(input, null,
                    android.graphics.BitmapFactory.Options().apply { inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888 })
                    ?: return
                val buf = java.nio.ByteBuffer.allocate(bmp.byteCount)
                bmp.copyPixelsToBuffer(buf)   // ARGB_8888 na memória = R, G, B, A
                engine.setEffectPreviewSource(buf.array(), bmp.width, bmp.height)
                bmp.recycle()
            }
        }
    }

    /**
     * Carimbo desta instalação (muda a cada atualização do app): versão das
     * prévias de efeito guardadas em disco. Efeito ou foto novos → prévias
     * refeitas, nunca a imagem velha de um efeito que mudou.
     */
    private fun installStamp(): String {
        val app = getApplication<Application>()
        return runCatching { app.packageManager.getPackageInfo(app.packageName, 0).lastUpdateTime.toString(16) }
            .getOrDefault("0")
    }

    /** O que o motor decidiu para ESTE aparelho (mostrado nos Ajustes). */
    var deviceReport by mutableStateOf<com.aurea.aurea.engine.DeviceReport?>(null)
        private set
    var deviceName by mutableStateOf("")
        private set

    /** Relê o que o motor decidiu (a faixa térmica e o que ela muda são de agora). */
    fun refreshDeviceReport() {
        if (!ready) return
        engine.deviceReport()?.let { deviceReport = it }
    }

    /** Esquece a sondagem guardada: a próxima abertura mede o aparelho de novo. */
    fun remeasureDevice() {
        com.aurea.aurea.engine.DeviceProfile.forget(getApplication())
        showToast(appText(R.string.msg_o_aparelho_sera_medido_de_novo))
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
                errorMessage = appText(R.string.msg_o_preview_nao_conseguiu_usar_a)
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
        // A gravação sai da main (antes travava a UI no onStop pelo tempo do
        // encode + fsync + miniatura) e vai para a thread de ciclo de vida,
        // ANTES do suspend na mesma fila: o motor ainda está de pé quando grava.
        val path = if (screen == Screen.Editor && project.dirty) project.path else null
        lifecycleThread.execute {
            if (path != null) {
                val t0 = System.nanoTime()
                val code = saveBlocking(path)
                Log.i(TAG, "gravacao ao ir para segundo plano: codigo $code, ${(System.nanoTime() - t0) / 1_000_000} ms fora da main")
            }
            synchronized(lifecycleLock) { if (ready) engine.suspend() }
        }
    }

    /**
     * Pressão de memória do SISTEMA (ComponentCallbacks2.onTrimMemory, Fase 8B
     * §13). A UI solta os bitmaps dela (miniaturas e prévias fora da tela) e o
     * motor segue a ordem do spec. O projeto, as alterações não salvas e o
     * histórico nunca entram — só cache que se refaz.
     */
    fun onTrimMemory(level: Int) {
        when {
            level >= TRIM_UI_HIDDEN || level == TRIM_RUNNING_CRITICAL -> {
                thumbnails.clear()
                effectPreviews?.trimMemory()
            }
            level >= TRIM_RUNNING_LOW -> thumbnails.trimTo(0.5f)
        }
        lifecycleThread.execute {
            synchronized(lifecycleLock) {
                if (!ready) return@synchronized
                val freed = engine.trimMemory(level)
                android.util.Log.i("Aurea", "onTrimMemory($level): motor liberou ${freed / 1024} KB")
            }
        }
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
    //
    //  Fase 8 §38 (bateria): o laço rodava a CADA vsync enquanto o app estava
    //  aberto — 60 a 120 acordadas por segundo da thread da UI, cada uma com uma
    //  leitura JNI do status, com o app parado na Home ou o editor pausado.
    //  Agora: no ritmo do vsync enquanto algo muda (tocando, scrub, gesto, HUD,
    //  status diferente do anterior); depois de [STATUS_IDLE_FRAMES] quadros
    //  iguais, cai para uma leitura a cada [STATUS_IDLE_POLL_MS] ms (4/s). Um
    //  comando local (`send`, `group`, `refreshNow`) volta ao vsync na hora; o
    //  que muda sozinho no motor (miniatura pronta, fim do play) aparece em até
    //  250 ms e também devolve o vsync.
    // =========================================================================
    private var statusIdleFrames = 0
    private var statusSlow = false
    /** Última assinatura do status (o que a UI mostra); igual = nada a fazer. */
    private val statusSig = LongArray(16)
    private val statusSigNow = LongArray(16)

    /** Acordadas do laço de estado desde que o app subiu (HUD / medição §38). */
    var statusWakeups = 0L
        private set

    /** Lê o status do motor e publica. Devolve se algo que a UI mostra mudou. */
    private fun pollStatus(): Boolean {
        engine.readStatus(statusBuffer)
        status.readFrom(statusBuffer)
        val n = statusSigNow
        n[0] = status.playhead; n[1] = if (status.playing) 1 else 0; n[2] = status.modelRevision.toLong()
        n[3] = status.thumbnailGeneration.toLong(); n[4] = if (status.dirty) 1 else 0
        n[5] = (if (status.canUndo) 1L else 0L) or (if (status.canRedo) 2L else 0L)
        n[6] = status.previewWidth.toLong(); n[7] = status.previewHeight.toLong()
        n[8] = status.previewDenominator.toLong() * 2 + (if (status.previewAuto) 1 else 0)
        n[9] = status.duration; n[10] = status.compWidth.toLong() * 65536 + status.compHeight
        n[11] = status.layerCount.toLong(); n[12] = status.selectedCount.toLong()
        n[13] = status.state.toLong() * 65536 + status.lastError; n[14] = status.assetCount.toLong()
        n[15] = if (status.recoveryAvailable) 1 else 0
        val changed = !n.contentEquals(statusSig)
        if (changed) n.copyInto(statusSig)
        publish()
        return changed
    }

    /** Algo em curso que precisa do ritmo do vsync mesmo sem mudança no status. */
    private fun statusBusy(): Boolean = playing || scrubbing || gestureDepth > 0 || hudVisible || autosaving

    private fun startStatusLoop() {
        if (statusLoop != null) return
        statusSlow = false
        statusIdleFrames = 0
        statusLoop = RenderLoop { frameTimeNanos ->
            if (!ready) return@RenderLoop
            engine.readStatus(statusBuffer)
            status.readFrom(statusBuffer)
            updateIdle()
            publish()
            uiFrames++
            if (hudVisible) {
                // Quadro lento da UI: o Choreographer chamou mais de 1,5 vsync
                // depois do anterior (a thread principal ficou presa).
                if (lastUiFrameNs != 0L) {
                    val dt = frameTimeNanos - lastUiFrameNs
                    if (dt * 2 > vsyncNs * 3) uiSlowFrames++
                    if (dt > uiWorstFrameNs) uiWorstFrameNs = dt
                }
                lastUiFrameNs = frameTimeNanos
            } else {
                lastUiFrameNs = 0L
            }
            if (hudVisible && frameTimeNanos - lastPerfNs > 250_000_000L) {
                if (lastPerfNs != 0L) uiFps = uiFrames * 1e9f / (frameTimeNanos - lastPerfNs)
                uiFrames = 0
                lastPerfNs = frameTimeNanos
                engine.readPerf(perfBuffer)
                perf = PerfStats.read(perfBuffer)
                readAppMemory(frameTimeNanos)
            }
        }.also { it.start() }
    }

    /** HUD: memória do app a 4 Hz; a do sistema e os quadros lentos por janela de 1 s. */
    private fun readAppMemory(nowNs: Long) {
        val rt = Runtime.getRuntime()
        if (nowNs - lastSystemMemNs >= 1_000_000_000L) {
            val am = getApplication<Application>().getSystemService(android.content.Context.ACTIVITY_SERVICE) as? android.app.ActivityManager
            val mi = systemMem ?: android.app.ActivityManager.MemoryInfo().also { systemMem = it }
            am?.getMemoryInfo(mi)
            appMemory = appMemory.copy(
                systemAvailBytes = mi.availMem,
                systemTotalBytes = mi.totalMem,
                lowMemory = mi.lowMemory,
                uiSlowFrames = uiSlowFrames,
                uiWorstFrameMs = uiWorstFrameNs / 1e6f,
            )
            uiSlowFrames = 0
            uiWorstFrameNs = 0L
            lastSystemMemNs = nowNs
        }
        appMemory = appMemory.copy(
            nativeHeapBytes = android.os.Debug.getNativeHeapAllocatedSize(),
            javaHeapBytes = rt.totalMemory() - rt.freeMemory(),
        )
    }

    // --- Laço ocioso (fase 8D) ---------------------------------------------------
    //  Parado (sem tocar, sem scrub, sem gesto, nada mudando no motor), o laço
    //  deixa o vsync e lê o status 4×/s: o que o motor termina sozinho (miniatura,
    //  waveform, import) aparece em até 250 ms; qualquer comando acorda na hora.
    private var quietFrames = 0
    private var lastSignature = 0L

    private fun statusSignature(): Long {
        var h = status.modelRevision.toLong()
        h = h * 31 + status.playhead
        h = h * 31 + status.thumbnailGeneration
        h = h * 31 + status.duration
        h = h * 31 + status.previewWidth * 7919 + status.previewHeight
        h = h * 31 + (if (status.dirty) 1 else 0) + (if (status.canUndo) 2 else 0) + (if (status.canRedo) 4 else 0)
        h = h * 31 + status.state + status.lastError * 17
        return h
    }

    private fun updateIdle() {
        val loop = statusLoop ?: return
        val sig = statusSignature()
        val busy = status.playing || scrubbing || gestureDepth > 0 || hudVisible || sig != lastSignature
        lastSignature = sig
        quietFrames = if (busy) 0 else quietFrames + 1
        if (busy) loop.wake() else if (quietFrames >= QUIET_FRAMES) loop.idleDelayMs = IDLE_POLL_MS
    }

    /** Volta ao vsync (um comando saiu: o resultado tem de aparecer no próximo quadro). */
    private fun wakeStatusLoop() {
        quietFrames = 0
        statusLoop?.wake()
    }

    private fun stopStatusLoop() {
        statusLoop?.stop()
        statusLoop = null
        statusSlow = false
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
    /** Última falha do autosave (código Errc); a mesma não é avisada de novo a cada tentativa. */
    private var lastAutosaveError = 0
    /** Depois de uma falha, espera mais antes de tentar de novo (disco cheio não some em 3 s). */
    private var autosaveRetryAfterNs = 0L

    private fun autosaveIfIdle() {
        if (!status.dirty || autosaving || playing || scrubbing || gestureDepth > 0) return
        val now = System.nanoTime()
        if (now - lastModelChangeNs < AUTOSAVE_IDLE_NS || now < autosaveRetryAfterNs) return
        val path = project.path ?: return
        if (layers.isEmpty()) return
        autosaving = true
        viewModelScope.launch {
            // Tudo fora da main: o motor copia o modelo sob o lock (encode, ms) e
            // grava sem ele (fsync). A main só dispara e recebe o código.
            val code = withContext(Dispatchers.IO) { saveBlocking(path, withThumbnail = false) }
            autosaving = false
            if (code != 0) {
                autosaveRetryAfterNs = System.nanoTime() + AUTOSAVE_RETRY_NS
                if (code != lastAutosaveError) errorMessage = appText(R.string.msg_salvamento_automatico_falhou, humanError(code))
            } else {
                autosaveRetryAfterNs = 0L
            }
            lastAutosaveError = code
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
        // Uma consulta para todas as camadas; o mapa só troca se algum keyframe mudou.
        if (readAllKeyframes()) keyframes = keySnapshot.map
        refreshComposition()
        refreshDetail()
        refreshEffects()
    }

    private fun readLayers(): List<LayerRow> {
        while (true) {
            val n = max(0, engine.queryLayers(layerBuffer, layerCapacity, nameBlob))
            // Cheio = pode haver mais: dobra e pergunta de novo. Nomes perto do fim
            // do blob (o motor pula o que não cabe) também dobram o blob.
            var used = 0
            for (i in 0 until n) {
                val b = i * PodLayout.LAYER_ROW_BYTES
                used = max(used, layerBuffer.getInt(b + PodLayout.LAYER_OFF_NAME_OFFSET) + layerBuffer.getInt(b + PodLayout.LAYER_OFF_NAME_LENGTH))
            }
            val full = n >= layerCapacity
            val namesFull = used > nameBlob.capacity() * 3 / 4
            if (!full && !namesFull) return List(n) { LayerRow.read(layerBuffer, it, nameBlob) }
            if (full) {
                layerCapacity *= 2
                layerBuffer = directBuffer(layerCapacity * PodLayout.LAYER_ROW_BYTES)
            }
            if (namesFull || full) nameBlob = directBuffer(nameBlob.capacity() * 2)
        }
    }

    /** Keyframes de todas as camadas numa consulta; devolve se o mapa mudou. */
    private fun readAllKeyframes(): Boolean {
        while (true) {
            val r = engine.queryAllKeyframes(keyIndex, keyIndexCapacity, allKeys, allKeysCapacity)
            val count = (r ushr 32).toInt()
            val total = (r and 0xFFFFFFFFL).toInt()
            if (count <= keyIndexCapacity && total <= allKeysCapacity) return keySnapshot.update(keyIndex, count, allKeys)
            if (count > keyIndexCapacity) {
                while (keyIndexCapacity < count) keyIndexCapacity *= 2
                keyIndex = directBuffer(keyIndexCapacity * KeyframeSnapshot.INDEX_BYTES)
            }
            if (total > allKeysCapacity) {
                while (allKeysCapacity < total) allKeysCapacity *= 2
                allKeys = directBuffer(allKeysCapacity * PodLayout.KEYFRAME_ROW_BYTES)
            }
        }
    }

    private fun refreshDetail() {
        val id = primary
        detail = if (id != null && engine.queryLayerDetail(id, detailBuffer)) readDetailIfChanged() else null
        expressions = if (id != null) engine.queryExpressions(id) else emptyList()
        gizmo = same(gizmo, if (id != null) {
            val out = FloatArray(8)
            if (engine.queryGizmo(id, GIZMO_LENGTH, out)) out else null
        } else {
            null
        })
        particles = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Particles.kind) {
            val out = FloatArray(8)
            if (engine.queryParticles(id, out)) out.toList() else null
        } else {
            null
        }
        timeRemap = if (id != null && detail?.timeRemap == true) {
            val out = FloatArray(5 + 7 * 64)
            val n = engine.queryTimeRemap(id, out)
            same(timeRemap, if (n >= 5) out.copyOf(n) else null)
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
            textStyle = same(textStyle, if (engine.queryTextStyle(id, st)) st else null)
            val anim = engine.queryTextAnimators(id)?.let { a -> List(a.size / 40) { i -> a.copyOfRange(i * 40, i * 40 + 40) } } ?: emptyList()
            val old = textAnimators
            if (anim.size != old.size || anim.indices.any { !anim[it].contentEquals(old[it]) }) textAnimators = anim
        } else {
            textFont = null
            textStyle = null
            textAnimators = emptyList()
        }
        shapeParams = same(shapeParams, if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Shape.kind && !isVectorLayer) engine.queryShapeParams(id) else null)
        textDetail = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Text.kind) {
            engine.queryText(id, textFloats)?.let { com.aurea.aurea.engine.TextDetail.of(it, textFloats) }
        } else {
            null
        }
        refreshMasks()
        val tp = if (id != null && detail?.kind == com.aurea.aurea.ui.theme.LayerType.Text.kind) engine.queryTextPath(id) else null
        if (!(tp === textPath || (tp != null && textPath?.contentEquals(tp) == true))) textPath = tp
        refreshVector()
    }

    // --- Releitura sem escrita à toa (fase 8D) ----------------------------------
    //  Arrays e o detalhe (que tem arrays dentro) nunca comparam iguais no
    //  estado do Compose: cada releitura — a cada quadro tocando, a cada
    //  revisão do modelo — invalidava quem os lê mesmo sem mudar nada. Aqui o
    //  valor velho volta quando o conteúdo é o mesmo.
    private val detailRaw = ByteArray(LayerDetail.BYTES)
    private var detailRawOf: LayerDetail? = null

    private fun readDetailIfChanged(): LayerDetail {
        val cur = detail
        var same = cur != null && cur === detailRawOf
        for (i in detailRaw.indices) {
            val b = detailBuffer.get(i)
            if (b != detailRaw[i]) {
                same = false
                detailRaw[i] = b
            }
        }
        if (same) return cur!!
        return LayerDetail.read(detailBuffer).also { detailRawOf = it }
    }

    private fun same(old: FloatArray?, new: FloatArray?): FloatArray? =
        if (old != null && new != null && old.contentEquals(new)) old else new

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
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_a_camada, -id)
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_o_desenho, -id)
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
                errorMessage = appText(R.string.msg_nao_foi_possivel_ler_o_svg)
                return@launch
            }
            val name = displayName(uri)?.substringBeforeLast('.') ?: "SVG"
            val id = engine.importSvg(bytes, name)
            if (id < 0) {
                errorMessage = if (-id == ERRC_UNSUPPORTED_FORMAT) appText(R.string.msg_svg_sem_formas_suportadas) else appText(R.string.msg_nao_foi_possivel_importar_o_svg, -id)
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
            showToast(appText(R.string.msg_a_guia_precisa_ser_uma_camada))
        }
        refreshNow()
    }

    /** Receita do texto 3D da camada principal (nulo = não é texto 3D). */
    var text3d by mutableStateOf<Text3DInfo?>(null)
        private set

    fun addText3D(): Long {
        val id = engine.addText3d("Texto", 0.25f, 1, 1f, 1f, 1f)
        if (id < 0) {
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_o_texto, -id)
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

    // Buffers próprios da ficha do catálogo: `effectSpecs` é chamado do
    // navegador, que pode estar compondo enquanto o laço de frame usa o
    // `rowBuffer`. Compartilhar os dois seria uma corrida silenciosa.
    private val specRows = directBuffer(64 * EffectParam.ROW_BYTES)
    private val specBlob = directBuffer(16 * 1024)
    private val specCache = HashMap<Int, List<EffectParam>>()

    /**
     * A DECLARAÇÃO dos parâmetros de um tipo de efeito, sem precisar de uma
     * camada aberta: é o que o navegador mostra na ficha (§68). Fica em cache
     * porque a declaração de um tipo nunca muda enquanto o motor está de pé.
     */
    fun effectSpecs(typeId: Int): List<EffectParam> = specCache.getOrPut(typeId) {
        val n = engine.queryEffectSpecs(typeId, specRows, 64, specBlob)
        List(max(0, n)) { EffectParam.read(specRows, it, specBlob) }
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
        lastRevision = -1
        wakeStatusLoop()
        // Fase 8D: DENTRO de um gesto (arrasto, slider) a releitura fica para o
        // próximo quadro do laço. Ler agora era refazer o modelo inteiro a cada
        // evento de toque (vários por quadro; um por trilha no losango) e ainda
        // esperar o motor largar o modelo — e o comando recém-enviado só é
        // aplicado no quadro do motor, então a leitura imediata era a velha.
        if (gestureDepth > 0 && statusLoop?.isRunning == true) return
        engine.readStatus(statusBuffer)
        status.readFrom(statusBuffer)
        publish()
        wakeStatusLoop()
    }

    // =========================================================================
    // Comandos
    // =========================================================================
    private inline fun send(block: CommandBatch.() -> Unit) {
        engine.beginCommandBatch()
        batch.block()
        engine.submitCommands()
        wakeStatusLoop()
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

    // --- Papel e organização da camada (ajuste, guia, etiqueta, solo, busca) ------------

    /** Camada de ajuste nova (no topo): um nulo marcado como ajuste — os efeitos que entrarem nela valem para tudo abaixo. */
    fun addAdjustmentLayer() {
        val id = engine.addNull(false)
        if (id < 0) {
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_a_camada_2, -id)
            return
        }
        engine.setLayerAdjustment(id, true)
        renameLayer(id, "Camada de ajuste")
        refreshNow()
        select(id)
        showToast(appText(R.string.msg_camada_de_ajuste_adicione_efeitos_nela))
    }

    /** Camada de ajuste: os efeitos dela passam a valer para tudo o que está abaixo. */
    fun setLayerAdjustment(layer: Long, on: Boolean) {
        if (!engine.setLayerAdjustment(layer, on)) return
        refreshNow()
        showToast(if (on) appText(R.string.msg_camada_de_ajuste_os_efeitos_valem) else appText(R.string.msg_camada_de_ajuste_desligada))
    }

    /** Guia: aparece aqui no editor e fica fora do vídeo exportado. */
    fun setLayerGuide(layer: Long, on: Boolean) {
        if (!engine.setLayerGuide(layer, on)) return
        refreshNow()
        showToast(if (on) appText(R.string.msg_guia_aparece_no_editor_nao_sai) else appText(R.string.msg_guia_desligada_a_camada_volta_ao))
    }

    /** Etiqueta de cor (0 = nenhuma, 1..12 = paleta da casca). */
    fun setLayerLabel(layer: Long, label: Int) {
        if (engine.setLayerLabel(layer, label)) refreshNow()
    }

    /** Solo: com alguma camada em solo, a prévia e o som só tocam as que estão. */
    fun setLayerSolo(layer: Long, on: Boolean) {
        if (!engine.setLayerSolo(layer, on)) return
        refreshNow()
    }

    /** Busca de camadas (nome ou texto, sem maiúscula nem acento), da frente para o fundo. */
    fun searchLayers(query: String): List<Long> =
        if (query.isBlank()) emptyList() else engine.searchLayers(query.trim()).toList()

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
     * Arrasto de clipes na timeline: início/fim ABSOLUTOS de cada camada (o
     * gesto os calcula do estado no começo dele). Idempotente — não depende de
     * a releitura do modelo já ter chegado (fase 8D).
     */
    fun setLayerRanges(ids: LongArray, starts: IntArray, ends: IntArray) {
        if (ids.isEmpty()) return
        send { for (i in ids.indices) setLayerTimeRange(ids[i], max(0, starts[i]), max(1, ends[i])) }
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
        val rot = intArrayOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z)
        val axis = rot.indexOf(property)
        if (axis >= 0 && rot.any { d.isAnimated(it) }) {
            // Rotação X/Y/Z têm UM keyframe só: mexer num eixo grava os três
            // no mesmo instante (os outros com o valor que já têm ali).
            send {
                for (k in 0 until 3) insertKeyframe(id, rot[k], NO_EFFECT, 0, d.localPlayhead, if (k == axis) value else d.rotation[k])
            }
        } else if (d.isAnimated(property)) {
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
        wakeStatusLoop()   // o HUD mede o FPS da UI: precisa do vsync
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
                errorMessage = appText(R.string.msg_nao_foi_possivel_importar_o_video, humanError((-id).toInt()))
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
                    "Não foi possível importar o áudio. ${humanError((-id).toInt())}"
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
            errorMessage = appText(R.string.msg_este_video_nao_tem_som_para)
            return
        }
        refreshNow()
        select(created)
        showToast(appText(R.string.msg_audio_extraido_o_video_ficou_mudo))
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_o_texto_2, -id)
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
            errorMessage = appText(R.string.msg_use_um_hdri_hdr_radiance)
            return
        }
        busyMessage = "Carregando o HDRI…"
        viewModelScope.launch {
            val id = withContext(Dispatchers.IO) {
                val file = copyModelToSandbox(uri, "hdr") ?: return@withContext -1_000L
                engine.importHdri(file.absolutePath)
            }
            busyMessage = null
            if (id < 0) errorMessage = if (id == -1_000L) appText(R.string.msg_hdri_unreadable_file) else appText(R.string.msg_hdri_unreadable_error, -id)
            else showToast(appText(R.string.msg_hdri_aplicado_aos_modelos_3d))
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_agrupar_erro, -id)
            return
        }
        refreshNow()
        select(id)
        showToast(appText(R.string.msg_agrupado_toque_em_editar_o_grupo))
    }

    /** "Desagrupar": as camadas voltam para cá, no mesmo lugar e tempo da tela. */
    fun ungroupPrecomp(layer: Long) {
        val why = engine.ungroupPrecomp(layer)
        if (why != null) {
            errorMessage = appText(R.string.msg_nao_da_para_desagrupar_o_resultado, why)
            return
        }
        selection = LinkedHashSet()
        refreshNow()
        showToast(appText(R.string.msg_desagrupado))
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
            showToast(appText(R.string.msg_escolha_uma_camada_de_video))
            return
        }
        pointPick = stabilize
        showToast(appText(R.string.msg_toque_no_ponto_a_seguir_um))
    }

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
        showToast(if (stabilize) appText(R.string.msg_estabilizando) else appText(R.string.msg_rastreando_o_ponto))
        lifecycleThread.execute {
            val tracked = IntArray(1)
            val r = synchronized(lifecycleLock) { if (ready) engine.trackPoint(id, x, y, stabilize, tracked) else -1L }
            trackHandler.post {
                tracking = false
                pickCursor = null
                refreshNow()
                when {
                    r >= 0 && !stabilize -> { select(r); showToast(appText(R.string.msg_rastreio_quadros_o_nulo_segue_o, tracked[0])) }
                    r >= 0 -> showToast(appText(R.string.msg_estabilizado_em_quadros, tracked[0]))
                    else -> showToast(appText(R.string.msg_nao_deu_para_seguir_este_ponto, tracked[0]))
                }
            }
        }
    }


    // --- Máscaras (roto) e track matte ----------------------------------------------------------
    /**
     * Uma máscara no cabeçote: modo (0 somar, 1 subtrair, 2 intersectar, 3
     * diferença, 4 nenhum), ajustes e os pontos (6 floats cada: x, y, entrada
     * x/y, saída x/y — px da camada; tangentes relativas ao ponto).
     */
    class MaskPath(
        val id: Int,
        val op: Int,
        val inverted: Boolean,
        val feather: Float,
        val expansion: Float,
        val opacity: Float,
        val closed: Boolean,
        val keyCount: Int,
        val keyHere: Boolean,
        val active: Boolean,
        val points: FloatArray,
    ) {
        val count: Int get() = points.size / 6
    }

    /** Máscaras da camada principal; [m] = composição ← camada (a b c d tx ty). */
    class MaskState(val layer: Long, val m: FloatArray, val masks: List<MaskPath>) {
        fun find(id: Int?) = masks.firstOrNull { it.id == id }
        fun toCompX(x: Float, y: Float) = m[0] * x + m[2] * y + m[4]
        fun toCompY(x: Float, y: Float) = m[1] * x + m[3] * y + m[5]
        /** Composição → camada (inversa da afim); nulo se degenerada. */
        fun toLayer(cx: Float, cy: Float): FloatArray? {
            val det = m[0] * m[3] - m[1] * m[2]
            if (kotlin.math.abs(det) < 1e-9f) return null
            val px = cx - m[4]
            val py = cy - m[5]
            return floatArrayOf((m[3] * px - m[2] * py) / det, (-m[1] * px + m[0] * py) / det)
        }
        /** Vetor da composição → camada (tangentes). */
        fun toLayerVec(dx: Float, dy: Float): FloatArray? {
            val det = m[0] * m[3] - m[1] * m[2]
            if (kotlin.math.abs(det) < 1e-9f) return null
            return floatArrayOf((m[3] * dx - m[2] * dy) / det, (-m[1] * dx + m[0] * dy) / det)
        }
    }

    var masks by mutableStateOf<MaskState?>(null)
        private set
    /** Máscara em edição no palco (modo roto); nulo = palco normal. */
    var maskEdit by mutableStateOf<Int?>(null)
    /** Desenhando o caminho (toques no palco acrescentam pontos). */
    var maskDrawing by mutableStateOf(false)
    /** Ponto escolhido da máscara em edição (alças de bezier dele no palco). */
    var maskPoint by mutableStateOf(-1)
    /** Track matte da camada principal: {matte, modo}. */
    var trackMatte by mutableStateOf<LongArray?>(null)
        private set
    var maskTracking by mutableStateOf(false)
        private set
    private var maskBuf = FloatArray(1024)

    private fun refreshMasks() {
        val id = primary
        if (id == null) {
            masks = null
            trackMatte = null
            maskEdit = null
            maskDrawing = false
            return
        }
        var need = engine.queryMasks(id, maskBuf)
        if (need > maskBuf.size) {
            maskBuf = FloatArray(need + 256)
            need = engine.queryMasks(id, maskBuf)
        }
        masks = if (need >= 7) {
            val b = maskBuf
            val n = b[6].toInt()
            var o = 7
            val list = ArrayList<MaskPath>(n)
            repeat(n) {
                val pc = b[o + 7].toInt()
                val pts = b.copyOfRange(o + MASK_HEADER, o + MASK_HEADER + pc * 6)
                list += MaskPath(
                    id = b[o].toInt(), op = b[o + 1].toInt(), inverted = b[o + 2] > 0.5f, feather = b[o + 3],
                    expansion = b[o + 4], opacity = b[o + 5], closed = b[o + 6] > 0.5f, keyCount = b[o + 8].toInt(),
                    keyHere = b[o + 9] > 0.5f, active = b[o + 10] > 0.5f, points = pts,
                )
                o += MASK_HEADER + pc * 6
            }
            MaskState(id, b.copyOfRange(0, 6), list)
        } else {
            null
        }
        if (masks?.find(maskEdit) == null) {
            maskEdit = null
            maskDrawing = false
            maskPoint = -1
        }
        val tm = LongArray(2)
        trackMatte = if (engine.queryTrackMatte(id, tm)) tm else null
    }

    /** Começa uma máscara desenhada à mão: os próximos toques no palco põem pontos. */
    fun startMaskDrawing() {
        val id = primary ?: return
        val mid = engine.addMask(id, null, 0, false)
        if (mid < 0) { showToast(appText(R.string.msg_esta_camada_nao_aceita_mais_mascaras)); return }
        refreshNow()
        maskEdit = mid
        maskDrawing = true
        maskPoint = -1
        showToast(appText(R.string.msg_toque_no_palco_para_por_pontos))
    }

    /** Máscara pronta (0 retângulo, 1 elipse) em 70 % da camada, centrada. */
    fun addMaskPreset(shape: Int) {
        val id = primary ?: return
        val d = detail ?: return
        val w = com.aurea.aurea.editor.LayerGeometry.width(d)
        val h = com.aurea.aurea.editor.LayerGeometry.height(d)
        if (w <= 0f || h <= 0f) return
        val cx = w / 2
        val cy = h / 2
        val rx = w * 0.35f
        val ry = h * 0.35f
        val pts = if (shape == 0) {
            floatArrayOf(cx - rx, cy - ry, 0f, 0f, 0f, 0f, cx + rx, cy - ry, 0f, 0f, 0f, 0f,
                cx + rx, cy + ry, 0f, 0f, 0f, 0f, cx - rx, cy + ry, 0f, 0f, 0f, 0f)
        } else {
            // Elipse de 4 cúbicas (k = 0,5523: erro radial < 0,03 %).
            val kx = rx * 0.5523f
            val ky = ry * 0.5523f
            floatArrayOf(cx, cy - ry, -kx, 0f, kx, 0f, cx + rx, cy, 0f, -ky, 0f, ky,
                cx, cy + ry, kx, 0f, -kx, 0f, cx - rx, cy, 0f, ky, 0f, -ky)
        }
        val mid = engine.addMask(id, pts, 4, true)
        if (mid < 0) { showToast(appText(R.string.msg_esta_camada_nao_aceita_mais_mascaras)); return }
        refreshNow()
        maskEdit = mid
        maskDrawing = false
        maskPoint = -1
    }

    /** Troca o caminho da máscara em edição (`undo` = abre um passo de desfazer). */
    fun setMaskPoints(mask: Int, pts: FloatArray, closed: Boolean, undo: Boolean) {
        val id = primary ?: return
        engine.setMaskPath(id, mask, pts, pts.size / 6, closed, undo)
        refreshMasks()
    }

    /** Fim de um gesto de máscara no palco: relê tudo (timeline, keyframes). */
    fun maskGestureEnd() = refreshNow()

    fun closeMaskPath() {
        val mid = maskEdit ?: return
        val m = masks?.find(mid) ?: return
        if (m.count < 3) { showToast(appText(R.string.msg_ponha_pelo_menos_3_pontos)); return }
        setMaskPoints(mid, m.points, true, true)
        maskDrawing = false
        refreshNow()
    }

    fun setMaskProps(mask: Int, op: Int, inverted: Boolean, feather: Float, expansion: Float, opacity: Float) {
        val id = primary ?: return
        engine.setMaskProps(id, mask, op, inverted, feather, expansion, opacity)
        refreshMasks()
    }

    fun deleteMask(mask: Int) {
        val id = primary ?: return
        if (engine.removeMask(id, mask)) {
            if (maskEdit == mask) { maskEdit = null; maskDrawing = false; maskPoint = -1 }
            refreshNow()
        }
    }

    fun toggleMaskKey(mask: Int) {
        val id = primary ?: return
        when (engine.toggleMaskPathKey(id, mask)) {
            1 -> showToast(appText(R.string.msg_keyframe_do_caminho_no_cabecote))
            0 -> showToast(appText(R.string.msg_keyframe_do_caminho_removido))
        }
        refreshNow()
    }

    /** Rastreia a máscara no vídeo (0 posição, 1 posição + escala + giro). */
    fun trackMask(mask: Int, mode: Int) {
        val id = primary ?: return
        val row = layers.firstOrNull { it.id == id }
        if (row == null || row.kind != com.aurea.aurea.ui.theme.LayerType.Video.kind) {
            showToast(appText(R.string.msg_o_rastreio_de_mascara_precisa_de))
            return
        }
        if (maskTracking || tracking) return
        maskTracking = true
        showToast(appText(R.string.msg_rastreando_a_mascara))
        lifecycleThread.execute {
            val r = synchronized(lifecycleLock) { if (ready) engine.trackMask(id, mask, mode) else -1 }
            trackHandler.post {
                maskTracking = false
                refreshNow()
                if (r >= 0) showToast(appText(R.string.msg_mascara_rastreada_em_quadros, r))
                else showToast(appText(R.string.msg_nao_deu_para_seguir_a_mascara))
            }
        }
    }

    fun setTrackMatte(matte: Long, mode: Int) {
        val id = primary ?: return
        if (!engine.setTrackMatte(id, matte, mode)) showToast(appText(R.string.msg_escolha_outra_camada_como_matte))
        refreshNow()
    }

    // --- Partículas ------------------------------------------------------------------------
    /** Parâmetros da camada de partículas escolhida (8, ver Engine::query_particles). */
    var particles by mutableStateOf<List<Float>?>(null)
        private set

    fun addParticles(preset: Int = 0): Long {
        val id = engine.addParticles(preset)
        if (id < 0) {
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_as_particulas, -id)
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

    /** Com keyframe, o valor vai para o keyframe do playhead (como nos efeitos). */
    fun setParticleParam(param: Int, value: Float) {
        val id = primary ?: return
        val d = detail ?: return
        if (particleKeyed(id, param)) {
            send { insertKeyframe(id, TrackProperty.PARTICLE_PARAM, NO_EFFECT, param, d.localPlayhead, value) }
        } else {
            engine.setParticleParam(id, param, value)
        }
        refreshDetail()
    }

    /** Losango de keyframe de um parâmetro do Aurea Particular. */
    fun toggleParticleKeyframe(param: Int) {
        val id = primary ?: return
        val d = detail ?: return
        val here = (keyframes[id] ?: emptyList()).filter {
            it.property == TrackProperty.PARTICLE_PARAM && it.paramIndex == param && it.time == d.localPlayhead
        }
        group(if (here.isNotEmpty()) "remover keyframe" else "adicionar keyframe") {
            if (here.isNotEmpty()) {
                here.forEach { deleteKeyframe(id, it.property, it.effectIndex, it.paramIndex, it.time) }
            } else {
                insertKeyframe(id, TrackProperty.PARTICLE_PARAM, NO_EFFECT, param, d.localPlayhead,
                    particles?.getOrNull(param) ?: 0f)
            }
        }
    }

    /** Keyframe neste instante exatamente (para o losango cheio). */
    fun particleKeyHere(param: Int): Boolean {
        val id = primary ?: return false
        val d = detail ?: return false
        return (keyframes[id] ?: emptyList()).any {
            it.property == TrackProperty.PARTICLE_PARAM && it.paramIndex == param && it.time == d.localPlayhead
        }
    }

    /** A trilha do parâmetro existe (animado em algum lugar da timeline). */
    fun particleKeyed(id: Long, param: Int): Boolean =
        (keyframes[id] ?: emptyList()).any {
            it.property == TrackProperty.PARTICLE_PARAM && it.paramIndex == param
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
        showToast(if (on) appText(R.string.msg_desfoque_de_movimento_ligado) else appText(R.string.msg_desfoque_de_movimento_desligado))
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_analisar_esta_camada)
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_a_camera, -id)
            return
        }
        refreshNow()
        select(id)
        showToast(appText(R.string.msg_camera_rastreada_criada_ligue_modelos_3d))
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
        if (!ok) showToast(appText(R.string.msg_a_curva_precisa_de_pelo_menos))
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

    // =========================================================================
    // Presets (JSON do motor; arquivos em filesDir/presets/<tipo>/)
    // =========================================================================
    /** Criada no primeiro uso (8I): a abertura não lista as pastas de presets. */
    val presets by lazy { com.aurea.aurea.presets.PresetLibrary(app) }

    /**
     * JSON do preset `kind` a partir do que está na tela: a camada escolhida
     * (efeitos, texto, animação) ou as opções de legenda. Curva vem do painel
     * de curva ([curvePresetJson]). Nulo = nada a salvar.
     */
    fun capturePreset(kind: com.aurea.aurea.presets.PresetKind, name: String, parts: Int = 3): String? {
        return when (kind) {
            com.aurea.aurea.presets.PresetKind.Caption -> {
                val s = captions.settings
                engine.makeCaptionPreset(
                    name,
                    intArrayOf(
                        s.mode, s.maxWords, s.maxChars, s.maxLines, s.style,
                        if (s.highlight) 1 else 0, if (s.uppercase) 1 else 0, if (s.breakOnPause) 1 else 0, if (s.removeFillers) 1 else 0,
                    ),
                    floatArrayOf(s.pauseSec, s.posY, s.sizeFrac, s.highlightColor[0], s.highlightColor[1], s.highlightColor[2]),
                )
            }
            com.aurea.aurea.presets.PresetKind.Curve -> null
            else -> primary?.let { engine.savePreset(it, kind.id, name, parts) }
        }
    }

    fun curvePresetJson(name: String, interp: Int, x1: Float, y1: Float, x2: Float, y2: Float): String? =
        engine.makeCurvePreset(name, interp, x1, y1, x2, y2)

    /** [interp, x1, y1, x2, y2] do preset de curva; nulo = inválido. */
    fun curveOfPreset(e: com.aurea.aurea.presets.PresetEntry): FloatArray? = presets.jsonOf(e)?.let { engine.parseCurvePreset(it) }

    fun savePreset(kind: com.aurea.aurea.presets.PresetKind, name: String, json: String?): Boolean {
        if (json == null) {
            showToast(
                when (kind) {
                    com.aurea.aurea.presets.PresetKind.Effects -> appText(R.string.msg_esta_camada_nao_tem_efeitos)
                    com.aurea.aurea.presets.PresetKind.Text -> appText(R.string.msg_so_camada_de_texto_tem_estilo)
                    com.aurea.aurea.presets.PresetKind.Animation -> appText(R.string.msg_esta_camada_nao_tem_keyframes_de)
                    com.aurea.aurea.presets.PresetKind.Curve -> appText(R.string.msg_toque_num_keyframe_com_o_seguinte)
                    else -> appText(R.string.msg_nada_para_salvar)
                },
            )
            return false
        }
        val e = presets.save(kind, name, json)
        showToast(if (e != null) appText(R.string.msg_preset_salvo, e.name) else appText(R.string.msg_nao_foi_possivel_salvar_o_preset))
        return e != null
    }

    fun deletePreset(e: com.aurea.aurea.presets.PresetEntry) {
        showToast(if (presets.delete(e)) appText(R.string.msg_preset_apagado, e.name) else appText(R.string.msg_nao_foi_possivel_apagar))
    }

    /**
     * Aplica um preset de camada (efeitos, texto, animação: um passo de
     * desfazer) ou de legenda (opções da geração; refaz as legendas que já
     * existem). Curva é aplicada pelo painel de curva, no keyframe escolhido.
     * `stretch` = a animação ocupa do cabeçote até o fim da camada.
     */
    fun applyPreset(e: com.aurea.aurea.presets.PresetEntry, stretch: Boolean = false): Boolean {
        when (e.kind) {
            com.aurea.aurea.presets.PresetKind.Curve -> return false
            com.aurea.aurea.presets.PresetKind.Caption -> {
                val v = presets.jsonOf(e)?.let { engine.parseCaptionPreset(it) }
                if (v == null || v.size < 15) {
                    showToast(appText(R.string.msg_preset_de_legenda_invalido))
                    return false
                }
                captions.settings = com.aurea.aurea.captions.CaptionSettings(
                    mode = v[0].toInt(), maxWords = v[1].toInt(), maxChars = v[2].toInt(), maxLines = v[3].toInt(), style = v[4].toInt(),
                    highlight = v[5] != 0f, uppercase = v[6] != 0f, breakOnPause = v[7] != 0f, removeFillers = v[8] != 0f,
                    pauseSec = v[9], posY = v[10], sizeFrac = v[11], highlightColor = floatArrayOf(v[12], v[13], v[14]),
                )
                val id = primary
                if (id != null && captions.layer == id && captions.captionCount > 0 && captions.words.isNotEmpty()) {
                    captions.generate()
                    showToast(appText(R.string.msg_legendas_refeitas_com, e.name))
                } else {
                    showToast(appText(R.string.msg_estilo_de_legenda_escolhido, e.name))
                }
            }
            else -> {
                val id = primary ?: return false
                val native = e.textPreset
                if (native != null) {
                    if (!engine.applyTextPreset(id, native)) {
                        showToast(appText(R.string.msg_animacao_de_texto_so_vale_para))
                        return false
                    }
                } else {
                    val json = presets.jsonOf(e) ?: run {
                        showToast(appText(R.string.msg_arquivo_do_preset_nao_encontrado))
                        return false
                    }
                    var duration = 0L
                    if (stretch && e.kind == com.aurea.aurea.presets.PresetKind.Animation) {
                        layers.firstOrNull { it.id == id }?.let { l ->
                            val from = if (playhead >= l.startFrame && playhead < l.endFrame) playhead else l.startFrame
                            duration = (l.endFrame - from - 1).coerceAtLeast(1).toLong()
                        }
                    }
                    val err = engine.applyPreset(id, json, duration)
                    if (err != null) {
                        showToast(appText(R.string.msg_preset_nao_aplicado, err))
                        return false
                    }
                }
                refreshNow()
                refreshDetail()
                showToast(appText(R.string.msg_aplicado, e.name))
            }
        }
        presets.markUsed(e)
        return true
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

    // =========================================================================
    // Expressões
    // =========================================================================

    /**
     * Propriedade aberta na folha de expressão. [tracks] = as trilhas que
     * recebem o MESMO texto (Posição = X e Y: a expressão é vetorial e cada
     * trilha pega o seu componente). [scale] converte a unidade guardada para a
     * mostrada (opacidade/escala/volume: ×100).
     */
    data class ExpressionTarget(val layer: Long, val label: String, val tracks: List<TrackKey>, val scale: Float = 1f, val unit: String = "")

    /** Folha aberta (nulo = fechada). */
    var expressionTarget by mutableStateOf<ExpressionTarget?>(null)
        private set

    /** Trilhas com expressão na camada principal (o "=" das linhas). */
    var expressions by mutableStateOf<List<ExpressionRow>>(emptyList())
        private set

    fun openExpression(label: String, tracks: List<TrackKey>, scale: Float = 1f, unit: String = "") {
        val id = primary ?: return
        if (tracks.isEmpty()) return
        expressionTarget = ExpressionTarget(id, label, tracks, scale, unit)
    }

    fun closeExpression() {
        expressionTarget = null
    }

    /** Estado da 1ª trilha do alvo, avaliada no playhead agora. */
    fun expressionInfo(t: ExpressionTarget): ExpressionInfo? = engine.queryExpression(t.layer, t.tracks.first())

    /** Valor resultante (unidade mostrada) de cada trilha do alvo, no playhead. */
    fun expressionValues(t: ExpressionTarget): List<Float> =
        t.tracks.map { k -> (engine.queryExpression(t.layer, k)?.value ?: 0f) * t.scale }

    /**
     * Grava o texto em todas as trilhas do alvo (o motor faz UM passo de
     * desfazer). Vazio remove. Nulo = o motor recusou (propriedade inválida).
     */
    fun applyExpression(t: ExpressionTarget, source: String): ExpressionDiag? {
        val d = engine.setExpression(t.layer, t.tracks, source)
        refreshModel()
        refreshNow()
        return d
    }

    fun setExpressionEnabled(t: ExpressionTarget, enabled: Boolean) {
        engine.setExpressionEnabled(t.layer, t.tracks, enabled)
        refreshModel()
        refreshNow()
    }

    /** Só a sintaxe (enquanto digita). */
    fun checkExpressionSyntax(source: String): ExpressionDiag = engine.checkExpressionSyntax(source)

    /** O "=" de uma linha: alguma das [tracks] tem expressão? (erro > ligada > desligada). */
    fun expressionLook(tracks: List<TrackKey>): ExpressionLook {
        var look = ExpressionLook.None
        for (r in expressions) {
            if (r.key !in tracks) continue
            if (r.hasError && r.enabled) return ExpressionLook.Error
            look = if (r.enabled) ExpressionLook.On else if (look == ExpressionLook.None) ExpressionLook.Off else look
        }
        return look
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
            errorMessage = appText(R.string.msg_use_uma_fonte_ttf_ou_otf)
            return
        }
        viewModelScope.launch {
            val line = withContext(Dispatchers.IO) {
                val file = copyToDir(uri, "fontes", ext) ?: return@withContext null
                engine.importFont(file.absolutePath)
            }
            val item = line?.let { parseFont(it) }
            if (item == null) {
                errorMessage = appText(R.string.msg_nao_deu_para_ler_essa_fonte)
                return@launch
            }
            fonts = (fonts.filterNot { it.path == item.path } + item).sortedWith(compareBy({ it.family }, { it.italic }, { it.weight }))
            applyTextFont(item)
            showToast(appText(R.string.msg_fonte_importada, item.family))
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
        // Temporário tem dono (§52): a cópia pela metade sai; o motivo fica no log (sem a URI).
        Log.w(TAG, "copia para o app falhou ($sub): ${e.javaClass.simpleName}: ${e.message}")
        File(File(File(getApplication<Application>().filesDir, "projetos"), sub), "importando.$ext").delete()
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
        showToast(if (on) appText(R.string.msg_desfoque_do_movimento_do_video_ligado) else appText(R.string.msg_desfoque_do_movimento_do_video_desligado))
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
        showToast(if (n == 1) appText(R.string.msg_camada_copiada) else appText(R.string.msg_camadas_copiadas, n))
    }

    fun pasteLayers() {
        // As coladas ficam escolhidas (o mesmo caminho do duplicar).
        selectCreatedAfter = layers.map { it.id }.toSet()
        val n = engine.pasteLayers(playhead.toLong())
        if (n <= 0) selectCreatedAfter = null
        refreshNow()
        showToast(
            when {
                n <= 0 -> appText(R.string.msg_nada_para_colar_a_midia_nao)
                n == 1 -> appText(R.string.msg_camada_colada_no_cabecote)
                else -> appText(R.string.msg_camadas_coladas_no_cabecote, n)
            },
        )
    }

    fun copyStyle() {
        val id = primary ?: return
        if (engine.copyStyle(id)) showToast(appText(R.string.msg_estilo_copiado))
        afterClipboard()
    }

    fun pasteStyle(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteStyle(ids.toLongArray())
        refreshNow()
        if (n > 0) showToast(appText(R.string.msg_estilo_colado))
    }

    fun copyEffects() {
        val id = primary ?: return
        val n = engine.copyEffects(id)
        afterClipboard()
        showToast(if (n > 0) appText(R.string.msg_efeito_s_copiado_s, n) else appText(R.string.msg_esta_camada_nao_tem_efeitos))
    }

    fun pasteEffects(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteEffects(ids.toLongArray())
        refreshNow()
        if (n > 0) showToast(appText(R.string.msg_efeitos_colados))
    }

    fun copyKeyframes() {
        val id = primary ?: return
        val n = engine.copyKeyframes(id, playhead.toLong())
        afterClipboard()
        showToast(if (n > 0) appText(R.string.msg_keyframe_s_copiado_s, n) else appText(R.string.msg_nenhum_keyframe_no_cabecote))
    }

    fun pasteKeyframes(ids: Collection<Long> = selection) {
        if (ids.isEmpty()) return
        val n = engine.pasteKeyframes(ids.toLongArray(), playhead.toLong())
        refreshNow()
        showToast(if (n > 0) appText(R.string.msg_keyframe_s_colado_s_no_cabecote, n) else appText(R.string.msg_nenhuma_propriedade_compativel))
    }

    // --- Modo Edição (timeline magnética) -------------------------------------------
    /** Aparar empurra/puxa as seguintes; excluir fecha o buraco. */
    var editMode by mutableStateOf(false)
        private set

    fun toggleEditMode() {
        engine.setEditMode(!editMode)
        refreshNow()
        showToast(if (editMode) appText(R.string.msg_modo_edicao_a_timeline_fecha_os) else appText(R.string.msg_modo_composicao_camadas_livres_no_tempo))
    }

    fun removeGaps() {
        val removed = engine.removeGaps()
        refreshNow()
        val fps = project.fps.takeIf { it > 0f } ?: 30f
        showToast(if (removed > 0) appText(R.string.msg_gaps_removed, "%.1f".format(removed / fps)) else appText(R.string.msg_nao_ha_espacos_vazios))
    }

    fun trimProjectAtPlayhead() {
        if (engine.trimComposition(playhead.toLong())) {
            refreshNow()
            showToast(appText(R.string.msg_projeto_aparado_no_cabecote))
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
    fun toggleMarker() = toggleMarkerAt(playhead)

    /**
     * Marca (ou desmarca) um frame qualquer. O toque na régua cai aqui com o
     * frame do DEDO, não com o do cabeçote: a marca nasce onde se tocou, mesmo
     * que a prévia ainda esteja alcançando aquele quadro.
     */
    fun toggleMarkerAt(frame: Int) {
        val f = clampFrame(frame)
        val on = engine.toggleMarker(f.toLong())
        refreshNow()
        showToast(if (on) appText(R.string.msg_marca_adicionada) else appText(R.string.msg_marca_removida))
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
            showToast(appText(R.string.msg_escolha_uma_camada_de_audio_ou))
            return
        }
        if (detectingBeats) return
        detectingBeats = true
        showToast(appText(R.string.msg_detectando_batidas))
        lifecycleThread.execute {
            val bpm = DoubleArray(1)
            val n = synchronized(lifecycleLock) { if (ready) engine.detectBeats(id, bpm) else -1L }
            beatHandler.post {
                detectingBeats = false
                refreshNow()
                when {
                    n > 0 -> showToast(appText(R.string.msg_batidas_bpm, n, bpm[0].roundToInt()))
                    n == 0L -> showToast(appText(R.string.msg_nenhuma_batida_clara_neste_som))
                    else -> showToast(appText(R.string.msg_nao_foi_possivel_analisar_o_som, -n))
                }
            }
        }
    }

    // --- Nulo e parentesco --------------------------------------------------------
    fun addNull(threeD: Boolean) {
        val id = engine.addNull(threeD)
        if (id < 0) {
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_o_nulo, -id)
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

    /** Vários filhos para o mesmo pai (0 = soltar), num passo de desfazer. */
    fun setParentMany(ids: Collection<Long>, parent: Long) {
        val children = ids.filter { it != parent && (parent == 0L || parentCandidates(it).any { c -> c.id == parent }) }
        if (children.isEmpty()) return
        beginGesture(if (parent == 0L) "soltar camadas" else "vincular camadas")
        children.forEach { c -> send { setLayerParent(c, parent) } }
        endGesture()
        refreshNow()
        if (parent != 0L) showToast(appText(R.string.msg_camada_s_seguindo_o_pai_escolhido, children.size))
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
            errorMessage = appText(R.string.msg_nao_foi_possivel_criar_a_forma, -id)
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

    /**
     * Parâmetro da forma. Do 1 ao 6 (raio, lados, raio interno, contorno,
     * largura, altura) é ANIMÁVEL: com keyframes, grava no cabeçote; parado,
     * muda o valor. O 0 (tipo da forma) continua sendo um comando.
     */
    fun setShapeParam(param: Int, value: Float, continuing: Boolean = false) {
        val id = primary ?: return
        if (param in 1..6) {
            engine.setShapeParamAnim(id, param, value, continuing)
            refreshDetail()
            return
        }
        send { setShapeParam(id, param, value) }
        refreshDetail()
    }

    /** Liga/desliga o keyframe do parâmetro da forma no cabeçote. */
    fun toggleShapeParamKey(param: Int) {
        val id = primary ?: return
        if (engine.toggleShapeParamKey(id, param)) refreshNow()
    }

    /** Valores da forma no cabeçote + bits de animado + bits de keyframe aqui. */
    var shapeParams by mutableStateOf<FloatArray?>(null)
        private set

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
            errorMessage = appText(R.string.msg_posicione_o_cabecote_sobre_o_video)
            return
        }
        refreshNow()
        select(created)
        showToast(appText(R.string.msg_quadro_congelado_por_3_s))
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
            errorMessage = appText(R.string.msg_esse_arquivo_nao_e_um_modelo)
            return
        }
        busyMessage = "Importando modelo 3D…"
        viewModelScope.launch {
            val poll = launch {
                while (true) {
                    kotlinx.coroutines.delay(150)
                    val p = engine.importModelProgress()
                    val phase = when (p / 1000) {
                        1 -> appText(R.string.msg_lendo_o_arquivo)
                        2 -> appText(R.string.msg_geometria)
                        3 -> appText(R.string.msg_texturas)
                        4 -> appText(R.string.msg_otimizando)
                        5, 6 -> appText(R.string.msg_preparando)
                        else -> appText(R.string.msg_importando)
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
                id == -1_000L -> errorMessage = appText(R.string.msg_nao_consegui_ler_esse_arquivo)
                id < 0 -> errorMessage = appText(R.string.msg_model_import_failed, detail[0]?.trim()?.ifBlank { null } ?: humanError((-id).toInt()))
                else -> {
                    refreshNow()
                    select(id)
                    val warnings = detail[0]?.lines()?.filter { it.isNotBlank() }.orEmpty()
                    if (warnings.isNotEmpty()) showToast(appText(R.string.msg_modelo_importado_aviso_s, warnings.size, warnings.first()))
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
        // Temporário tem dono (§52): a cópia pela metade (disco cheio, stream cortado) sai.
        Log.w(TAG, "copia do modelo 3D para o app falhou: ${e.javaClass.simpleName}: ${e.message}")
        File(File(getApplication<Application>().filesDir, "modelos"), "importando.$ext").delete()
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
                errorMessage = appText(R.string.msg_nao_foi_possivel_importar_a_imagem)
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
                errorMessage = appText(R.string.msg_nao_foi_possivel_criar_o_projeto)
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
                Log.w(TAG, "abrir projeto falhou: codigo $code")
                errorMessage = appText(R.string.msg_nao_foi_possivel_abrir_o_projeto, humanError(code))
                return@launch
            }
            val meta = readMeta(File(path))
            project = ProjectState(title = meta?.title ?: File(path).nameWithoutExtension, path = path)
            enterEditor()
            // O que a abertura precisou fazer vira aviso, não silêncio (§55, §120, §124).
            val notice = engine.loadNotice()
            val missing = notice ushr 16
            val parts = buildList {
                if (notice and 1 != 0) add("o arquivo principal estava danificado; abrimos a última cópia válida")
                if (notice and 2 != 0) add("o projeto abriu com partes faltando (o arquivo original foi guardado)")
                if (notice and 4 != 0) add("projeto de versão anterior: uma cópia do original foi guardada")
                if (notice and 8 != 0) add("$missing mídia(s)/fonte(s)/modelo(s) não encontrada(s) — o espaço fica marcado para religar")
            }
            if (parts.isNotEmpty()) {
                Log.w(TAG, "abertura com avisos: 0x${Integer.toHexString(notice)}")
                errorMessage = parts.joinToString(";\n", postfix = ".").replaceFirstChar { it.uppercase() }
            }
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
            if (code != 0) errorMessage = appText(R.string.msg_nao_foi_possivel_salvar, humanError(code))
            onDone?.invoke()
        }
    }

    /** Salva projeto, miniatura e o sidecar que a Home lê. */
    private fun saveBlocking(path: String, withThumbnail: Boolean = true): Int {
        val code = engine.saveProject(path)
        if (code != 0) {
            Log.w(TAG, "salvar projeto falhou: codigo $code (o arquivo anterior segue intacto)")
            return code
        }
        val file = File(path)
        val thumbFile = File(directories().thumbs, file.nameWithoutExtension + ".jpg")
        // Miniatura e sidecar são derivados: falhar neles (disco cheio) não
        // desfaz o projeto já gravado, mas fica no log e o autosave tenta de novo.
        try {
            if (withThumbnail || !thumbFile.exists()) writeThumbnail(thumbFile)
            val meta = JSONObject()
                .put("title", project.title)
                .put("width", project.width)
                .put("height", project.height)
                .put("fps", project.fps.toDouble())
                .put("durationFrames", project.durationFrames)
                .put("thumbnail", thumbFile.absolutePath)
            writeTextAtomic(File(path + META_SUFFIX), meta.toString())
        } catch (e: java.io.IOException) {
            Log.w(TAG, "miniatura/sidecar do projeto nao gravados: ${e.message}")
            return if (e.message?.contains("ENOSPC") == true) ERRC_STORAGE_FULL else ERRC_IO
        }
        return 0
    }

    private fun writeThumbnail(target: File) {
        val bmp = captureBitmap(THUMB_MAX) ?: return
        // Temporário → rename: uma miniatura cortada pela metade não vira a capa do projeto.
        val tmp = File(target.path + ".tmp")
        try {
            FileOutputStream(tmp).use { bmp.compress(Bitmap.CompressFormat.JPEG, 85, it) }
            java.nio.file.Files.move(
                tmp.toPath(), target.toPath(),
                java.nio.file.StandardCopyOption.ATOMIC_MOVE, java.nio.file.StandardCopyOption.REPLACE_EXISTING,
            )
        } finally {
            tmp.delete()
            bmp.recycle()
        }
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
            showToast(appText(R.string.msg_aguarde_a_exportacao_terminar))
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
            keySnapshot.clear()
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
            } catch (e: Exception) {
                Log.w(TAG, "sidecar ilegivel ao renomear (refeito so com o titulo): ${e.message}")
                JSONObject()
            }
            try {
                writeTextAtomic(metaFile, j.put("title", clean).toString())
            } catch (e: java.io.IOException) {
                Log.w(TAG, "renomear projeto: sidecar nao gravado: ${e.message}")
                withContext(Dispatchers.Main) { errorMessage = appText(R.string.msg_nao_foi_possivel_renomear, humanError(ERRC_IO)) }
            }
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
                // Arquivos de recuperação do motor: .bak, .tmp, .corrompido, .vN.bak.
                f.parentFile?.listFiles { s -> s.name.startsWith(f.name + ".") }?.forEach { it.delete() }
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
            // Disco cheio no meio da cópia derrubava o app (exceção sem dono na
            // corrotina). Agora: o que foi copiado pela metade sai, e a pessoa é avisada.
            val tmp = File(dst.path + ".tmp")
            try {
                src.copyTo(tmp, overwrite = true)
                if (!tmp.renameTo(dst)) throw java.io.IOException("rename da copia falhou")
                meta?.thumbnailPath?.let { t ->
                    val tf = File(t)
                    if (tf.exists()) tf.copyTo(File(directories().thumbs, dst.nameWithoutExtension + ".jpg"), overwrite = true)
                }
                writeTextAtomic(
                    File(dst.absolutePath + META_SUFFIX),
                    JSONObject()
                        .put("title", title)
                        .put("width", meta?.width ?: 0)
                        .put("height", meta?.height ?: 0)
                        .put("fps", (meta?.fps ?: 30f).toDouble())
                        .put("durationFrames", meta?.durationFrames ?: 0)
                        .put("thumbnail", File(directories().thumbs, dst.nameWithoutExtension + ".jpg").absolutePath)
                        .toString(),
                )
            } catch (e: java.io.IOException) {
                Log.w(TAG, "duplicar projeto falhou: ${e.message}")
                tmp.delete()
                dst.delete()
                File(dst.absolutePath + META_SUFFIX).delete()
                val code = if (e.message?.contains("ENOSPC") == true) ERRC_STORAGE_FULL else ERRC_IO
                withContext(Dispatchers.Main) { errorMessage = appText(R.string.msg_nao_foi_possivel_duplicar, humanError(code)) }
            }
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
        } catch (e: Exception) {
            // Sidecar ilegível: o cartão sai com o nome do arquivo (o projeto em si não depende dele).
            Log.w(TAG, "sidecar ilegivel (${file.name}): ${e.message}")
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

    /** Uso de memória do motor por categoria (null = motor ainda subindo). */
    fun engineMemory(): LongArray? = if (ready) engine.memoryReport() else null

    /** Bitmaps de miniatura guardados pela UI agora. */
    fun uiThumbnailBytes(): Long = thumbnails.bytes()

    /**
     * Limpa um tipo de cache (Ajustes › Armazenamento). `MEMORY` = caches de
     * memória (miniaturas, waveform, quadros decodificados, cache de render):
     * o mesmo caminho do aviso de pressão do sistema, no nível mais forte.
     * Devolve os bytes liberados. Nunca toca em projeto.
     */
    fun clearStorage(id: String): Long {
        if (id == MEMORY) {
            thumbnails.clear()
            effectPreviews?.trimMemory()
            var freed = 0L
            synchronized(lifecycleLock) { if (ready) freed = engine.trimMemory(TRIM_COMPLETE) }
            return freed
        }
        if (id == CacheStorage.EXPORT && exporter.busy) return 0L
        val freed = storage.clear(id)
        if (id == CacheStorage.PREVIEWS) effectPreviews?.clear()
        return freed
    }

    /**
     * "Limpar tudo": todos os tipos regeneráveis, em disco e na memória.
     * Onde fica cada um está em [CacheStorage]; projetos nunca. Devolve os
     * bytes liberados (disco + memória do motor).
     */
    fun clearCache(): Long {
        var freed = 0L
        for (k in storage.scan(exporter.busy)) freed += clearStorage(k.id)
        freed += clearStorage(MEMORY)
        main.post { showToast(appText(R.string.msg_cache_cleared, "%.1f".format(freed / (1024.0 * 1024.0)))) }
        return freed
    }

    fun dismissError() {
        errorMessage = null
    }

    companion object {
        const val THUMB_MAX = 512
        /** Quadros parados antes de o laço de status ficar ocioso (~0,5 s a 60 Hz). */
        const val QUIET_FRAMES = 30
        const val IDLE_POLL_MS = 250L
        /** Silêncio depois da última mudança antes do autosave. */
        const val AUTOSAVE_IDLE_NS = 3_000_000_000L
        /** Espera depois de um autosave que falhou (ex.: armazenamento cheio). */
        const val AUTOSAVE_RETRY_NS = 30_000_000_000L
        const val META_SUFFIX = ".meta.json"
        /** `kInvalidIndex` do C++: keyframe que não é de efeito. */
        const val NO_EFFECT = -1
        /** Id do "tipo" memória na tela Armazenamento. */
        const val MEMORY = "memoria"
        // ComponentCallbacks2.TRIM_MEMORY_* (valores do SDK).
        const val TRIM_RUNNING_LOW = 10
        const val TRIM_RUNNING_CRITICAL = 15
        const val TRIM_UI_HIDDEN = 20
        const val TRIM_COMPLETE = 80
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

    // Por BYTES (Fase 8B §12): 400 miniaturas de 96 px de altura em 16:9 já
    // passavam de 25 MB. Teto: 1/16 do heap do app, entre 8 e 24 MB.
    private val cache = object : android.util.LruCache<Key, Bitmap>(MAX_BYTES) {
        override fun sizeOf(key: Key, value: Bitmap) = value.allocationByteCount
    }
    private val buffer: ByteBuffer = directBuffer(512 * 512 * 4)
    private val width = IntArray(1)

    /** Quem chama arredonda o frame para a grade de 250 ms do motor (a timeline pede um frame por balde). */
    fun get(layer: Long, timelineFrame: Int, heightPx: Int): Bitmap? {
        val key = Key(layer, timelineFrame, heightPx)
        cache.get(key)?.let { return it }
        buffer.clear()
        val bytes = engine.queryThumbnail(layer, timelineFrame, heightPx, buffer, width)
        if (bytes <= 0 || width[0] <= 0) return null
        buffer.rewind()
        val bmp = Bitmap.createBitmap(width[0], heightPx, Bitmap.Config.ARGB_8888)
        bmp.copyPixelsFromBuffer(buffer)
        cache.put(key, bmp)
        return bmp
    }

    fun clear() = cache.evictAll()

    /** Pressão de memória: fica só a fração mais recente (o que está na tela). */
    fun trimTo(fraction: Float) = cache.trimToSize((cache.size() * fraction).toInt())

    fun bytes(): Long = cache.size().toLong()

    private companion object {
        val MAX_BYTES: Int = (Runtime.getRuntime().maxMemory() / 16).coerceIn(8L shl 20, 24L shl 20).toInt()
    }
}

/** Comprimento das setas do gizmo 3D, em unidades do mundo (px da composição no plano Z = 0). */
const val GIZMO_LENGTH = 320f

/** Receita do texto 3D: texto, profundidade (em alturas de letra), alinhamento e cor sRGB. */
data class Text3DInfo(val content: String, val depth: Float, val alignment: Int, val color: FloatArray)
