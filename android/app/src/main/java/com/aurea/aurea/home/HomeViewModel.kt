package com.aurea.aurea.home

import android.app.Application
import android.content.Context
import android.content.res.Resources
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.OpenableColumns
import android.util.LruCache
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.content.edit
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.state.Screen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.math.max

/**
 * Estado de APRESENTAÇÃO da Home — o que o motor não conhece e que precisa
 * sobreviver à ida ao editor (a Home sai da composição enquanto se edita):
 * aba atual, rolagem de cada aba, busca, ordem, seleção, padrões da folha
 * "Novo projeto", favoritos de efeito e o cache de miniaturas.
 *
 * Nada de projeto mora aqui: a lista, abrir, criar, duplicar, apagar e
 * renomear passam pelo [EditorStore].
 */
class HomeViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // --- Abas ------------------------------------------------------------------
    var tab by mutableIntStateOf(HOME_TAB)
        private set

    fun selectTab(i: Int) {
        tab = i
    }

    private val scrollIndex = IntArray(TAB_COUNT)
    private val scrollOffset = IntArray(TAB_COUNT)

    internal fun scrollIndex(tab: Int) = scrollIndex[tab]
    internal fun scrollOffset(tab: Int) = scrollOffset[tab]

    internal fun saveScroll(tab: Int, index: Int, offset: Int) {
        scrollIndex[tab] = index
        scrollOffset[tab] = offset
    }

    // --- Lista de projetos -------------------------------------------------------
    internal var sort by mutableStateOf(ProjectSort.fromIndex(prefs.getInt(KEY_SORT, 0)))
        private set

    internal fun changeSort(s: ProjectSort) {
        sort = s
        prefs.edit { putInt(KEY_SORT, s.ordinal) }
    }

    var query by mutableStateOf("")
    var searching by mutableStateOf(false)

    /** Projetos marcados (caminhos). Não vazio = modo de seleção. */
    var selection by mutableStateOf<Set<String>>(emptySet())
        private set

    /** Marca/desmarca; desmarcar o último sai do modo. */
    fun toggle(path: String) {
        selection = if (path in selection) selection - path else selection + path
    }

    fun selectAll(paths: Collection<String>) {
        selection = paths.toSet()
    }

    fun clearSelection() {
        if (selection.isNotEmpty()) selection = emptySet()
    }

    /** Some com marcas de projetos que deixaram de existir (apagados por fora). */
    internal fun pruneSelection(alive: List<ProjectEntry>) {
        if (selection.isEmpty()) return
        val paths = alive.mapTo(HashSet()) { it.path }
        if (!paths.containsAll(selection)) selection = selection.filterTo(HashSet()) { it in paths }
    }

    // --- Padrões de novos projetos (Ajustes) --------------------------------------
    internal var defaultAspectKey by mutableStateOf(prefs.getString(KEY_ASPECT, null) ?: "16:9")
        private set
    internal var defaultResolution by mutableIntStateOf(prefs.getInt(KEY_RESOLUTION, 1080))
        private set
    internal var defaultFps by mutableIntStateOf(prefs.getInt(KEY_FPS, 30))
        private set

    internal fun changeDefaultAspect(key: String) {
        defaultAspectKey = key
        prefs.edit { putString(KEY_ASPECT, key) }
    }

    internal fun changeDefaultResolution(v: Int) {
        defaultResolution = v
        prefs.edit { putInt(KEY_RESOLUTION, v) }
    }

    internal fun changeDefaultFps(v: Int) {
        defaultFps = v
        prefs.edit { putInt(KEY_FPS, v) }
    }

    // --- Sobre: ferramentas de desenvolvedor (sete toques na versão) -------------
    var devTools by mutableStateOf(prefs.getBoolean(KEY_DEV, false))
        private set

    fun toggleDevTools(): Boolean {
        devTools = !devTools
        prefs.edit { putBoolean(KEY_DEV, devTools) }
        return devTools
    }

    // --- Miniaturas ------------------------------------------------------------------
    internal val thumbnails = HomeThumbnails(app.resources)

    // --- Ações que esperam o motor -------------------------------------------------
    /**
     * Roda [block] quando o motor estiver pronto. Um toque logo na abertura
     * (motor ainda subindo) não pode virar "Não foi possível criar o
     * projeto". Se o motor não sobe, o store já mostrou o erro.
     */
    internal fun afterEngine(store: EditorStore, block: () -> Unit) {
        if (store.engineReady) {
            block()
            return
        }
        viewModelScope.launch { if (awaitEngine(store)) block() }
    }

    private suspend fun awaitEngine(store: EditorStore): Boolean =
        store.engineReady || withTimeoutOrNull(ENGINE_WAIT_MS) { snapshotFlow { store.engineReady }.first { it } } != null

    /**
     * Atalho "Mídia": como na A.01, o projeto nasce com a PROPORÇÃO da mídia
     * escolhida (fps 30, nome = arquivo sem extensão) e a mídia entra nele.
     *
     * A importação só pode ser pedida com o projeto já aberto — então este
     * fluxo mora aqui (sobrevive à Home saindo da tela) e espera o store
     * trocar para o editor antes de chamar `importVideo`/`importImage`.
     */
    internal fun createFromMedia(store: EditorStore, uri: Uri) {
        viewModelScope.launch {
            val media = withContext(Dispatchers.IO) { probeMedia(uri) }
            if (media == null) {
                store.showToast("Não consegui importar essa mídia.")
                return@launch
            }
            if (!awaitEngine(store)) return@launch
            val ratio = if (media.width > 0 && media.height > 0) media.width.toFloat() / media.height else 9f / 16f
            val frame = frameFor(ratio, 1080)
            // Encoders de vídeo pedem medidas pares.
            store.newProject(frame.width and 1.inv(), frame.height and 1.inv(), 30f, media.name)
            val opened = withTimeoutOrNull(ENGINE_WAIT_MS) { snapshotFlow { store.screen }.first { it == Screen.Editor } }
            if (opened == null) return@launch   // a falha já foi avisada pelo store
            if (media.video) store.importVideo(uri) else store.importImage(uri)
        }
    }

    private class Media(val name: String, val video: Boolean, val width: Int, val height: Int)

    private fun probeMedia(uri: Uri): Media? = try {
        val app = getApplication<Application>()
        val cr = app.contentResolver
        val video = cr.getType(uri)?.startsWith("video/") == true
        val display = cr.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
        val name = display?.substringBeforeLast('.')?.takeIf { it.isNotBlank() } ?: "Mídia Importada"
        if (video) {
            val r = MediaMetadataRetriever()
            try {
                r.setDataSource(app, uri)
                val w = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
                val h = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
                val rot = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
                if (rot == 90 || rot == 270) Media(name, true, h, w) else Media(name, true, w, h)
            } finally {
                r.release()
            }
        } else {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
                null
            } else {
                val orientation = cr.openInputStream(uri)?.use {
                    ExifInterface(it).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
                } ?: ExifInterface.ORIENTATION_NORMAL
                val turned = orientation == ExifInterface.ORIENTATION_ROTATE_90 ||
                    orientation == ExifInterface.ORIENTATION_ROTATE_270 ||
                    orientation == ExifInterface.ORIENTATION_TRANSPOSE ||
                    orientation == ExifInterface.ORIENTATION_TRANSVERSE
                if (turned) Media(name, false, bounds.outHeight, bounds.outWidth)
                else Media(name, false, bounds.outWidth, bounds.outHeight)
            }
        }
    } catch (_: Exception) {
        null
    }

    companion object {
        /** Início · Projetos · Ajustes. */
        const val TAB_COUNT = 3
        const val HOME_TAB = 0
        const val PROJECTS_TAB = 1
        const val SETTINGS_TAB = 2

        private const val PREFS = "aurea.home"
        private const val KEY_SORT = "projetos.ordem"
        private const val KEY_ASPECT = "settings.defaultAspect"
        private const val KEY_RESOLUTION = "settings.defaultResolution"
        private const val KEY_FPS = "settings.defaultFps"
        private const val KEY_DEV = "dev.escondido"
        private const val ENGINE_WAIT_MS = 15_000L
    }
}

