package com.aurea.aurea.editor

import com.aurea.aurea.ui.i18n.ltrPlain
import android.content.ActivityNotFoundException
import android.graphics.Bitmap
import android.provider.Settings
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.home.DonationCard
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportFormat
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.state.ImageExportRules
import com.aurea.aurea.state.VideoExportRules
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Lado menor → rótulo da ficha (o mesmo do iOS). */
private val Resolutions = listOf(480 to "480p", 720 to "720p", 1080 to "1080p", 1440 to "2K", 2160 to "4K")
/** Atalhos de taxa do vídeo; "Personalizado…" digita qualquer uma de 1 a 240 (o teto dos encoders). */
private val FrameRates = listOf(24.0, 25.0, 30.0, 50.0, 60.0, 120.0)
/** Mbps manuais do Avançado (0 = automático pela qualidade). */
private val CustomBitrates = listOf(5, 10, 15, 25, 40, 60)

/**
 * A tela Exportar (rota "fullscreenDialog" da A.01, `topo-exportar`): prévia
 * no topo, escolhas agrupadas (Resolução, Taxa de quadros, Qualidade com o
 * tamanho estimado, Formato) e o resto em "Avançado". Durante o render, o
 * animador em pixel art, o progresso e frases que trocam a cada 3 s. O motor
 * renderiza com o MESMO renderer do preview; o preview congela até terminar.
 * Espelho: engine/platform/ios/app/ExportView.swift.
 */
@Composable
internal fun ExportScreen(store: EditorStore, onDismiss: () -> Unit) {
    val exporter = store.exporter
    val st = exporter.state
    val comp = store.composition
    val compW = comp?.width ?: store.project.width
    val compH = comp?.height ?: store.project.height
    val compFps = comp?.fps ?: store.project.fps.toDouble()

    var options by remember { mutableStateOf(ExportOptions(shortSide = min(1080, max(720, min(compW, compH))))) }
    val seconds = if (compFps > 0) store.exportDuration(options.trimToContent) / compFps else 0.0
    var preview by remember { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(store) {
        exporter.reset()
        var captured: Bitmap? = null
        try {
            withContext(Dispatchers.IO) { captured = runCatching { store.captureBitmap(640) }.getOrNull() }
            preview = captured
            captured = null
        } finally {
            // A dismissed/recreated screen may cancel while the native readback completes.
            captured?.recycle()
        }
    }
    DisposableEffect(Unit) { onDispose { preview?.recycle() } }

    val close = {
        if (!exporter.busy) {
            exporter.reset()
            onDismiss()
        }
    }
    Dialog(
        onDismissRequest = close,
        properties = DialogProperties(usePlatformDefaultWidth = false, dismissOnClickOutside = false, decorFitsSystemWindows = false),
    ) {
        Column(
            Modifier
                .fillMaxSize()
                .background(AureaColors.Background)
                .statusBarsPadding()
                .navigationBarsPadding(),
        ) {
            ExportTopBar(canClose = !exporter.busy, onClose = close)
            Column(
                Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 18.dp),
            ) {
                when (st.phase) {
                    ExportPhase.Running, ExportPhase.Publishing -> Rendering(
                        st.fraction, st.framesDone, st.framesTotal, st.fps, st.etaSeconds,
                        publishing = st.phase == ExportPhase.Publishing, notice = st.notice,
                        title = stringResource(when (st.format) {
                            ExportFormat.Video -> R.string.exp2_rendering
                            ExportFormat.Frame -> R.string.exp2_rendering_frame
                            ExportFormat.Sequence -> R.string.exp2_rendering_sequence
                            ExportFormat.Gif -> R.string.exp2_rendering_gif
                        }),
                    )
                    ExportPhase.Done -> {
                        PreviewCard(preview, compW, compH)
                        Spacer(Modifier.height(18.dp))
                        Done(st.message, stringResource(when (st.format) {
                            ExportFormat.Video -> R.string.editor_video_pronto
                            ExportFormat.Frame -> R.string.exp2_done_frame
                            ExportFormat.Sequence -> R.string.exp2_done_sequence
                            ExportFormat.Gif -> R.string.exp2_done_gif
                        }))
                    }
                    else -> {
                        PreviewCard(preview, compW, compH)
                        Spacer(Modifier.height(10.dp))
                        if (st.phase != ExportPhase.Idle) {
                            Notice(st.message, danger = st.phase == ExportPhase.Failed)
                            Spacer(Modifier.height(4.dp))
                        }
                        Options(store, options, compW, compH, compFps, seconds) { options = it }
                    }
                }
                Spacer(Modifier.height(16.dp))
                DonationCard(exporting = exporter.busy)
                Spacer(Modifier.height(24.dp))
            }
            BottomAction(store, options, compW, compH, compFps)
        }
    }
}

