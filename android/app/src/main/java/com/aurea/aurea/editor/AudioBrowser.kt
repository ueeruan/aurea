package com.aurea.aurea.editor

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyHorizontalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.itemsIndexed as listItemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

// =============================================================================
// A biblioteca de áudio do app (D2: sem READ_MEDIA_AUDIO)
// =============================================================================

/** Um áudio da biblioteca: o arquivo (caminho no app ou URI com permissão) e os metadados. */
internal data class AudioEntry(
    val source: String,
    val title: String,
    val album: String,
    val artist: String,
    val genre: String,
    val durationMs: Long,
    val lastUsed: Long,
)

/**
 * Omega adicionar (área 08, P1-3/P1-4): a biblioteca = os áudios já usados no
 * app (URI com permissão guardada, ou a cópia que o app fez) + as gravações de
 * voz + as cópias de áudio importadas. Nada da biblioteca de música do
 * aparelho (sem `READ_MEDIA_AUDIO`, decisão D2). Os metadados (título, álbum,
 * artista, gênero, duração) vêm de `MediaMetadataRetriever` e ficam guardados
 * aqui para não reler o arquivo a cada abertura. A entrada cujo arquivo sumiu
 * sai da lista.
 */
internal object OmegaAudioLibrary {
    private const val KEY = "audio.library"
    private const val MAX = 400
    private val AUDIO_EXT = setOf("m4a", "mp3", "wav", "aac", "ogg", "oga", "opus", "flac", "amr", "3gp", "mid", "midi", "aiff", "aif", "caf", "wma")

    private fun read(context: Context): MutableList<AudioEntry> {
        val raw = omegaAddPrefs(context).getString(KEY, null) ?: return mutableListOf()
        return runCatching {
            val array = JSONArray(raw)
            MutableList(array.length()) { i ->
                val o = array.getJSONObject(i)
                AudioEntry(o.getString("s"), o.optString("t"), o.optString("al"), o.optString("ar"), o.optString("g"),
                    o.optLong("d"), o.optLong("u"))
            }
        }.getOrDefault(mutableListOf())
    }

    private fun write(context: Context, list: List<AudioEntry>) {
        val array = JSONArray()
        list.take(MAX).forEach { e ->
            array.put(JSONObject().put("s", e.source).put("t", e.title).put("al", e.album).put("ar", e.artist)
                .put("g", e.genre).put("d", e.durationMs).put("u", e.lastUsed))
        }
        omegaAddPrefs(context).edit().putString(KEY, array.toString()).apply()
    }

    private fun exists(context: Context, source: String): Boolean =
        if (source.startsWith("content:")) runCatching {
            val uri = Uri.parse(source)
            context.contentResolver.persistedUriPermissions.any { it.uri == uri && it.isReadPermission }
        }.getOrDefault(false)
        else File(source).isFile

