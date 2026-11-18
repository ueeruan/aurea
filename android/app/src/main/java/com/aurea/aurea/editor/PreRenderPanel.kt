package com.aurea.aurea.editor

import android.media.MediaPlayer
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.engine.PreRenderFit
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

// =============================================================================
// Pre Render & Pre Export (1.0.2). O painel escolhe o trecho (Início, Fim,
// Inteiro, Limpar), vai a ele (Ir ao início/fim), repete (Loop), escolhe a
// qualidade (Inteira, 1/2, 1/4, 1/8) com o "cabe ~Xs" ao vivo e pede ao motor
// o PreRender (quadros no cache) ou o pré-export (um MP4 no cache que o player
// toca no lugar do render ao vivo). Toda conta é do motor
// (EnginePreRenderRange.cpp); o iOS tem o mesmo painel (PreRenderPanelView.swift).
// =============================================================================

/** As qualidades do painel: divisor da composição. */
internal val PreRenderQualities = listOf(1, 2, 4, 8)

/** "12s", "2m 05s", "1h 03m": o "cabe ~Xs" sem casas (o mesmo formato no iOS). */
internal fun preRenderDuration(seconds: Double): String {
    val s = seconds.coerceAtLeast(0.0)
    if (s < 10.0) return "%.1fs".format(java.util.Locale.ROOT, s)
    val total = s.toLong()
    return when {
        total < 60 -> "${total}s"
        total < 3600 -> "${total / 60}m ${(total % 60).toString().padStart(2, '0')}s"
        else -> "${total / 3600}h ${((total % 3600) / 60).toString().padStart(2, '0')}m"
    }
}

@Composable
private fun qualityLabel(divisor: Int): String =
    if (divisor == 1) stringResource(R.string.prx_quality_full) else "1/$divisor"

/** O painel inteiro (folha de baixo). Aberto por [EditorStore.openPreRenderPanel]. */
@Composable
internal fun PreRenderPanel(store: EditorStore) {
    var help by remember { mutableStateOf(false) }
    ShellMenuSheet(onDismiss = { store.closePreRenderPanel() }, maxHeightFraction = 0.82f) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(bottom = 16.dp)
                .testTag("prerender.panel"),
        ) {
            PanelHeader(onHelp = { help = true })
            ModeSelector(store)
            QualitySelector(store)
            FitSummary(store)
            RangeSection(store)
            ActionSection(store)
        }
    }
    if (help) {
        AureaAlert(
            title = stringResource(R.string.prx_help_title),
            message = stringResource(R.string.prx_help_body),
            confirmLabel = stringResource(R.string.prx_close),
            showCancel = false,
            onConfirm = { help = false },
            onDismiss = { help = false },
        )
    }
}

@Composable
private fun PanelHeader(onHelp: () -> Unit) {
    val helpLabel = stringResource(R.string.prx_help)
    Row(Modifier.fillMaxWidth().padding(start = 20.dp, end = 8.dp, top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(
            stringResource(R.string.prx_title),
            modifier = Modifier.weight(1f).semantics { heading() },
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
        )
        Box(
            Modifier.size(44.dp).testTag("prerender.help")
                .semantics { contentDescription = helpLabel }
                .tocavel(onClick = onHelp),
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.InfoCircle, 20.dp, AureaColors.Muted) }
    }
}

@Composable
private fun ModeSelector(store: EditorStore) {
    val modeLabel = stringResource(R.string.prx_mode)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp).height(36.dp)
            .clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
            .semantics { contentDescription = modeLabel },
    ) {
        listOf(0 to R.string.prx_mode_prerender, 1 to R.string.prx_mode_preexport).forEach { (mode, label) ->
            val on = store.preRenderMode == mode
            val text = stringResource(label)
            Box(
                Modifier.weight(1f).height(36.dp).padding(3.dp).clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.Accent else Color.Transparent)
                    .testTag(if (mode == 0) "prerender.mode.prerender" else "prerender.mode.preexport")
                    .semantics { selected = on; role = Role.Tab; contentDescription = text }
                    .tocavel(shrink = 1f) { store.changePreRenderMode(mode) },
                contentAlignment = Alignment.Center,
            ) {
                Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600,
                    color = if (on) AureaColors.OnAccent else AureaColors.Text)))
            }
        }
    }
}

