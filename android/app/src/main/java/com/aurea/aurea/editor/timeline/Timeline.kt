package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.ShellColors
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel

/**
 * CONTRATO da timeline. A casca dá a área; a timeline desenha régua,
 * cabeçote, camadas, keyframes e trata os gestos, lendo e escrevendo SÓ pelo
 * [EditorStore].
 *
 * Visual da A.01 (`am_timeline.dart@aba36bb`, spec 03 §1.B): régua 20 + 18,
 * linha 46, barra 36 raio 8, pílula 58×28, cabeçote 1,6 fixo no centro,
 * relógio `MM:SS:FF`. Comportamento corrigido da spec (§2–§9): o conteúdo
 * rola sob o cabeçote e isso É o scrub do motor.
 *
 * Desenho num Canvas só e gestos num `pointerInput` só: o relógio anda e nada
 * recompõe — a fase de desenho lê o tempo (spec §9.1).
 *
 * @param compact modo compacto da A.01 (painel aberto): uma linha, setas
 *   tampa "‹" volta, setas ‹ › trocam de camada, cabeçote em destaque.
 * @param compactDock camada escolhida com a doca aberta: a mesma fileira
 *   única, mas só enquanto não há trilhas de propriedade abertas nem escolha
 *   de keyframes ([timelineCompact]); o ícone do tipo abre as trilhas.
 * @param onEmptyTap toque no vazio (a casca fecha o painel ou desseleciona).
 * @param onKeyframeTap toque num losango: a timeline já chamou
 *   `store.selectKeyframe`; a casca decide se abre o editor de curva.
 * @param timecodeStyle relógio sublinhado (editor principal) ou em caixa com
 *   borda (camada escolhida / efeitos) — a mesma timeline serve aos dois.
 */
@Composable
fun Timeline(
    store: EditorStore,
    compact: Boolean,
    onEmptyTap: () -> Unit,
    modifier: Modifier = Modifier,
    compactDock: Boolean = false,
    onTrackTap: (layer: Long, property: Int, effect: Int) -> Unit = { _, _, _ -> },
    onKeyframeTap: (layer: Long, key: KeyframeRow) -> Unit = { _, _ -> },
    timecodeStyle: TimecodeStyle = TimecodeStyle.Underline,
) {
    val density = LocalDensity.current
    val metrics = remember(density.density, density.fontScale) { TimelineMetrics(density.density, density.fontScale) }
    val measurer = rememberTextMeasurer(cacheSize = 16)
    val haptics = LocalHapticFeedback.current
    val scope = rememberCoroutineScope()
    val state = remember { TimelineState() }
    val controller = remember(store) { TimelineController(store, state, scope) }
    val painter = remember(metrics, measurer) { TimelinePainter(metrics, measurer) }
    // Trilhas abertas de uma camada que ainda existe (só o booleano recompõe).
    val tracksOpen by remember(controller) {
        derivedStateOf {
            val open = controller.expandedLayers.value
            open.isNotEmpty() && store.layers.any { it.id in open }
        }
    }
    // Escolhendo keyframes ou várias camadas, a timeline fica inteira.
    val shownCompact = timelineCompact(compact, compactDock, tracksOpen, store.keySelectMode || store.layerSelectMode)

    // Parâmetros entram depois da composição (o desenho e os gestos leem daqui).
    SideEffect {
        controller.metrics = metrics
        controller.onEmptyTap = onEmptyTap
        controller.onKeyframeTap = onKeyframeTap
        controller.onTrackTap = onTrackTap
        controller.haptics = haptics
        state.compact = shownCompact
        state.compactByDock = shownCompact && !compact
        state.timecodeBox = timecodeStyle == TimecodeStyle.Box
    }
    // Saiu da tela no meio de um gesto: scrub e passo de desfazer fecham em par.
    DisposableEffect(controller) { onDispose { controller.dispose() } }
    LaunchedEffect(controller) {
        snapshotFlow { controller.revealKey() }.collect { controller.reveal() }
    }
    LaunchedEffect(controller) {
        snapshotFlow { Triple(store.project.path, store.project.durationFrames, state.width) }
            .collect { controller.autoFit() }
    }

    Box(modifier.testTag("editor.timeline").clipToBounds()) {
        Box(
            Modifier
                .fillMaxSize()
                .background(AureaColors.EditorCanvas)
                .onSizeChanged {
                    state.width = it.width
                    state.height = it.height
                }
                .pointerInput(controller) { with(controller) { handleGestures() } }
                .drawBehind { painter.draw(this, controller) },
        )
        // Irmã (não filha) da superfície de gestos: o toque num botão da barra
        // não chega à timeline embaixo dele.
        KeyActionBar(store, shownCompact, Modifier.align(if (shownCompact) Alignment.TopEnd else Alignment.BottomCenter))
        LayerPickBar(store, Modifier.align(Alignment.BottomCenter))
    }
}