@Composable
private fun ExportTopBar(canClose: Boolean, onClose: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(40.dp).tocavel(enabled = canClose, onClick = onClose),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, if (canClose) AureaColors.Text else AureaColors.Disabled)
        }
        Text(stringResource(R.string.editor_exportar), style = AureaType.TitleMedium, modifier = Modifier.padding(start = 4.dp))
    }
}

@Composable
private fun PreviewCard(bmp: Bitmap?, w: Int, h: Int) {
    val ratio = if (h > 0) w.toFloat() / h else 16f / 9f
    val previewLabel = stringResource(R.string.editor_previa)
    Box(
        Modifier
            .fillMaxWidth()
            .heightIn(max = 220.dp)
            .padding(top = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .aspectRatio(ratio, matchHeightConstraintsFirst = ratio < 1f)
                .clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Stage)
                .border(1.dp, AureaColors.Border, RoundedCornerShape(12.dp))
                .semantics { contentDescription = previewLabel },
            contentAlignment = Alignment.Center,
        ) {
            if (bmp != null) {
                Image(bmp.asImageBitmap(), contentDescription = null, contentScale = ContentScale.Fit,
                      modifier = Modifier.fillMaxSize())
            } else {
                CupertinoIcon(CupertinoGlyph.Film, 34.dp, AureaColors.Muted)
            }
        }
    }
}

