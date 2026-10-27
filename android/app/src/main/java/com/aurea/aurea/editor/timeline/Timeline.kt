package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.offset
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
import androidx.compose.runtime.setValue
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
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.foundation.layout.size
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.stateDescription
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.ShellColors
import com.aurea.aurea.editor.ChromeButton
import com.aurea.aurea.editor.ShellGlyph
import com.aurea.aurea.ui.theme.CupertinoGlyph
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
    onTrackTap: (layer: Long, property: Int, effect: Int, param: Int) -> Unit = { _, _, _, _ -> },
    onKeyframeTap: (layer: Long, key: KeyframeRow) -> Unit = { _, _ -> },
    timecodeStyle: TimecodeStyle = TimecodeStyle.Underline,
    /** Omega (área 03, P1-8): painel Velocidade aberto — as alças do foco esticam a velocidade. */
    speedHandles: Boolean = false,
    /** 0.3.6: a base da timeline coberta pelo painel por cima (doca, painel, adicionar). */
    obscuredBottom: androidx.compose.ui.unit.Dp = 0.dp,
) {
    val density = LocalDensity.current
    val referenceTrial = com.aurea.aurea.editor.EditorTimelineRefresh.enabled
    val metrics = remember(density.density, density.fontScale, referenceTrial) { TimelineMetrics(density.density, density.fontScale, referenceTrial) }
    val measurer = rememberTextMeasurer(cacheSize = 16)
    val haptics = LocalHapticFeedback.current
    val view = androidx.compose.ui.platform.LocalView.current
    val scope = rememberCoroutineScope()
    val state = remember { TimelineState() }
    val controller = remember(store) { TimelineController(store, state, scope) }
    val painter = remember(metrics, measurer) { TimelinePainter(metrics, measurer) }
    val cachedFrames by remember(store) { derivedStateOf { store.previewBufferRanges.sumOf { it.endFrame - it.startFrame }.toInt() } }
    // Trilhas abertas de uma camada que ainda existe (só o booleano recompõe).
    val tracksOpen by remember(controller) {
        derivedStateOf {
            val open = controller.expandedLayers.value
            open.isNotEmpty() && store.layers.any { it.id in open }
        }
    }
    // Escolhendo keyframes ou várias camadas, a timeline fica inteira.
    var showAllLayers by androidx.compose.runtime.saveable.rememberSaveable { androidx.compose.runtime.mutableStateOf(false) }
    // Omega (P1-3): o menu ancorado na aba do tempo atual (segurar o relógio).
    var timeMenu by remember { androidx.compose.runtime.mutableStateOf(false) }
    val shownCompact = !showAllLayers && timelineCompact(compact, compactDock, tracksOpen, store.keySelectMode || store.layerSelectMode)

    // Parâmetros entram depois da composição (o desenho e os gestos leem daqui).
    SideEffect {
        controller.metrics = metrics
        controller.onEmptyTap = onEmptyTap
        controller.onTimeMenu = { timeMenu = true }
        controller.onKeyframeTap = onKeyframeTap
        controller.onTrackTap = onTrackTap
        controller.haptics = haptics
        controller.announce = { view.announceForAccessibility(it) }
        state.compact = shownCompact
        state.compactByDock = shownCompact && !compact
        state.timecodeBox = timecodeStyle == TimecodeStyle.Box
        state.speedHandles = speedHandles
        state.obscuredBottom = obscuredBottom.value * density.density
    }
    // Omega (P1-9): a linha que sai do foco volta com o fade de 600 ms.
    LaunchedEffect(controller) {
        snapshotFlow { state.compact to store.primary }
            .collect { (isCompact, primary) -> controller.noteCompact(isCompact, primary, System.nanoTime()) }
    }
    // 0.3.3 recursos: sem a dica "toque no relógio" — o marcador antigo voltou
    // (botão do transporte, âncora do palco e régua, como na 0.3.2).
    // Saiu da tela no meio de um gesto: scrub e passo de desfazer fecham em par.
    DisposableEffect(controller) { onDispose { controller.dispose() } }
    LaunchedEffect(controller) {
        // 0.3.6: também quando o painel por cima muda de altura (a doca abre depois da escolha).
        snapshotFlow { Triple(controller.revealKey(), state.obscuredBottom, state.height) }.collect { controller.reveal() }
    }
    LaunchedEffect(controller) {
        snapshotFlow { Triple(store.project.path, store.project.durationFrames, state.width to state.height) }
            .collect { controller.autoFit() }
    }
    // Camada adicionada: a fileira nova entra com fade (RowFade). A lista é a
    // da composição aberta; o caminho separa um projeto do outro.
    LaunchedEffect(controller) {
        snapshotFlow { store.project.path to LongArray(store.layers.size) { store.layers[it].id } }
            .collect { (path, ids) -> controller.noteLayers(path, ids, System.nanoTime()) }
    }

    Box(modifier.testTag("editor.timeline").clipToBounds()) {
        Box(
            Modifier
                .fillMaxSize()
                .background(if (referenceTrial) AureaColors.EditorCanvas else com.aurea.aurea.ui.theme.AureaTimeline.Background)
                .onSizeChanged {
                    state.width = it.width
                    state.height = it.height
                }
                .pointerInput(controller) { with(controller) { handleGestures() } }
                .drawBehind { painter.draw(this, controller) },
        )
        // Máscara de recorte: a pílula desenha o "↳"; o leitor de tela ouve
        // "Recortada pela camada abaixo" (mesmo id do iOS).
        val clippedDescription = stringResource(R.string.editor_recortada_camada_abaixo)
        store.layers.forEach { l ->
            if (l.clipToBelow) {
                Box(Modifier.width(1.dp).height(1.dp).testTag("timeline.layer.${l.id}.clip")
                    .semantics { contentDescription = l.name + ": " + clippedDescription })
            }
        }
        if (cachedFrames > 0) {
            val bufferDescription = stringResource(R.string.preview_buffer_timeline, cachedFrames)
            Box(Modifier.fillMaxWidth().height((metrics.rulerTicks / density.density).dp)
                .testTag("timeline.preview.buffer").semantics { contentDescription = bufferDescription })
        }
        // Omega (P0-3, P2-4): cabeçalhos, régua e tempo atual para o leitor de tela.
        if (referenceTrial) {
            OmegaLayerSemantics(store)
            OmegaRulerSemantics(store, Modifier.fillMaxWidth().height((metrics.rowsTop / density.density).dp))
            if (shownCompact) OmegaTrimSemantics(store, speedHandles)
            // A âncora do menu do tempo atual: a zona de 100 × 38 dp da aba (sem toque próprio).
            Box(Modifier.align(Alignment.TopCenter).padding(top = 5.dp).size(100.dp, 38.dp)) {
                if (timeMenu) OmegaTimeMenu(store) { timeMenu = false }
            }
        }
        // Irmã (não filha) da superfície de gestos: o toque num botão da barra
        // não chega à timeline embaixo dele.
        KeyActionBar(store, shownCompact, Modifier.align(if (shownCompact) Alignment.TopEnd else Alignment.BottomCenter))
        // 0.3.3 recursos: a barra do "Selecionar várias" da 0.3.2 volta (contador ·
        // Todas · Limpar · Concluir). Na Omega fica logo acima da barra flutuante da
        // seleção múltipla (42 + 6 dp), que mora na base da zona.
        LayerPickBar(store, Modifier.align(Alignment.BottomCenter).padding(bottom = if (referenceTrial) 48.dp else 0.dp))
        // Omega (P1-5/P1-7): barra de info do gesto (início, deslocamento, corte).
        store.timelineInfo?.let { info -> OmegaInfoBar(info, Modifier.align(Alignment.TopCenter)) }
        // 0.3.3 recursos: o botão "Todas/Seleção" da 0.3.2 volta: com camada escolhida
        // (foco de uma linha), mostra todas as camadas e volta só à escolhida.
        if (store.selection.isNotEmpty() && (compact || compactDock)) {
            // 0.3.4 presets: pílula pequena (24 dp) centrada na faixa da régua, no lugar do
            // TextButton (o fundo do toque vazava como uma mancha oval). Alvo de 44 dp invisível.
            val filterDescription = stringResource(if (showAllLayers) R.string.timeline_selected_only else R.string.timeline_show_all)
            Box(
                Modifier.align(Alignment.TopStart).height(44.dp).widthIn(min = 44.dp)
                    .semantics { contentDescription = filterDescription }.testTag("timeline.showAllLayers")
                    .tocavel { showAllLayers = !showAllLayers }
                    .padding(horizontal = 8.dp),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    Modifier.height(24.dp)
                        .background(if (showAllLayers) AureaColors.AccentDim else AureaColors.Chip, RoundedCornerShape(12.dp))
                        .padding(horizontal = 10.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(stringResource(if (showAllLayers) R.string.timeline_selection_short else R.string.timeline_all_short),
                        fontSize = 11.5.sp, maxLines = 1,
                        fontWeight = androidx.compose.ui.text.font.FontWeight.W600,
                        color = if (showAllLayers) AureaColors.Accent else AureaColors.Text)
                }
            }
        }
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

// =============================================================================
// Omega 0.3.2 (área 03)
// =============================================================================

/**
 * Barra de informação do gesto (P1-5/P1-7): pares rótulo–valor do mover, do
 * aparar, da alça de velocidade e do losango. Fica por cima da régua enquanto o
 * dedo arrasta; a barra de reprodução pode ler o mesmo `store.timelineInfo`.
 */
@Composable
private fun OmegaInfoBar(info: List<Pair<String, String>>, modifier: Modifier) {
    Row(
        modifier
            .padding(top = 2.dp)
            .background(AureaColors.EditorPanel, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 4.dp)
            .testTag("timeline.info")
            .semantics(mergeDescendants = true) {},
        verticalAlignment = Alignment.CenterVertically,
    ) {
        info.forEachIndexed { i, (label, value) ->
            if (i > 0) Box(Modifier.width(10.dp))
            Text(label, fontSize = 11.sp, maxLines = 1, color = AureaColors.Muted)
            Box(Modifier.width(4.dp))
            Text(value, fontSize = 12.sp, maxLines = 1, color = AureaColors.Text,
                style = androidx.compose.ui.text.TextStyle(fontFeatureSettings = "tnum"))
        }
    }
}

/**
 * Menu do tempo atual (P1-3, segurar o relógio): editar o marcador do quadro
 * (ou criar um com nome) e excluí-lo, se houver. Os itens de introdução/final
 * da referência não se aplicam; o quadro de capa (P2-6) ainda não existe no motor.
 */
@Composable
private fun OmegaTimeMenu(store: EditorStore, onDismiss: () -> Unit) {
    val frame = store.playhead
    val marked = frame in store.markers.frames
    val entries = buildList {
        add(MenuEntry.Item(stringResource(R.string.marker_edit), glyph = CupertinoGlyph.Bookmark, tag = "timeline.currentTime.menu.edit") {
            store.openMarkerEditor(frame)
        })
        if (marked) {
            add(MenuEntry.Item(stringResource(R.string.common_delete), glyph = CupertinoGlyph.Trash, destructive = true,
                tag = "timeline.currentTime.menu.delete") { store.deleteMarker(frame) })
        }
    }
    AnchoredMenu(expanded = true, onDismiss = onDismiss, entries = entries, tag = "timeline.currentTime.menu")
}

/** A dica do marcador (P1-3), uma vez por aparelho, quando a timeline abre sem marcadores. */
@Composable
private fun OmegaMarkerHint(store: EditorStore) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val hint = stringResource(R.string.om_tl_marker_hint)
    LaunchedEffect(Unit) {
        val prefs = context.applicationContext.getSharedPreferences(OMEGA_PREFS, android.content.Context.MODE_PRIVATE)
        if (prefs.getBoolean(OMEGA_MARKER_HINT_KEY, false)) return@LaunchedEffect
        kotlinx.coroutines.delay(OMEGA_HINT_DELAY_MS)
        if (store.markers.size > 0) return@LaunchedEffect
        store.showToast(hint)
        prefs.edit().putBoolean(OMEGA_MARKER_HINT_KEY, true).apply()
    }
}

/**
 * Cada cabeçalho é um elemento do leitor de tela (P0-3, mesmo id do iOS):
 * nome, estado (visível/oculta), escolhida, e as ações mostrar/ocultar, pôr na
 * seleção e mover para cima/baixo (quem não faz o gesto de reordenar). O olho
 * tem o seu elemento (`.eye`).
 */
@Composable
private fun OmegaLayerSemantics(store: EditorStore) {
    val show = stringResource(R.string.editor_mostrar_camada)
    val hide = stringResource(R.string.editor_ocultar_camada)
    val toggleSelection = stringResource(R.string.om_tl_toggle_selection)
    val up = stringResource(R.string.om_tl_move_up)
    val down = stringResource(R.string.om_tl_move_down)
    val visibleText = stringResource(R.string.om_tl_visible)
    val hiddenText = stringResource(R.string.om_tl_hidden)
    val layers = store.layers
    val selection = store.selection
    layers.forEachIndexed { index, l ->
        val id = l.id
        val visible = l.visible
        val name = l.name
        val count = layers.size
        Box(
            Modifier.size(1.dp).testTag("timeline.layer.$id").semantics {
                contentDescription = name
                stateDescription = if (visible) visibleText else hiddenText
                selected = id in selection
                onClick {
                    store.select(id)
                    true
                }
                customActions = buildList {
                    add(CustomAccessibilityAction(if (visible) hide else show) { store.setLayerVisible(id, !visible); true })
                    add(CustomAccessibilityAction(toggleSelection) {
                        if (!store.layerSelectMode) {
                            store.select(id, openOptions = false)
                            store.changeLayerSelectMode(true)
                        } else {
                            store.toggleLayerPick(id)
                        }
                        true
                    })
                    if (index > 0) add(CustomAccessibilityAction(up) { store.reorderLayer(id, index - 1); true })
                    if (index < count - 1) add(CustomAccessibilityAction(down) { store.reorderLayer(id, index + 1); true })
                }
            },
        )
        Box(
            Modifier.size(1.dp).testTag("timeline.layer.$id.eye").semantics {
                contentDescription = (if (visible) hide else show) + ": " + name
                onClick {
                    store.setLayerVisible(id, !visible)
                    true
                }
            },
        )
    }
}

/**
 * Régua e tempo atual para o leitor de tela (P2-4): a régua navega entre os
 * marcadores; o tempo atual marca/desmarca o instante e abre as opções dele (o
 * editor do marcador). O valor do tempo só é lido com o leitor de tela ligado
 * (senão o relógio recomporia a cada quadro do play).
 */
@Composable
private fun OmegaRulerSemantics(store: EditorStore, modifier: Modifier) {
    val ruler = stringResource(R.string.om_tl_ruler)
    val previous = stringResource(R.string.om_tl_prev_marker)
    val next = stringResource(R.string.om_tl_next_marker)
    val current = stringResource(R.string.om_tl_current_time)
    val mark = stringResource(R.string.editor_marcar_ou_desmarcar_este_instante)
    val options = stringResource(R.string.om_tl_time_options)
    val context = androidx.compose.ui.platform.LocalContext.current
    val reading = remember(context) {
        (context.getSystemService(android.content.Context.ACCESSIBILITY_SERVICE) as? android.view.accessibility.AccessibilityManager)
            ?.isTouchExplorationEnabled == true
    }
    Box(
        modifier.testTag("timeline.ruler").semantics {
            contentDescription = ruler
            customActions = listOf(
                CustomAccessibilityAction(previous) { store.seekToPreviousMarker() },
                CustomAccessibilityAction(next) { store.seekToFollowingMarker() },
            )
        },
    ) {
        val value = if (reading) Timecode.format(store.playhead, store.project.fps) else ""
        Box(
            Modifier.align(Alignment.TopCenter).size(100.dp, 38.dp).testTag("timeline.currentTime").semantics {
                contentDescription = current
                if (value.isNotEmpty()) stateDescription = value
                onClick(label = mark) {
                    store.toggleMarker()
                    true
                }
                customActions = listOf(CustomAccessibilityAction(options) { store.openMarkerEditor(store.playhead); true })
            },
        )
    }
}

/**
 * Alças do foco para o leitor de tela (P1-7/P1-8): aparar início e fim (ou
 * esticar a velocidade, com o painel Velocidade) um quadro por vez, para quem
 * não faz o gesto de segurar e arrastar.
 */
@Composable
private fun OmegaTrimSemantics(store: EditorStore, speed: Boolean) {
    val id = store.primary ?: return
    if (store.selection.size != 1) return
    val layer = store.layers.firstOrNull { it.id == id } ?: return
    if (layer.locked) return
    val name = layer.name
    val start = layer.startFrame
    val end = layer.endFrame
    val earlier = stringResource(R.string.om_tl_frame_earlier)
    val later = stringResource(R.string.om_tl_frame_later)
    val startLabel = stringResource(if (speed) R.string.om_tl_speed_handle_a11y else R.string.om_tl_trim_start_a11y, name)
    val endLabel = stringResource(if (speed) R.string.om_tl_speed_handle_a11y else R.string.om_tl_trim_end_a11y, name)
    fun startTo(frame: Int) {
        if (frame < 0 || frame >= end) return
        if (speed) store.stretchLayerSpan(id, frame, end) else store.trimStart(id, frame)
    }
    fun endTo(frame: Int) {
        if (frame <= start) return
        if (speed) store.stretchLayerSpan(id, start, frame) else store.trimEnd(id, frame)
    }
    Box(
        Modifier.size(1.dp).testTag("timeline.clip.$id.trimStart").semantics {
            contentDescription = startLabel
            customActions = listOf(
                CustomAccessibilityAction(earlier) { startTo(start - 1); true },
                CustomAccessibilityAction(later) { startTo(start + 1); true },
            )
        },
    )
    Box(
        Modifier.size(1.dp).testTag("timeline.clip.$id.trimEnd").semantics {
            contentDescription = endLabel
            customActions = listOf(
                CustomAccessibilityAction(earlier) { endTo(end - 1); true },
                CustomAccessibilityAction(later) { endTo(end + 1); true },
            )
        },
    )
}

private const val OMEGA_PREFS = "aurea.timeline"
private const val OMEGA_MARKER_HINT_KEY = "omega_marker_hint_shown"
private const val OMEGA_HINT_DELAY_MS = 900L

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