/**
 * Miniaturas da Home (projetos e modelos): decodificadas UMA vez, fora da
 * thread principal, no tamanho de exibição, e guardadas num LruCache por bytes.
 *
 * A chave leva o `modifiedMs` do projeto: salvar reescreve a miniatura no
 * MESMO caminho, e sem o carimbo o cache mostraria a imagem velha.
 *
 * A chave leva TAMBÉM a largura pedida (Fase 7.3 §74): o hero decodifica em
 * 1280 px e a grade em 512 px, e cada um tem a sua entrada. Antes os dois
 * dividiam uma imagem de 1280 px só — numa grade de 40 projetos isso eram
 * 40 bitmaps de 1280 px na RAM sem nenhum motivo.
 */
internal class HomeThumbnails(private val resources: Resources) {
    private val cache = object : LruCache<String, ImageBitmap>(MAX_BYTES) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    fun peek(key: String): ImageBitmap? = cache.get(key)

    suspend fun load(key: String, decode: () -> Bitmap?): ImageBitmap? {
        cache.get(key)?.let { return it }
        val bmp = withContext(Dispatchers.IO) { decode()?.asImageBitmap() } ?: return null
        cache.put(key, bmp)
        return bmp
    }

    fun decodeFile(path: String, widthPx: Int): Bitmap? = try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(path, bounds)
        BitmapFactory.decodeFile(path, BitmapFactory.Options().apply { inSampleSize = sampleFor(bounds.outWidth, widthPx) })
    } catch (_: Exception) {
        null
    }

    /** Maior potência de 2 que ainda deixa a imagem com ≥ a largura pedida. */
    private fun sampleFor(sourceWidth: Int, targetWidth: Int): Int {
        var sample = 1
        while (sourceWidth / (sample * 2) >= max(1, targetWidth)) sample *= 2
        return sample
    }

    private companion object {
        const val MAX_BYTES = 24 * 1024 * 1024
    }
}

/** Largura de decodificação das miniaturas de projeto, por onde ela aparece. */
internal const val HERO_DECODE_PX = 1080
internal const val CARD_DECODE_PX = 512
/** Largura de decodificação das imagens dos modelos (o `cacheWidth` da A.01). */
internal const val MODEL_DECODE_PX = 464

/**
 * Miniatura de um projeto no tamanho em que ela vai aparecer.
 *
 * O hero e a grade NÃO dividem mais a mesma imagem: o hero lê em
 * [HERO_DECODE_PX] e a grade em [CARD_DECODE_PX], com entradas separadas no
 * cache. O projeto que sai do hero para a grade decodifica de novo — mais
 * barato que carregar 1080 px por cartão de grade.
 */
@Composable
internal fun rememberProjectThumbnail(
    thumbs: HomeThumbnails,
    entry: ProjectEntry,
    widthPx: Int = CARD_DECODE_PX,
): ImageBitmap? {
    val path = entry.thumbnailPath
    val key = remember(path, entry.modifiedMs, widthPx) {
        path?.let { "$it|${entry.modifiedMs}|$widthPx" }
    }
    val state = produceState(key?.let { thumbs.peek(it) }, key) {
        if (key == null || path == null) {
            value = null
            return@produceState
        }
        value = thumbs.peek(key) ?: thumbs.load(key) { thumbs.decodeFile(path, widthPx) }
    }
    return state.value
}
