package com.aurea.aurea.editor

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.onLongClick
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
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AnchoredMenuDefaults
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuColors
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.i18n.LocaleDirection
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.roundToInt

// =============================================================================
// Omega chrome (área 02, P0-5): a barra de reprodução da referência — 46 dp,
// 7 botões de peso igual: ↶ · ↷ · ⇤ · ▶ · ⇥ · Copiar e colar · painel.
// =============================================================================

/**
 * Desfazer · refazer · evento anterior · play · próximo evento · Copiar e
 * colar (popup) · painel (abre e fecha a coluna de modos à direita da prévia).
 * As setas vão ao evento estrito mais próximo: o começo, o último quadro, os
 * marcadores e, com camada escolhida, o início, o fim e os keyframes dela (sem
 * toque longo). A grade foi para a coluna; a resolução da prévia, para os
 * Ajustes (seção "Prévia"); "Selecionar uma camada", para o Copiar e colar.
 * 0.3.3 recursos: o marcador da 0.3.2 voltou, depois do ⇥ (8 células). Enquanto um dedo manipula
 * algo no palco, a barra vira a de informações.
 */
@Composable
internal fun TransportBar(store: EditorStore, ui: EditorUi) {
    // Transporte em LTR também em árabe: ⇤ à esquerda do ▶ e ⇥ à direita, no
    // sentido do tempo da timeline logo abaixo (ver `KeepLtr`).
    KeepLtr { TransportBarContent(store, ui) }
}