@Composable
private fun Options(
    store: EditorStore,
    options: ExportOptions,
    compW: Int,
    compH: Int,
    compFps: Double,
    seconds: Double,
    onChange: (ExportOptions) -> Unit,
) {
    val comp = store.composition
    val short = max(1, min(compW, compH))
    // A regra do motor (ExportRules.hpp): lado maior em múltiplo de 16.
    fun sizeFor(side: Int): Pair<Int, Int> = VideoExportRules.frameSize(compW, compH, side)
    // Tudo aparece (§109): o que o aparelho não exporta fica marcado e
    // desligado, com a frase do porquê embaixo — o motor recusaria, e o
    // usuário não descobre só depois de tocar.
    val neuralSide = short * options.aiUpscale
    val resolutions = if (options.aiUpscale > 0 && Resolutions.none { it.first == neuralSide })
        (Resolutions + (neuralSide to "${neuralSide}p (${options.aiUpscale}×)")).sortedBy { it.first } else Resolutions
    val available = resolutions.filter { (side, _) -> val (w, h) = sizeFor(side); comp?.fits(w, h) ?: true }
    val blocked = resolutions.filter { it !in available }.map { it.second }.toSet()
    val device = store.deviceReport
    val (w, h) = sizeFor(options.shortSide)
    val fps = if (options.fps > 0) options.fps else compFps
    val mbps = store.exporter.estimatedMbps(w, h, fps, options)
    val bytes = store.exporter.estimatedBytes(w, h, fps, seconds, options)
    val codecName = if (options.hevc) "HEVC" else "H.264"
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted))
    // PNG, sequência e GIF têm as próprias escolhas (o motor codifica).
    if (options.format != ExportFormat.Video) {
        ImageOptions(store, options, compW, compH, compFps, muted, onChange)
        return
    }

    // Linha-resumo logo abaixo da prévia: o que vai sair, de relance. Cada
    // peça isolada em LTR: em árabe a lista corre da direita, mas "30 fps" e
    // "1080 × 1920" não se embaralham com os vizinhos.
    Text(
        listOf("$w × $h", "${fmt(fps)} fps", codecName, fmtTime(seconds)).joinToString(" · ") { ltrPlain(it) },
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted, fontFeatureSettings = "tnum")),
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().padding(bottom = 6.dp),
    )

    FormatGroup(options, muted, onChange)

    Group(stringResource(R.string.editor_resolucao)) {
        Chips(resolutions.map { it.second }, available.firstOrNull { it.first == options.shortSide }?.second, blocked) { label ->
            onChange(options.copy(shortSide = available.first { it.second == label }.first))
        }
        if (blocked.isNotEmpty()) {
            Text(
                device?.exportLimitReason(androidx.compose.ui.platform.LocalContext.current) ?: stringResource(R.string.sh_export_above_device, blocked.joinToString()),
                style = muted, modifier = Modifier.padding(top = 8.dp),
            )
        }
    }

    Group(stringResource(R.string.editor_quadros_segundo)) {
        // A ficha "do projeto" é achada pelo texto inteiro (traduzido), não pelo começo.
        val fromProject = stringResource(R.string.sh_export_fps_from_project, fmt(compFps))
        val customLabel = stringResource(R.string.project_fps_custom)
        val customTitle = stringResource(R.string.project_fps_custom_title)
        var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
        val presets = FrameRates.filter { kotlin.math.abs(it - compFps) > 0.01 }
        // Taxa digitada fora dos atalhos: ganha a própria ficha (escolhida) e
        // "Personalizado…" continua ao lado para digitar outra.
        val typed = options.fps.takeIf { it > 0.0 && presets.none { p -> kotlin.math.abs(p - it) < 0.01 } }
        Chips(listOf(fromProject) + presets.map { fmt(it) } + listOfNotNull(typed?.let { fmt(it) }) + customLabel,
              if (options.fps == 0.0) fromProject else fmt(options.fps)) { label ->
            when (label) {
                fromProject -> onChange(options.copy(fps = 0.0))
                customLabel -> keypad = KeypadRequest(customTitle, fps.toFloat(), "fps", 1f, 240f, 3) { v ->
                    onChange(options.copy(fps = v.toDouble()))
                }
                else -> onChange(options.copy(fps = label.replace(',', '.').toDouble()))
            }
        }
        if (fps > 60.01) Text(stringResource(R.string.export_fps_high_note), style = muted, modifier = Modifier.padding(top = 8.dp))
        keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    }

    Group(stringResource(R.string.editor_qualidade)) {
        val labels = listOf(
            stringResource(R.string.exp2_quality_low),
            stringResource(R.string.exp2_quality_normal),
            stringResource(R.string.exp2_quality_high),
        )
        Chips(labels, if (options.customMbps > 0) null else labels[options.quality.coerceIn(0, 2)]) { label ->
            onChange(options.copy(quality = labels.indexOf(label).coerceAtLeast(0), customMbps = 0))
        }
        Text(
            stringResource(R.string.exp2_estimated_line, fmtSize(bytes), fmtMbps(mbps)),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
            modifier = Modifier.padding(top = 10.dp),
        )
    }

    Advanced {
        Label(stringResource(R.string.exp2_codec))
        val hevcOk = device?.hevcExportAvailable ?: true
        Chips(listOf("H.264", "HEVC"), codecName, if (hevcOk) emptySet() else setOf("HEVC")) {
            onChange(options.copy(hevc = it == "HEVC"))
        }
        Text(
            if (!hevcOk) device?.hevcExportReason(androidx.compose.ui.platform.LocalContext.current) ?: ""
            else if (options.hevc) stringResource(R.string.editor_hevc_arquivo_menor_mesma_qualidade_alguns)
            else stringResource(R.string.editor_h_264_abre_qualquer_aparelho_rede),
            style = muted, modifier = Modifier.padding(top = 6.dp),
        )

        Label(stringResource(R.string.exp2_bitrate))
        val auto = stringResource(R.string.exp2_bitrate_auto)
        Chips(listOf(auto) + CustomBitrates.map { it.toString() },
              if (options.customMbps > 0) options.customMbps.toString() else auto) { label ->
            onChange(options.copy(customMbps = if (label == auto) 0 else label.toInt()))
        }

        Label(stringResource(R.string.exp2_range))
        val contentRange = stringResource(R.string.export_range_content)
        val fullRange = stringResource(R.string.export_range_full)
        Chips(listOf(contentRange, fullRange), if (options.trimToContent) contentRange else fullRange) {
            onChange(options.copy(trimToContent = it == contentRange))
        }

        Label(stringResource(R.string.ai_upscale_title))
        val off = stringResource(R.string.common_off)
        Chips(listOf(off, "2×", "4×"), if (options.aiUpscale == 0) off else "${options.aiUpscale}×") { label ->
            val factor = when (label) { "2×" -> 2; "4×" -> 4; else -> 0 }
            onChange(options.copy(aiUpscale = factor, shortSide = if (factor > 0) short * factor else min(1080, max(720, short))))
        }
        if (options.aiUpscale > 0) {
            Text(stringResource(R.string.ai_upscale_note), style = muted, modifier = Modifier.padding(top = 8.dp))
            val factor = options.aiUpscale
            Text(stringResource(R.string.ai_upscale_dimensions, factor, ((w + factor * 2 - 1) / (factor * 2)) * 2,
                ((h + factor * 2 - 1) / (factor * 2)) * 2, w, h), style = muted, modifier = Modifier.padding(top = 6.dp))
        }

        Spacer(Modifier.height(12.dp))
        SummaryRow(stringResource(R.string.editor_video), listOf("$w × $h", "${fmt(fps)} fps", codecName).joinToString(" · ") { ltrPlain(it) })
        SummaryRow(stringResource(R.string.editor_duracao), fmtTime(seconds))
        SummaryRow(stringResource(R.string.editor_tamanho_estimado), fmtSize(bytes))
        SummaryRow(stringResource(R.string.editor_cor), stringResource(R.string.editor_sdr_bt_709))
    }
}