    /** Os metadados de um arquivo; null = não é áudio legível. */
    fun probe(context: Context, source: String, fallbackTitle: String): AudioEntry? {
        val retriever = MediaMetadataRetriever()
        return try {
            if (source.startsWith("content:")) retriever.setDataSource(context, Uri.parse(source)) else retriever.setDataSource(source)
            if (retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO) == null &&
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION) == null) return null
            AudioEntry(
                source = source,
                title = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE)?.takeIf { it.isNotBlank() } ?: fallbackTitle,
                album = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM).orEmpty(),
                artist = (retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST)
                    ?: retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUMARTIST)).orEmpty(),
                genre = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_GENRE).orEmpty(),
                durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L,
                lastUsed = 0L,
            )
        } catch (_: Exception) {
            null
        } finally {
            runCatching { retriever.release() }
        }
    }

    /** A capa embutida (ou null). Fora da thread principal. */
    fun cover(context: Context, source: String): Bitmap? {
        val retriever = MediaMetadataRetriever()
        return try {
            if (source.startsWith("content:")) retriever.setDataSource(context, Uri.parse(source)) else retriever.setDataSource(source)
            retriever.embeddedPicture?.let { bytes ->
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = 4 })
            }
        } catch (_: Exception) {
            null
        } finally {
            runCatching { retriever.release() }
        }
    }

    /**
     * A biblioteca inteira, os usados por último primeiro. Lê o disco: chamar
     * fora da thread principal.
     */
    fun load(context: Context): List<AudioEntry> {
        val stored = read(context)
        val kept = stored.filter { exists(context, it.source) }.toMutableList()
        var changed = kept.size != stored.size
        val known = kept.mapTo(HashSet()) { it.source }
        val projects = File(context.filesDir, "projetos")
        val scanned = listOf(File(projects, "voz"), File(projects, "import-sources"))
            .flatMap { dir -> dir.listFiles()?.filter { it.isFile && it.extension.lowercase() in AUDIO_EXT }.orEmpty() }
        for (file in scanned) {
            if (file.absolutePath in known) continue
            probe(context, file.absolutePath, file.nameWithoutExtension)?.let { kept += it; changed = true }
        }
        if (changed) write(context, kept)
        return kept.sortedWith(compareByDescending<AudioEntry> { it.lastUsed }.thenBy { it.title.lowercase() })
    }

    /** Usado agora: vai para o topo dos recentes (entra na biblioteca se for novo). Fora da thread principal. */
    fun markUsed(context: Context, source: String, title: String) {
        val list = read(context)
        val index = list.indexOfFirst { it.source == source }
        val now = System.currentTimeMillis()
        if (index >= 0) list[index] = list[index].copy(lastUsed = now)
        else probe(context, source, title)?.let { list += it.copy(lastUsed = now) } ?: return
        write(context, list)
    }

    /** Os N usados por último (a faixa mostra 3 dos 5 guardados). */
    fun recents(entries: List<AudioEntry>, count: Int): List<AudioEntry> =
        entries.filter { it.lastUsed > 0 }.sortedByDescending { it.lastUsed }.take(5).take(count)
}

/** `m:ss` de uma duração em ms. */
private fun audioDuration(ms: Long): String = mediaDuration(ms)

// =============================================================================
// Pré-escuta: um tocador por vez (P1-7: trocar de botão, fechar ou adicionar para)
// =============================================================================

internal object OmegaAudioPreview {
    /** O que está tocando (a fonte), ou null. */
    var playing by mutableStateOf<String?>(null)
        private set
    private var player: MediaPlayer? = null

    fun toggle(context: Context, source: String, onUnsupported: () -> Unit) {
        if (playing == source) { stop(); return }
        stop()
        val mp = MediaPlayer()
        player = mp
        playing = source
        try {
            if (source.startsWith("content:")) mp.setDataSource(context, Uri.parse(source)) else mp.setDataSource(source)
            mp.setOnPreparedListener { if (player === it) it.start() }
            mp.setOnCompletionListener { if (player === it) stop() }
            mp.setOnErrorListener { p, _, _ -> if (player === p) { stop(); onUnsupported() }; true }
            mp.prepareAsync()
        } catch (_: Exception) {
            stop()
            onUnsupported()
        }
    }

    fun stop() {
        val mp = player
        player = null
        playing = null
        if (mp != null) runCatching { mp.reset(); mp.release() }
    }

    /** Fração tocada (0..1) para o anel de progresso. */
    fun progress(): Float = runCatching {
        val mp = player ?: return 0f
        if (mp.duration > 0) (mp.currentPosition.toFloat() / mp.duration).coerceIn(0f, 1f) else 0f
    }.getOrDefault(0f)
}

/** O aviso de formato não suportado (diálogo claro). */
@Composable
private fun UnsupportedAudioDialog(onDismiss: () -> Unit) {
    AureaDialog(
        title = null,
        message = stringResource(R.string.om_audio_unsupported),
        actions = listOf(DialogAction(stringResource(R.string.common_ok), DialogRole.Positive)),
        onDismiss = onDismiss,
        tag = "audio.unsupported",
    )
}

// =============================================================================
// A faixa rápida de áudio (P0-4)
// =============================================================================

/**
 * Grade HORIZONTAL de 2 fileiras: [Ver tudo] (o navegador de áudio), as
 * fichas do AUREA (arquivos, gravar voz, som de um vídeo, marca no cabeçote),
 * até 3 recentes (relógio) e o resto da biblioteca. Tocar no cartão liga e
 * desliga a pré-escuta; o "+" da base adiciona e fecha.
 */