@Composable
private fun TransportBarContent(store: EditorStore, ui: EditorUi) {
    if (ui.manipulating) {
        InfoBar(store)
        return
    }
    val canUndo by remember { derivedStateOf { store.project.canUndo } }
    val canRedo by remember { derivedStateOf { store.project.canRedo } }
    val h = ShellDims.Transport
    Row(
        Modifier
            .fillMaxWidth()
            .height(h)
            .background(OmegaChrome.Bar),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 0.3.6: ⇤ ▶ ⇥ no CENTRO da barra. Com 8 células iguais o ▶ era a 4ª (43,75 %
        // da largura): os dois lados agora têm o mesmo peso (TransportLayout) e as
        // células de cada lado dividem o seu.
        Row(Modifier.weight(TransportLayout.SIDE_WEIGHT), verticalAlignment = Alignment.CenterVertically) {
            ChromeButton(
                CupertinoGlyph.ArrowUturnLeft, stringResource(R.string.editor_desfazer),
                onClick = if (canUndo) ({ store.undo() }) else null,
                modifier = Modifier.weight(1f).testTag("transport.undo"), size = 20.dp, height = h,
                tint = if (canUndo) AureaColors.Text else AureaColors.EditorIconDisabled,
            )
            ChromeButton(
                CupertinoGlyph.ArrowUturnRight, stringResource(R.string.editor_refazer),
                onClick = if (canRedo) ({ store.redo() }) else null,
                modifier = Modifier.weight(1f).testTag("transport.redo"), size = 20.dp, height = h,
                tint = if (canRedo) AureaColors.Text else AureaColors.EditorIconDisabled,
            )
        }
        Row(Modifier.weight(TransportLayout.CENTER_WEIGHT), verticalAlignment = Alignment.CenterVertically) {
            ChromeButton(
                // 0.3.3 recursos: segurar vai ao início (como na 0.3.2).
                CupertinoGlyph.BackwardEnd, stringResource(R.string.r33_tr_previous_hold),
                onClick = { store.stepTransportEdges(-1) }, onLongClick = { store.seek(0) },
                modifier = Modifier.weight(1f).testTag("transport.previous"), size = 20.dp, height = h,
            )
            PlayButton(store, Modifier.weight(1f))
            ChromeButton(
                // 0.3.3 recursos: segurar vai ao fim (como na 0.3.2).
                CupertinoGlyph.ForwardEnd, stringResource(R.string.r33_tr_next_hold),
                onClick = { store.stepTransportEdges(1) }, onLongClick = { store.seek(store.project.durationFrames) },
                modifier = Modifier.weight(1f).testTag("transport.next"), size = 20.dp, height = h,
            )
        }
        Row(Modifier.weight(TransportLayout.SIDE_WEIGHT), verticalAlignment = Alignment.CenterVertically) {
            // 0.3.3 recursos: o marcador da 0.3.2 volta ao transporte — tocar marca ou
            // desmarca o instante (aceso num marcador); segurar abre o editor dele
            // (nome, quadro e cor).
            val marked = store.playhead in store.markers.frames
            ChromeButton(
                if (marked) ShellGlyph.BookmarkSolid else CupertinoGlyph.Bookmark,
                stringResource(R.string.editor_marcar_ou_desmarcar_este_instante),
                onClick = { store.toggleMarker() }, onLongClick = { store.editMarkerAtPlayhead() },
                modifier = Modifier.weight(1f).testTag("transport.marker"), size = 18.dp, height = h,
                tint = if (marked) AureaColors.Accent else AureaColors.Text,
            )
            CopyPasteButton(store, ui, Modifier.weight(1f).height(h))
            val open = ViewModes.columnOpen
            val viewModes = stringResource(R.string.om_tr_view_modes)
            val onLabel = stringResource(R.string.common_on)
            val offLabel = stringResource(R.string.common_off)
            ChromeButton(
                if (ViewModes.camera) CupertinoGlyph.CameraViewfinder else ShellGlyph.SidebarRight,
                viewModes,
                onClick = { ViewModes.columnOpen = !ViewModes.columnOpen },
                modifier = Modifier
                    .weight(1f)
                    .testTag("transport.viewModes")
                    .semantics { stateDescription = if (open) onLabel else offLabel },
                size = 20.dp, height = h,
                tint = if (open) AureaColors.Accent else AureaColors.Text,
            )
        }
    }
}

/**
 * 0.3.6: as larguras da barra de reprodução — o grupo ⇤ ▶ ⇥ no centro (3 células de
 * 1/8 da largura, como antes) e os lados com o mesmo peso: ↶ ↷ à esquerda dividem o
 * dela, marcador · Copiar e colar · painel à direita dividem o dela.
 */
internal object TransportLayout {
    const val CENTER_WEIGHT = 3f
    const val SIDE_WEIGHT = (8f - CENTER_WEIGHT) / 2f

    /** O centro do ▶ (fração da largura): sempre o meio. */
    fun playCenter(): Float = (SIDE_WEIGHT + CENTER_WEIGHT / 2f) / (2f * SIDE_WEIGHT + CENTER_WEIGHT)
}

/**
 * Play/pausa (ícone 22, a célula inteira é o alvo). Segurar parado liga ou
 * desliga a repetição (o play fica em destaque e ganha o selinho do laço);
 * segurar e arrastar de lado pré-renderiza para aquele lado.
 */
@Composable
private fun PlayButton(store: EditorStore, modifier: Modifier = Modifier) {
    val playing = store.playing || store.preview.buffering
    val loop = store.looping
    val haptic = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }
    val playDescription = stringResource(when {
        playing -> R.string.editor_pausar
        loop -> R.string.editor_repeticao_ligada_segure_desligar
        else -> R.string.editor_reproduzir_segure_repetir
    })
    val forward = stringResource(R.string.prerender_forward)
    val backward = stringResource(R.string.prerender_backward)
    val cancel = stringResource(R.string.prerender_cancel)
    val hint = stringResource(R.string.prerender_gesture_hint)
    val progress = store.preRenderProgress
    val progressDescription = when {
        store.preRenderPreparing -> stringResource(R.string.prerender_preparing)
        progress.running -> stringResource(R.string.prerender_progress, (progress.fraction * 100).roundToInt())
        progress.complete -> stringResource(R.string.prerender_ready)
        else -> ""
    }
    Box(
        modifier.height(ShellDims.Transport).testTag("transport.play")
            .alpha(if (pressed) 0.65f else 1f)
            .semantics {
                role = Role.Button
                contentDescription = "$playDescription. $hint"
                stateDescription = progressDescription
                onClick { if (store.preview.buffering) store.pause() else store.togglePlayback(); true }
                onLongClick { store.setLoop(!store.looping); true }
                customActions = listOf(
                    CustomAccessibilityAction(forward) { store.startPreRender(1); true },
                    CustomAccessibilityAction(backward) { store.startPreRender(-1); true },
                    CustomAccessibilityAction(cancel) { store.cancelPreRender(); true },
                )
            }
            .onKeyEvent { event ->
                if (event.type == KeyEventType.KeyUp && event.key in listOf(Key.Enter, Key.NumPadEnter, Key.Spacebar)) {
                    if (store.preview.buffering) store.pause() else store.togglePlayback()
                    true
                } else false
            }
            .focusable()
            .pointerInput(store) {
                val threshold = 24.dp.toPx()
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    pressed = true
                    var change = down
                    var cancelled = false
                    try {
                        val releasedEarly = withTimeoutOrNull(500L) {
                            while (change.pressed) {
                                val next = awaitPointerEvent().changes.firstOrNull { it.id == down.id }
                                if (next == null || next.isConsumed || (next.position - down.position).getDistance() > viewConfiguration.touchSlop) {
                                    cancelled = true
                                    return@withTimeoutOrNull false
                                }
                                change = next
                                change.consume()
                            }
                            true
                        }
                        if (!cancelled && releasedEarly == true) {
                            haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                            if (store.preview.buffering) store.pause() else store.togglePlayback()
                        } else if (!cancelled) {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            var fired = false
                            while (change.pressed) {
                                val next = awaitPointerEvent().changes.firstOrNull { it.id == down.id }
                                if (next == null || next.isConsumed) { cancelled = true; break }
                                change = next
                                val delta = change.position - down.position
                                if (abs(delta.y) >= threshold && abs(delta.y) > abs(delta.x)) cancelled = true
                                if (!fired && !cancelled && abs(delta.x) >= threshold && abs(delta.x) > abs(delta.y)) {
                                    fired = true
                                    store.startPreRender(if (delta.x > 0) 1 else -1)
                                }
                                change.consume()
                            }
                            if (!fired && !cancelled) store.setLoop(!store.looping)
                        }
                    } finally { pressed = false }
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(if (playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill,
            22.dp, if (loop) AureaColors.Accent else AureaColors.Text)
        if (store.preRenderPreparing || progress.running || progress.complete) {
            val color = AureaColors.Accent
            Canvas(Modifier.size(30.dp)) {
                drawArc(color, -90f, 360f * if (store.preRenderPreparing) 0.08f else progress.fraction,
                    false, style = androidx.compose.ui.graphics.drawscope.Stroke(2.dp.toPx(), cap = StrokeCap.Round))
            }
        }
        if (loop) {
            // O selinho do laço, no canto do ícone (não da célula, que é larga).
            Box(Modifier.size(34.dp)) {
                CupertinoIcon(
                    CupertinoGlyph.Repeat,
                    11.dp,
                    AureaColors.Accent,
                    Modifier.align(Alignment.BottomEnd),
                )
            }
        }
    }
}

/**
 * Omega chrome (área 02, P1-4): a barra de informações no lugar da de
 * reprodução enquanto um dedo manipula algo no palco — modo texto, até 6 pares
 * em 3 colunas: rótulo 12 sp a 50 % e valor 12 sp. (A timeline mostra o tempo
 * do arrasto dela na própria faixa: área 03.)
 */
@Composable
private fun InfoBar(store: EditorStore) {
    val d = store.detail
    val pairs = if (d == null) emptyList() else listOf(
        "X" to d.position[0].roundToInt().toString(),
        "Y" to d.position[1].roundToInt().toString(),
        stringResource(R.string.editor_escala) to "${(d.scale[0] * 100f).roundToInt()}%",
        stringResource(R.string.editor_rotacao) to "${"%.1f".format(java.util.Locale.ROOT, d.rotation[2]).replace('.', ',')}°",
    )
    Column(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.Transport)
            .background(OmegaChrome.Bar)
            .testTag("transport.info")
            .padding(horizontal = 12.dp, vertical = 4.dp),
        verticalArrangement = androidx.compose.foundation.layout.Arrangement.Center,
    ) {
        pairs.take(6).chunked(3).forEach { line ->
            Row(Modifier.fillMaxWidth().weight(1f), verticalAlignment = Alignment.CenterVertically) {
                line.forEach { (label, value) -> InfoPair(label, value) }
                repeat(3 - line.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

@Composable
private fun RowScope.InfoPair(label: String, value: String) {
    Row(Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text.copy(alpha = 0.5f))))
        Spacer(Modifier.width(6.dp))
        Text(
            value,
            maxLines = 1,
            overflow = TextOverflow.Clip,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)).merge(AureaType.Tabular),
        )
    }
}

// =============================================================================
// Barra de tempo da tela cheia (44 dp)
// =============================================================================

/**
 * Na tela cheia a timeline some; sem esta barra não havia como andar pelo
 * vídeo. Tocar pula para o ponto, arrastar percorre (scrub), e se estava
 * tocando volta a tocar ao soltar.
 */
@Composable
internal fun FullscreenTimeBar(store: EditorStore) {
    // A barra é tempo: o início à esquerda em qualquer idioma (ver `KeepLtr`).
    KeepLtr { FullscreenTimeBarContent(store) }
}

@Composable
private fun FullscreenTimeBarContent(store: EditorStore) {
    val haptic = LocalHapticFeedback.current
    var dragging by remember { mutableStateOf(false) }
    val style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)).merge(AureaType.Tabular)
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.FullscreenTimeBar)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(ShellTime.tenths(store.playhead, store.project.fps), style = style)
        Spacer(Modifier.width(12.dp))
        Canvas(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .pointerInput(store) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        fun frameAt(x: Float): Int {
                            val total = store.project.durationFrames
                            val f = if (size.width > 0) (x / size.width).coerceIn(0f, 1f) else 0f
                            return (f * (total - 1).coerceAtLeast(0)).roundToInt()
                        }
                        store.seek(frameAt(down.position.x))
                        val slop = viewConfiguration.touchSlop
                        var scrub = false
                        var wasPlaying = false
                        var travelled = 0f
                        while (true) {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!c.pressed) break
                            travelled += abs(c.positionChange().x)
                            if (!scrub && travelled > slop) {
                                scrub = true
                                wasPlaying = store.playing
                                if (wasPlaying) store.pause()
                                dragging = true
                                haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                store.scrubStart(frameAt(c.position.x))
                            } else if (scrub) {
                                store.scrubTo(frameAt(c.position.x))
                            }
                            c.consume()
                        }
                        if (scrub) {
                            store.scrubEnd()
                            dragging = false
                            if (wasPlaying) store.play()
                        }
                    }
                },
        ) {
            val total = store.project.durationFrames
            val f = if (total > 1) (store.playhead.toFloat() / (total - 1)).coerceIn(0f, 1f) else 0f
            val y = size.height / 2
            val thick = (if (dragging) 5.dp else 3.dp).toPx()
            val x = size.width * f
            drawLine(ShellColors.Track22, Offset(0f, y), Offset(size.width, y), thick, StrokeCap.Round)
            drawLine(AureaColors.Accent, Offset(0f, y), Offset(x, y), thick, StrokeCap.Round)
            drawCircle(AureaColors.Text, (if (dragging) 9.dp else 7.dp).toPx(), Offset(x, y))
        }
        Spacer(Modifier.width(12.dp))
        Text(ShellTime.tenths(store.project.durationFrames, store.project.fps), style = style.merge(TextStyle(color = AureaColors.Muted)))
    }
}

