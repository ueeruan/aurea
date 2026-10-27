package com.aurea.aurea.editor

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.drawable.AnimatedImageDrawable
import android.net.Uri
import android.os.Build
import android.os.StatFs
import android.provider.OpenableColumns
import android.widget.ImageView
import android.widget.VideoView
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.home.DonationCard
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportFormat
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.state.ImageExportRules
import com.aurea.aurea.state.QUALITY_MAX
import com.aurea.aurea.state.QUALITY_MIN
import com.aurea.aurea.state.VideoExportRules
import com.aurea.aurea.state.exportFpsOption
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.AureaDropdown
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.i18n.ltrPlain
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale
import java.util.zip.ZipInputStream
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Lado menor → rótulo (o mesmo do iOS). */
private val Resolutions = listOf(480 to "480p", 720 to "720p", 1080 to "1080p", 1440 to "2K", 2160 to "4K")
/** Atalhos de taxa do "Mais…"; "Personalizado…" digita qualquer uma de 1 a 240 (o teto dos encoders). */
private val FrameRates = listOf(24.0, 25.0, 30.0, 50.0, 60.0, 120.0)
/** Mbps manuais do Avançado (0 = automático pela qualidade). */
private val CustomBitrates = listOf(5, 10, 15, 25, 40, 60)
/** GIF acima disto: aviso antes (o motor ainda aceita até [ImageExportRules.GIF_MAX_FRAMES]). */
private const val GIF_RECOMMENDED_FRAMES = 450

/** Os itens da lista "Exportar e compartilhar" (na ordem da referência, só o que o AUREA tem; D4). */
private enum class ExportItem(val tag: String, val glyph: Char, val format: ExportFormat?) {
    Video("video", CupertinoGlyph.Film, ExportFormat.Video),
    Package("package", CupertinoGlyph.DocOnDoc, null),
    Frame("frame", CupertinoGlyph.Photo, ExportFormat.Frame),
    Sequence("sequence", CupertinoGlyph.SquareStack3dUp, ExportFormat.Sequence),
    Gif("gif", CupertinoGlyph.PlayRectangle, ExportFormat.Gif),
}

private fun ExportItem.nameRes(): Int = when (this) {
    ExportItem.Video -> R.string.om_exp_opt_video
    ExportItem.Package -> R.string.om_exp_opt_package
    ExportItem.Frame -> R.string.om_exp_opt_frame
    ExportItem.Sequence -> R.string.om_exp_opt_sequence
    ExportItem.Gif -> R.string.om_exp_opt_gif
}

private fun ExportItem.noteRes(): Int = when (this) {
    ExportItem.Video -> R.string.om_exp_note_video
    ExportItem.Package -> R.string.om_exp_note_package
    ExportItem.Frame -> R.string.om_exp_note_frame
    ExportItem.Sequence -> R.string.om_exp_note_sequence
    ExportItem.Gif -> R.string.om_exp_note_gif
}

/** Uma checagem antes de exportar (P1-11): mensagem e se dá para seguir mesmo assim. */
private data class Precheck(val message: String, val canContinue: Boolean)

/**
 * Omega adicionar (área 08, P0-8..P0-11): a página "Exportar e compartilhar",
 * clara como a da referência (D1). Estimativas (espaço disponível e tamanho),
 * a lista de cartões com rádio (Vídeo com a resolução embutida, Arquivo do
 * projeto, Quadro atual, Sequência, GIF), a sub-página de ajustes que desliza
 * (200 ms) e o botão "Exportar" azul-marinho. O progresso é o painel sobre
 * véu; o resultado, a página azul-marinho com o player, Salvar, Compartilhar e
 * Fechar. Todo o motor de exportação 2.0 (escada, retomada, segundo plano,
 * avisos) continua no [com.aurea.aurea.state.Exporter].
 * Espelho: engine/platform/ios/app/ExportView.swift.
 */
