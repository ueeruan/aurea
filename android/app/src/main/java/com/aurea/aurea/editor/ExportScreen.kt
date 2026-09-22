package com.aurea.aurea.editor

import android.content.ActivityNotFoundException
import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

private val Resolutions = listOf(720 to "HD 720p", 1080 to "Full HD 1080p", 1440 to "QHD 1440p", 2160 to "4K 2160p")

/**
 * A tela Exportar (rota "fullscreenDialog" da A.01, `topo-exportar`). Tela
 * nova sobre o design system do app: opções → progresso → resultado.
 * O motor renderiza com o MESMO renderer do preview; o preview congela até
 * terminar.
 */
@Composable
internal fun ExportScreen(store: EditorStore, onDismiss: () -> Unit) {
    val exporter = store.exporter
    val st = exporter.state
    val comp = store.composition
    val compW = comp?.width ?: store.project.width
    val compH = comp?.height ?: store.project.height
    val compFps = comp?.fps ?: store.project.fps.toDouble()
    val seconds = if (compFps > 0) (comp?.durationFrames ?: store.project.durationFrames) / compFps else 0.0

    var options by remember { mutableStateOf(ExportOptions(shortSide = min(1080, max(720, min(compW, compH))))) }
    var preview by remember { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(Unit) {
        exporter.reset()
        preview = store.captureBitmap(640)
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
                PreviewCard(preview, compW, compH)
                Spacer(Modifier.height(18.dp))
                when (st.phase) {
                    ExportPhase.Idle, ExportPhase.Failed, ExportPhase.Cancelled -> {
                        if (st.phase != ExportPhase.Idle) Notice(st.message, danger = st.phase == ExportPhase.Failed)
                        Options(store, options, compW, compH, compFps, seconds) { options = it }
                    }
                    ExportPhase.Running, ExportPhase.Publishing -> Progress(st.fraction, st.framesDone, st.framesTotal,
                        st.fps, st.etaSeconds, publishing = st.phase == ExportPhase.Publishing, notice = st.notice)
                    ExportPhase.Done -> Done(st.message)
                }
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
        Text("Exportar", style = AureaType.TitleMedium, modifier = Modifier.padding(start = 4.dp))
    }
}

@Composable
private fun PreviewCard(bmp: Bitmap?, w: Int, h: Int) {
    val ratio = if (h > 0) w.toFloat() / h else 16f / 9f
    Box(
        Modifier
            .fillMaxWidth()
            .heightIn(max = 260.dp)
            .padding(top = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .aspectRatio(ratio, matchHeightConstraintsFirst = ratio < 1f)
                .clip(RoundedCornerShape(10.dp))
                .background(AureaColors.Stage)
                .border(1.dp, AureaColors.Border, RoundedCornerShape(10.dp)),
            contentAlignment = Alignment.Center,
        ) {
            if (bmp != null) {
                Image(bmp.asImageBitmap(), contentDescription = "Prévia", contentScale = ContentScale.Fit,
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
    fun sizeFor(side: Int): Pair<Int, Int> {
        val w = ((compW.toDouble() * side / short) / 2).roundToInt() * 2
        val h = ((compH.toDouble() * side / short) / 2).roundToInt() * 2
        return w to h
    }
    // Tudo aparece (§109): o que o aparelho não exporta fica marcado e
    // desligado, com a frase do porquê embaixo — o motor recusaria, e o
    // usuário não descobre só depois de tocar.
    val available = Resolutions.filter { (side, _) -> val (w, h) = sizeFor(side); comp?.fits(w, h) ?: true }
    val blocked = Resolutions.filter { it !in available }.map { it.second }.toSet()
    val device = store.deviceReport
    val fpsOptions = listOf(0.0, 24.0, 30.0, 60.0)
    val (w, h) = sizeFor(options.shortSide)
    val fps = if (options.fps > 0) options.fps else compFps
    val mbps = store.exporter.estimatedMbps(w, h, fps, options)
    val sizeMb = mbps * seconds / 8.0

    Section("Resolução")
    Chips(Resolutions.map { it.second }, available.firstOrNull { it.first == options.shortSide }?.second, blocked) { label ->
        onChange(options.copy(shortSide = available.first { it.second == label }.first))
    }
    if (blocked.isNotEmpty()) {
        Text(
            device?.exportLimitReason() ?: "${blocked.joinToString()}: acima do que este aparelho exporta.",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            modifier = Modifier.padding(top = 6.dp),
        )
    }
    Section("Quadros por segundo")
    Chips(fpsOptions.map { if (it == 0.0) "Do projeto (${fmt(compFps)})" else fmt(it) },
          if (options.fps == 0.0) "Do projeto (${fmt(compFps)})" else fmt(options.fps)) { label ->
        onChange(options.copy(fps = if (label.startsWith("Do projeto")) 0.0 else label.replace(',', '.').toDouble()))
    }
    Section("Formato")
    val hevcOk = device?.hevcExportAvailable ?: true
    Chips(listOf("H.264", "HEVC"), if (options.hevc) "HEVC" else "H.264", if (hevcOk) emptySet() else setOf("HEVC")) {
        onChange(options.copy(hevc = it == "HEVC"))
    }
    Text(
        if (!hevcOk) device?.hevcExportReason() ?: ""
        else if (options.hevc) "HEVC: arquivo menor, mesma qualidade. Alguns aparelhos antigos não reproduzem."
        else "H.264: abre em qualquer aparelho e rede social.",
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        modifier = Modifier.padding(top = 6.dp),
    )
    Section("Qualidade")
    Chips(listOf("Padrão", "Alta"), if (options.highQuality) "Alta" else "Padrão") {
        onChange(options.copy(highQuality = it == "Alta"))
    }
    Spacer(Modifier.height(18.dp))
    SummaryRow("Vídeo", "$w × $h · ${fmt(fps)} fps · ${if (options.hevc) "HEVC" else "H.264"}")
    SummaryRow("Duração", fmtTime(seconds))
    SummaryRow("Tamanho estimado", if (sizeMb >= 1000) "${fmt(sizeMb / 1000.0)} GB" else "${sizeMb.roundToInt()} MB")
    SummaryRow("Cor", "SDR · BT.709")
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Chips(options: List<String>, selected: String?, disabled: Set<String> = emptySet(), onPick: (String) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEach { o ->
            val on = o == selected
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
                    .tocavel(enabled = !off, shrink = 1f) { if (!on && !off) onPick(o) }
                    .padding(horizontal = 14.dp, vertical = 10.dp),
            )
        }
    }
}

@Composable
private fun Section(title: String) {
    Text(
        title.uppercase(Locale.ROOT),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, letterSpacing = 0.6.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        modifier = Modifier.padding(top = 16.dp, bottom = 8.dp),
    )
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

@Composable
private fun Progress(fraction: Float, done: Int, total: Int, fps: Float, eta: Int, publishing: Boolean, notice: String) {
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(12.dp))
        Text(
            if (publishing) "Salvando na galeria…" else "${(fraction * 100).roundToInt()}%",
            style = AureaType.HeadlineLarge,
        )
        Spacer(Modifier.height(14.dp))
        Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(AureaColors.Chip)) {
            Box(
                Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(if (publishing) 1f else fraction.coerceIn(0f, 1f))
                    .background(AureaColors.Accent),
            )
        }
        Spacer(Modifier.height(12.dp))
        if (!publishing) {
            Text(
                "Quadro $done de $total · ${fmt(fps.toDouble())} q/s · falta ${fmtTime(eta.toDouble())}",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted, fontFeatureSettings = "tnum")),
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(6.dp))
            Text(
                "Mantenha o Aurea aberto até terminar.",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
            // Encoder de software ou aparelho quente: dito, não escondido.
            if (notice.isNotEmpty()) {
                Spacer(Modifier.height(12.dp))
                Notice(notice, danger = false)
            }
        }
    }
}

@Composable
private fun Done(message: String) {
    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(8.dp))
        CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 44.dp, AureaColors.Success)
        Spacer(Modifier.height(10.dp))
        Text("Vídeo pronto", style = AureaType.TitleLarge)
        Spacer(Modifier.height(6.dp))
        Text(message, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)), textAlign = TextAlign.Center)
    }
}