// =============================================================================
// Omega chrome (área 02, P0-6/P1-3): popup "Copiar e colar" (`layer_copy_paste_menu_popup`)
// =============================================================================

/**
 * O 6º botão do transporte e o popup dele, ancorado no botão. Itens na ordem
 * da referência: selecionar todas (ou limpar a seleção, com todas escolhidas) ·
 * duplicar · salvar preset · copiar (com estilo, quando é uma) · colar ·
 * colar estilo ›. Depois de um fio, o grupo "Mais do AUREA": selecionar uma
 * camada, modo de escolher várias, efeitos, keyframes e animação e, com
 * várias escolhidas, ajustar/preencher a tela, desagrupar e vincular.
 */
@Composable
private fun CopyPasteButton(store: EditorStore, ui: EditorUi, modifier: Modifier) {
    var menu by remember { mutableStateOf(false) }
    var pasteStyle by remember { mutableStateOf(false) }
    var picker by remember { mutableStateOf(false) }
    var linking by remember { mutableStateOf(false) }
    Box(modifier) {
        ChromeButton(
            CupertinoGlyph.DocOnClipboard, stringResource(R.string.om_tr_copy_paste),
            onClick = {
                if (store.playing) store.pause()
                menu = true
            },
            modifier = Modifier.matchParentSize().testTag("transport.copyPaste"), size = 20.dp, height = ShellDims.Transport,
        )
        LocaleDirection {
            if (menu) {
                AnchoredMenu(
                    expanded = true,
                    onDismiss = { menu = false },
                    entries = copyPasteEntries(
                        store, ui,
                        onPasteStyle = { pasteStyle = true },
                        onPick = { picker = true },
                        onLink = { linking = true },
                    ),
                    tag = "copyPaste.menu",
                )
            }
            if (pasteStyle) PasteStylePage(store) { pasteStyle = false }
            if (picker) LayerPickerPopup(store) { picker = false }
            if (linking) LinkMenu(store, store.selection.toList()) { linking = false }
        }
    }
}