@Composable
internal fun ExportScreen(store: EditorStore, onOpenProjectSettings: (() -> Unit)? = null, onDismiss: () -> Unit) {
    val exporter = store.exporter
    val st = exporter.state
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val comp = store.composition
    val compW = comp?.width ?: store.project.width
    val compH = comp?.height ?: store.project.height
    val compFps = comp?.fps ?: store.project.fps.toDouble()

    var options by remember { mutableStateOf(ExportOptions(shortSide = min(1080, max(720, min(compW, compH))))) }
    var item by remember { mutableStateOf(ExportItem.Video) }
    var settings by remember { mutableStateOf<ExportItem?>(null) }
    var includeMedia by remember { mutableStateOf(true) }
    var precheck by remember { mutableStateOf<Precheck?>(null) }
    var info by remember { mutableStateOf<ExportItem?>(null) }
    LaunchedEffect(store, store.projectGeneration) { exporter.reset() }

    val close = {
        if (!exporter.busy) {
            exporter.reset()
            onDismiss()
        }
    }
    val opts = options.copy(format = item.format ?: ExportFormat.Video)
    // 0.3.5 (bug 1): as opções são lidas do ESTADO na hora do toque. Os lambdas
    // abaixo podem ser memorizados pelo compilador do Compose com o `opts` da
    // primeira composição (Vídeo): escolher "Quadro atual", "Sequência" ou GIF
    // mudava a tela, mas o botão Exportar ainda mandava o vídeo.
    val currentOpts = { options.copy(format = item.format ?: ExportFormat.Video) }
    val startExport = {
        val chosen = currentOpts()
        exporter.plannedSeconds = if (compFps > 0) store.exportDuration(chosen.trimToContent) / compFps else 0.0
        exporter.start(store.project.title, compW, compH, compFps, chosen)
    }
    // Android 8–9: a galeria só vê o que está na pasta PÚBLICA (Filmes/Aurea),
    // e lá só se escreve com WRITE_EXTERNAL_STORAGE. Negada, o export segue e
    // fica na pasta do app (a mensagem final diz onde).
    val legacyStorage = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { _ -> startExport() }
    val runExport = {
        if (com.aurea.aurea.state.GalleryPublish.needsLegacyWritePermission(context)) legacyStorage.launch(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
        else startExport()
    }
    val packageTarget = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val path = store.project.path
        if (uri != null && path != null) {
            store.exportProjectFile(path, includeMedia, uri)
            close()
        }
    }
    // Pré-checagens (P1-11): textos e limites que o AUREA já tem.
    val emptyText = stringResource(R.string.om_exp_empty_project)
    val shortText = stringResource(R.string.om_exp_too_short)
    val tooLong3h = stringResource(R.string.exp_too_long_3h)
    val gifTooLong = stringResource(R.string.exp2_too_long)
    val gifHeavyTemplate = stringResource(R.string.om_exp_gif_heavy, GIF_RECOMMENDED_FRAMES)
    fun go() {
        if (item == ExportItem.Package) {
            val path = store.project.path ?: return
            packageTarget.launch(store.projectFileName(path))
            return
        }
        val opts = currentOpts()
        val frames = store.exportDuration(opts.trimToContent)
        val seconds = if (compFps > 0) frames / compFps else 0.0
        precheck = when {
            store.layers.isEmpty() -> Precheck(emptyText, false)
            frames < 1 -> Precheck(shortText, false)
            item == ExportItem.Video && seconds > EXPORT_MAX_SECONDS -> Precheck(tooLong3h, false)
            item == ExportItem.Gif || item == ExportItem.Sequence -> {
                val plan = exporter.imagePlan(opts, compW, compH, compFps, frames)
                when {
                    ImageExportRules.tooLong(opts.format, plan.frames) -> Precheck(gifTooLong, false)
                    item == ExportItem.Gif && plan.frames > GIF_RECOMMENDED_FRAMES -> Precheck(gifHeavyTemplate, true)
                    else -> null
                }
            }
            else -> null
        }
        if (precheck == null) runExport()
    }

    Dialog(
        onDismissRequest = { if (settings != null && !exporter.busy) settings = null else close() },
        properties = DialogProperties(usePlatformDefaultWidth = false, dismissOnClickOutside = false, decorFitsSystemWindows = false),
    ) {
        if (st.phase == ExportPhase.Done) {
            ResultPage(store, item, opts, compW, compH, compFps, onClose = close)
            return@Dialog
        }
        Box(Modifier.fillMaxSize().background(AureaColors.ExportPage).testTag("export.page")) {
            Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
                ExportTopBar(
                    title = settings?.let { stringResource(it.nameRes()) } ?: stringResource(R.string.om_exp_title),
                    back = settings != null, enabled = !exporter.busy,
                    onClick = { if (settings != null) settings = null else close() },
                )
                val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
                val shiftPx = with(LocalDensity.current) { 135.dp.roundToPx() }
                AnimatedContent(
                    targetState = settings,
                    transitionSpec = {
                        // Sub-página: entra de +135 dp para 0 em 200 ms, desacelerando; a
                        // lista volta do lado oposto. Em árabe o sentido espelha.
                        val forward = targetState != null
                        val sign = (if (forward) 1 else -1) * (if (rtl) -1 else 1)
                        (slideInHorizontally(tween(200, easing = LinearOutSlowInEasing)) { sign * shiftPx } + fadeIn(tween(200))) togetherWith
                            (slideOutHorizontally(tween(200, easing = LinearOutSlowInEasing)) { -sign * shiftPx } + fadeOut(tween(120)))
                    },
                    modifier = Modifier.weight(1f).fillMaxWidth(),
                    label = "export settings",
                ) { page ->
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(bottom = 10.dp)) {
                        if (page == null) {
                            if (st.phase == ExportPhase.Failed || st.phase == ExportPhase.Cancelled) {
                                Notice(st.message, danger = st.phase == ExportPhase.Failed)
                            }
                            Estimates(store, item, opts, compW, compH, compFps, includeMedia)
                            OptionList(store, item, opts, compW, compH, onPick = { item = it }, onSettings = { item = it; settings = it },
                                onInfo = { info = it }) { options = it }
                        } else {
                            Estimates(store, page, opts, compW, compH, compFps, includeMedia)
                            SettingsPage(store, page, opts, compW, compH, compFps, includeMedia, { includeMedia = it },
                                onOpenProjectSettings?.let { open -> { if (!exporter.busy) { exporter.reset(); open() } } }) { options = it }
                        }
                    }
                }
                ExportButton(enabled = !exporter.busy, onClick = ::go)
            }
            if (st.phase == ExportPhase.Running || st.phase == ExportPhase.Publishing) Progress(st, st.format) { exporter.cancel() }
        }
        precheck?.let { check ->
            AureaDialog(
                title = null, message = check.message, tag = "export.precheck",
                onDismiss = { precheck = null },
                actions = if (check.canContinue) listOf(
                    DialogAction(stringResource(R.string.common_close), DialogRole.Negative),
                    DialogAction(stringResource(R.string.om_exp_anyway), DialogRole.Positive, tag = "export.precheck.anyway") { runExport() },
                ) else listOf(DialogAction(stringResource(R.string.common_ok), DialogRole.Positive)),
            )
        }
        info?.let { which ->
            AureaDialog(
                title = stringResource(which.nameRes()),
                message = stringResource(if (which == ExportItem.Package) R.string.om_exp_package_info else which.noteRes()),
                actions = listOf(DialogAction(stringResource(R.string.common_ok), DialogRole.Positive)),
                onDismiss = { info = null }, tag = "export.info",
            )
        }
    }
}

// =============================================================================
// Barra, estimativas, lista
// =============================================================================

/** Barra de 49 dp, branca: ✕ (ou ← na sub-página) de 44 dp e o título centrado, 17 sp negrito. */
@Composable
private fun ExportTopBar(title: String, back: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val description = stringResource(if (back) R.string.om_add_back else R.string.editor_fechar)
    Box(Modifier.fillMaxWidth().height(49.dp).background(AureaColors.ExportCard)) {
        Box(
            Modifier.align(Alignment.CenterStart).size(44.dp).testTag(if (back) "export.back" else "export.close")
                .semantics { contentDescription = description }.tocavel(enabled = enabled, onClick = onClick),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(if (back) CupertinoGlyph.ChevronLeft else CupertinoGlyph.Xmark, 20.dp,
                if (enabled) AureaColors.ExportText else AureaColors.ExportAction)
        }
        Text(title, modifier = Modifier.align(Alignment.Center).padding(horizontal = 52.dp), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700, color = AureaColors.ExportText)))
    }
    Box(Modifier.fillMaxWidth().height(0.7.dp).background(AureaColors.ExportLine))
}

/** Bloco branco das estimativas: espaço disponível e tamanho (no pacote, por tipo de mídia). */
@Composable
private fun Estimates(store: EditorStore, item: ExportItem, opts: ExportOptions, compW: Int, compH: Int, compFps: Double, includeMedia: Boolean) {
    val context = LocalContext.current
    val free by produceState(-1L) { value = withContext(Dispatchers.IO) { runCatching { StatFs(context.filesDir.absolutePath).availableBytes }.getOrDefault(-1L) } }
    val pack by produceState<PackageSizes?>(null, item == ExportItem.Package, store.project.path) {
        val path = store.project.path
        value = if (item == ExportItem.Package && path != null) withContext(Dispatchers.IO) { packageSizes(store, path) } else null
    }
    Column(Modifier.padding(horizontal = 10.dp, vertical = 10.dp).clip(RoundedCornerShape(6.dp)).background(AureaColors.ExportCard)) {
        EstimateRow(stringResource(R.string.om_exp_space), if (free >= 0) fmtSize(free) else "—", "export.estimate.space")
        Box(Modifier.fillMaxWidth().height(0.7.dp).background(AureaColors.ExportLine))
        if (item == ExportItem.Package) {
            val p = pack
            if (includeMedia && p != null) {
                EstimateRow(stringResource(R.string.om_exp_package_images), fmtSize(p.images), "export.estimate.images")
                EstimateRow(stringResource(R.string.om_exp_package_videos), fmtSize(p.videos), "export.estimate.videos")
                EstimateRow(stringResource(R.string.om_exp_package_audio), fmtSize(p.audio), "export.estimate.audio")
            }
            EstimateRow(stringResource(R.string.om_exp_package_size),
                if (p == null) "—" else fmtSize(p.project + if (includeMedia) p.images + p.videos + p.audio + p.other else 0L), "export.estimate.size")
        } else {
            val bytes = estimatedBytes(store, item, opts, compW, compH, compFps)
            EstimateRow(stringResource(R.string.om_exp_size), fmtSize(bytes), "export.estimate.size")
        }
    }
}