@Composable
private fun QualitySelector(store: EditorStore) {
    MenuSection(stringResource(R.string.prx_quality))
    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        PreRenderQualities.forEach { divisor ->
            QualityChip(store, divisor, store.preRenderFits[divisor])
        }
    }
}

@Composable
private fun RowScope.QualityChip(store: EditorStore, divisor: Int, fit: PreRenderFit?) {
    val on = store.preRenderQuality == divisor
    val label = qualityLabel(divisor)
    val fits = fit?.let { stringResource(R.string.prx_fits, preRenderDuration(it.fitsSeconds)) } ?: "…"
    Column(
        Modifier.weight(1f).heightIn(min = 58.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.Accent else AureaColors.Chip)
            .testTag("prerender.quality.$divisor")
            .semantics { selected = on; contentDescription = "$label, $fits" }
            .tocavel(shrink = 1f, haptic = true) { store.changePreRenderQuality(divisor) }
            .padding(vertical = 8.dp, horizontal = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        val color = if (on) AureaColors.OnAccent else AureaColors.Text
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = color)))
        Spacer(Modifier.height(2.dp))
        Text(
            fits, maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = if (on) color.copy(alpha = 0.85f)
                else if (fit != null && !fit.fits) AureaColors.Warning else AureaColors.Muted)).merge(AureaType.Tabular),
        )
    }
}

@Composable
private fun FitSummary(store: EditorStore) {
    val fit = store.preRenderFits[store.preRenderQuality] ?: return
    val hint = stringResource(R.string.prx_fits_hint)
    Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp).semantics(mergeDescendants = true) {}) {
        Text(
            stringResource(R.string.prx_fits, preRenderDuration(fit.fitsSeconds)),
            modifier = Modifier.testTag("prerender.fits"),
            style = AureaType.Base.merge(TextStyle(fontSize = 22.sp, fontWeight = FontWeight.W600,
                color = if (fit.fits) AureaColors.Text else AureaColors.Warning)).merge(AureaType.Tabular),
        )
        Text(
            stringResource(R.string.prx_fits_detail, fit.width, fit.height, preRenderDuration(fit.rangeSeconds)),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)).merge(AureaType.Tabular),
        )
        Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Subtle)))
        if (!fit.fits && fit.rangeSeconds > 0.0) {
            Text(
                stringResource(R.string.prx_not_fit),
                modifier = Modifier.padding(top = 4.dp).testTag("prerender.notFit"),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)),
            )
        }
    }
}

@Composable
private fun RangeSection(store: EditorStore) {
    val range = store.preRenderRange
    val fps = range.fps.toFloat()
    val from = ShellTime.short(range.effectiveIn.toInt(), fps)
    val to = ShellTime.short(range.effectiveOut.toInt(), fps)
    Text(
        if (range.chosen) stringResource(R.string.prx_range, from, to) else stringResource(R.string.prx_range_whole, from, to),
        modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp).testTag("prerender.range"),
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)).merge(AureaType.Tabular),
    )
    // Botões do trecho: em LTR também em árabe (o tempo corre da esquerda para a direita).
    KeepLtr {
        Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                RangeButton(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.prx_start), "prerender.start") { store.preRenderRangeAction(0) }
                RangeButton(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.prx_end), "prerender.end") { store.preRenderRangeAction(1) }
                RangeButton(CupertinoGlyph.Film, stringResource(R.string.prx_full), "prerender.full") { store.preRenderRangeAction(2) }
                RangeButton(CupertinoGlyph.Xmark, stringResource(R.string.prx_clear), "prerender.clear", enabled = range.chosen) { store.preRenderRangeAction(3) }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                RangeButton(CupertinoGlyph.BackwardEnd, stringResource(R.string.prx_go_start), "prerender.goStart") { store.preRenderGoStart() }
                RangeButton(CupertinoGlyph.ForwardEnd, stringResource(R.string.prx_go_end), "prerender.goEnd") { store.preRenderGoEnd() }
                RangeButton(CupertinoGlyph.Repeat, stringResource(R.string.prx_loop), "prerender.loop", on = range.loop) {
                    store.setPreRenderLoop(!range.loop)
                }
            }
        }
    }
}