@Composable
internal fun AudioStrip(store: EditorStore, ui: EditorUi, close: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var entries by remember { mutableStateOf<List<AudioEntry>?>(null) }
    var unsupported by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { entries = withContext(Dispatchers.IO) { OmegaAudioLibrary.load(context) } }
    // Fechar o painel ou trocar de botão para a pré-escuta.
    DisposableEffect(Unit) { onDispose { OmegaAudioPreview.stop() } }
    val files = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            store.importAudio(uri)   // guarda a permissão antes
            scope.launch(Dispatchers.IO) { OmegaAudioLibrary.markUsed(context, uri.toString(), uri.lastPathSegment.orEmpty()) }
            close()
        }
    }
    val videos = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) {
            store.importAudio(uri)
            close()
        }
    }
    val list = entries.orEmpty()
    val recents = remember(list) { OmegaAudioLibrary.recents(list, 3) }
    val rest = remember(list, recents) { list.filter { it !in recents } }
    fun add(entry: AudioEntry) {
        OmegaAudioPreview.stop()
        store.importAudioSource(entry.source, entry.title)
        scope.launch(Dispatchers.IO) { OmegaAudioLibrary.markUsed(context, entry.source, entry.title) }
        close()
    }
    BoxWithConstraints(Modifier.fillMaxSize().testTag("add.audio.strip")) {
        val cell = maxHeight / 2
        LazyHorizontalGrid(rows = GridCells.Fixed(2), modifier = Modifier.fillMaxSize()) {
            item(key = "all") {
                StripActionCell(cell, stringResource(R.string.om_add_view_all), CupertinoGlyph.MusicNote2, "add.audio.viewAll") {
                    OmegaAudioPreview.stop(); ui.audioBrowser = true
                }
            }
            item(key = "files") {
                StripActionCell(cell, stringResource(R.string.editor_musica_ou_som), CupertinoGlyph.Folder, "add.audio.files") {
                    // SAF (sem READ_MEDIA_AUDIO desde a 0.0.3): provedores que marcam o
                    // .mp3/.m4a/.opus baixado como genérico ou como vídeo MP4 não ficam
                    // cinza. O motor diz "áudio ilegível" se não for som.
                    files.launch(arrayOf("audio/*", "application/ogg", "video/mp4", "application/octet-stream"))
                }
            }
            item(key = "voice") {
                // Narração: o painel de gravação abre por cima da timeline (o palco fica à vista).
                StripActionCell(cell, stringResource(R.string.voice_title), CupertinoGlyph.MicFill, "add.voice") {
                    close()
                    if (store.playing) store.pause()
                    ui.voice = true
                }
            }
            item(key = "videoSound") {
                StripActionCell(cell, stringResource(R.string.sh_add_video_sound), CupertinoGlyph.Film, "add.audio.videoSound") {
                    videos.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
                }
            }
            item(key = "marker") {
                StripActionCell(cell, stringResource(R.string.sh_add_marker_at_playhead), CupertinoGlyph.Bookmark, "add.audio.marker") {
                    close()
                    store.toggleMarker()
                }
            }
            itemsIndexed(recents, key = { _, e -> "recent:" + e.source }) { index, entry ->
                AudioCard(cell, entry, recent = true, tag = "add.audio.item.$index",
                    onPreview = { OmegaAudioPreview.toggle(context, entry.source) { unsupported = true } }, onAdd = { add(entry) })
            }
            if (recents.isNotEmpty()) item(key = "gap", span = { GridItemSpan(maxLineSpan) }) { Spacer(Modifier.width(7.5.dp).fillMaxHeight()) }
            itemsIndexed(rest, key = { _, e -> e.source }) { index, entry ->
                AudioCard(cell, entry, recent = false, tag = "add.audio.item.${index + recents.size}",
                    onPreview = { OmegaAudioPreview.toggle(context, entry.source) { unsupported = true } }, onAdd = { add(entry) })
            }
        }
    }
    if (unsupported) UnsupportedAudioDialog { unsupported = false }
}