/** Formato: Vídeo (MP4) | Quadro atual (PNG) | Sequência PNG (.zip) | GIF, com a frase de cada um. */
@Composable
private fun FormatGroup(options: ExportOptions, muted: TextStyle, onChange: (ExportOptions) -> Unit) {
    val formats = ExportFormat.entries
    val labels = formats.map {
        stringResource(when (it) {
            ExportFormat.Video -> R.string.exp2_format_video
            ExportFormat.Frame -> R.string.exp2_format_frame
            ExportFormat.Sequence -> R.string.exp2_format_sequence
            ExportFormat.Gif -> R.string.exp2_format_gif
        })
    }
    Group(stringResource(R.string.editor_formato)) {
        Chips(labels, labels[options.format.ordinal]) { label ->
            onChange(options.copy(format = formats[labels.indexOf(label).coerceAtLeast(0)]))
        }
        Text(
            stringResource(when (options.format) {
                ExportFormat.Video -> R.string.exp2_format_note
                ExportFormat.Frame -> R.string.exp2_format_note_frame
                ExportFormat.Sequence -> R.string.exp2_format_note_sequence
                ExportFormat.Gif -> R.string.exp2_format_note_gif
            }),
            style = muted, modifier = Modifier.padding(top = 8.dp),
        )
    }
}

