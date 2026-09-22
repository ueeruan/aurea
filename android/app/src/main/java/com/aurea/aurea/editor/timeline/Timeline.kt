package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.rememberTextMeasurer
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors

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

    Box(
        modifier
            .clipToBounds()
            .background(AureaColors.Stage)
            .onSizeChanged {
                state.width = it.width
                state.height = it.height
            }
            .pointerInput(controller) { with(controller) { handleGestures() } }
            .drawBehind { painter.draw(this, controller) },
    )
}