@Composable
private fun EstimateRow(label: String, value: String, tag: String) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 14.dp).testTag(tag).semantics(mergeDescendants = true) {},
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.ExportText)))
        Text(ltrPlain(value), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.ExportText, fontFeatureSettings = "tnum")))
    }
}

private data class PackageSizes(val project: Long, val images: Long, val videos: Long, val audio: Long, val other: Long)

/** A soma dos tamanhos da mídia do projeto, por tipo (AssetKind: 1 vídeo, 2 áudio, 3 imagem). */
private fun packageSizes(store: EditorStore, path: String): PackageSizes {
    val app = store.getApplication<android.app.Application>()
    val refs = runCatching { store.projectMediaRefs(path) }.getOrNull() ?: return PackageSizes(File(path).length(), 0, 0, 0, 0)
    var images = 0L; var videos = 0L; var audio = 0L; var other = 0L
    for (i in 0 until refs.size / 4) {
        val resolved = refs[i * 4 + 1]
        val kind = refs[i * 4 + 3].toIntOrNull() ?: 0
        val size = if (resolved.startsWith("content://")) runCatching {
            app.contentResolver.query(Uri.parse(resolved), arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c -> if (c.moveToFirst()) c.getLong(0) else 0L } ?: 0L
        }.getOrDefault(0L) else File(resolved.removePrefix("file://")).length()
        when (kind) { 1 -> videos += size; 2 -> audio += size; 3 -> images += size; else -> other += size }
    }
    return PackageSizes(File(path).length(), images, videos, audio, other)
}

private fun estimatedBytes(store: EditorStore, item: ExportItem, opts: ExportOptions, compW: Int, compH: Int, compFps: Double): Long {
    val frames = store.exportDuration(opts.trimToContent)
    return if (item == ExportItem.Video) {
        val (w, h) = VideoExportRules.frameSize(compW, compH, opts.shortSide)
        val fps = if (opts.fps > 0) opts.fps else compFps
        val seconds = if (compFps > 0) frames / compFps else 0.0
        store.exporter.estimatedBytes(w, h, fps, seconds, opts)
    } else store.exporter.imagePlan(opts, compW, compH, compFps, frames).bytes
}

/**
 * A lista: cartões brancos (raio 6, padding 5) com a área tocável de 70 dp —
 * rádio, ícone, nome 17 sp negrito, nota 11 sp e a ação à direita (seta que
 * abre os ajustes, ou ⓘ). O Vídeo escolhido mostra a resolução embutida.
 */
@Composable
private fun OptionList(
    store: EditorStore,
    current: ExportItem,
    opts: ExportOptions,
    compW: Int,
    compH: Int,
    onPick: (ExportItem) -> Unit,
    onSettings: (ExportItem) -> Unit,
    onInfo: (ExportItem) -> Unit,
    onChange: (ExportOptions) -> Unit,
) {
    Column(Modifier.padding(horizontal = 10.dp)) {
        ExportItem.entries.forEach { entry ->
            Column(Modifier.padding(vertical = 5.dp).clip(RoundedCornerShape(6.dp)).background(AureaColors.ExportCard)) {
                val name = stringResource(entry.nameRes())
                val on = entry == current
                Row(
                    Modifier
                        .fillMaxWidth()
                        .height(70.dp)
                        .semantics { selected = on; role = Role.RadioButton; contentDescription = name }
                        .testTag("export.option.${entry.tag}")
                        .tocavel(shrink = 1f) { onPick(entry) },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(Modifier.width(52.dp), contentAlignment = Alignment.Center) { Radio(on) }
                    CupertinoIcon(entry.glyph, 22.dp, AureaColors.ExportText)
                    Column(Modifier.weight(1f).padding(start = 12.dp)) {
                        Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis,
                            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700, color = AureaColors.ExportText)))
                        Text(stringResource(entry.noteRes()), maxLines = 1, overflow = TextOverflow.Ellipsis,
                            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.ExportHint)))
                    }
                    val hasSettings = entry != ExportItem.Package
                    val actionLabel = stringResource(if (hasSettings) R.string.om_exp_settings_named else R.string.om_exp_info_named, name)
                    Box(
                        Modifier.size(width = 52.dp, height = 70.dp).testTag("export.option.${entry.tag}." + if (hasSettings) "settings" else "info")
                            .semantics { contentDescription = actionLabel }
                            .tocavel { if (hasSettings) onSettings(entry) else onInfo(entry) },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(if (hasSettings) CupertinoGlyph.ChevronRight else CupertinoGlyph.InfoCircle, 18.dp, AureaColors.ExportAction)
                    }
                }
                if (entry == ExportItem.Video && on) {
                    Box(Modifier.fillMaxWidth().height(2.dp).background(AureaColors.ExportPage))
                    ResolutionDropdown(store, opts, compW, compH, "export.option.video.resolution", Modifier.padding(10.dp), onChange)
                }
            }
        }
    }
}

@Composable
private fun Radio(on: Boolean) {
    val ink = AureaColors.ExportText
    val accent = AureaColors.LightAccentInk
    Canvas(Modifier.size(20.dp)) {
        val r = size.minDimension / 2f
        drawCircle(if (on) accent else ink.copy(alpha = 0.35f), r - 1.dp.toPx(), style = androidx.compose.ui.graphics.drawscope.Stroke(2.dp.toPx()))
        if (on) drawCircle(accent, r * 0.45f)
    }
}

/** O seletor de resolução (44 dp): as resoluções de sempre e o teto do aparelho. */
@Composable
private fun ResolutionDropdown(store: EditorStore, opts: ExportOptions, compW: Int, compH: Int, tag: String, modifier: Modifier, onChange: (ExportOptions) -> Unit) {
    val comp = store.composition
    fun sizeFor(side: Int): Pair<Int, Int> = VideoExportRules.frameSize(compW, compH, side)
    val available = Resolutions.filter { (side, _) -> val (w, h) = sizeFor(side); comp?.fits(w, h) ?: true }
    val current = Resolutions.firstOrNull { it.first == opts.shortSide }?.second ?: "${opts.shortSide}p"
    val (w, h) = sizeFor(opts.shortSide)
    Column(modifier) {
        AureaDropdown(
            value = "$current · ${ltrPlain("$w × $h")}",
            entries = Resolutions.map { (side, label) ->
                val enabled = available.any { it.first == side }
                MenuEntry.Item(label, sublabel = sizeFor(side).let { ltrPlain("${it.first} × ${it.second}") }, selected = side == opts.shortSide,
                    enabled = enabled, tag = "$tag.$side") { onChange(opts.copy(shortSide = side)) }
            },
            width = null,
            modifier = Modifier.fillMaxWidth().height(44.dp),
            tag = tag,
            contentDescription = stringResource(R.string.om_exp_resolution),
        )
        if (available.size < Resolutions.size) {
            val blocked = Resolutions.filter { it !in available }.map { it.second }
            Text(store.deviceReport?.exportLimitReason(LocalContext.current) ?: stringResource(R.string.sh_export_above_device, blocked.joinToString()),
                modifier = Modifier.padding(top = 6.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.ExportHint)))
        }
    }
}