/**
 * Escolhas do export como imagem. As dimensões, os quadros e o tamanho
 * estimado vêm do MOTOR (plan_image_export) — a tela mostra o que sai.
 */
@Composable
private fun ImageOptions(
    store: EditorStore,
    options: ExportOptions,
    compW: Int,
    compH: Int,
    compFps: Double,
    muted: TextStyle,
    onChange: (ExportOptions) -> Unit,
) {
    val format = options.format
    val plan = store.exporter.imagePlan(options, compW, compH, compFps, store.exportDuration(options.trimToContent))
    val seconds = if (plan.fps > 0) plan.frames / plan.fps else 0.0
    val framesText = stringResource(R.string.exp2_frames_count, plan.frames)
    Text(
        if (format == ExportFormat.Frame) "${ltrPlain("${plan.width} × ${plan.height}")} · PNG"
        // A contagem de quadros é frase do idioma (FSI: segue a direção dela).
        else listOf(ltrPlain("${plan.width} × ${plan.height}"), ltrPlain("${fmt(plan.fps)} fps"), "\u2068$framesText\u2069", ltrPlain(fmtTime(seconds))).joinToString(" · "),
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted, fontFeatureSettings = "tnum")),
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().padding(bottom = 6.dp),
    )
    FormatGroup(options, muted, onChange)

    if (format == ExportFormat.Gif) {
        Group(stringResource(R.string.exp2_gif_width)) {
            val labels = ImageExportRules.GifWidths.map { "$it px" }
            Chips(labels, "${options.gifWidth} px") { label -> onChange(options.copy(gifWidth = label.removeSuffix(" px").toInt())) }
        }
        Group(stringResource(R.string.editor_quadros_segundo)) {
            Chips(ImageExportRules.GifFrameRates.map { fmt(it) }, fmt(options.gifFps)) { label ->
                onChange(options.copy(gifFps = label.replace(',', '.').toDouble()))
            }
        }
    } else {
        Group(stringResource(R.string.editor_resolucao)) {
            // "Original" = a resolução cheia da composição (o padrão do PNG).
            val short = max(1, min(compW, compH))
            val choices = listOf(0 to "${stringResource(R.string.exp2_resolution_original)} (${ltrPlain("$compW × $compH")})") +
                Resolutions.filter { it.first != short }
            Chips(choices.map { it.second }, (choices.firstOrNull { it.first == options.imageShortSide } ?: choices[0]).second) { label ->
                onChange(options.copy(imageShortSide = choices.first { it.second == label }.first))
            }
        }
        if (format == ExportFormat.Sequence) {
            Group(stringResource(R.string.editor_quadros_segundo)) {
                val fromProject = stringResource(R.string.sh_export_fps_from_project, fmt(compFps))
                Chips(listOf(fromProject) + FrameRates.filter { kotlin.math.abs(it - compFps) > 0.01 }.map { fmt(it) },
                      if (options.fps == 0.0) fromProject else fmt(options.fps)) { label ->
                    onChange(options.copy(fps = if (label == fromProject) 0.0 else label.replace(',', '.').toDouble()))
                }
            }
        }
    }
    if (format != ExportFormat.Frame) {
        Group(stringResource(R.string.exp2_range)) {
            val contentRange = stringResource(R.string.export_range_content)
            val fullRange = stringResource(R.string.export_range_full)
            Chips(listOf(contentRange, fullRange), if (options.trimToContent) contentRange else fullRange) {
                onChange(options.copy(trimToContent = it == contentRange))
            }
        }
    }
    Group(stringResource(R.string.exp2_summary)) {
        Text(
            stringResource(R.string.exp2_estimated_size, fmtSize(plan.bytes)),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
        )
        if (format != ExportFormat.Frame) {
            SummaryRow(stringResource(R.string.editor_duracao), fmtTime(seconds))
        }
        if (ImageExportRules.tooLong(format, plan.frames)) {
            Text(stringResource(R.string.exp2_too_long), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger)),
                 modifier = Modifier.padding(top = 8.dp))
        }
    }
}