@Composable
private fun copyPasteEntries(
    store: EditorStore,
    ui: EditorUi,
    onPasteStyle: () -> Unit,
    onPick: () -> Unit,
    onLink: () -> Unit,
): List<MenuEntry> {
    val selection = store.selection
    val count = selection.size
    val total = store.layers.size
    val clip = store.clipboard
    val primary = store.primary
    val out = mutableListOf<MenuEntry>()
    out += if (total > 0 && count == total) {
        MenuEntry.Item(stringResource(R.string.editor_limpar_selecao), glyph = CupertinoGlyph.Square, tag = "copyPaste.clearSelection") {
            store.clearSelection()
        }
    } else {
        MenuEntry.Item(
            stringResource(R.string.editor_selecionar_todas_camadas), glyph = CupertinoGlyph.CheckmarkSquare,
            enabled = total > 0, tag = "copyPaste.selectAll",
        ) { store.selectAll() }
    }
    out += MenuEntry.Item(
        pluralStringResource(R.plurals.om_cp_duplicate, max(1, count), count),
        glyph = CupertinoGlyph.PlusSquareOnSquare, enabled = count > 0, tag = "copyPaste.duplicate",
    ) { store.duplicateLayers() }
    out += MenuEntry.Item(
        stringResource(R.string.om_cp_save_preset), glyph = CupertinoGlyph.SquareStack3dUp,
        enabled = count == 1, tag = "copyPaste.savePreset",
    ) { openPanel(store, ui, EditorPanel.Presets) }
    out += MenuEntry.Item(
        pluralStringResource(R.plurals.om_cp_copy, max(1, count), count),
        glyph = CupertinoGlyph.DocOnDoc,
        sublabel = if (count == 1) stringResource(R.string.om_cp_with_style) else null,
        enabled = count > 0, tag = "copyPaste.copy",
    ) { store.copyLayersWithStyle() }
    out += MenuEntry.Item(
        pluralStringResource(R.plurals.om_cp_paste, 1, 1),
        glyph = CupertinoGlyph.DocOnClipboard, enabled = clip and 1 != 0, tag = "copyPaste.paste",
    ) { store.pasteLayers() }
    out += MenuEntry.Item(
        stringResource(R.string.om_cp_paste_style_more),
        glyph = ShellGlyph.PaintbrushFill, chevron = true,
        enabled = count > 0 && clip and 2 != 0, tag = "copyPaste.pasteStyle",
    ) { onPasteStyle() }

    // Mais do AUREA (o que a referência não tem), depois de um fio.
    out += MenuEntry.Divider
    out += MenuEntry.Header(stringResource(R.string.om_settings_more))
    out += MenuEntry.Item(
        stringResource(R.string.editor_selecionar_uma_camada), glyph = CupertinoGlyph.RectangleStack,
        enabled = total > 0, tag = "copyPaste.more.pickLayer",
    ) { onPick() }
    out += MenuEntry.Item(
        stringResource(R.string.edt_tl_pick_layers), glyph = CupertinoGlyph.CheckmarkSquare,
        sublabel = stringResource(R.string.edt_tl_pick_layers_detail),
        checked = store.layerSelectMode, enabled = total > 0, tag = "copyPaste.more.pickMode",
    ) { store.changeLayerSelectMode(!store.layerSelectMode) }
    out += MenuEntry.Item(
        stringResource(R.string.edt_cmd_search_desc), glyph = CupertinoGlyph.Search, tag = "commandSearchOpen",
    ) { openSheet(store, ui, ShellSheet.CommandSearch) }
    out += MenuEntry.Item(
        stringResource(R.string.editor_copiar_efeitos), glyph = CupertinoGlyph.Sparkles,
        enabled = primary != null, tag = "copyPaste.more.copyEffects",
    ) { store.copyEffects() }
    out += MenuEntry.Item(
        stringResource(R.string.editor_colar_efeitos), glyph = CupertinoGlyph.WandStars,
        enabled = count > 0 && clip and 4 != 0, tag = "copyPaste.more.pasteEffects",
    ) { store.pasteEffects() }
    out += MenuEntry.Item(
        stringResource(R.string.editor_copiar_keyframes_cabecote), glyph = CupertinoGlyph.DocOnDoc,
        enabled = primary != null, tag = "copyPaste.more.copyKeys",
    ) { store.copyKeyframes() }
    out += MenuEntry.Item(
        stringResource(R.string.editor_colar_keyframes_cabecote), glyph = CupertinoGlyph.DocOnClipboard,
        enabled = count > 0 && clip and 8 != 0, tag = "copyPaste.more.pasteKeys",
    ) { store.pasteKeyframes() }
    out += MenuEntry.Item(
        stringResource(R.string.editor_copiar_animacao), glyph = CupertinoGlyph.DocOnDoc,
        sublabel = stringResource(R.string.editor_copiar_animacao_detalhe),
        enabled = primary != null, tag = "copyPaste.more.copyAnimation",
    ) { store.copyAnimation() }
    out += MenuEntry.Item(
        stringResource(R.string.editor_otimizar_keyframes), glyph = CupertinoGlyph.WandStars,
        sublabel = stringResource(R.string.editor_otimizar_keyframes_detalhe),
        enabled = primary != null, tag = "copyPaste.more.optimizeKeys",
    ) { store.optimizeKeyframes() }
    if (count >= 2) {
        out += MenuEntry.Item(
            stringResource(R.string.editor_ajustar_tela), glyph = CupertinoGlyph.FullscreenExit, tag = "copyPaste.more.fit",
        ) { LayerOps.fitToCanvas(store, store.selection, fill = false) }
        out += MenuEntry.Item(
            stringResource(R.string.editor_preencher_tela), glyph = CupertinoGlyph.Fullscreen, tag = "copyPaste.more.fill",
        ) { LayerOps.fitToCanvas(store, store.selection, fill = true) }
        // 0.3.3 recursos: "Em escada" (cada uma começa quando a de cima termina) — no
        // telefone em pé a folha de lote da 0.3.2 saiu e a barra de baixo não tem esta.
        out += MenuEntry.Item(
            stringResource(R.string.editor_escada_comeca_quando_cima_termina), glyph = CupertinoGlyph.ArrowRightToLine,
            tag = "copyPaste.more.stairs",
        ) { store.arrangeLayerTimes(1) }
        val groups = store.layers.filter { it.id in selection && it.kind == LayerType.Group.kind }.map { it.id }
        if (groups.isNotEmpty()) {
            out += MenuEntry.Item(stringResource(R.string.editor_desagrupar), glyph = ShellGlyph.SquareSplit2x2, tag = "copyPaste.more.ungroup") {
                groups.forEach { store.ungroupPrecomp(it) }
            }
        }
        out += MenuEntry.Item(
            stringResource(R.string.om_cp_link_selected), glyph = CupertinoGlyph.Link, tag = "copyPaste.more.link",
        ) {
            if (store.playing) store.pause()
            onLink()
        }
    }
    return out
}