/** Cartão de 75 dp: capa ou nota, nome e duração no alto, barra "+" de 26 dp na base. */
@Composable
private fun AudioCard(cell: Dp, entry: AudioEntry, recent: Boolean, tag: String, onPreview: () -> Unit, onAdd: () -> Unit) {
    val context = LocalContext.current
    val playing = OmegaAudioPreview.playing == entry.source
    val previewLabel = stringResource(if (playing) R.string.om_audio_stop_named else R.string.om_audio_preview_named, entry.title)
    val addLabel = stringResource(R.string.om_audio_add_named, entry.title)
    val cover by produceState<Bitmap?>(null, entry.source) {
        value = withContext(Dispatchers.IO) { OmegaAudioLibrary.cover(context, entry.source) }
    }
    Box(Modifier.size(cell), contentAlignment = Alignment.Center) {
        Box(
            Modifier
                .padding(horizontal = 2.5.dp)
                .size(75.dp)
                .clip(RoundedCornerShape(2.dp))
                .background(AureaColors.AddTile)
                .then(if (playing) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(2.dp)) else Modifier),
        ) {
            Box(
                Modifier.fillMaxSize().semantics { contentDescription = previewLabel }.testTag(tag).tocavel(shrink = 0.97f, onClick = onPreview),
            ) {
                val image = cover
                if (image != null) Image(image.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                else CupertinoIcon(CupertinoGlyph.MusicNote, 24.dp, AureaColors.Muted, Modifier.align(Alignment.Center).padding(bottom = 18.dp))
                Box(Modifier.fillMaxSize().background(AureaColors.AddScrimSoft))
                Column(Modifier.align(Alignment.TopStart).padding(start = 5.dp, top = 4.dp, end = 18.dp)) {
                    Text(entry.title, maxLines = 1, overflow = TextOverflow.Ellipsis,
                        style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = AureaColors.OnImage)))
                    Text(audioDuration(entry.durationMs), style = AureaType.Base.merge(TextStyle(fontSize = 9.sp, color = AureaColors.OnImage70)))
                }
                Row(Modifier.align(Alignment.TopEnd).padding(3.dp)) {
                    if (playing) CupertinoIcon(CupertinoGlyph.PlayFill, 15.dp, AureaColors.Accent)
                    if (recent) CupertinoIcon(CupertinoGlyph.Clock, 15.dp, AureaColors.OnImage)
                }
            }
            Box(
                Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .height(26.dp)
                    .background(AureaColors.AddScrimBar)
                    .semantics { contentDescription = addLabel }
                    .testTag("$tag.add")
                    .tocavel(onClick = onAdd)
                    .padding(5.dp),
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Plus, 15.dp, AureaColors.OnImage) }
        }
    }
}

// =============================================================================
// O navegador de áudio em tela cheia (P1-3)
// =============================================================================

private enum class AudioCategory(val key: String, val tag: String) {
    Songs("songs", "audio.category.songs"),
    Albums("albums", "audio.category.albums"),
    Artists("artists", "audio.category.artists"),
    Genres("genres", "audio.category.genres"),
}

/**
 * Categorias (Músicas · Álbuns · Artistas · Gêneros) em linhas de 70 dp e,
 * no alto, "Arquivos do aparelho…" (o seletor de arquivos de hoje). Álbuns,
 * artistas e gêneros abrem a lista dos grupos e depois as faixas; cada faixa
 * tem ▶ com anel de progresso e "+" (adiciona e fecha).
 */