/** Um grupo de escolhas: título pequeno e o conteúdo num cartão. */
@Composable
private fun Group(title: String, content: @Composable ColumnScope.() -> Unit) {
    Text(
        title.uppercase(Locale.ROOT),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, letterSpacing = 0.6.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        modifier = Modifier.padding(top = 16.dp, bottom = 8.dp, start = 2.dp),
    )
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(AureaColors.Surface)
            .padding(14.dp),
        content = content,
    )
}

/** "Avançado": fechado por padrão; tudo o que não é do dia a dia mora aqui. */
@Composable
private fun Advanced(content: @Composable ColumnScope.() -> Unit) {
    var open by remember { mutableStateOf(false) }
    Spacer(Modifier.height(16.dp))
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(AureaColors.Surface)) {
        Row(
            Modifier
                .fillMaxWidth()
                .semantics { role = Role.Button }
                .tocavel(shrink = 1f) { open = !open }
                .padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                stringResource(R.string.exp2_advanced),
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)),
                modifier = Modifier.weight(1f),
            )
            CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 16.dp, AureaColors.Muted)
        }
        if (open) Column(Modifier.fillMaxWidth().padding(start = 14.dp, end = 14.dp, bottom = 14.dp), content = content)
    }
}

@Composable
private fun Label(text: String) {
    Text(
        text,
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        modifier = Modifier.padding(top = 12.dp, bottom = 8.dp),
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Chips(options: List<String>, current: String?, disabled: Set<String> = emptySet(), onPick: (String) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEach { o ->
            val on = o == current
            val off = o in disabled
            Text(
                o,
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = 14.sp, fontWeight = FontWeight.W600,
                        color = if (off) AureaColors.Disabled else if (on) AureaColors.Accent else AureaColors.Text,
                    ),
                ),
                modifier = Modifier
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (on) AureaColors.ActionDim else AureaColors.Chip)
                    .then(if (on) Modifier.border(1.dp, AureaColors.Brand, RoundedCornerShape(10.dp)) else Modifier)
                    .semantics { selected = on; role = Role.RadioButton }
                    .tocavel(enabled = !off, shrink = 1f) { if (!on && !off) onPick(o) }
                    .padding(horizontal = 14.dp, vertical = 10.dp),
            )
        }
    }
}

@Composable
private fun SummaryRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)), modifier = Modifier.weight(1f))
        Text(value, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontFeatureSettings = "tnum")))
    }
}

@Composable
private fun Notice(text: String, danger: Boolean) {
    Text(
        text,
        style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = if (danger) AureaColors.Danger else AureaColors.Warning)),
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(AureaColors.Surface)
            .padding(14.dp),
    )
}