/** As partes do "Colar estilo" (bits de `Engine::Style*`), na ordem das duas fileiras da referência. */
private data class StylePart(val bit: Int, val glyph: Char, val label: Int, val tag: String)

private val StyleRowA = listOf(
    StylePart(1, CupertinoGlyph.DropFill, R.string.om_ps_color_fill, "pasteStyle.colorFill"),
    StylePart(2, CupertinoGlyph.SquareOnSquare, R.string.om_ps_border_shadow, "pasteStyle.borderShadow"),
    StylePart(4, CupertinoGlyph.CircleLefthalfFill, R.string.om_ps_blend, "pasteStyle.blend"),
)
private val StyleRowB = listOf(
    StylePart(8, CupertinoGlyph.Move, R.string.om_ps_transform, "pasteStyle.transform"),
    StylePart(16, CupertinoGlyph.Textformat, R.string.om_ps_text, "pasteStyle.text"),
    StylePart(32, CupertinoGlyph.Speaker2, R.string.om_ps_volume, "pasteStyle.volume"),
    StylePart(64, CupertinoGlyph.Sparkles, R.string.om_ps_effects, "pasteStyle.effects"),
    StylePart(128, CupertinoGlyph.Speedometer, R.string.om_ps_speed, "pasteStyle.speed"),
)