// =============================================================================
// Sub-páginas de ajustes (P0-9)
// =============================================================================

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SettingsPage(
    store: EditorStore,
    item: ExportItem,
    opts: ExportOptions,
    compW: Int,
    compH: Int,
    compFps: Double,
    includeMedia: Boolean,
    onIncludeMedia: (Boolean) -> Unit,
    onOpenProjectSettings: (() -> Unit)?,
    onChange: (ExportOptions) -> Unit,
) {
    Column(Modifier.padding(horizontal = 10.dp).clip(RoundedCornerShape(6.dp)).background(AureaColors.ExportCard).padding(14.dp)) {
        when (item) {
            ExportItem.Video -> {
                SettingLabel(stringResource(R.string.om_exp_resolution))
                ResolutionDropdown(store, opts, compW, compH, "export.settings.resolution", Modifier, onChange)
                SettingLabel(stringResource(R.string.exp2_codec))
                val hevcOk = store.deviceReport?.hevcExportAvailable ?: true
                Segmented(listOf("h264" to "H.264/AVC", "hevc" to "H.265/HEVC"), if (opts.hevc) "hevc" else "h264", "export.settings.codec",
                    disabled = if (hevcOk) emptySet() else setOf("hevc")) {
                    // HEVC só no Automático: Compatível e Software são H.264 (ExportAttempts.hpp).
                    onChange(opts.copy(hevc = it == "hevc", encoder = if (it == "hevc") 0 else opts.encoder))
                }
                if (!hevcOk) Hint(store.deviceReport?.hevcExportReason(LocalContext.current) ?: "")
                FpsRow(opts, compFps, "export.settings.fps", onChange)
                QualityRow(opts, onChange)
                Advanced(store, opts, compW, compH, compFps, onChange)
            }
            ExportItem.Package -> {
                SettingLabel(stringResource(R.string.om_exp_opt_package))
                Segmented(listOf("media" to stringResource(R.string.project_file_include_media), "project" to stringResource(R.string.project_file_without_media)),
                    if (includeMedia) "media" else "project", "export.settings.package") { onIncludeMedia(it == "media") }
                Hint(stringResource(R.string.om_exp_package_info))
            }
            ExportItem.Frame, ExportItem.Sequence -> {
                SettingLabel(stringResource(R.string.om_exp_resolution))
                val short = max(1, min(compW, compH))
                val original = "${stringResource(R.string.exp2_resolution_original)} (${ltrPlain("$compW × $compH")})"
                val choices = listOf(0 to original) + Resolutions.filter { it.first != short }
                AureaDropdown(
                    value = (choices.firstOrNull { it.first == opts.imageShortSide } ?: choices[0]).second,
                    entries = choices.map { (side, label) ->
                        MenuEntry.Item(label, selected = side == opts.imageShortSide, tag = "export.settings.resolution.$side") { onChange(opts.copy(imageShortSide = side)) }
                    },
                    width = null, modifier = Modifier.fillMaxWidth().height(44.dp), tag = "export.settings.resolution",
                    contentDescription = stringResource(R.string.om_exp_resolution),
                )
                if (item == ExportItem.Sequence) {
                    FpsRow(opts, compFps, "export.settings.fps", onChange)
                    RangeRow(opts, onChange)
                }
                BackgroundRow(store, onOpenProjectSettings)
            }
            ExportItem.Gif -> {
                SettingLabel(stringResource(R.string.exp2_gif_width))
                Segmented(ImageExportRules.GifWidths.map { it.toString() to "$it px" }, opts.gifWidth.toString(), "export.settings.gifWidth") {
                    onChange(opts.copy(gifWidth = it.toInt()))
                }
                SettingLabel(stringResource(R.string.editor_quadros_segundo), value = stringResource(R.string.om_exp_fps_value, fmt(opts.gifFps)))
                Segmented(ImageExportRules.GifFrameRates.map { fmt(it) to fmt(it) }, fmt(opts.gifFps), "export.settings.gifFps") {
                    onChange(opts.copy(gifFps = it.replace(',', '.').toDouble()))
                }
                RangeRow(opts, onChange)
                BackgroundRow(store, onOpenProjectSettings)
            }
        }
        if (item != ExportItem.Package) {
            val frames = store.exportDuration(opts.trimToContent)
            if (item != ExportItem.Video && item != ExportItem.Frame) {
                val plan = store.exporter.imagePlan(opts.copy(format = item.format ?: ExportFormat.Video), compW, compH, compFps, frames)
                Hint(listOf(ltrPlain("${plan.width} × ${plan.height}"), stringResource(R.string.exp2_frames_count, plan.frames)).joinToString(" · "))
                if (ImageExportRules.tooLong(item.format ?: ExportFormat.Gif, plan.frames)) Hint(stringResource(R.string.exp2_too_long), danger = true)
            }
        }
    }
}

@Composable
private fun SettingLabel(text: String, value: String? = null) {
    Row(Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 7.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(text, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.ExportText)))
        if (value != null) Text(ltrPlain(value), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.ExportHint, fontFeatureSettings = "tnum")))
    }
}

@Composable
private fun Hint(text: String, danger: Boolean = false) {
    if (text.isBlank()) return
    Text(text, modifier = Modifier.padding(top = 8.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (danger) AureaColors.LightDanger else AureaColors.ExportHint)))
}

/**
 * O segmentado da referência: 44 dp, raio 5, padding 3; o ativo com fundo
 * azul-marinho e texto de destaque, os outros com texto azul-marinho.
 */
@Composable
private fun Segmented(options: List<Pair<String, String>>, current: String?, tag: String, disabled: Set<String> = emptySet(), onPick: (String) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.ExportPage).padding(3.dp),
    ) {
        options.forEach { (key, label) ->
            val on = key == current
            val off = key in disabled
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(5.dp))
                    .background(if (on) AureaColors.ExportPrimary else Color.Transparent)
                    .semantics { selected = on; role = Role.RadioButton }
                    .testTag("$tag.$key")
                    .tocavel(enabled = !off, shrink = 1f) { if (!on) onPick(key) },
                contentAlignment = Alignment.Center,
            ) {
                Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600,
                        color = when { off -> AureaColors.ExportAction; on -> AureaColors.ExportOnPrimary; else -> AureaColors.ExportText })))
            }
        }
    }
}