@Composable
internal fun AudioBrowser(store: EditorStore, onAdded: () -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var entries by remember { mutableStateOf<List<AudioEntry>?>(null) }
    var category by rememberSaveable { mutableStateOf<String?>(null) }
    var group by rememberSaveable { mutableStateOf<String?>(null) }
    var unsupported by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { entries = withContext(Dispatchers.IO) { OmegaAudioLibrary.load(context) } }
    DisposableEffect(Unit) { onDispose { OmegaAudioPreview.stop() } }
    val unknown = stringResource(R.string.om_audio_unknown)
    val files = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            store.importAudio(uri)
            scope.launch(Dispatchers.IO) { OmegaAudioLibrary.markUsed(context, uri.toString(), uri.lastPathSegment.orEmpty()) }
            onAdded(); onDismiss()
        }
    }
    fun add(entry: AudioEntry) {
        OmegaAudioPreview.stop()
        store.importAudioSource(entry.source, entry.title)
        scope.launch(Dispatchers.IO) { OmegaAudioLibrary.markUsed(context, entry.source, entry.title) }
        onAdded(); onDismiss()
    }
    val back = {
        OmegaAudioPreview.stop()
        if (group != null) group = null else if (category != null) category = null else onDismiss()
    }
    val cat = AudioCategory.entries.firstOrNull { it.key == category }
    fun keyOf(e: AudioEntry, c: AudioCategory): String = when (c) {
        AudioCategory.Albums -> e.album.ifBlank { unknown }
        AudioCategory.Artists -> e.artist.ifBlank { unknown }
        AudioCategory.Genres -> e.genre.ifBlank { unknown }
        AudioCategory.Songs -> ""
    }
    Dialog(onDismissRequest = back, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        BackHandler(enabled = category != null) { back() }
        Column(Modifier.fillMaxSize().background(AureaColors.AddPanel).statusBarsPadding().navigationBarsPadding().testTag("audio.browser")) {
            val title = group ?: cat?.let { stringResource(it.labelRes()) } ?: stringResource(R.string.om_audio_title)
            if (category == null) BrowserTopBar(title, CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), "audio.close", onDismiss)
            else BrowserTopBar(title, CupertinoGlyph.ChevronLeft, stringResource(R.string.om_add_back), "audio.back", back)
            val all = entries
            when {
                all == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(Modifier.size(28.dp), color = AureaColors.Accent)
                }
                cat == null -> LazyColumn(Modifier.fillMaxSize()) {
                    item {
                        AudioListRow(stringResource(R.string.om_audio_device_files), null, CupertinoGlyph.Folder, "audio.files", chevron = false) {
                            OmegaAudioPreview.stop()
                            files.launch(arrayOf("audio/*", "application/ogg", "video/mp4", "application/octet-stream"))
                        }
                    }
                    AudioCategory.entries.forEach { c ->
                        item(key = c.key) {
                            val count = if (c == AudioCategory.Songs) all.size else all.map { keyOf(it, c) }.distinct().size
                            AudioListRow(stringResource(c.labelRes()), pluralStringResource(R.plurals.om_audio_count, count, count),
                                c.glyph(), c.tag) { category = c.key }
                        }
                    }
                    if (all.isEmpty()) item {
                        Text(stringResource(R.string.om_audio_empty), Modifier.fillMaxWidth().padding(24.dp), textAlign = TextAlign.Center,
                            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                    }
                }
                cat == AudioCategory.Songs || group != null -> {
                    val tracks = if (group == null) all else all.filter { keyOf(it, cat) == group }
                    TrackList(tracks, onPreview = { e -> OmegaAudioPreview.toggle(context, e.source) { unsupported = true } }, onAdd = { add(it) })
                }
                else -> {
                    val groups = all.groupBy { keyOf(it, cat) }.toList().sortedBy { it.first.lowercase() }
                    LazyColumn(Modifier.fillMaxSize()) {
                        listItemsIndexed(groups, key = { _, g -> g.first }) { index, (name, list) ->
                            AudioListRow(name, pluralStringResource(R.plurals.om_audio_count, list.size, list.size), cat.glyph(),
                                "audio.group.$index") { group = name }
                        }
                    }
                }
            }
        }
        if (unsupported) UnsupportedAudioDialog { unsupported = false }
    }
}

private fun AudioCategory.labelRes(): Int = when (this) {
    AudioCategory.Songs -> R.string.om_audio_songs
    AudioCategory.Albums -> R.string.om_audio_albums
    AudioCategory.Artists -> R.string.om_audio_artists
    AudioCategory.Genres -> R.string.om_audio_genres
}

