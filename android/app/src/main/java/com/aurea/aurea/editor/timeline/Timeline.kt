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
 *   ‹ › trocam de camada, cabeçote vermelho.
 * @param onEmptyTap toque no vazio (a casca fecha o painel ou desseleciona).
 * @param onKeyframeTap toque num losango: a timeline já chamou
 *   `store.selectKeyframe`; a casca decide se abre o editor de curva.
 */
@Composable
fun Timeline(
    store: EditorStore,
    compact: Boolean,
    onEmptyTap: () -> Unit,
    modifier: Modifier = Modifier,
    onTrackTap: (layer: Long, property: Int, effect: Int) -> Unit = { _, _, _ -> },
    onKeyframeTap: (layer: Long, key: KeyframeRow) -> Unit = { _, _ -> },
) {
    val density = LocalDensity.current
    val metrics = remember(density.density, density.fontScale) { TimelineMetrics(density.density, density.fontScale) }
    val measurer = rememberTextMeasurer(cacheSize = 16)
    val haptics = LocalHapticFeedback.current
    val scope = rememberCoroutineScope()
    val state = remember { TimelineState() }
    val controller = remember(store) { TimelineController(store, state, scope) }
    val painter = remember(metrics, measurer) { TimelinePainter(metrics, measurer) }

    // Parâmetros entram depois da composição (o desenho e os gestos leem daqui).
    SideEffect {
        controller.metrics = metrics
        controller.onEmptyTap = onEmptyTap
        controller.onKeyframeTap = onKeyframeTap
        controller.onTrackTap = onTrackTap
        controller.haptics = haptics
        state.compact = compact
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
                .background(AureaColors.Stage)
                .onSizeChanged {
                    state.width = it.width
                    state.height = it.height
                }
                .pointerInput(controller) { with(controller) { handleGestures() } }
                .drawBehind { painter.draw(this, controller) },
        )
        // Irmã (não filha) da superfície de gestos: o toque num botão da barra
        // não chega à timeline embaixo dele.
        KeyActionBar(store, compact, Modifier.align(if (compact) Alignment.TopEnd else Alignment.BottomCenter))
    }
}

/**
 * Barra de ações da seleção de keyframes (aparece com um losango escolhido na
 * timeline). Com painel aberto (timeline compacta, uma linha) só o
 * "Selecionar" cabe, por cima da régua; ligar o modo fecha o painel e a
 * timeline volta alta com a barra inteira embaixo.
 *
 * Selecionar liga/desliga o modo (toque soma/tira); Todos = todos os
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
        KeyAction(stringResource(R.string.common_copy), "timeline.keys.copy", enabled = count > 0) { store.copyTimelineKeys() }
        KeyAction(stringResource(R.string.common_paste), "timeline.keys.paste", enabled = (store.clipboard and 8) != 0) { store.pasteTimelineKeys() }
        KeyAction(stringResource(R.string.common_duplicate), "timeline.keys.duplicate", enabled = count > 0) { store.duplicateTimelineKeys() }
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