@Composable
private fun RowScope.RangeButton(
    glyph: Char, label: String, tag: String, enabled: Boolean = true, on: Boolean? = null, onClick: () -> Unit,
) {
    val onLabel = stringResource(R.string.common_on)
    val offLabel = stringResource(R.string.common_off)
    Column(
        Modifier.weight(1f).heightIn(min = 54.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on == true) AureaColors.Accent else AureaColors.Chip)
            .testTag(tag)
            .semantics {
                contentDescription = label
                if (on != null) stateDescription = if (on) onLabel else offLabel
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, shrink = 1f, haptic = true, onClick = onClick)
            .alpha(if (enabled) 1f else 0.4f)
            .padding(vertical = 6.dp, horizontal = 2.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        val color = if (on == true) AureaColors.OnAccent else AureaColors.Text
        CupertinoIcon(glyph, 18.dp, color, autoMirror = false)
        Spacer(Modifier.height(3.dp))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = color)))
    }
}

@Composable
private fun ActionSection(store: EditorStore) {
    val mode = store.preRenderMode
    val progress = store.preRenderProgress
    val export = store.preExport
    val cancel = stringResource(R.string.prx_cancel)
    Spacer(Modifier.height(12.dp))
    val running = if (mode == 0) store.preRenderPreparing || progress.running else store.preExportPreparing || export.running
    if (running) {
        val preparing = if (mode == 0) store.preRenderPreparing else store.preExportPreparing
        val fraction = if (mode == 0) progress.fraction else export.fraction
        val text = when {
            preparing -> stringResource(R.string.prx_preparing)
            mode == 0 -> stringResource(R.string.prx_progress_prerender, (fraction * 100).roundToInt())
            else -> stringResource(R.string.prx_progress_preexport, (fraction * 100).roundToInt())
        }
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp).semantics(mergeDescendants = true) { stateDescription = text },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(text, modifier = Modifier.testTag("prerender.progress"),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)).merge(AureaType.Tabular))
                Spacer(Modifier.height(6.dp))
                Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(AureaColors.Chip)) {
                    Box(Modifier.fillMaxWidth(if (preparing) 0.06f else fraction.coerceIn(0f, 1f)).height(6.dp)
                        .clip(RoundedCornerShape(3.dp)).background(AureaColors.Accent))
                }
            }
            Spacer(Modifier.width(12.dp))
            Box(
                Modifier.heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp)).border(1.dp, AureaColors.Muted, RoundedCornerShape(10.dp))
                    .testTag("prerender.cancel").semantics { contentDescription = cancel }
                    .tocavel { if (mode == 0) store.cancelPreRender() else store.cancelPreExport() }
                    .padding(horizontal = 14.dp, vertical = 10.dp),
                contentAlignment = Alignment.Center,
            ) { Text(cancel, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text))) }
        }
    } else {
        val label = stringResource(if (mode == 0) R.string.prx_action_prerender else R.string.prx_action_preexport)
        Box(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp).heightIn(min = 48.dp).clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Accent)
                .testTag(if (mode == 0) "prerender.start.prerender" else "prerender.start.preexport")
                .semantics { contentDescription = label }
                .tocavel(haptic = true) { if (mode == 0) store.startPreRenderRange() else store.startPreExport() },
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W600, color = AureaColors.OnAccent)))
        }
    }
    // Estado do que já está pronto.
    val status: String? = if (mode == 0) when {
        progress.complete && progress.dirtySegments > 0 -> stringResource(R.string.prx_stale, progress.dirtySegments)
        progress.complete -> stringResource(R.string.prx_ready_prerender)
        else -> null
    } else when {
        export.complete && export.stale -> stringResource(R.string.prx_stale_export)
        export.complete -> stringResource(R.string.prx_ready_preexport)
        else -> null
    }
    if (status != null) {
        val warn = (mode == 0 && progress.dirtySegments > 0) || (mode == 1 && export.stale)
        Text(
            status,
            modifier = Modifier.padding(horizontal = 20.dp, vertical = 10.dp).testTag("prerender.status"),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = if (warn) AureaColors.Warning else AureaColors.Success)),
        )
    }
    if (mode == 1 && export.playable) {
        val watch = stringResource(R.string.prx_watch)
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp).heightIn(min = 46.dp).clip(RoundedCornerShape(12.dp))
                .border(1.dp, AureaColors.Accent, RoundedCornerShape(12.dp))
                .testTag("prerender.watch").semantics { contentDescription = watch }
                .tocavel { store.watchPreExport() },
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.PlayFill, 16.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text(watch, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
        }
    }
}

