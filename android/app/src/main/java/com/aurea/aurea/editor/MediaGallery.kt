package com.aurea.aurea.editor

import android.Manifest
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.database.ContentObserver
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyHorizontalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
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
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

// =============================================================================
// Mídia do aparelho (MediaStore): itens, álbuns, permissões
// =============================================================================

internal data class GalleryItem(val uri: Uri, val name: String, val durationMs: Long)

/** Fotos OU vídeos, do mais novo para o mais antigo (fica para os testes e quem já chamava). */
internal fun galleryItems(context: Context, video: Boolean): List<GalleryItem> {
    val collection = if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI
    val columns = mutableListOf(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME)
    if (video) columns.add(MediaStore.Video.Media.DURATION)
    return buildList {
        context.contentResolver.query(collection, columns.toTypedArray(), null, null,
            "${MediaStore.MediaColumns.DATE_ADDED} DESC, ${MediaStore.MediaColumns._ID} DESC")?.use { cursor ->
            while (cursor.moveToNext()) add(GalleryItem(ContentUris.withAppendedId(collection, cursor.getLong(0)),
                cursor.getString(1).orEmpty(), if (video) cursor.getLong(2) else 0))
        }
    }
}

/** Omega adicionar (área 08): uma foto ou vídeo do aparelho, com o álbum. */
internal data class MediaItem(
    val uri: Uri,
    val name: String,
    val video: Boolean,
    val durationMs: Long,
    val dateAdded: Long,
    val bucketId: Long,
    val bucketName: String,
    val width: Int,
    val height: Int,
    val sizeBytes: Long,
)

/** Um álbum: id 0 = "Tudo". */
internal data class MediaAlbum(val id: Long, val name: String, val count: Int, val cover: MediaItem?, val video: Boolean)

private const val MEDIA_LIMIT = 6000

/** Fotos e vídeos JUNTOS (a faixa da referência), do mais novo para o mais antigo. */
internal fun mediaItems(context: Context): List<MediaItem> {
    fun query(video: Boolean, out: MutableList<MediaItem>) {
        val collection = if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        val bucketId = if (video) MediaStore.Video.VideoColumns.BUCKET_ID else MediaStore.Images.ImageColumns.BUCKET_ID
        val bucketName = if (video) MediaStore.Video.VideoColumns.BUCKET_DISPLAY_NAME else MediaStore.Images.ImageColumns.BUCKET_DISPLAY_NAME
        val columns = arrayOf(
            MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME, MediaStore.MediaColumns.DATE_ADDED,
            bucketId, bucketName, MediaStore.MediaColumns.WIDTH, MediaStore.MediaColumns.HEIGHT, MediaStore.MediaColumns.SIZE,
            if (video) MediaStore.Video.Media.DURATION else MediaStore.MediaColumns.DATE_MODIFIED,
        )
        runCatching {
            context.contentResolver.query(collection, columns, null, null,
                "${MediaStore.MediaColumns.DATE_ADDED} DESC, ${MediaStore.MediaColumns._ID} DESC")?.use { c ->
                var n = 0
                while (c.moveToNext() && n++ < MEDIA_LIMIT) {
                    out += MediaItem(
                        uri = ContentUris.withAppendedId(collection, c.getLong(0)),
                        name = c.getString(1).orEmpty(),
                        video = video,
                        durationMs = if (video) c.getLong(8) else 0L,
                        dateAdded = c.getLong(2),
                        bucketId = c.getLong(3),
                        bucketName = c.getString(4).orEmpty(),
                        width = c.getInt(5),
                        height = c.getInt(6),
                        sizeBytes = c.getLong(7),
                    )
                }
            }
        }
    }
    val out = ArrayList<MediaItem>()
    query(false, out)
    query(true, out)
    return out.sortedWith(compareByDescending<MediaItem> { it.dateAdded }.thenByDescending { ContentUris.parseId(it.uri) })
}