/**
 * Quadros por segundo: o valor "N fps" (até 2 casas) e o segmentado Baixo |
 * Reduzido | Cheio pela regra do motor (export_fps_option: fps × 100,
 * truncado). "Mais…" abre as taxas do AUREA (24 a 120 e personalizado).
 */
@Composable
private fun FpsRow(opts: ExportOptions, compFps: Double, tag: String, onChange: (ExportOptions) -> Unit) {
    val fps100 = (compFps * 100.0).roundToInt().coerceIn(0, 1_000_000)
    val values = (0..2).map { exportFpsOption(fps100, it) / 100.0 }
    val current = if (opts.fps > 0) opts.fps else compFps
    val selected = when {
        opts.fps <= 0.0 -> "full"
        abs(opts.fps - values[0]) < 0.005 -> "low"
        abs(opts.fps - values[1]) < 0.005 -> "reduced"
        abs(opts.fps - values[2]) < 0.005 -> "full"
        else -> null
    }
    var more by remember { mutableStateOf(false) }
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    SettingLabel(stringResource(R.string.editor_quadros_segundo), value = stringResource(R.string.om_exp_fps_value, fmt(current)))
    Segmented(
        listOf("low" to stringResource(R.string.om_exp_fps_low), "reduced" to stringResource(R.string.om_exp_fps_reduced), "full" to stringResource(R.string.om_exp_fps_full)),
        selected, tag,
    ) {
        onChange(opts.copy(fps = when (it) { "low" -> values[0]; "reduced" -> values[1]; else -> 0.0 }))
    }
    Box {
        Text(
            stringResource(R.string.om_exp_fps_more),
            modifier = Modifier.padding(top = 6.dp).testTag("$tag.more").tocavel { more = true }.padding(vertical = 8.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.LightAccentInk)),
        )
        val customLabel = stringResource(R.string.project_fps_custom)
        val customTitle = stringResource(R.string.project_fps_custom_title)
        com.aurea.aurea.ui.ds.AnchoredMenu(
            expanded = more, onDismiss = { more = false }, tag = "$tag.more.menu",
            entries = FrameRates.map { r ->
                MenuEntry.Item(stringResource(R.string.om_exp_fps_value, fmt(r)), selected = abs(r - current) < 0.01) { onChange(opts.copy(fps = r)) }
            } + MenuEntry.Item(customLabel) {
                keypad = KeypadRequest(customTitle, current.toFloat(), "fps", 1f, 240f, 3) { v -> onChange(opts.copy(fps = v.toDouble())) }
            },
        )
    }
    if (current > 60.01) Hint(stringResource(R.string.export_fps_high_note))
    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
}

/** Qualidade: o valor à direita, "Baixa" e "Alta" nas pontas e o controle de 20 a 100. */
@Composable
private fun QualityRow(opts: ExportOptions, onChange: (ExportOptions) -> Unit) {
    val value = if (opts.quality >= QUALITY_MIN) opts.quality else when (opts.quality) { 0 -> 20; 2 -> 100; else -> 60 }
    val manual = opts.customMbps > 0
    SettingLabel(stringResource(R.string.editor_qualidade), value = stringResource(R.string.om_exp_quality_value, value))
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.om_exp_quality_low), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.ExportHint)))
        QualitySlider(value, enabled = !manual, modifier = Modifier.weight(1f).padding(horizontal = 10.dp)) {
            onChange(opts.copy(quality = it, customMbps = 0))
        }
        Text(stringResource(R.string.om_exp_quality_high), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.ExportHint)))
    }
}

/** Controle deslizante de 35 dp: trilha A9, polegar claro de 18 dp. */
@Composable
private fun QualitySlider(value: Int, enabled: Boolean, modifier: Modifier, onChange: (Int) -> Unit) {
    val track = AureaColors.AddPanel
    val fill = AureaColors.LightAccentInk
    val thumb = AureaColors.LightSurface
    val ring = AureaColors.ExportLine
    val label = stringResource(R.string.editor_qualidade)
    val latest by androidx.compose.runtime.rememberUpdatedState(onChange)
    BoxWithConstraints(modifier.height(35.dp).testTag("export.settings.quality")
        .semantics { contentDescription = label; stateDescription = "$value%" }) {
        val widthPx = constraints.maxWidth.toFloat().coerceAtLeast(1f)
        fun valueAt(x: Float) = (QUALITY_MIN + (x / widthPx).coerceIn(0f, 1f) * (QUALITY_MAX - QUALITY_MIN)).roundToInt()
        Canvas(
            Modifier.fillMaxSize()
                .pointerInput(enabled, widthPx) {
                    if (!enabled) return@pointerInput
                    detectTapGestures { latest(valueAt(it.x)) }
                }
                .pointerInput(enabled, widthPx) {
                    if (!enabled) return@pointerInput
                    detectHorizontalDragGestures { change, _ -> change.consume(); latest(valueAt(change.position.x)) }
                },
        ) {
            val y = size.height / 2f
            val t = (value - QUALITY_MIN).toFloat() / (QUALITY_MAX - QUALITY_MIN)
            val x = t * size.width
            drawLine(track, Offset(0f, y), Offset(size.width, y), 4.dp.toPx(), androidx.compose.ui.graphics.StrokeCap.Round)
            drawLine(if (enabled) fill else track, Offset(0f, y), Offset(x, y), 4.dp.toPx(), androidx.compose.ui.graphics.StrokeCap.Round)
            drawCircle(ring, 9.5.dp.toPx(), Offset(x, y))
            drawCircle(thumb, 9.dp.toPx(), Offset(x, y))
        }
    }
}

@Composable
private fun RangeRow(opts: ExportOptions, onChange: (ExportOptions) -> Unit) {
    SettingLabel(stringResource(R.string.exp2_range))
    Segmented(listOf("content" to stringResource(R.string.export_range_content), "full" to stringResource(R.string.export_range_full)),
        if (opts.trimToContent) "content" else "full", "export.settings.range") { onChange(opts.copy(trimToContent = it == "content")) }
}