@Composable
private fun BottomAction(store: EditorStore, options: ExportOptions, compW: Int, compH: Int, compFps: Double) {
    val exporter = store.exporter
    val st = exporter.state
    val context = LocalContext.current
    Column(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 12.dp)) {
        when (st.phase) {
            ExportPhase.Running -> WideButton("Cancelar", filled = false) { exporter.cancel() }
            ExportPhase.Publishing -> WideButton("Salvando…", filled = false, enabled = false) {}
            ExportPhase.Done -> Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(Modifier.weight(1f)) {
                    WideButton("Abrir", filled = false) {
                        exporter.viewIntent()?.let { runCatching { context.startActivity(it) }.onFailure { e ->
                            if (e is ActivityNotFoundException) store.showToast("Nenhum app abre vídeo") } }
                    }
                }
                Box(Modifier.weight(1f)) {
                    WideButton("Compartilhar", filled = true) { exporter.shareIntent()?.let { context.startActivity(it) } }
                }
            }
            else -> WideButton("Exportar", filled = true) {
                exporter.start(store.project.title, compW, compH, compFps, options)
            }
        }
    }
}

@Composable
private fun WideButton(label: String, filled: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(52.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(if (filled) AureaColors.Accent else AureaColors.Chip)
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

private fun fmtTime(seconds: Double): String {
    val s = seconds.roundToInt().coerceAtLeast(0)
    return if (s >= 3600) String.format(Locale.ROOT, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
    else String.format(Locale.ROOT, "%d:%02d", s / 60, s % 60)
}