/** Os álbuns da lista (o "Tudo" primeiro), do maior para o menor. */
internal fun mediaAlbums(items: List<MediaItem>, allLabel: String): List<MediaAlbum> {
    val groups = items.groupBy { it.bucketId }
    val albums = groups.map { (id, list) -> MediaAlbum(id, list.first().bucketName.ifBlank { "—" }, list.size, list.first(), list.all { it.video }) }
        .sortedByDescending { it.count }
    return listOf(MediaAlbum(0L, allLabel, items.size, items.firstOrNull(), false)) + albums
}

private fun Context.galleryGranted(permission: String) = ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
internal fun Context.galleryFull(video: Boolean) = galleryGranted(if (Build.VERSION.SDK_INT >= 33)
    if (video) Manifest.permission.READ_MEDIA_VIDEO else Manifest.permission.READ_MEDIA_IMAGES
    else Manifest.permission.READ_EXTERNAL_STORAGE)
internal fun Context.galleryPartial() = Build.VERSION.SDK_INT >= 34 && galleryGranted(Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)
internal fun galleryPermissions(): Array<String> = when {
    Build.VERSION.SDK_INT >= 34 -> arrayOf(Manifest.permission.READ_MEDIA_IMAGES, Manifest.permission.READ_MEDIA_VIDEO, Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)
    Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.READ_MEDIA_IMAGES, Manifest.permission.READ_MEDIA_VIDEO)
    else -> arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
}

/**
 * Preferências de interface da mídia (não são do projeto): o álbum escolhido,
 * as mídias usadas (até 30; a faixa mostra as N primeiras quando "mídia
 * recente" > 0 — padrão 0, como a referência) e a última duração de imagem.
 */
internal object OmegaMediaPrefs {
    private const val RECENTS = "media.recents"
    const val MAX_RECENTS = 30

    fun bucket(context: Context): Pair<Long, String> =
        omegaAddPrefs(context).let { it.getLong("media.bucket", 0L) to it.getString("media.bucketName", "").orEmpty() }

    fun setBucket(context: Context, id: Long, name: String) {
        omegaAddPrefs(context).edit().putLong("media.bucket", id).putString("media.bucketName", name).apply()
    }

    /** Quantas mídias recentes a faixa mostra na frente (o ajuste fica em outra área; 0 = desligado). */
    fun recentCount(context: Context): Int = omegaAddPrefs(context).getInt("media.recentCount", 0).coerceIn(0, MAX_RECENTS)

    fun recents(context: Context): List<String> =
        omegaAddPrefs(context).getString(RECENTS, "").orEmpty().split('\n').filter { it.isNotEmpty() }

    fun markUsed(context: Context, uris: List<Uri>) {
        val keys = uris.map { it.toString() }
        val next = (keys + recents(context).filter { it !in keys }).take(MAX_RECENTS)
        omegaAddPrefs(context).edit().putString(RECENTS, next.joinToString("\n")).apply()
    }

    /** O arquivo sumiu do aparelho: a entrada sai. */
    fun prune(context: Context, existing: Set<String>) {
        val kept = recents(context).filter { it in existing }
        if (kept.size != recents(context).size) omegaAddPrefs(context).edit().putString(RECENTS, kept.joinToString("\n")).apply()
    }

    /** A última duração de imagem usada no navegador (quadros); 0 = nenhuma ainda. */
    fun imageDurationFrames(context: Context): Long = omegaAddPrefs(context).getLong("media.imageDuration", 0L)

    fun setImageDurationFrames(context: Context, frames: Long) {
        omegaAddPrefs(context).edit().putLong("media.imageDuration", frames).apply()
    }

    fun albumTipShown(context: Context): Boolean = omegaAddPrefs(context).getBoolean("media.albumTip", false)
    fun setAlbumTipShown(context: Context) { omegaAddPrefs(context).edit().putBoolean("media.albumTip", true).apply() }
}