/** P2-4: "Fundo: <cor | Transparente>" com o atalho para os ajustes do projeto. */
@Composable
private fun BackgroundRow(store: EditorStore, onOpen: (() -> Unit)?) {
    val bg = store.composition?.background ?: listOf(0f, 0f, 0f, 1f)
    val transparent = bg.getOrElse(3) { 1f } < 0.5f
    val color = Color(bg.getOrElse(0) { 0f }, bg.getOrElse(1) { 0f }, bg.getOrElse(2) { 0f }, 1f)
    val hex = String.format(Locale.ROOT, "#%02X%02X%02X", (color.red * 255).roundToInt(), (color.green * 255).roundToInt(), (color.blue * 255).roundToInt())
    val value = if (transparent) stringResource(R.string.sh_bg_transparent) else hex
    val description = stringResource(R.string.om_exp_background_open)
    Row(
        Modifier.fillMaxWidth().padding(top = 14.dp).height(44.dp).testTag("export.settings.background")
            .semantics { contentDescription = description; stateDescription = value }
            .tocavel(enabled = onOpen != null, shrink = 1f) { onOpen?.invoke() },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(stringResource(R.string.om_exp_background), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.ExportText)))
        Box(Modifier.size(20.dp).clip(CircleShape).border(1.dp, AureaColors.ExportLine, CircleShape)
            .then(if (transparent) Modifier.background(AureaColors.ExportPage) else Modifier.background(color)))
        Spacer(Modifier.width(8.dp))
        Text(ltrPlain(value), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.ExportHint)))
        if (onOpen != null) { Spacer(Modifier.width(6.dp)); CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.ExportAction) }
    }
}

/** "Avançado" (só vídeo, recolhido): codificador, taxa de bits, trecho e o resumo de hoje. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Advanced(store: EditorStore, opts: ExportOptions, compW: Int, compH: Int, compFps: Double, onChange: (ExportOptions) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().padding(top = 16.dp).height(44.dp).testTag("export.settings.advanced")
            .semantics { role = Role.Button }.tocavel(shrink = 1f) { open = !open },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(stringResource(R.string.exp2_advanced), modifier = Modifier.weight(1f),
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.ExportText)))
        CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 16.dp, AureaColors.ExportAction)
    }
    if (!open) return
    SettingLabel(stringResource(R.string.export_encoder))
    val encoders = listOf(
        "0" to stringResource(R.string.export_encoder_auto),
        "1" to stringResource(R.string.export_encoder_compatible),
        "2" to stringResource(R.string.export_encoder_software),
    )
    Segmented(encoders, opts.encoder.coerceIn(0, 2).toString(), "export.settings.encoder") {
        val chosen = it.toInt()
        onChange(opts.copy(encoder = chosen, hevc = opts.hevc && chosen == 0))
    }
    Hint(stringResource(when (opts.encoder) {
        1 -> R.string.export_encoder_compatible_note
        2 -> R.string.export_encoder_software_note
        else -> R.string.export_encoder_auto_note
    }))
    SettingLabel(stringResource(R.string.exp2_bitrate))
    val auto = stringResource(R.string.exp2_bitrate_auto)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        (listOf(0) + CustomBitrates).forEach { mbps ->
            val on = opts.customMbps == mbps
            Text(
                if (mbps == 0) auto else mbps.toString(),
                modifier = Modifier.clip(RoundedCornerShape(5.dp)).background(if (on) AureaColors.ExportPrimary else AureaColors.ExportPage)
                    .semantics { selected = on; role = Role.RadioButton }.testTag("export.settings.bitrate.$mbps")
                    .tocavel(shrink = 1f) { onChange(opts.copy(customMbps = mbps)) }.padding(horizontal = 14.dp, vertical = 10.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.ExportOnPrimary else AureaColors.ExportText)),
            )
        }
    }
    RangeRow(opts, onChange)
    val (w, h) = VideoExportRules.frameSize(compW, compH, opts.shortSide)
    val fps = if (opts.fps > 0) opts.fps else compFps
    val seconds = if (compFps > 0) store.exportDuration(opts.trimToContent) / compFps else 0.0
    val mbps = store.exporter.estimatedMbps(w, h, fps, opts)
    Spacer(Modifier.height(10.dp))
    SummaryRow(stringResource(R.string.editor_video), listOf("$w × $h", "${fmt(fps)} fps", if (opts.hevc) "HEVC" else "H.264").joinToString(" · ") { ltrPlain(it) })
    SummaryRow(stringResource(R.string.editor_duracao), fmtTime(seconds))
    if (seconds > EXPORT_LONG_SECONDS) {
        Hint(if (seconds > EXPORT_MAX_SECONDS) stringResource(R.string.exp_too_long_3h) else stringResource(R.string.exp_long_duration, fmtTime(seconds)), danger = true)
    }
    SummaryRow(stringResource(R.string.exp2_bitrate), "${fmtMbps(mbps)} Mbps")
    SummaryRow(stringResource(R.string.editor_cor), stringResource(R.string.editor_sdr_bt_709))
}

@Composable
private fun SummaryRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.ExportHint)), modifier = Modifier.weight(1f))
        Text(value, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.ExportText, fontFeatureSettings = "tnum")))
    }
}

@Composable
private fun Notice(text: String, danger: Boolean) {
    if (text.isBlank()) return
    Text(
        text,
        style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = if (danger) AureaColors.LightDanger else AureaColors.ExportText)),
        modifier = Modifier.padding(start = 10.dp, end = 10.dp, top = 10.dp).fillMaxWidth().clip(RoundedCornerShape(6.dp))
            .background(AureaColors.ExportCard).padding(14.dp).testTag("export.notice"),
    )
}

/** O botão "Exportar": faixa branca na base, azul-marinho com texto de destaque de 20 sp. */
@Composable
private fun ExportButton(enabled: Boolean, onClick: () -> Unit) {
    val label = stringResource(R.string.om_exp_go)
    Box(Modifier.fillMaxWidth().background(AureaColors.ExportCard).padding(horizontal = 15.dp, vertical = 15.dp)) {
        Box(
            Modifier
                .fillMaxWidth()
                .shadow(3.dp, RoundedCornerShape(5.dp))
                .clip(RoundedCornerShape(5.dp))
                .background(AureaColors.ExportPrimary)
                .semantics { contentDescription = label; role = Role.Button }
                .testTag("export.go")
                .tocavel(enabled = enabled, onClick = onClick)
                .padding(12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 20.sp, lineHeight = 24.sp, fontWeight = FontWeight.W600,
                color = if (enabled) AureaColors.ExportOnPrimary else AureaColors.ExportAction)))
        }
    }
}

// =============================================================================
// Progresso (P0-10)
// =============================================================================

/**
 * O painel sobre véu de 50 % (a página continua por baixo): mensagem 18 sp,
 * barra de 220 × 7, % à esquerda e tempo restante à direita, a caixa de aviso
 * do motor 2.0 (retomada, finalizando, validando, modo seguro, software,
 * térmico), "Cancelar" em destaque e o rodapé "não saia do app".
 */