/** Render em andamento: animador, porcentagem, barra, ETA e a frase da vez. */
@Composable
private fun Rendering(fraction: Float, done: Int, total: Int, fps: Float, eta: Int, publishing: Boolean, notice: String, title: String) {
    val context = LocalContext.current
    // "Remover animações" do sistema: o animador fica parado no 1º quadro.
    val reduceMotion = remember {
        runCatching { Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f }
            .getOrDefault(false)
    }
    var frame by remember { mutableIntStateOf(0) }
    var phrase by remember { mutableIntStateOf(0) }
    if (!reduceMotion) LaunchedEffect(Unit) {
        while (true) { delay(PixelAnimator.FRAME_MS); frame = (frame + 1) % PixelAnimator.frames.size }
    }
    LaunchedEffect(Unit) {
        while (true) { delay(3000); phrase = (phrase + 1) % FunPhrases.size }
    }
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(20.dp))
        PixelAnimatorView(frame, Modifier.width(200.dp).aspectRatio(PixelAnimator.WIDTH.toFloat() / PixelAnimator.HEIGHT))
        Spacer(Modifier.height(18.dp))
        Text(
            if (publishing) stringResource(R.string.editor_salvando_galeria) else title,
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)),
        )
        Spacer(Modifier.height(6.dp))
        Text(
            if (publishing) "100%" else "${(fraction * 100).roundToInt()}%",
            style = AureaType.HeadlineLarge.merge(TextStyle(fontFeatureSettings = "tnum")),
        )
        Spacer(Modifier.height(12.dp))
        Box(Modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)).background(AureaColors.Chip)) {
            Box(
                Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(if (publishing) 1f else fraction.coerceIn(0f, 1f))
                    .clip(RoundedCornerShape(4.dp))
                    .background(AureaColors.Accent),
            )
        }
        if (!publishing) {
            Spacer(Modifier.height(10.dp))
            if (eta > 0) {
                Text(
                    stringResource(R.string.exp2_eta, fmtTime(eta.toDouble())),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
                )
                Spacer(Modifier.height(4.dp))
            }
            Text(
                // Com o "faltam" grande em cima, a linha de detalhe não repete o tempo.
                if (eta > 0) stringResource(R.string.exp2_frames, done, total, fmt(fps.toDouble()))
                else stringResource(R.string.sh_export_progress, done, total, fmt(fps.toDouble()), fmtTime(eta.toDouble())),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted, fontFeatureSettings = "tnum")),
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(16.dp))
            Text(
                stringResource(FunPhrases[phrase]),
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Accent, fontWeight = FontWeight.W500)),
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().heightIn(min = 22.dp),
            )
            Spacer(Modifier.height(10.dp))
            Text(
                stringResource(R.string.editor_mantenha_aurea_aberto_ate_terminar),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                textAlign = TextAlign.Center,
            )
            // Encoder de software ou aparelho quente: dito, não escondido.
            if (notice.isNotEmpty()) {
                Spacer(Modifier.height(12.dp))
                Notice(notice, danger = false)
            }
        }
    }
}

private val FunPhrases = listOf(
    R.string.exp2_fun_1, R.string.exp2_fun_2, R.string.exp2_fun_3, R.string.exp2_fun_4, R.string.exp2_fun_5,
    R.string.exp2_fun_6, R.string.exp2_fun_7, R.string.exp2_fun_8, R.string.exp2_fun_9, R.string.exp2_fun_10,
)

/**
 * O animador em pixel art: um bonequinho de boina na mesa, desenhando
 * quadros no papel. 4 quadros de 20×14 "pixels", um caractere por pixel
 * (paleta abaixo). Os MESMOS dados estão em ExportView.swift (PixelAnimator).
 */
private object PixelAnimator {
    const val WIDTH = 20
    const val HEIGHT = 14
    const val FRAME_MS = 180L
    val palette = mapOf(
        'K' to Color(0xFF1E1B2E), 'B' to Color(0xFFE5484D), 'H' to Color(0xFF6B4226), 'S' to Color(0xFFF6C9A0),
        'R' to Color(0xFFE86A8A), 'T' to Color(0xFF4C7DFF), 'D' to Color(0xFF9A6232), 'P' to Color(0xFFF4F1E8),
        'Y' to Color(0xFFFFC53D),
    )
    private val head = listOf(
        "....................",
        "...BBBBB............",
        "..BBBBBBB...........",
        "..KHHHHHK...........",
        "..KSSSSSK...........",
    )
    private const val EYES = "..KSKSKSK..........."
    private const val BLINK = "..KSSSSSK..........."
    private const val CHEEKS = "..KRSSSRK..........."
    private const val MOUTH = "...KSRSK............"
    private const val NECK = "....KKK............."
    private const val BODY = "..TTTTTTT...PPPPPP.."
    private val desk = listOf("DDDDDDDDDDDDDDDDDDDD", ".DD..............DD.")
    val frames: List<List<String>> = listOf(
        head + listOf(EYES, CHEEKS, MOUTH, NECK, BODY,
            ".TTTTTTTTSSSYPPPPP..", ".TTTTTTTT...PPPPPP..") + desk,
        head + listOf(EYES, CHEEKS, MOUTH, NECK, BODY,
            ".TTTTTTTTTSSSYPPPP..", ".TTTTTTTT...KPPPPP..") + desk,
        head + listOf(EYES, CHEEKS, "...KSRSK........Y...", NECK, BODY,
            ".TTTTTTTTTTSSSYPPP..", ".TTTTTTTT...KKPPPP..") + desk,
        head + listOf(BLINK, "..KRSSSRK.......Y...", MOUTH, NECK, BODY,
            ".TTTTTTTTTTTSSSYPP..", ".TTTTTTTT...KKKPPP..") + desk,
    )
}