/** `h:mm:ss` (ou `m:ss`) da duração de um vídeo. */
internal fun mediaDuration(ms: Long): String {
    val s = (ms / 1000).coerceAtLeast(0)
    return if (s >= 3600) String.format(Locale.ROOT, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
    else String.format(Locale.ROOT, "%d:%02d", s / 60, s % 60)
}

// =============================================================================
// Bugs 0.3.4 (seleção de mídia): só o último pedido vence; áudio pelos arquivos
// =============================================================================

/** Um valor carregado e a chave (a mídia) a que ele pertence. */
internal data class LoadedFor<T>(val key: Any?, val value: T?) {
    /** O valor só vale para a SUA chave: o de outra mídia nunca aparece. */
    fun valueFor(current: Any?): T? = if (key == current) value else null
}

/**
 * Carrega fora da thread principal um valor que pertence a UMA chave (a mídia
 * em foco). Trocou a chave: o pedido anterior é cancelado e o resultado
 * guardado leva a chave junto, então o de outra mídia nunca é mostrado —
 * antes o `produceState` mantinha a mídia ANTERIOR na prévia até a nova chegar
 * (e numa troca rápida parecia sempre "uma atrás").
 */
@Composable
internal fun <T : Any> rememberLoaded(key: Any?, load: () -> T?): T? {
    var loaded by remember { mutableStateOf(LoadedFor<T>(Unit, null)) }
    LaunchedEffect(key) {
        val value = withContext(Dispatchers.IO) { runCatching { load() }.getOrNull() }
        loaded = LoadedFor(key, value)
    }
    return loaded.valueFor(key)
}

/**
 * O que a pessoa escolheu em "Outros arquivos e nuvem" da faixa de mídia:
 * fotos, vídeos E áudio (o seletor de documentos do sistema não precisa de
 * `READ_MEDIA_AUDIO`, que a 0.3.3 decidiu não pedir). O áudio vira camada de
 * áudio; o resto segue como antes.
 */
internal object PickedMedia {
    enum class Kind { Image, Video, Audio }

    /** Tipos do seletor de documentos (SAF). */
    val PICKER_TYPES = arrayOf("image/*", "video/*", "audio/*", "application/ogg")

    /** Extensões de áudio: alguns provedores marcam M4A como `video/mp4` ou mandam `application/octet-stream`. */
    val AUDIO_EXTENSIONS = setOf("mp3", "m4a", "m4b", "aac", "wav", "wave", "aif", "aiff", "aifc", "caf", "flac", "ogg", "oga", "opus", "amr", "3ga", "wma", "mka")

    fun kind(mime: String?, displayName: String): Kind {
        val ext = displayName.substringAfterLast('.', "").lowercase(Locale.ROOT)
        if (ext in AUDIO_EXTENSIONS) return Kind.Audio
        val type = mime?.lowercase(Locale.ROOT).orEmpty()
        return when {
            type.startsWith("audio/") || type == "application/ogg" -> Kind.Audio
            type.startsWith("video/") -> Kind.Video
            else -> Kind.Image
        }
    }
}

/** Miniatura do MediaStore (256 px), carregada fora da thread principal. */
@Suppress("DEPRECATION")
@Composable
internal fun MediaThumb(uri: Uri, video: Boolean, modifier: Modifier = Modifier, sizePx: Int = 256) {
    val context = LocalContext.current
    // Bugs 0.3.4: a capa de um álbum que mudou não mostra a miniatura antiga.
    val bitmap = rememberLoaded(uri to sizePx) {
        if (Build.VERSION.SDK_INT >= 29) context.contentResolver.loadThumbnail(uri, android.util.Size(sizePx, sizePx), null)
        else if (video) MediaStore.Video.Thumbnails.getThumbnail(context.contentResolver, ContentUris.parseId(uri), MediaStore.Video.Thumbnails.MINI_KIND, null)
        else MediaStore.Images.Thumbnails.getThumbnail(context.contentResolver, ContentUris.parseId(uri), MediaStore.Images.Thumbnails.MINI_KIND, null)
    }
    Box(modifier.background(AureaColors.AddTile)) {
        bitmap?.let { Image(it.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
        Box(Modifier.fillMaxSize().background(AureaColors.AddScrimSoft))
    }
}

/** Texto branco com sombra sobre miniatura (duração). */
internal val OnThumbStyle: TextStyle
    @Composable get() = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W600, color = AureaColors.OnImage,
        shadow = Shadow(AureaColors.ImageScrim, blurRadius = 3f)))

// =============================================================================
// A faixa rápida de mídia (P0-3)
// =============================================================================

/**
 * Grade HORIZONTAL de 2 fileiras com fotos e vídeos juntos, do mais recente.
 * Primeira célula partida: "Ver tudo" (o navegador em tela cheia) em cima e o
 * álbum ▾ embaixo (menu de álbuns). Com acesso parcial, "Gerenciar". No fim,
 * os extras do AUREA: outros arquivos, sequência de imagens e vídeo com IA.
 * Tocar num item adiciona e fecha.
 */
@Composable
internal fun MediaStrip(store: EditorStore, ui: EditorUi, close: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    var revision by remember { mutableIntStateOf(0) }
    var requested by rememberSaveable { mutableStateOf(false) }
    var items by remember { mutableStateOf<List<MediaItem>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var failed by remember { mutableStateOf(false) }
    var bucket by remember { mutableStateOf(OmegaMediaPrefs.bucket(context)) }
    var albumMenu by remember { mutableStateOf(false) }
    val full = remember(revision) { context.galleryFull(false) || context.galleryFull(true) }
    val partial = remember(revision) { context.galleryPartial() }
    val permissions = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { revision++ }
    // Bugs 0.3.4: "Outros arquivos e nuvem" abre o seletor de documentos com
    // fotos, vídeos E áudio (antes o seletor de fotos: MP3/M4A/WAV não
    // apareciam). Sem permissão de áudio; o store faz do áudio camada de áudio.
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
        if (uris.isNotEmpty()) { store.importMediaBatch(uris); close() }
    }
    // 0.3.0: sequência de imagens (renders do Blender etc.) pela pasta ou pelos quadros.
    val sequence = rememberImageSequencePicker(store, onPicked = close)
    LaunchedEffect(Unit) {
        if (!requested && !full && !partial) {
            requested = true; permissions.launch(galleryPermissions())
        }
    }
    DisposableEffect(owner, context) {
        val observer = object : ContentObserver(Handler(Looper.getMainLooper())) { override fun onChange(selfChange: Boolean) { revision++ } }
        context.contentResolver.registerContentObserver(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, true, observer)
        context.contentResolver.registerContentObserver(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, true, observer)
        val lifecycle = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) revision++ }
        owner.lifecycle.addObserver(lifecycle)
        onDispose { context.contentResolver.unregisterContentObserver(observer); owner.lifecycle.removeObserver(lifecycle) }
    }
    LaunchedEffect(revision) {
        loading = items.isEmpty(); failed = false
        if (full || partial) {
            val result = withContext(Dispatchers.IO) { runCatching { mediaItems(context) } }
            items = result.getOrDefault(emptyList()); failed = result.isFailure
            if (result.isSuccess) withContext(Dispatchers.IO) { OmegaMediaPrefs.prune(context, items.mapTo(HashSet()) { it.uri.toString() }) }
        }
        loading = false
    }
    val allLabel = stringResource(R.string.om_media_all_albums)
    val albums = remember(items, allLabel) { mediaAlbums(items, allLabel) }
    // Álbum que sumiu (apagado): volta para "Tudo".
    val bucketId = if (bucket.first == 0L || albums.any { it.id == bucket.first }) bucket.first else 0L
    val shown = remember(items, bucketId) { if (bucketId == 0L) items else items.filter { it.bucketId == bucketId } }
    val recentCount = remember { OmegaMediaPrefs.recentCount(context) }
    val recents = remember(items, recentCount) {
        if (recentCount <= 0) emptyList()
        else OmegaMediaPrefs.recents(context).take(recentCount).mapNotNull { key -> items.firstOrNull { it.uri.toString() == key } }
    }
    val rest = remember(shown, recents) { if (recents.isEmpty()) shown else shown.filter { it !in recents } }
    val albumName = if (bucketId == 0L) allLabel else albums.firstOrNull { it.id == bucketId }?.name ?: allLabel
    fun pick(item: MediaItem) {
        OmegaMediaPrefs.markUsed(context, listOf(item.uri))
        store.importMediaBatch(listOf(item.uri))
        close()
    }

    Box(Modifier.fillMaxSize().testTag("add.media.strip")) {
        when {
            loading -> CircularProgressIndicator(Modifier.size(28.dp).align(Alignment.Center), color = AureaColors.Accent)
            !full && !partial -> AccessNotice(Modifier.align(Alignment.Center)) {
                if (!requested) { requested = true; permissions.launch(galleryPermissions()) }
                else context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
            }
            failed -> Text(
                stringResource(R.string.gallery_error),
                modifier = Modifier.align(Alignment.Center).tocavel { revision++ }.padding(16.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)),
            )
            else -> BoxWithConstraints(Modifier.fillMaxSize()) {
                val cell = maxHeight / 2
                val grid = rememberLazyGridState()
                val scope = rememberCoroutineScope()
                val scrolled by remember { derivedStateOf { grid.firstVisibleItemIndex > 0 || grid.firstVisibleItemScrollOffset > 0 } }
                LazyHorizontalGrid(rows = GridCells.Fixed(2), state = grid, modifier = Modifier.fillMaxSize()) {
                    item(key = "first") {
                        Column(Modifier.size(cell).padding(6.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                            HalfButton(Modifier.weight(1f), "add.media.viewAll", stringResource(R.string.om_add_view_all)) { ui.mediaBrowser = true }
                            Box(Modifier.weight(1f)) {
                                HalfButton(Modifier.fillMaxSize(), "add.media.album", albumName, chevron = true) { albumMenu = true }
                                AnchoredMenu(
                                    expanded = albumMenu, onDismiss = { albumMenu = false }, tag = "add.media.album.menu",
                                    entries = albums.mapIndexed { i, album ->
                                        MenuEntry.Item(album.name, sublabel = album.count.toString(), selected = album.id == bucketId,
                                            tag = "add.media.album.$i") {
                                            bucket = album.id to album.name
                                            OmegaMediaPrefs.setBucket(context, album.id, album.name)
                                        }
                                    },
                                )
                            }
                        }
                    }
                    if (partial && !context.galleryFull(false)) {
                        item(key = "manage") {
                            StripIconCell(cell, stringResource(R.string.gallery_manage), CupertinoGlyph.PlusSquareOnSquare, "add.media.manage") {
                                permissions.launch(galleryPermissions())
                            }
                        }
                    }
                    items(recents, key = { "recent:" + it.uri }) { item -> MediaStripCell(cell, item, recent = true) { pick(item) } }
                    items(rest, key = { it.uri.toString() }) { item -> MediaStripCell(cell, item, recent = false) { pick(item) } }
                    if (shown.isEmpty()) {
                        item(key = "empty", span = { GridItemSpan(maxLineSpan) }) {
                            Box(Modifier.width(200.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                                Text(stringResource(R.string.gallery_empty), textAlign = TextAlign.Center,
                                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
                            }
                        }
                    }
                    // Extras do AUREA, no fim da faixa.
                    item(key = "files") {
                        StripIconCell(cell, stringResource(R.string.gallery_files), CupertinoGlyph.Folder, "add.media.files") {
                            picker.launch(PickedMedia.PICKER_TYPES)
                        }
                    }
                    item(key = "sequence") {
                        StripIconCell(cell, stringResource(R.string.seq_title), CupertinoGlyph.SquareStack3dUp, "add.media.sequence", onClick = sequence)
                    }
                    item(key = "ai") {
                        StripIconCell(cell, stringResource(R.string.sh_add_ai_video), CupertinoGlyph.WandStars, "add.media.ai") {
                            openPanel(store, ui, EditorPanel.AiVideo)
                        }
                    }
                }
                // Seta de volta (15 × 30 dp): aparece depois de rolar; leva ao início.
                if (scrolled) {
                    val back = stringResource(R.string.om_media_back_to_start)
                    Box(
                        Modifier
                            .align(Alignment.CenterStart)
                            .width(24.dp)
                            .height(44.dp)
                            .testTag("add.media.cue")
                            .semantics { contentDescription = back }
                            .tocavel { scope.launch { grid.animateScrollToItem(0) } },
                        contentAlignment = Alignment.CenterStart,
                    ) {
                        Box(Modifier.width(15.dp).height(30.dp).clip(RoundedCornerShape(topEnd = 4.dp, bottomEnd = 4.dp))
                            .background(AureaColors.AddPanelDeep.copy(alpha = 0.85f)), contentAlignment = Alignment.Center) {
                            CupertinoIcon(CupertinoGlyph.ChevronLeft, 12.dp, AureaColors.Text)
                        }
                    }
                }
            }
        }
    }
}

/** Metade da primeira célula ("Ver tudo" / álbum ▾). */
@Composable
private fun HalfButton(modifier: Modifier, tag: String, label: String, chevron: Boolean = false, onClick: () -> Unit) {
    Row(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(4.dp))
            .background(AureaColors.AddTile)
            .semantics { contentDescription = label }
            .testTag(tag)
            .tocavel(onClick = onClick)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        if (!chevron) {
            CupertinoIcon(CupertinoGlyph.SquareGrid2x2, 14.dp, AureaColors.Text)
            Spacer(Modifier.width(4.dp))
        }
        Text(
            label, maxLines = 2, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 13.sp, color = AureaColors.Text)),
        )
        if (chevron) {
            Spacer(Modifier.width(2.dp))
            CupertinoIcon(CupertinoGlyph.ChevronDown, 11.dp, AureaColors.Text)
        }
    }
}