@Composable
private fun Progress(st: com.aurea.aurea.state.ExportUiState, format: ExportFormat, onCancel: () -> Unit) {
    val publishing = st.phase == ExportPhase.Publishing
    // Tempo restante da referência: só depois de 5 s e só quando a estimativa
    // muda menos de 40 s da anterior (estabilizou). Constantes de interface.
    var startedMs by remember { mutableLongStateOf(0L) }
    var lastEta by remember { mutableIntStateOf(-1) }
    var shownEta by remember { mutableIntStateOf(-1) }
    LaunchedEffect(st.preparing) { if (!st.preparing && startedMs == 0L) startedMs = System.currentTimeMillis() }
    LaunchedEffect(st.etaSeconds) {
        val eta = st.etaSeconds
        if (eta <= 0 || startedMs == 0L || System.currentTimeMillis() - startedMs < 5_000) return@LaunchedEffect
        if (lastEta >= 0 && abs(eta - lastEta) < 40) shownEta = eta
        lastEta = eta
    }
    val title = stringResource(when {
        st.preparing -> R.string.msg_preparando
        publishing -> R.string.editor_salvando_galeria
        else -> when (format) {
            ExportFormat.Video -> R.string.exp2_rendering
            ExportFormat.Frame -> R.string.exp2_rendering_frame
            ExportFormat.Sequence -> R.string.exp2_rendering_sequence
            ExportFormat.Gif -> R.string.exp2_rendering_gif
        }
    })
    val percent = if (publishing) 100 else (st.fraction * 100).roundToInt()
    Box(
        Modifier.fillMaxSize().background(AureaColors.ExportScrim).pointerInput(Unit) { detectTapGestures { } }.testTag("export.progress"),
    ) {
        Column(Modifier.align(Alignment.Center).padding(horizontal = 15.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, textAlign = TextAlign.Center, style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, color = AureaColors.OnImage)))
            Spacer(Modifier.height(15.dp))
            Box(Modifier.width(220.dp).height(7.dp).clip(RoundedCornerShape(3.5.dp)).background(if (st.preparing) Color.Transparent else AureaColors.AddPanelDeep)) {
                if (!st.preparing) Box(Modifier.fillMaxHeight().fillMaxWidth(if (publishing) 1f else st.fraction).background(AureaColors.Accent))
            }
            Row(Modifier.width(220.dp).padding(top = 6.dp)) {
                Text(if (st.preparing) "" else "$percent%", modifier = Modifier.weight(1f).testTag("export.progress.percent"),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.OnImage, fontFeatureSettings = "tnum")))
                val eta = shownEta
                if (!publishing && !st.preparing && eta > 0) {
                    Text(
                        if (eta < 60) stringResource(R.string.om_exp_eta_less)
                        else pluralStringResource(R.plurals.om_exp_eta_minutes, ceil(eta / 60.0).toInt(), ceil(eta / 60.0).toInt()),
                        modifier = Modifier.testTag("export.progress.eta"),
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.OnImage)),
                    )
                }
            }
            if (!st.preparing && st.notice.isNotEmpty()) {
                Text(
                    st.notice, modifier = Modifier.padding(top = 14.dp).width(260.dp).clip(RoundedCornerShape(6.dp))
                        .background(AureaColors.AddPanelDeep).padding(10.dp).testTag("export.progress.notice"),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)),
                )
            }
        }
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().navigationBarsPadding().padding(start = 30.dp, end = 30.dp, bottom = 30.dp),
            horizontalAlignment = Alignment.CenterHorizontally) {
            if (!publishing) {
                Text(
                    stringResource(R.string.editor_cancelar),
                    modifier = Modifier.testTag("export.progress.cancel").tocavel(onClick = onCancel).padding(horizontal = 16.dp, vertical = 10.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
                )
                Spacer(Modifier.height(30.dp))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, 14.dp, AureaColors.OnImage70)
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.editor_mantenha_aurea_aberto_ate_terminar),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.OnImage70)))
            }
        }
    }
}

// =============================================================================
// Resultado (P0-11)
// =============================================================================

/**
 * A página azul-marinho do resultado: a prévia (70 %) — vídeo com ▶ de 62 dp e
 * barra de busca, GIF animado, PNG, a 1ª imagem da sequência — e a base
 * (30 %) com Salvar | Compartilhar (57 dp), Fechar e o cartão de apoio.
 * O AUREA já salvou sozinho: o Salvar diz "Salvo na galeria ✓" e abre
 * "Salvar em…"; se a galeria recusou, volta a ser "Salvar" e tenta de novo.
 */
@Composable
private fun ResultPage(store: EditorStore, item: ExportItem, opts: ExportOptions, compW: Int, compH: Int, compFps: Double, onClose: () -> Unit) {
    val exporter = store.exporter
    val st = exporter.state
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val uri = st.outputUri
    val savedLabel = stringResource(R.string.om_exp_saved_gallery)
    val saveAs = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument(st.format.mime)) { target ->
        if (target != null) scope.launch {
            val ok = exporter.saveCopy(target)
            store.showToast(if (ok) savedLabel else context.getString(R.string.app_export_gallery_failed))
        }
    }
    val sequenceFrames = remember(st.format) {
        if (st.format == ExportFormat.Sequence) exporter.imagePlan(opts, compW, compH, compFps, store.exportDuration(opts.trimToContent)).frames else 0
    }
    Column(Modifier.fillMaxSize().background(AureaColors.ExportResult).statusBarsPadding().navigationBarsPadding().testTag("export.result")) {
        Box(Modifier.fillMaxWidth().height(42.dp), contentAlignment = Alignment.Center) {
            Text(stringResource(when (st.format) {
                ExportFormat.Video -> R.string.editor_video_pronto
                ExportFormat.Frame -> R.string.exp2_done_frame
                ExportFormat.Sequence -> R.string.exp2_done_sequence
                ExportFormat.Gif -> R.string.exp2_done_gif
            }), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700, color = AureaColors.OnImage)))
        }
        Box(Modifier.weight(0.7f).fillMaxWidth().background(AureaColors.AddPanelDeep)) {
            if (uri != null) when (st.format) {
                ExportFormat.Video -> ResultVideo(uri, Modifier.fillMaxSize())
                ExportFormat.Gif -> ResultGif(uri, Modifier.fillMaxSize())
                ExportFormat.Frame -> ResultImage(uri, zip = false, Modifier.fillMaxSize())
                ExportFormat.Sequence -> Box(Modifier.fillMaxSize()) {
                    ResultImage(uri, zip = true, Modifier.fillMaxSize())
                    if (sequenceFrames > 0) Text(
                        stringResource(R.string.om_exp_sequence_first, sequenceFrames),
                        modifier = Modifier.align(Alignment.BottomCenter).padding(10.dp),
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.OnImage)),
                    )
                }
            }
        }
        Column(Modifier.weight(0.3f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(20.dp)) {
            if (st.message.isNotBlank()) {
                Text(st.message, maxLines = 4, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(bottom = 10.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.OnImage70)))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                val saved = st.savedToGallery
                ResultButton(
                    if (saved) savedLabel else stringResource(R.string.om_exp_save),
                    if (saved) CupertinoGlyph.CheckmarkCircleFill else CupertinoGlyph.ArrowDownToLine,
                    "export.result.save", Modifier.weight(1f),
                ) {
                    if (saved) saveAs.launch(exporter.suggestedName()) else exporter.retryGallery()
                }
                ResultButton(stringResource(R.string.editor_compartilhar), CupertinoGlyph.SquareArrowUp, "export.result.share", Modifier.weight(1f)) {
                    exporter.shareIntent()?.let { runCatching { context.startActivity(it) } }
                }
            }
            Spacer(Modifier.height(15.dp))
            Box(
                Modifier.fillMaxWidth().height(57.dp).testTag("export.result.close").tocavel(onClick = onClose),
                contentAlignment = Alignment.Center,
            ) {
                Text(stringResource(R.string.om_exp_close), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
            }
            Spacer(Modifier.height(12.dp))
            DonationCard(exporting = false)
        }
    }
}