/**
 * O pré-export tocando: o MP4 do trecho decodificado pelo `MediaPlayer` (sem
 * render ao vivo), do ponto do cabeçote dentro do trecho, repetindo com o Loop.
 * Fechar leva o cabeçote ao instante em que parou.
 */
@Composable
internal fun PreExportPlayer(store: EditorStore, path: String) {
    val media = remember(path) { MediaPlayer() }
    var ready by remember(path) { mutableStateOf(false) }
    val status = store.preExport
    val loop = store.preRenderRange.loop
    val startMs = remember(path) {
        val frame = store.playhead.toLong()
        if (frame in status.inFrame until status.outFrame && status.fps > 0.0)
            ((frame - status.inFrame) / status.fps * 1000.0).toInt() else 0
    }
    fun close() {
        val seconds = runCatching { media.currentPosition / 1000.0 }.getOrDefault(-1.0)
        store.closePreExportPlayer(seconds)
    }
    DisposableEffect(path) {
        onDispose {
            runCatching { if (media.isPlaying) media.stop() }
            runCatching { media.release() }
        }
    }
    val closeLabel = stringResource(R.string.prx_close)
    val title = stringResource(R.string.prx_watch)
    Dialog(onDismissRequest = { close() }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().background(Color.Black).testTag("prerender.player").semantics { contentDescription = title }) {
            val aspect = if (status.width > 0 && status.height > 0) status.width.toFloat() / status.height else 16f / 9f
            AndroidView(
                factory = { ctx ->
                    SurfaceView(ctx).also { view ->
                        view.holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                runCatching {
                                    media.setDataSource(path)
                                    media.setDisplay(holder)
                                    media.setOnPreparedListener {
                                        ready = true
                                        it.isLooping = loop
                                        if (startMs > 0) it.seekTo(startMs)
                                        it.start()
                                    }
                                    media.prepareAsync()
                                }
                            }
                            override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, hh: Int) = Unit
                            override fun surfaceDestroyed(h: SurfaceHolder) {
                                runCatching { media.reset() }
                                ready = false
                            }
                        })
                    }
                },
                modifier = Modifier.align(Alignment.Center).fillMaxWidth().aspectRatio(aspect)
                    .tocavel(shrink = 1f) { if (ready) { if (media.isPlaying) media.pause() else media.start() } },
            )
            if (!ready) {
                Text(stringResource(R.string.prx_preparing), modifier = Modifier.align(Alignment.Center),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Subtle)))
            }
            Box(
                Modifier.align(Alignment.TopEnd).padding(12.dp).size(44.dp).clip(RoundedCornerShape(22.dp))
                    .background(Color.Black.copy(alpha = 0.5f))
                    .testTag("prerender.player.close").semantics { contentDescription = closeLabel }
                    .tocavel { close() },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Xmark, 18.dp, Color.White) }
        }
    }
}