private fun AudioCategory.glyph(): Char = when (this) {
    AudioCategory.Songs -> CupertinoGlyph.MusicNote
    AudioCategory.Albums -> CupertinoGlyph.RectangleStack
    AudioCategory.Artists -> CupertinoGlyph.PersonCropCircle
    AudioCategory.Genres -> CupertinoGlyph.MusicNote2
}

/** Linha de 70 dp: ícone quadrado, título 15 sp + contagem 12 sp, seta ›. */
@Composable
private fun AudioListRow(title: String, count: String?, glyph: Char, tag: String, chevron: Boolean = true, onClick: () -> Unit) {
    Column {
        Row(
            Modifier
                .fillMaxWidth()
                .height(70.dp)
                .background(AureaColors.AddPanel)
                .semantics { contentDescription = if (count != null) "$title, $count" else title }
                .testTag(tag)
                .tocavel(shrink = 1f, onClick = onClick)
                .padding(horizontal = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(Modifier.size(56.dp).clip(RoundedCornerShape(2.dp)).background(AureaColors.AddPanelDeep), contentAlignment = Alignment.Center) {
                CupertinoIcon(glyph, 24.dp, AureaColors.Text)
            }
            Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
                if (count != null) Text(count, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text.copy(alpha = 0.3f))))
            }
            if (chevron) CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Muted)
        }
        Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.AddPanelDeep))
    }
}

/** A lista de faixas: capa com ▶ e anel de progresso, título, álbum e duração, "+". */
@Composable
private fun TrackList(tracks: List<AudioEntry>, onPreview: (AudioEntry) -> Unit, onAdd: (AudioEntry) -> Unit) {
    if (tracks.isEmpty()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.om_audio_empty), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        }
        return
    }
    LazyColumn(Modifier.fillMaxSize()) {
        listItemsIndexed(tracks, key = { _, e -> e.source }) { index, entry -> TrackRow(index, entry, onPreview, onAdd) }
    }
}

@Composable
private fun TrackRow(index: Int, entry: AudioEntry, onPreview: (AudioEntry) -> Unit, onAdd: (AudioEntry) -> Unit) {
    val context = LocalContext.current
    val playing = OmegaAudioPreview.playing == entry.source
    var progress by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(playing) {
        progress = 0f
        while (playing) { progress = OmegaAudioPreview.progress(); delay(15) }
    }
    val cover by produceState<Bitmap?>(null, entry.source) {
        value = withContext(Dispatchers.IO) { OmegaAudioLibrary.cover(context, entry.source) }
    }
    val previewLabel = stringResource(if (playing) R.string.om_audio_stop_named else R.string.om_audio_preview_named, entry.title)
    val addLabel = stringResource(R.string.om_audio_add_named, entry.title)
    val detail = listOf(entry.album, audioDuration(entry.durationMs)).filter { it.isNotBlank() }.joinToString(" · ")
    val accent = AureaColors.Accent
    Column(Modifier.testTag("audio.row.$index")) {
        Row(Modifier.fillMaxWidth().height(70.dp).padding(start = 7.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(56.dp)
                    .clip(RoundedCornerShape(2.dp))
                    .background(AureaColors.AddPanelDeep)
                    .semantics { contentDescription = previewLabel }
                    .testTag("audio.row.$index.play")
                    .tocavel { onPreview(entry) },
                contentAlignment = Alignment.Center,
            ) {
                val image = cover
                if (image != null) Image(image.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                Box(Modifier.fillMaxSize().background(AureaColors.AddScrimBar))
                CupertinoIcon(if (playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill, 18.dp, AureaColors.OnImage)
                if (playing) {
                    Canvas(Modifier.size(40.dp)) {
                        val w = 2.dp.toPx()
                        drawArc(accent, -90f, 360f * progress, false, Offset(w / 2, w / 2), Size(size.width - w, size.height - w),
                            style = Stroke(w, cap = StrokeCap.Round))
                    }
                }
            }
            Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(entry.title, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
                Text(detail, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text.copy(alpha = 0.3f))))
            }
            Box(
                Modifier.size(48.dp).semantics { contentDescription = addLabel }.testTag("audio.row.$index.add").tocavel { onAdd(entry) },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Plus, 20.dp, AureaColors.Text) }
        }
        Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.AddPanelDeep))
    }
}