@Composable
private fun ResultButton(label: String, glyph: Char, tag: String, modifier: Modifier, onClick: () -> Unit) {
    Row(
        modifier.height(57.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.ExportResultButton)
            .semantics(mergeDescendants = true) { role = Role.Button }.testTag(tag).tocavel(onClick = onClick).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(glyph, 20.dp, AureaColors.OnImage)
        Spacer(Modifier.width(8.dp))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.OnImage)))
    }
}

/** O vídeo pronto (VideoView da plataforma) com a barra de 62 dp: ▶ de 62 e a busca. */
@Composable
private fun ResultVideo(uri: Uri, modifier: Modifier) {
    var view by remember { mutableStateOf<VideoView?>(null) }
    var playing by remember { mutableStateOf(false) }
    var position by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(playing) {
        val v = view ?: return@LaunchedEffect
        if (playing) runCatching { v.start() } else runCatching { v.pause() }
        while (playing) {
            delay(100)
            val d = runCatching { v.duration }.getOrDefault(0)
            position = if (d > 0) runCatching { v.currentPosition.toFloat() / d }.getOrDefault(0f) else 0f
            if (runCatching { !v.isPlaying }.getOrDefault(true)) playing = false
        }
    }
    DisposableEffect(Unit) { onDispose { runCatching { view?.stopPlayback() } } }
    val playLabel = stringResource(if (playing) R.string.editor_pausar else R.string.editor_reproduzir)
    val seekLabel = stringResource(R.string.om_exp_seek)
    Column(modifier) {
        Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
            AndroidView(factory = { ctx -> VideoView(ctx).also { v -> v.setVideoURI(uri); v.setOnPreparedListener { v.seekTo(1) }; view = v } },
                modifier = Modifier.fillMaxSize())
        }
        Row(Modifier.fillMaxWidth().height(62.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.size(62.dp).background(AureaColors.AddPanelDeep).testTag("export.result.play")
                    .semantics { contentDescription = playLabel }.tocavel { playing = !playing },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(if (playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill, 24.dp, AureaColors.OnImage) }
            val track = AureaColors.ExportResultButton
            val fill = AureaColors.Accent
            BoxWithConstraints(Modifier.weight(1f).fillMaxHeight().padding(horizontal = 14.dp).testTag("export.result.seek")
                .semantics { contentDescription = seekLabel; stateDescription = "${(position * 100).roundToInt()}%" }) {
                val widthPx = constraints.maxWidth.toFloat().coerceAtLeast(1f)
                fun seek(x: Float) {
                    val v = view ?: return
                    val f = (x / widthPx).coerceIn(0f, 1f)
                    position = f
                    runCatching { v.seekTo((v.duration * f).toInt()) }
                }
                Canvas(Modifier.fillMaxSize()
                    .pointerInput(widthPx) { detectTapGestures { seek(it.x) } }
                    .pointerInput(widthPx) { detectHorizontalDragGestures { change, _ -> change.consume(); seek(change.position.x) } }) {
                    val y = size.height / 2f
                    drawLine(track, Offset(0f, y), Offset(size.width, y), 3.dp.toPx())
                    drawLine(fill, Offset(0f, y), Offset(size.width * position, y), 3.dp.toPx())
                    drawCircle(fill, 7.dp.toPx(), Offset(size.width * position, y))
                }
            }
        }
    }
}

/** GIF animado (ImageDecoder no Android 9+; a imagem parada antes). */
@Composable
private fun ResultGif(uri: Uri, modifier: Modifier) {
    AndroidView(
        factory = { ctx ->
            ImageView(ctx).apply {
                scaleType = ImageView.ScaleType.FIT_CENTER
                runCatching {
                    if (Build.VERSION.SDK_INT >= 28) {
                        val drawable = ImageDecoder.decodeDrawable(ImageDecoder.createSource(ctx.contentResolver, uri))
                        setImageDrawable(drawable)
                        (drawable as? AnimatedImageDrawable)?.start()
                    } else setImageURI(uri)
                }
            }
        },
        modifier = modifier,
    )
}

/** PNG pronto, ou a 1ª imagem do .zip da sequência. */
@Composable
private fun ResultImage(uri: Uri, zip: Boolean, modifier: Modifier) {
    val context = LocalContext.current
    val bitmap by produceState<Bitmap?>(null, uri) {
        value = withContext(Dispatchers.IO) {
            runCatching {
                val options = BitmapFactory.Options().apply { inSampleSize = 2 }
                context.contentResolver.openInputStream(uri)?.use { input ->
                    if (!zip) BitmapFactory.decodeStream(input, null, options)
                    else ZipInputStream(input).use { z ->
                        var entry = z.nextEntry
                        var found: Bitmap? = null
                        while (entry != null && found == null) {
                            if (!entry.isDirectory && entry.name.lowercase(Locale.ROOT).endsWith(".png")) found = BitmapFactory.decodeStream(z, null, options)
                            entry = z.nextEntry
                        }
                        found
                    }
                }
            }.getOrNull()
        }
    }
    Box(modifier, contentAlignment = Alignment.Center) {
        bitmap?.let { Image(it.asImageBitmap(), null, Modifier.fillMaxSize().padding(10.dp), contentScale = ContentScale.Fit) }
            ?: CircularProgressIndicator(Modifier.size(28.dp), color = AureaColors.Accent)
    }
}

// =============================================================================
// Formatos de número
// =============================================================================

private fun fmt(v: Double): String =
    if (abs(v - v.roundToInt()) < 0.0005) v.roundToInt().toString()
    else String.format(Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.').replace('.', ',')

private fun fmtMbps(mbps: Double): String =
    if (mbps >= 10) mbps.roundToInt().toString() else String.format(Locale.ROOT, "%.1f", mbps).replace('.', ',')

@Composable
private fun fmtSize(bytes: Long): String {
    val mb = bytes / 1e6
    return if (mb >= 1000) stringResource(R.string.unit_gigabyte, fmt((mb / 100.0).roundToInt() / 10.0))
    else stringResource(R.string.unit_megabyte, max(1, mb.roundToInt()))
}

private fun fmtTime(seconds: Double): String {
    val s = seconds.roundToInt().coerceAtLeast(0)
    return if (s >= 3600) String.format(Locale.ROOT, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
    else String.format(Locale.ROOT, "%d:%02d", s / 60, s % 60)
}

/** Acima disso a tela avisa da duração; acima do teto o motor recusa (EngineConfig::maxExportSeconds). */
private const val EXPORT_LONG_SECONDS = 30.0 * 60.0
internal const val EXPORT_MAX_SECONDS = 3.0 * 3600.0