@Composable
private fun PixelAnimatorView(frame: Int, modifier: Modifier) {
    val description = stringResource(R.string.exp2_animator_a11y)
    Canvas(modifier.semantics { contentDescription = description }) {
        val rows = PixelAnimator.frames[frame % PixelAnimator.frames.size]
        val cell = min(size.width / PixelAnimator.WIDTH, size.height / PixelAnimator.HEIGHT)
        val ox = (size.width - cell * PixelAnimator.WIDTH) / 2f
        val oy = (size.height - cell * PixelAnimator.HEIGHT) / 2f
        rows.forEachIndexed { y, row ->
            row.forEachIndexed { x, c ->
                val color = PixelAnimator.palette[c] ?: return@forEachIndexed
                // +0,5 px: sem frestas entre pixels vizinhos em densidade fracionária.
                drawRect(color, Offset(ox + x * cell, oy + y * cell), Size(cell + 0.5f, cell + 0.5f))
            }
        }
    }
}

@Composable
private fun Done(message: String, title: String) {
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(8.dp))
        CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 44.dp, AureaColors.Success)
        Spacer(Modifier.height(10.dp))
        Text(title, style = AureaType.TitleLarge)
        Spacer(Modifier.height(6.dp))
        Text(message, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)), textAlign = TextAlign.Center)
    }
}

@Composable
private fun BottomAction(store: EditorStore, options: ExportOptions, compW: Int, compH: Int, compFps: Double) {
    val exporter = store.exporter
    val st = exporter.state
    val context = LocalContext.current
    val noViewer = stringResource(R.string.editor_nenhum_app_abre_video)
    Column(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 12.dp)) {
        when (st.phase) {
            ExportPhase.Running -> WideButton(stringResource(R.string.editor_cancelar), filled = false) { exporter.cancel() }
            ExportPhase.Publishing -> WideButton(stringResource(R.string.editor_salvando), filled = false, enabled = false) {}
            ExportPhase.Done -> Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(Modifier.weight(1f)) {
                    WideButton(stringResource(R.string.editor_abrir), filled = false) {
                        exporter.viewIntent()?.let { runCatching { context.startActivity(it) }.onFailure { e ->
                            if (e is ActivityNotFoundException) store.showToast(noViewer) } }
                    }
                }
                Box(Modifier.weight(1f)) {
                    WideButton(stringResource(R.string.editor_compartilhar), filled = true) { exporter.shareIntent()?.let { context.startActivity(it) } }
                }
            }
            else -> WideButton(stringResource(R.string.editor_exportar), filled = true, tall = true) {
                exporter.start(store.project.title, compW, compH, compFps, options)
            }
        }
    }
}

@Composable
private fun WideButton(label: String, filled: Boolean, enabled: Boolean = true, tall: Boolean = false, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(if (tall) 56.dp else 52.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(if (filled) AureaColors.Accent else AureaColors.Chip)
            .semantics { role = Role.Button }
            .tocavel(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600,
                color = if (filled) AureaColors.OnAccent else if (enabled) AureaColors.Text else AureaColors.Muted)),
        )
    }
}

private fun fmt(v: Double): String =
    if (v == v.roundToInt().toDouble()) v.roundToInt().toString()
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