/** Célula de ação da faixa (gerenciar, arquivos, sequência, IA): ícone e nome. */
@Composable
internal fun StripIconCell(cell: Dp, label: String, glyph: Char, tag: String, onClick: () -> Unit) {
    Box(Modifier.size(cell).padding(6.dp)) {
        Column(
            Modifier
                .fillMaxSize()
                .clip(RoundedCornerShape(4.dp))
                .background(AureaColors.AddTile)
                .semantics { contentDescription = label }
                .testTag(tag)
                .tocavel(onClick = onClick)
                .padding(4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            CupertinoIcon(glyph, 22.dp, AureaColors.Text)
            Spacer(Modifier.height(4.dp))
            Text(label, maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 13.sp, color = AureaColors.Text)))
        }
    }
}

/** Item da faixa: cantos retos, duração e ▶ no vídeo, relógio no recente. */
@Composable
private fun MediaStripCell(cell: Dp, item: MediaItem, recent: Boolean, onClick: () -> Unit) {
    val label = if (item.video) stringResource(R.string.om_media_video_named, item.name, mediaDuration(item.durationMs))
        else stringResource(R.string.om_media_photo_named, item.name)
    val recentLabel = stringResource(R.string.om_media_recent)
    Box(
        Modifier
            .size(cell)
            .semantics { contentDescription = if (recent) "$label, $recentLabel" else label }
            .testTag("add.media.item.${item.name}")
            .tocavel(shrink = 0.96f, onClick = onClick),
    ) {
        MediaThumb(item.uri, item.video, Modifier.fillMaxSize().padding(0.5.dp))
        if (item.video) {
            CupertinoIcon(CupertinoGlyph.PlayFill, 18.dp, AureaColors.OnImage, Modifier.align(Alignment.Center))
            Text(mediaDuration(item.durationMs), Modifier.align(Alignment.BottomEnd).padding(4.dp), style = OnThumbStyle)
        }
        if (recent) CupertinoIcon(CupertinoGlyph.Clock, 15.dp, AureaColors.OnImage, Modifier.align(Alignment.BottomStart).padding(3.dp))
    }
}

/** Sem acesso: a explicação e "Permitir acesso" (o pedido do sistema ou os ajustes). */
@Composable
internal fun AccessNotice(modifier: Modifier = Modifier, onAllow: () -> Unit) {
    Column(modifier.padding(horizontal = 24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(stringResource(R.string.gallery_access), textAlign = TextAlign.Center, maxLines = 3,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
        Spacer(Modifier.height(10.dp))
        Box(
            Modifier
                .height(38.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(AureaColors.AddTile)
                .testTag("add.media.allow")
                .tocavel(onClick = onAllow)
                .padding(horizontal = 16.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(stringResource(R.string.gallery_allow), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
        }
    }
}