/** Como o relógio da timeline se desenha (mockup 2026-09-29). */
enum class TimecodeStyle {
    /** Editor principal: dígitos 16 sp com sublinhado branco de 2 dp. */
    Underline,
    /** Camada escolhida / efeitos: dígitos dentro de uma caixa com borda em destaque. */
    Box,
}

/**
 * Barra do modo "Selecionar várias camadas" (mesmo desenho da barra dos
 * keyframes): o contador, Todas, Limpar e Concluir. Some quando há
 * keyframes escolhidos (a barra deles manda) ou o modo desliga.
 */
@Composable
private fun LayerPickBar(store: EditorStore, modifier: Modifier) {
    if (!store.layerSelectMode || store.keySelection != null) return
    val count = store.selection.size
    Row(
        modifier
            .padding(horizontal = 8.dp, vertical = 6.dp)
            .background(ShellColors.FloatingDark, RoundedCornerShape(14.dp))
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        KeyAction("${stringResource(R.string.panel_selecionar)} · $count", "timeline.layers.select", active = true) { store.changeLayerSelectMode(false) }
        KeyAction(stringResource(R.string.common_all), "timeline.layers.all", enabled = store.layers.size >= 2) { store.selectAll() }
        KeyAction(stringResource(R.string.editor_limpar_selecao), "timeline.layers.clear", enabled = count > 0) { store.clearSelection() }
        KeyAction(stringResource(R.string.editor_concluir), "timeline.layers.done") { store.changeLayerSelectMode(false) }
    }
}

/**
 * Barra de ações da seleção de keyframes (aparece com um losango escolhido na
 * timeline). Com painel aberto (timeline compacta, uma linha) só o
 * "Selecionar" cabe, por cima da régua; ligar o modo fecha o painel e a
 * timeline volta alta com a barra inteira embaixo.
 *
 * Selecionar liga/desliga o modo (toque soma/tira, também em losangos de
 * outras camadas); Copiar/Duplicar valem só com uma camada; Todos = todos os
 * keyframes da camada que a timeline mostra; Colar = no cabeçote; Duplicar =
 * cópia 1 frame depois do último escolhido; Concluir fecha a barra.
 */
@Composable
private fun KeyActionBar(store: EditorStore, compact: Boolean, modifier: Modifier) {
    val sel = store.keySelection ?: return
    val mode = store.keySelectMode
    val count = sel.size
    val select = stringResource(R.string.panel_selecionar).let { if (mode) "$it · $count" else it }
    val shape = RoundedCornerShape(14.dp)
    if (compact) {
        Row(modifier.padding(end = 6.dp).background(ShellColors.FloatingDark, shape)) {
            KeyAction(select, "timeline.keys.select", active = mode) { store.changeKeySelectMode(!mode) }
        }
        return
    }
    Row(
        modifier
            .padding(horizontal = 8.dp, vertical = 6.dp)
            .background(ShellColors.FloatingDark, shape)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        KeyAction(select, "timeline.keys.select", active = mode) { store.changeKeySelectMode(!mode) }
        KeyAction(stringResource(R.string.common_all), "timeline.keys.all") { store.selectAllTimelineKeys() }
        KeyAction(stringResource(R.string.common_copy), "timeline.keys.copy", enabled = count > 0 && !sel.crossLayer) { store.copyTimelineKeys() }
        KeyAction(stringResource(R.string.common_paste), "timeline.keys.paste", enabled = (store.clipboard and 8) != 0) { store.pasteTimelineKeys() }
        KeyAction(stringResource(R.string.common_duplicate), "timeline.keys.duplicate", enabled = count > 0 && !sel.crossLayer) { store.duplicateTimelineKeys() }
        KeyAction(stringResource(R.string.common_delete), "timeline.keys.delete", enabled = count > 0, danger = true) { store.deleteTimelineKeys() }
        KeyAction(stringResource(R.string.editor_concluir), "timeline.keys.done") { store.clearKeySelection() }
    }
}

/** Botão da barra: alvo de 48 dp no mínimo (texto curto no meio). */
@Composable
private fun KeyAction(
    label: String,
    tag: String,
    enabled: Boolean = true,
    active: Boolean = false,
    danger: Boolean = false,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .heightIn(min = 48.dp)
            .widthIn(min = 48.dp)
            .testTag(tag)
            .then(if (active) Modifier.background(AureaColors.Accent.copy(alpha = 0.28f), RoundedCornerShape(10.dp)) else Modifier)
            .tocavel(enabled = enabled, onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            fontSize = 13.sp,
            maxLines = 1,
            color = when {
                !enabled -> AureaColors.Muted
                danger -> AureaColors.Danger
                active -> AureaColors.Accent
                else -> Color.White
            },
        )
    }
}