/** O repique da subpágina (o `OvershootInterpolator` da referência). */
private val PageOvershoot = CubicBezierEasing(0.34f, 1.4f, 0.64f, 1f)

/**
 * A subpágina "Colar estilo": duas fileiras de botões de 58 dp (cor e
 * preenchimento · borda e sombra · mesclagem e opacidade / mover e transformar ·
 * texto · volume · efeitos · velocidade). Habilitado = a parte existe no estilo
 * copiado; aceso = escolhido. Ao trocar, o título mostra o nome da parte por 1 s.
 * Colar aplica só as escolhidas (um passo de desfazer).
 */
@Composable
private fun PasteStylePage(store: EditorStore, onDismiss: () -> Unit) {
    val density = LocalDensity.current
    val provider = remember(density) {
        with(density) { AnchorPopupPosition(AnchoredMenuDefaults.ScreenMargin.roundToPx(), AnchoredMenuDefaults.AnchorGap.roundToPx()) }
    }
    val colors = MenuColors.of(AureaSurfaceTone.Light)
    val available = remember { store.copiedStyleParts() }
    var chosen by remember { mutableIntStateOf(available) }
    var flash by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(flash) {
        if (flash != null) {
            delay(1000)
            flash = null
        }
    }
    val enter = remember { Animatable(0.5f) }
    LaunchedEffect(Unit) { enter.animateTo(0f, tween(250, easing = PageOvershoot)) }
    val title = stringResource(R.string.om_cp_paste_style_more)
    val back = stringResource(R.string.om_menu_back)
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        Column(
            Modifier
                .width(AnchoredMenuDefaults.Width)
                .graphicsLayer { translationX = enter.value * size.width }
                .clip(RoundedCornerShape(AnchoredMenuDefaults.Radius))
                .background(colors.background)
                .testTag("pasteStyle.page"),
        ) {
            Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier
                        .size(40.dp, 40.dp)
                        .testTag("pasteStyle.back")
                        .semantics { contentDescription = back }
                        .tocavel(shrink = 1f, onClick = onDismiss),
                    contentAlignment = Alignment.Center,
                ) { CupertinoIcon(CupertinoGlyph.ChevronLeft, 16.dp, colors.text) }
                Text(
                    flash ?: title,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = colors.text)),
                    modifier = Modifier.weight(1f).padding(end = 12.dp),
                )
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(colors.divider))
            listOf(StyleRowA, StyleRowB.filter { it.bit != 32 || available and 32 != 0 }).forEach { row ->
                Row(Modifier.fillMaxWidth().height(58.dp).padding(horizontal = 4.dp, vertical = 3.dp)) {
                    row.forEach { part ->
                        val label = stringResource(part.label)
                        val enabled = available and part.bit != 0
                        val on = enabled && chosen and part.bit != 0
                        Box(
                            Modifier
                                .weight(1f)
                                .fillMaxHeight()
                                .padding(3.dp)
                                .clip(RoundedCornerShape(6.dp))
                                .background(if (on) AureaColors.Accent else colors.pressed)
                                .testTag(part.tag)
                                .semantics {
                                    contentDescription = label
                                    selected = on
                                    if (!enabled) disabled()
                                }
                                .tocavel(enabled = enabled, shrink = 1f, haptic = true) {
                                    chosen = chosen xor part.bit
                                    if (chosen and part.bit != 0) flash = label
                                }
                                .alpha(if (enabled) 1f else AnchoredMenuDefaults.DISABLED_ALPHA),
                            contentAlignment = Alignment.Center,
                        ) { CupertinoIcon(part.glyph, 20.dp, if (on) AureaColors.OnAccent else colors.text) }
                    }
                }
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(colors.divider))
            Row(Modifier.fillMaxWidth().height(46.dp)) {
                val paste = stringResource(R.string.om_ps_paste)
                Text(
                    paste,
                    textAlign = TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = colors.accent)),
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .testTag("pasteStyle.paste")
                        .alpha(if (chosen != 0) 1f else AnchoredMenuDefaults.DISABLED_ALPHA)
                        .tocavel(enabled = chosen != 0, shrink = 1f, haptic = true) {
                            onDismiss()
                            store.pasteStyleParts(chosen)
                        }
                        .padding(top = 13.dp),
                )
                Box(Modifier.width(1.dp).fillMaxHeight().background(colors.divider))
                Text(
                    stringResource(R.string.common_cancel),
                    textAlign = TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = colors.text)),
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .testTag("pasteStyle.cancel")
                        .tocavel(shrink = 1f, onClick = onDismiss)
                        .padding(top = 13.dp),
                )
            }
        }
    }
}
