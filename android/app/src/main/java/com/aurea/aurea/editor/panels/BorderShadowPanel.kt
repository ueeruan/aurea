package com.aurea.aurea.editor.panels

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import com.aurea.aurea.R
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.FxParamLabel
import com.aurea.aurea.ui.ds.FxRuler
import com.aurea.aurea.ui.ds.FxSegmented
import com.aurea.aurea.ui.ds.FxValueBox
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.displayToEngine
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import kotlin.math.max
import kotlin.math.roundToInt

// =============================================================================
// Omega aparência (área 05, P0-4 + P1-9): BORDA E SOMBRA.
//
// A moldura comum (PanelFrame) com três abas na coluna direita:
//   · Traço (só forma SDF e texto): amostra · "Traço" · interruptor, régua de
//     largura 0–300 px (passo 0,5) e caixa 55×45. Forma: contorno da forma
//     (cor RGBA8 + parâmetro de forma 4, animável); texto: TextData.stroke*.
//   · Bordas: as decorações role 2 do motor (efeito "Contorno da silhueta"),
//     itens de 37 (seta · tamanho · cor · direção · ≡) e "+ Adicionar borda".
//     Tocar abre os ajustes NO LUGAR (recorte + translação em 200 ms) com
//     Direção · Cor · Tamanho, cada linha com ◆; ≡ arrasta e reordena; deslizar
//     para a esquerda apaga (com "Desfazer").
//   · Sombra: a decoração role 3 ("Sombra projetada"): Cor · Tamanho
//     (suavidade) · Alfa · Deslocamento X/Y (o motor converte para direção e
//     distância).
// O trilho (◆ · curva) mira o parâmetro em foco: a largura do traço da forma,
// a linha tocada da borda aberta ou da sombra. Arrasto = um passo de desfazer.
// Par exato do BorderShadowPanel.swift (mesmos ids, textos e regras).
// =============================================================================

private const val TAB_STROKE = "stroke"
private const val TAB_BORDERS = "borders"
private const val TAB_SHADOW = "shadow"

/** Parâmetros reaproveitados (EffectRegistry): borda = `aurea.stylize.stroke_outline`, sombra = `aurea.stylize.drop_shadow`. */
private object BsParam {
    const val BORDER_WIDTH = 0
    const val BORDER_COLOR = 1
    const val BORDER_POSITION = 3     // enum 0 Fora, 1 Centro, 2 Dentro
    const val SHADOW_COLOR = 0
    const val SHADOW_OPACITY = 1      // %
    const val SHADOW_DIRECTION = 2    // °
    const val SHADOW_DISTANCE = 3     // px
    const val SHADOW_SOFTNESS = 4     // px
    const val SHAPE_STROKE_WIDTH = 4  // parâmetro de forma
}

/** Medidas (referência: `fragment_border_and_shadow`, `border_list_item`). */
private object BsDims {
    /** Linha amostra · título · interruptor (37,5 na referência; 44 = alvo de toque). */
    val Header = 44.dp
    val Swatch = 24.5.dp
    val Ruler = 57.dp
    val RulerTop = 5.dp
    val ValueW = 55.dp
    val ValueH = 45.dp
    val Gap = 6.5.dp
    /** Item de 37 com a margem de 6,5 em cima e 0,5 embaixo: o item inteiro é alvo de 44. */
    val Slot = 44.dp
    val HeaderSlot = 43.5.dp
    val Radius = 5.dp
    val Cue = 18.dp
    val Dot = 20.dp
    val Label = 60.dp
    val Key = 40.dp
    val Row = 44.dp
    val Pad = 10.dp
    const val OPEN_MS = 200
    const val STROKE_DEFAULT = 4f
    const val STROKE_MAX = 300f
    /** O motor prende a largura do contorno do texto em 200 px (EditorStore.setTextStrokeWidth). */
    const val TEXT_STROKE_MAX = 200f
    const val SIZE_MAX = 300f
    const val OFFSET_MAX = 1000f
}

/** O que o trilho mira (◆, curva, apagar todos e o foco da timeline). */
private sealed class BsFocus {
    object StrokeWidth : BsFocus()
    data class Param(val effectId: Int, val param: Int) : BsFocus()
    data class ShadowOffset(val effectId: Int) : BsFocus()
}

/** A última largura do traço por camada (o interruptor desliga com 0 e religa com ela). */
private object BsMemory {
    val strokeWidth = HashMap<Long, Float>()
}

/** Retângulo da amostra tocada (px da janela): o balão do seletor ancora nele. */
private class BsAnchor {
    var rect: Rect? = null
}

/** Coordenadas da lista de bordas: a caixa e cada item (para abrir os ajustes a partir da linha). */
private class BsCoords {
    var box: LayoutCoordinates? = null
    val items = HashMap<Int, LayoutCoordinates>()
}

@Composable
internal fun BorderShadowPanel(env: PanelEnv) {
    val store = env.store
    val layerId = store.primary
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val locked by remember(store) { derivedStateOf { store.detail?.locked == true } }
    val vector by remember(store) { derivedStateOf { store.isVectorLayer } }
    val isText = kind == LayerType.Text.kind
    val isShape = kind == LayerType.Shape.kind && !vector
    val hasStroke = isText || isShape
    var tab by rememberSaveable(layerId) { mutableStateOf(if (hasStroke) TAB_STROKE else TAB_BORDERS) }
    val currentTab = if (tab == TAB_STROKE && !hasStroke) TAB_BORDERS else tab
    var focus by remember(layerId) { mutableStateOf<BsFocus?>(null) }
    var openBorder by remember(layerId) { mutableStateOf<Int?>(null) }
    val shadowId = store.decorations.firstOrNull { it.role == LayerEffect.ROLE_SHADOW }?.effectId

    // O parâmetro em foco do trilho, pela aba.
    val railFocus: BsFocus? = when (currentTab) {
        TAB_STROKE -> if (isShape) BsFocus.StrokeWidth else null
        TAB_BORDERS -> openBorder?.let { id -> (focus as? BsFocus.Param)?.takeIf { it.effectId == id } ?: BsFocus.Param(id, BsParam.BORDER_WIDTH) }
        else -> shadowId?.let { id ->
            focus?.takeIf { (it is BsFocus.Param && it.effectId == id) || (it is BsFocus.ShadowOffset && it.effectId == id) }
                ?: BsFocus.Param(id, BsParam.SHADOW_SOFTNESS)
        }
    }
    val look = focusLook(store, railFocus)
    val track = focusTrack(store, railFocus)
    val canKey = railFocus != null && !locked && focusCanKey(store, railFocus)
    val timelineKeys = focusTimelineKeys(store, railFocus)
    DisposableEffect(store, layerId, timelineKeys) {
        store.timelineFocus = timelineKeys.ifEmpty { null }
        onDispose { store.timelineFocus = null }
    }

    val tabs = buildList<PanelTab> {
        if (hasStroke) add(PanelTab(TAB_STROKE, stringResource(R.string.om_ap_bs_stroke), glyph = CupertinoGlyph.Scribble))
        add(PanelTab(TAB_BORDERS, stringResource(R.string.om_ap_bs_borders), glyph = CupertinoGlyph.Square))
        add(PanelTab(TAB_SHADOW, stringResource(R.string.om_ap_bs_shadow), glyph = CupertinoGlyph.SquareStack3dDownRight))
    }
    PanelFrame(
        rail = PanelRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = if (canKey && railFocus != null) ({ toggleFocusKey(store, railFocus) }) else null,
            onDeleteAllKeyframes = if (canKey && railFocus != null) ({ deleteFocusKeys(store, railFocus) }) else null,
            onCurve = if (canKey && railFocus != null) ({ openFocusCurve(env, railFocus) }) else null,
            curveReady = track.size >= 2,
        ),
        tabs = tabs,
        selectedTab = currentTab,
        onTab = { id ->
            tab = id
            if (id != TAB_BORDERS) openBorder = null
        },
        scrollContent = false,
    ) {
        when (currentTab) {
            TAB_STROKE -> BsStrokeTab(env, isShape, locked, onFocus = { focus = BsFocus.StrokeWidth })
            TAB_BORDERS -> BordersTab(
                env, locked, openBorder, railFocus,
                onOpen = { openBorder = it },
                onFocus = { focus = it },
            )
            else -> ShadowTab(env, locked, railFocus, onFocus = { focus = it })
        }
    }
}

// =============================================================================
// Traço
// =============================================================================

@Composable
private fun ColumnScope.BsStrokeTab(env: PanelEnv, isShape: Boolean, locked: Boolean, onFocus: () -> Unit) {
    val store = env.store
    val owner = store.primary
    val width by remember(store, isShape) {
        derivedStateOf {
            if (isShape) store.shapeParams?.getOrNull(BsParam.SHAPE_STROKE_WIDTH) ?: store.detail?.shapeStrokeWidth ?: 0f
            else store.textDetail?.strokeWidth ?: 0f
        }
    }
    val color by remember(store, isShape) {
        derivedStateOf {
            bsColor(if (isShape) bsRgba8(store.detail?.shapeStroke ?: 0) else store.textDetail?.strokeColor ?: floatArrayOf(1f, 1f, 1f, 1f))
        }
    }
    val maxWidth = if (isShape) BsDims.STROKE_MAX else BsDims.TEXT_STROKE_MAX
    val title = stringResource(R.string.om_ap_bs_stroke)
    val widthTitle = stringResource(R.string.om_ap_bs_width)
    val anchor = remember { BsAnchor() }
    fun current(): Boolean = owner != null && store.primary == owner && store.detail?.locked != true
    fun setColor(r: Float, g: Float, b: Float, a: Float) {
        if (isShape) store.setShapeStroke(r, g, b, a) else store.setTextStrokeColor(r, g, b, a)
    }
    // Largura sem cor: o traço nasce visível (senão o número mente).
    fun ensureVisible() {
        if (color.alpha <= 0f) setColor(color.red, color.green, color.blue, 1f)
    }
    fun writeWidth(v: Float) {
        if (!current() || !v.isFinite()) return
        val w = v.coerceIn(0f, maxWidth)
        if (isShape) store.setShapeParam(BsParam.SHAPE_STROKE_WIDTH, w) else store.setTextStrokeWidth(w)
    }
    fun setOn(on: Boolean) {
        val id = owner ?: return
        if (!current()) return
        store.beginGesture("traço")
        if (on) {
            ensureVisible()
            writeWidth(BsMemory.strokeWidth[id]?.takeIf { it > 0f } ?: BsDims.STROKE_DEFAULT)
        } else {
            if (width > 0f) BsMemory.strokeWidth[id] = width
            writeWidth(0f)
        }
        store.endGesture()
    }
    fun openColor() {
        if (!current()) return
        store.beginGesture("cor do traço")
        env.openColor(
            ColorRequest(
                initial = floatArrayOf(color.red, color.green, color.blue, if (color.alpha > 0f) color.alpha else 1f),
                onChange = { r, g, b, a -> if (current()) setColor(r, g, b, a) },
                onDone = { store.endGesture() },
                anchor = anchor.rect,
            ),
        )
    }
    Column(
        Modifier
            .fillMaxSize()
            .testTag("border.tab.stroke")
            .verticalScroll(rememberScrollState())
            .padding(horizontal = BsDims.Pad),
    ) {
        BsHeaderLine(
            title = title,
            color = color,
            swatchTag = "border.stroke.color",
            switchTag = "border.stroke.switch",
            checked = width > 0f,
            enabled = !locked,
            swatchEnabled = !locked,
            anchor = anchor,
            onSwatch = { openColor() },
            onSwitch = { setOn(it) },
        )
        Row(
            Modifier.fillMaxWidth().padding(top = BsDims.RulerTop).height(BsDims.Ruler),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            FxRuler(
                value = width,
                step = 0.5f,
                min = 0f,
                max = maxWidth,
                onStart = {
                    onFocus()
                    store.beginGesture("largura do traço")
                    if (current()) ensureVisible()
                },
                onValue = { writeWidth(it) },
                onEnd = { store.endGesture() },
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .testTag("border.stroke.ruler")
                    .semantics { contentDescription = widthTitle },
                enabled = !locked,
                needle = AureaColors.Accent,
            )
            Spacer(Modifier.width(4.dp))
            FxValueBox(
                text = numeroPtBr(width, 1),
                height = BsDims.ValueH,
                modifier = Modifier.testTag("border.stroke.width").semantics { contentDescription = widthTitle },
                width = BsDims.ValueW,
                enabled = !locked,
                onClick = {
                    onFocus()
                    env.openKeypad(
                        KeypadRequest(widthTitle, width, "px", 0f, maxWidth, 1) { v ->
                            if (current()) {
                                store.beginGesture("largura do traço")
                                if (v > 0f) ensureVisible()
                                writeWidth(v)
                                store.endGesture()
                            }
                        },
                    )
                },
            )
        }
        Spacer(Modifier.height(8.dp))
    }
}

// =============================================================================
// Bordas
// =============================================================================

@Composable
private fun ColumnScope.BordersTab(
    env: PanelEnv,
    locked: Boolean,
    openId: Int?,
    focus: BsFocus?,
    onOpen: (Int?) -> Unit,
    onFocus: (BsFocus) -> Unit,
) {
    val store = env.store
    val layerId = store.primary
    val borders = store.decorations.filter { it.role == LayerEffect.ROLE_BORDER }
    val latest by rememberUpdatedState(borders)
    val pitch = with(LocalDensity.current) { BsDims.Slot.toPx() }
    val coords = remember(layerId) { BsCoords() }
    var dragId by remember(layerId) { mutableStateOf<Int?>(null) }
    var dragOffset by remember(layerId) { mutableFloatStateOf(0f) }
    var dragOrder by remember(layerId) { mutableStateOf<List<Int>?>(null) }

    // Abrir no lugar (P1-9): o painel de ajustes nasce na linha tocada e cresce
    // até ocupar a lista (recorte + translação em 200 ms); fechar faz o contrário.
    val progress = remember(layerId) { Animatable(0f) }
    var shown by remember(layerId) { mutableStateOf<Int?>(null) }
    var originY by remember(layerId) { mutableFloatStateOf(0f) }
    val reduceMotion = com.aurea.aurea.effects.rememberReduceMotion()
    LaunchedEffect(openId) {
        val target = openId
        if (target != null) {
            shown = target
            progress.snapTo(0f)
            if (reduceMotion) progress.snapTo(1f) else progress.animateTo(1f, tween(BsDims.OPEN_MS))
        } else if (shown != null) {
            if (reduceMotion) progress.snapTo(0f) else progress.animateTo(0f, tween(BsDims.OPEN_MS))
            shown = null
        }
    }
    // A borda aberta sumiu (desfazer, apagar por outro caminho): fecha.
    val ids = borders.map { it.effectId }
    LaunchedEffect(ids, openId) {
        if (openId != null && openId !in ids) onOpen(null)
    }

    fun openAt(id: Int) {
        val box = coords.box
        val item = coords.items[id]
        originY = if (box != null && item != null && box.isAttached && item.isAttached) box.localPositionOf(item, Offset.Zero).y else 0f
        onFocus(BsFocus.Param(id, BsParam.BORDER_WIDTH))
        onOpen(id)
    }
    fun swap() {
        val id = dragId ?: return
        val ord = dragOrder ?: return
        val i = ord.indexOf(id)
        if (i < 0) return
        if (dragOffset > pitch / 2f && i < ord.lastIndex) {
            dragOrder = ord.toMutableList().also { it[i] = ord[i + 1]; it[i + 1] = id }
            dragOffset -= pitch
        } else if (dragOffset < -pitch / 2f && i > 0) {
            dragOrder = ord.toMutableList().also { it[i] = ord[i - 1]; it[i - 1] = id }
            dragOffset += pitch
        }
    }
    fun finishDrag() {
        val id = dragId
        val ord = dragOrder
        if (id != null && ord != null) {
            val to = ord.indexOf(id)
            val from = latest.indexOfFirst { it.effectId == id }
            // Um comando só ao soltar = um passo de desfazer.
            if (to >= 0 && from >= 0 && to != from) store.moveDecoration(id, to)
        }
        dragId = null
        dragOffset = 0f
        dragOrder = null
    }
    fun handle(id: Int): Modifier = if (locked) Modifier else Modifier.pointerInput(id) {
        detectDragGestures(
            onDragStart = {
                dragOrder = latest.map { it.effectId }
                dragId = id
                dragOffset = 0f
            },
            onDrag = { change, amount ->
                change.consume()
                dragOffset += amount.y
                swap()
            },
            onDragEnd = { finishDrag() },
            onDragCancel = { finishDrag() },
        )
    }

    Box(
        Modifier
            .fillMaxSize()
            .testTag("border.tab.borders")
            .onGloballyPositioned { coords.box = it },
    ) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
            val order = (dragOrder ?: ids).mapNotNull { id -> borders.firstOrNull { it.effectId == id } }
            order.forEach { e ->
                key(e.effectId) {
                    val index = borders.indexOfFirst { it.effectId == e.effectId }
                    val lifted = dragId == e.effectId
                    BorderItem(
                        env = env,
                        effect = e,
                        index = index,
                        enabled = !locked,
                        lifted = lifted,
                        modifier = Modifier
                            .onGloballyPositioned { coords.items[e.effectId] = it }
                            .then(if (lifted) Modifier.zIndex(1f).graphicsLayer { translationY = dragOffset } else Modifier),
                        handle = handle(e.effectId),
                        onOpen = { openAt(e.effectId) },
                        onMoveUp = if (index > 0 && !locked) ({ store.moveDecoration(e.effectId, index - 1) }) else null,
                        onMoveDown = if (index in 0 until borders.lastIndex && !locked) ({ store.moveDecoration(e.effectId, index + 1) }) else null,
                    )
                }
            }
            AddBorderItem(enabled = !locked) { store.addDecoration(LayerEffect.ROLE_BORDER) }
            Spacer(Modifier.height(BsDims.Gap))
        }
        val s = shown
        if (s != null) {
            BorderSettings(
                env = env,
                effectId = s,
                index = borders.indexOfFirst { it.effectId == s },
                locked = locked,
                focus = focus,
                onFocus = onFocus,
                progress = progress.value,
                originY = originY,
                onClose = { onOpen(null) },
            )
        }
    }
}

@Composable
private fun BorderItem(
    env: PanelEnv,
    effect: LayerEffect,
    index: Int,
    enabled: Boolean,
    lifted: Boolean,
    modifier: Modifier,
    handle: Modifier,
    onOpen: () -> Unit,
    onMoveUp: (() -> Unit)?,
    onMoveDown: (() -> Unit)?,
) {
    val store = env.store
    val id = effect.effectId
    val width = store.paramOf(id, BsParam.BORDER_WIDTH)?.value?.getOrNull(0) ?: 0f
    val color = store.paramOf(id, BsParam.BORDER_COLOR)?.let { bsColor(engineToDisplay(it.value)) } ?: Color.White
    val position = store.paramOf(id, BsParam.BORDER_POSITION)?.value?.getOrNull(0)?.roundToInt()?.coerceIn(0, 2) ?: 2
    val name = stringResource(R.string.om_ap_bs_border_n, index + 1)
    val size = stringResource(R.string.om_ap_bs_px, numeroPtBr(width, 0))
    val direction = stringResource(directionRes(position))
    val deleteLabel = stringResource(R.string.om_ap_bs_delete_border)
    val reorderLabel = stringResource(R.string.om_ap_bs_reorder)
    val upLabel = stringResource(R.string.panel_mover_cima)
    val downLabel = stringResource(R.string.panel_mover_baixo)
    BsSwipe(
        enabled = enabled && !lifted,
        onDelete = { store.removeEffectWithUndo(id) },
        modifier = modifier,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(BsDims.Slot)
                .testTag("border.item.$index")
                .semantics(mergeDescendants = true) {
                    contentDescription = "$name, $size, $direction"
                    if (!enabled) disabled()
                    customActions = buildList<CustomAccessibilityAction> {
                        if (enabled) add(CustomAccessibilityAction(deleteLabel) { store.removeEffectWithUndo(id); true })
                        onMoveUp?.let { add(CustomAccessibilityAction(upLabel) { it(); true }) }
                        onMoveDown?.let { add(CustomAccessibilityAction(downLabel) { it(); true }) }
                    }
                }
                .tocavel(shrink = 1f, onClick = onOpen)
                .padding(start = BsDims.Gap, top = BsDims.Gap, end = BsDims.Gap, bottom = 0.5.dp),
        ) {
            Row(
                Modifier
                    .fillMaxSize()
                    .clip(RoundedCornerShape(BsDims.Radius))
                    .background(if (lifted) AureaColors.FxSunken else AureaColors.FxListSurface)
                    .padding(start = 3.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.ChevronRight, BsDims.Cue, AureaColors.Text)
                Spacer(Modifier.width(6.dp))
                Text(
                    size,
                    maxLines = 1,
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
                )
                Spacer(Modifier.width(10.dp))
                BsSwatch(color, Modifier.size(BsDims.Dot), CircleShape)
                Spacer(Modifier.width(10.dp))
                BasicText(
                    text = direction,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 17.sp, stepSize = 0.5.sp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.Text)),
                )
                Box(
                    Modifier
                        .width(44.dp)
                        .fillMaxHeight()
                        .testTag("border.item.$index.handle")
                        .semantics { contentDescription = reorderLabel }
                        .then(handle),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.LineHorizontal3, 18.dp, if (enabled) AureaColors.Text else AureaColors.FxDisabledText)
                }
            }
        }
    }
}

@Composable
private fun AddBorderItem(enabled: Boolean, onClick: () -> Unit) {
    val label = stringResource(R.string.om_ap_bs_add_border)
    Box(
        Modifier
            .fillMaxWidth()
            .height(BsDims.Slot)
            .testTag("border.add")
            .semantics {
                contentDescription = label
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, shrink = 1f, onClick = onClick)
            .padding(start = BsDims.Gap, top = BsDims.Gap, end = BsDims.Gap, bottom = 0.5.dp)
            .alpha(if (enabled) 1f else 0.45f),
    ) {
        Row(
            Modifier
                .fillMaxSize()
                .clip(RoundedCornerShape(BsDims.Radius))
                .background(AureaColors.FxListSurface),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 15.dp, AureaColors.Text)
            Spacer(Modifier.width(6.dp))
            Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
        }
    }
}

/**
 * Os AJUSTES DA BORDA abertos no lugar: cabeçalho (seta aberta · "Borda N" · 🗑)
 * e as linhas Direção · Cor · Tamanho. [progress] 0 → 1: nasce na linha
 * ([originY]) com a altura de um item e cresce até a lista inteira.
 */
@Composable
private fun BorderSettings(
    env: PanelEnv,
    effectId: Int,
    index: Int,
    locked: Boolean,
    focus: BsFocus?,
    onFocus: (BsFocus) -> Unit,
    progress: Float,
    originY: Float,
    onClose: () -> Unit,
) {
    val store = env.store
    val slotPx = with(LocalDensity.current) { BsDims.Slot.toPx() }
    val name = stringResource(R.string.om_ap_bs_border_n, max(1, index + 1))
    val deleteLabel = stringResource(R.string.om_ap_bs_delete_border)
    val tagIndex = max(0, index)
    Column(
        Modifier
            .fillMaxSize()
            .graphicsLayer { translationY = originY * (1f - progress) }
            .drawWithContent {
                val visible = (slotPx + (size.height - slotPx) * progress).coerceIn(0f, size.height)
                clipRect(bottom = visible) { this@drawWithContent.drawContent() }
            }
            .background(AureaColors.PanelFrameBackground)
            .background(AureaColors.PanelFrameContent)
            .testTag("border.settings")
            .padding(start = BsDims.Gap, end = BsDims.Gap, bottom = BsDims.Gap),
    ) {
        // Cabeçalho: tocar fecha (a margem de cima entra no alvo).
        Box(
            Modifier
                .fillMaxWidth()
                .height(BsDims.HeaderSlot)
                .semantics(mergeDescendants = false) { contentDescription = name }
                .tocavel(shrink = 1f, onClick = onClose)
                .padding(top = BsDims.Gap),
        ) {
            Row(
                Modifier
                    .fillMaxSize()
                    .clip(RoundedCornerShape(topStart = BsDims.Radius, topEnd = BsDims.Radius))
                    .background(AureaColors.FxListSurface)
                    .padding(start = 3.dp, end = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.ChevronDown, BsDims.Cue, AureaColors.Text.copy(alpha = 0.9f), Modifier.padding(start = 8.dp))
                Text(
                    name,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f).padding(start = 6.dp, end = 4.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
                )
                Box(
                    Modifier
                        .width(44.dp)
                        .fillMaxHeight()
                        .testTag("border.item.$tagIndex.delete")
                        .semantics {
                            contentDescription = deleteLabel
                            if (locked) disabled()
                        }
                        .tocavel(enabled = !locked, shrink = 1f) {
                            onClose()
                            store.removeEffectWithUndo(effectId)
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Trash, 20.dp, if (locked) AureaColors.FxDisabledText else AureaColors.FxListIcon)
                }
            }
        }
        Column(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .clip(RoundedCornerShape(bottomStart = BsDims.Radius, bottomEnd = BsDims.Radius))
                .background(AureaColors.FxListSurface)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 4.dp, vertical = 2.dp),
        ) {
            // Direção [Dentro | Centro | Fora] (enum do motor: 0 Fora, 1 Centro, 2 Dentro).
            val positionFocus = BsFocus.Param(effectId, BsParam.BORDER_POSITION)
            val position = store.paramOf(effectId, BsParam.BORDER_POSITION)?.value?.getOrNull(0)?.roundToInt()?.coerceIn(0, 2) ?: 2
            BsParamRow(
                env = env,
                label = stringResource(R.string.om_ap_bs_direction),
                height = BsDims.Row,
                focused = focus == positionFocus,
                effectId = effectId,
                param = BsParam.BORDER_POSITION,
                locked = locked,
                keyTag = "border.edit.direction.key",
                onFocus = { onFocus(positionFocus) },
            ) {
                FxSegmented(
                    options = listOf(
                        stringResource(R.string.om_ap_bs_inside),
                        stringResource(R.string.om_ap_bs_center),
                        stringResource(R.string.om_ap_bs_outside),
                    ),
                    selected = 2 - position,
                    onSelect = { i ->
                        onFocus(positionFocus)
                        writeEffectScalar(store, effectId, BsParam.BORDER_POSITION, (2 - i).toFloat())
                    },
                    modifier = Modifier.weight(1f).testTag("border.edit.direction"),
                    enabled = !locked,
                    tag = "border.edit.direction",
                )
            }
            BsColorParamRow(env, effectId, BsParam.BORDER_COLOR, stringResource(R.string.om_ap_bs_color), focus, onFocus, locked, "border.edit.color")
            BsRulerParamRow(
                env, effectId, BsParam.BORDER_WIDTH, stringResource(R.string.om_ap_bs_size), focus, onFocus, locked,
                tag = "border.edit.size", step = 1f, min = 0f, max = BsDims.SIZE_MAX, decimals = 0, unit = "px", gesture = "tamanho da borda",
            )
        }
    }
}

// =============================================================================
// Sombra
// =============================================================================

@Composable
private fun ColumnScope.ShadowTab(env: PanelEnv, locked: Boolean, focus: BsFocus?, onFocus: (BsFocus) -> Unit) {
    val store = env.store
    val shadow = store.decorations.firstOrNull { it.role == LayerEffect.ROLE_SHADOW }
    val title = stringResource(R.string.om_ap_bs_shadow)
    val anchor = remember { BsAnchor() }
    val shadowColor = shadow?.let { s -> store.paramOf(s.effectId, BsParam.SHADOW_COLOR)?.let { bsColor(engineToDisplay(it.value)) } }
    Column(
        Modifier
            .fillMaxSize()
            .testTag("border.tab.shadow")
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 6.dp),
    ) {
        Box(Modifier.padding(start = 4.dp)) {
            BsHeaderLine(
                title = title,
                color = shadowColor,
                swatchTag = "shadow.swatch",
                switchTag = "shadow.switch",
                checked = shadow?.enabled == true,
                enabled = !locked,
                swatchEnabled = !locked && shadow != null,
                anchor = anchor,
                onSwatch = { shadow?.let { openEffectColor(env, it.effectId, BsParam.SHADOW_COLOR, anchor.rect, "cor da sombra") } },
                onSwitch = { on ->
                    if (!locked) {
                        // Sem sombra: liga cria a decoração (nasce ligada); com sombra: liga/desliga o efeito.
                        if (shadow == null) {
                            if (on) store.addDecoration(LayerEffect.ROLE_SHADOW)
                        } else {
                            store.setEffectEnabled(shadow.effectId, on)
                        }
                    }
                },
            )
        }
        if (shadow != null) {
            val id = shadow.effectId
            BsColorParamRow(env, id, BsParam.SHADOW_COLOR, stringResource(R.string.om_ap_bs_color), focus, onFocus, locked, "shadow.color")
            BsRulerParamRow(
                env, id, BsParam.SHADOW_SOFTNESS, stringResource(R.string.om_ap_bs_size), focus, onFocus, locked,
                tag = "shadow.size", step = 1f, min = 0f, max = BsDims.SIZE_MAX, decimals = 0, unit = "px", gesture = "tamanho da sombra",
            )
            BsRulerParamRow(
                env, id, BsParam.SHADOW_OPACITY, stringResource(R.string.om_ap_bs_alpha), focus, onFocus, locked,
                tag = "shadow.alpha", step = 1f, min = 0f, max = 100f, decimals = 0, unit = "%", gesture = "alfa da sombra",
            )
            ShadowOffsetRows(env, id, focus, onFocus, locked)
        }
        Spacer(Modifier.height(8.dp))
    }
}

/**
 * DESLOCAMENTO X/Y da sombra em px. O motor guarda direção e distância; a
 * conversão é dele (`decorationSetShadowOffset` / `decorationShadowOffset`).
 * O ◆ das duas linhas marca direção e distância juntas.
 */
@Composable
private fun ShadowOffsetRows(env: PanelEnv, effectId: Int, focus: BsFocus?, onFocus: (BsFocus) -> Unit, locked: Boolean) {
    val store = env.store
    val owner = store.primary
    // Relê o deslocamento quando direção/distância mudam (os parâmetros são estado).
    val dir = store.paramOf(effectId, BsParam.SHADOW_DIRECTION)?.value?.getOrNull(0)
    val dist = store.paramOf(effectId, BsParam.SHADOW_DISTANCE)?.value?.getOrNull(0)
    val offset = remember(effectId, dir, dist) { store.shadowOffset(effectId)?.takeIf { it.size >= 2 } ?: floatArrayOf(0f, 0f) }
    val start = remember(effectId) { floatArrayOf(0f, 0f) }
    val offsetFocus = BsFocus.ShadowOffset(effectId)
    val look = combineLooks(effectLook(store, effectId, BsParam.SHADOW_DIRECTION), effectLook(store, effectId, BsParam.SHADOW_DISTANCE))
    val canKey = !locked && focusCanKey(store, offsetFocus)
    val label = stringResource(R.string.om_ap_bs_offset)
    fun current(): Boolean = owner != null && store.primary == owner && store.detail?.locked != true
    listOf(0, 1).forEach { axis ->
        val axisName = if (axis == 0) "X" else "Y"
        val rowLabel = "$label $axisName"
        val value = offset[axis]
        fun write(v: Float) {
            if (!current() || !v.isFinite()) return
            val clamped = v.coerceIn(-BsDims.OFFSET_MAX, BsDims.OFFSET_MAX)
            if (axis == 0) store.setShadowOffset(effectId, clamped, start[1]) else store.setShadowOffset(effectId, start[0], clamped)
        }
        BsRow(
            label = rowLabel,
            height = BsDims.Ruler,
            focused = focus == offsetFocus,
            look = look,
            canKey = canKey,
            keyTag = "shadow.offset.${axisName.lowercase()}.key",
            enabled = !locked,
            onFocus = { onFocus(offsetFocus) },
            onKey = { toggleFocusKey(store, offsetFocus) },
        ) {
            FxRuler(
                value = value,
                step = 0.5f,
                min = -BsDims.OFFSET_MAX,
                max = BsDims.OFFSET_MAX,
                onStart = {
                    onFocus(offsetFocus)
                    start[0] = offset[0]
                    start[1] = offset[1]
                    store.beginGesture("posição da sombra")
                },
                onValue = { write(it) },
                onEnd = { store.endGesture() },
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .testTag("shadow.offset.${axisName.lowercase()}.ruler")
                    .semantics { contentDescription = rowLabel },
                enabled = !locked,
                needle = AureaColors.Accent,
            )
            Spacer(Modifier.width(3.dp))
            FxValueBox(
                text = numeroPtBr(value, 1),
                height = BsDims.ValueH,
                modifier = Modifier.testTag("shadow.offset.${axisName.lowercase()}").semantics { contentDescription = rowLabel },
                width = BsDims.ValueW,
                enabled = !locked,
                onClick = {
                    onFocus(offsetFocus)
                    env.openKeypad(
                        KeypadRequest(rowLabel, value, "px", -BsDims.OFFSET_MAX, BsDims.OFFSET_MAX, 1) { v ->
                            if (current()) {
                                start[0] = offset[0]
                                start[1] = offset[1]
                                store.beginGesture("posição da sombra")
                                write(v)
                                store.endGesture()
                            }
                        },
                    )
                },
            )
        }
    }
}

// =============================================================================
// Peças das linhas
// =============================================================================

/** Amostra · título 15 · interruptor (a linha de 37,5 da referência). */
@Composable
private fun BsHeaderLine(
    title: String,
    color: Color?,
    swatchTag: String,
    switchTag: String,
    checked: Boolean,
    enabled: Boolean,
    swatchEnabled: Boolean,
    anchor: BsAnchor,
    onSwatch: () -> Unit,
    onSwitch: (Boolean) -> Unit,
) {
    val colorLabel = stringResource(R.string.om_ap_bs_color)
    Row(Modifier.fillMaxWidth().height(BsDims.Header), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .size(44.dp)
                .testTag(swatchTag)
                .semantics {
                    contentDescription = "$title, $colorLabel"
                    role = Role.Button
                    if (!swatchEnabled) disabled()
                }
                .tocavel(enabled = swatchEnabled, shrink = 1f, onClick = onSwatch),
            contentAlignment = Alignment.CenterStart,
        ) {
            BsSwatch(
                color,
                Modifier
                    .size(BsDims.Swatch)
                    .onGloballyPositioned { anchor.rect = it.boundsInWindow() }
                    .alpha(if (swatchEnabled) 1f else 0.4f),
                RoundedCornerShape(4.dp),
            )
        }
        Text(
            title,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).padding(start = 4.dp, end = 8.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
        )
        AureaToggle(
            checked = checked,
            onCheckedChange = onSwitch,
            modifier = Modifier.testTag(switchTag).semantics {
                contentDescription = title
                role = Role.Switch
                toggleableState = ToggleableState(checked)
            },
            enabled = enabled,
        )
    }
}

/**
 * Uma linha de parâmetro: rótulo (toque = foco do trilho) · controle · ◆ da
 * linha (marca/tira o keyframe do parâmetro no cabeçote).
 */
@Composable
private fun BsRow(
    label: String,
    height: Dp,
    focused: Boolean,
    look: KeyframeLook,
    canKey: Boolean,
    keyTag: String,
    enabled: Boolean,
    onFocus: () -> Unit,
    onKey: () -> Unit,
    content: @Composable RowScope.() -> Unit,
) {
    val keyLabel = "$label: " + stringResource(if (look == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui)
    Row(Modifier.fillMaxWidth().height(height), verticalAlignment = Alignment.CenterVertically) {
        FxParamLabel(
            label = label,
            height = height - 6.dp,
            width = BsDims.Label,
            active = focused,
            enabled = enabled,
            onClick = onFocus,
        )
        Spacer(Modifier.width(4.dp))
        content()
        Box(
            Modifier
                .width(BsDims.Key)
                .fillMaxHeight()
                .testTag(keyTag)
                .semantics {
                    contentDescription = keyLabel
                    if (!canKey) disabled()
                }
                .then(
                    if (canKey) {
                        Modifier.tocavel(shrink = 1f) {
                            onFocus()
                            onKey()
                        }
                    } else {
                        Modifier
                    },
                ),
            contentAlignment = Alignment.Center,
        ) {
            KeyframeDiamondIcon(look, enabled = canKey, modifier = Modifier.size(22.dp))
        }
    }
}

/** [BsRow] de um parâmetro de efeito (o ◆ marca pelo mesmo caminho da lista de efeitos). */
@Composable
private fun BsParamRow(
    env: PanelEnv,
    label: String,
    height: Dp,
    focused: Boolean,
    effectId: Int,
    param: Int,
    locked: Boolean,
    keyTag: String,
    onFocus: () -> Unit,
    content: @Composable RowScope.() -> Unit,
) {
    val store = env.store
    val p = store.paramOf(effectId, param)
    BsRow(
        label = label,
        height = height,
        focused = focused,
        look = effectLook(store, effectId, param),
        canKey = !locked && p?.canKey() == true,
        keyTag = keyTag,
        enabled = !locked,
        onFocus = onFocus,
        onKey = { store.paramOf(effectId, param)?.let { store.toggleEffectKeyframe(effectId, it) } },
        content = content,
    )
}

/** Cor: amostra que abre o seletor ancorado (o balão inteiro = um passo de desfazer). */
@Composable
private fun BsColorParamRow(
    env: PanelEnv,
    effectId: Int,
    param: Int,
    label: String,
    focus: BsFocus?,
    onFocus: (BsFocus) -> Unit,
    locked: Boolean,
    tag: String,
) {
    val store = env.store
    val me = BsFocus.Param(effectId, param)
    val color = store.paramOf(effectId, param)?.let { bsColor(engineToDisplay(it.value)) } ?: Color.White
    val anchor = remember(effectId, param) { BsAnchor() }
    BsParamRow(env, label, BsDims.Row, focus == me, effectId, param, locked, "$tag.key", onFocus = { onFocus(me) }) {
        Row(Modifier.weight(1f).fillMaxHeight(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(width = 64.dp, height = 44.dp)
                    .testTag(tag)
                    .semantics {
                        contentDescription = label
                        role = Role.Button
                        if (locked) disabled()
                    }
                    .tocavel(enabled = !locked, shrink = 1f) {
                        onFocus(me)
                        openEffectColor(env, effectId, param, anchor.rect, "cor $label")
                    },
                contentAlignment = Alignment.Center,
            ) {
                BsSwatch(
                    color,
                    Modifier
                        .size(width = 46.dp, height = 26.dp)
                        .onGloballyPositioned { anchor.rect = it.boundsInWindow() },
                    RoundedCornerShape(5.dp),
                )
            }
        }
    }
}

/** Número: régua de riscos (agulha em destaque) + caixa de valor (toque = teclado). */
@Composable
private fun BsRulerParamRow(
    env: PanelEnv,
    effectId: Int,
    param: Int,
    label: String,
    focus: BsFocus?,
    onFocus: (BsFocus) -> Unit,
    locked: Boolean,
    tag: String,
    step: Float,
    min: Float,
    max: Float,
    decimals: Int,
    unit: String,
    gesture: String,
) {
    val store = env.store
    val owner = store.primary
    val me = BsFocus.Param(effectId, param)
    val value = store.paramOf(effectId, param)?.value?.getOrNull(0) ?: 0f
    fun write(v: Float) {
        if (owner == null || store.primary != owner) return
        writeEffectScalar(store, effectId, param, if (decimals == 0) v.roundToInt().toFloat() else v)
    }
    BsParamRow(env, label, BsDims.Ruler, focus == me, effectId, param, locked, "$tag.key", onFocus = { onFocus(me) }) {
        FxRuler(
            value = value,
            step = step,
            min = min,
            max = max,
            onStart = {
                onFocus(me)
                store.beginGesture(gesture)
            },
            onValue = { write(it) },
            onEnd = { store.endGesture() },
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .testTag("$tag.ruler")
                .semantics { contentDescription = label },
            enabled = !locked,
            needle = AureaColors.Accent,
        )
        Spacer(Modifier.width(3.dp))
        FxValueBox(
            text = numeroPtBr(value, decimals) + if (unit == "%") "%" else "",
            height = BsDims.ValueH,
            modifier = Modifier.testTag(tag).semantics { contentDescription = label },
            width = BsDims.ValueW,
            enabled = !locked,
            onClick = {
                onFocus(me)
                env.openKeypad(
                    KeypadRequest(label, value, unit, min, max, decimals) { v ->
                        store.beginGesture(gesture)
                        write(v.coerceIn(min, max))
                        store.endGesture()
                    },
                )
            },
        )
    }
}

/** Deslizar para a esquerda apaga (faixa vermelha com 🗑 atrás do item); antes do limite, volta. */
@Composable
private fun BsSwipe(enabled: Boolean, onDelete: () -> Unit, modifier: Modifier, content: @Composable () -> Unit) {
    val scope = rememberCoroutineScope()
    val offset = remember { Animatable(0f) }
    var width by remember { mutableFloatStateOf(0f) }
    val minPx = with(LocalDensity.current) { 96.dp.toPx() }
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val hint = stringResource(R.string.fxui_a11y_swipe_delete)
    val state = rememberDraggableState { delta ->
        // Só para o início da linha; em RTL a tela espelha o gesto.
        val d = if (rtl) -delta else delta
        scope.launch { offset.snapTo((offset.value + d).coerceIn(-width, 0f)) }
    }
    Box(
        modifier
            .fillMaxWidth()
            .onSizeChanged { width = it.width.toFloat() }
            .semantics { if (enabled) stateDescription = hint },
    ) {
        if (offset.value < 0f) {
            Row(
                Modifier
                    .matchParentSize()
                    .padding(start = BsDims.Gap, top = BsDims.Gap, end = BsDims.Gap, bottom = 0.5.dp)
                    .clip(RoundedCornerShape(BsDims.Radius))
                    .background(AureaColors.Danger),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.Text, Modifier.padding(end = 14.dp))
            }
        }
        Box(
            Modifier
                .graphicsLayer { translationX = if (rtl) -offset.value else offset.value }
                .draggable(
                    state = state,
                    orientation = Orientation.Horizontal,
                    enabled = enabled,
                    onDragStopped = {
                        if (fxSwipeCommits(offset.value, width, minPx)) {
                            offset.animateTo(-width, tween(160))
                            onDelete()
                            offset.snapTo(0f)
                        } else {
                            offset.animateTo(0f, tween(200))
                        }
                    },
                ),
        ) { content() }
    }
}

/** Amostra de cor com o xadrez da transparência por baixo. */
@Composable
private fun BsSwatch(color: Color?, modifier: Modifier, shape: Shape) {
    Canvas(modifier.clip(shape).border(1.dp, AureaColors.FxSunken, shape)) {
        drawBsChecker()
        if (color != null) drawRect(color)
    }
}

private fun DrawScope.drawBsChecker() {
    val cell = 5.dp.toPx()
    drawRect(AureaColors.ChipHigh)
    var y = 0f
    var row = 0
    while (y < size.height) {
        var x = if (row % 2 == 0) cell else 0f
        while (x < size.width) {
            drawRect(AureaColors.Chip, Offset(x, y), androidx.compose.ui.geometry.Size(cell, cell))
            x += cell * 2
        }
        y += cell
        row++
    }
}

// =============================================================================
// Contas e escrita (lidas no toque, nunca guardadas)
// =============================================================================

private fun directionRes(position: Int): Int = when (position) {
    0 -> R.string.om_ap_bs_outside
    1 -> R.string.om_ap_bs_center
    else -> R.string.om_ap_bs_inside
}

/** RGBA8 (R no byte baixo) → RGBA 0..1. */
private fun bsRgba8(v: Int): FloatArray = floatArrayOf(
    (v and 0xFF) / 255f,
    ((v ushr 8) and 0xFF) / 255f,
    ((v ushr 16) and 0xFF) / 255f,
    ((v ushr 24) and 0xFF) / 255f,
)

private fun bsColor(v: FloatArray): Color = Color(
    v.getOrElse(0) { 0f }.coerceIn(0f, 1f),
    v.getOrElse(1) { 0f }.coerceIn(0f, 1f),
    v.getOrElse(2) { 0f }.coerceIn(0f, 1f),
    v.getOrElse(3) { 1f }.coerceIn(0f, 1f),
)

private fun EffectParam.canKey(): Boolean =
    (flags and ParamType.FLAG_ANIMATABLE) != 0 && ParamType.componentCount(type) > 0

private fun combineLooks(a: KeyframeLook, b: KeyframeLook): KeyframeLook = when {
    a == KeyframeLook.KeyHere || b == KeyframeLook.KeyHere -> KeyframeLook.KeyHere
    a == KeyframeLook.Animated || b == KeyframeLook.Animated -> KeyframeLook.Animated
    else -> KeyframeLook.None
}

/** Escalar de efeito (borda/sombra): animado → keyframe no cabeçote (regra do store). */
private fun writeEffectScalar(store: EditorStore, effectId: Int, param: Int, value: Float) {
    if (!value.isFinite() || store.detail?.locked == true) return
    val p = store.paramOf(effectId, param) ?: return
    store.setEffectParam(effectId, p, value.coerceIn(p.hardMin, p.hardMax))
}

/** Abre o seletor de cor ancorado na amostra para um parâmetro de cor de efeito. */
private fun openEffectColor(env: PanelEnv, effectId: Int, param: Int, anchor: Rect?, gesture: String) {
    val store = env.store
    val owner = store.primary ?: return
    if (store.detail?.locked == true) return
    val p = store.paramOf(effectId, param) ?: return
    val shown = engineToDisplay(p.value)
    store.beginGesture(gesture)
    env.openColor(
        ColorRequest(
            initial = floatArrayOf(shown[0], shown[1], shown[2], shown[3]),
            onChange = { r, g, b, a ->
                if (store.primary == owner) {
                    store.paramOf(effectId, param)?.let { store.writeParamVector(effectId, it, displayToEngine(r, g, b, a)) }
                }
            },
            onDone = { store.endGesture() },
            anchor = anchor,
        ),
    )
}

private fun focusLook(store: EditorStore, focus: BsFocus?): KeyframeLook = when (focus) {
    null -> KeyframeLook.None
    is BsFocus.StrokeWidth -> {
        val sp = store.shapeParams
        val bit = 1 shl BsParam.SHAPE_STROKE_WIDTH
        when {
            shapeKeyBits(sp) and bit != 0 -> KeyframeLook.KeyHere
            shapeAnimBits(sp) and bit != 0 -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
    }
    is BsFocus.Param -> effectLook(store, focus.effectId, focus.param)
    is BsFocus.ShadowOffset -> combineLooks(
        effectLook(store, focus.effectId, BsParam.SHADOW_DIRECTION),
        effectLook(store, focus.effectId, BsParam.SHADOW_DISTANCE),
    )
}

private fun focusCanKey(store: EditorStore, focus: BsFocus): Boolean = when (focus) {
    is BsFocus.StrokeWidth -> store.shapeParams != null
    is BsFocus.Param -> store.paramOf(focus.effectId, focus.param)?.canKey() == true
    is BsFocus.ShadowOffset ->
        store.paramOf(focus.effectId, BsParam.SHADOW_DIRECTION)?.canKey() == true &&
            store.paramOf(focus.effectId, BsParam.SHADOW_DISTANCE)?.canKey() == true
}

/** A trilha que a curva do trilho abre. */
private fun focusTrack(store: EditorStore, focus: BsFocus?): List<KeyframeRow> {
    val keys = store.primaryKeys()
    return when (focus) {
        null -> emptyList()
        is BsFocus.StrokeWidth -> keys.shapeTrack(BsParam.SHAPE_STROKE_WIDTH)
        is BsFocus.Param -> keys.effectTrack(focus.effectId, focus.param, 0)
        is BsFocus.ShadowOffset -> curveTrack(
            listOf(
                keys.effectTrack(focus.effectId, BsParam.SHADOW_DISTANCE, 0),
                keys.effectTrack(focus.effectId, BsParam.SHADOW_DIRECTION, 0),
            ),
        )
    }
}

/** As trilhas do parâmetro em foco (foco da timeline e "apagar todos"). */
private fun focusTimelineKeys(store: EditorStore, focus: BsFocus?): List<TrackKey> = when (focus) {
    null -> emptyList()
    is BsFocus.StrokeWidth -> shapePanelTimelineFocus(BsParam.SHAPE_STROKE_WIDTH)
    is BsFocus.Param -> {
        val n = max(1, ParamType.componentCount(store.paramOf(focus.effectId, focus.param)?.type ?: ParamType.FLOAT))
        List(n) { c -> TrackKey(TrackProperty.EFFECT_PARAM, focus.effectId, focus.param * 4 + c) }
    }
    is BsFocus.ShadowOffset -> listOf(
        TrackKey(TrackProperty.EFFECT_PARAM, focus.effectId, BsParam.SHADOW_DIRECTION * 4),
        TrackKey(TrackProperty.EFFECT_PARAM, focus.effectId, BsParam.SHADOW_DISTANCE * 4),
    )
}

/** ◆ do trilho (e o ◆ das linhas de deslocamento): marca/tira no cabeçote, um passo de desfazer. */
private fun toggleFocusKey(store: EditorStore, focus: BsFocus) {
    if (store.detail?.locked == true) return
    when (focus) {
        is BsFocus.StrokeWidth -> store.toggleShapeParamKey(BsParam.SHAPE_STROKE_WIDTH)
        is BsFocus.Param -> store.paramOf(focus.effectId, focus.param)?.let { store.toggleEffectKeyframe(focus.effectId, it) }
        is BsFocus.ShadowOffset -> {
            val id = focus.effectId
            val dir = store.paramOf(id, BsParam.SHADOW_DIRECTION) ?: return
            val dist = store.paramOf(id, BsParam.SHADOW_DISTANCE) ?: return
            val dirHere = effectLook(store, id, BsParam.SHADOW_DIRECTION) == KeyframeLook.KeyHere
            val distHere = effectLook(store, id, BsParam.SHADOW_DISTANCE) == KeyframeLook.KeyHere
            // Direção e distância andam juntas: com marca aqui em qualquer uma, tira; senão marca as duas.
            store.beginGesture("keyframe da sombra")
            if (dirHere || distHere) {
                if (dirHere) store.toggleEffectKeyframe(id, dir)
                if (distHere) store.toggleEffectKeyframe(id, dist)
            } else {
                store.toggleEffectKeyframe(id, dir)
                store.toggleEffectKeyframe(id, dist)
            }
            store.endGesture()
        }
    }
}

/** Segurar ◆: apaga todas as marcas do parâmetro em foco (um passo de desfazer, no store). */
private fun deleteFocusKeys(store: EditorStore, focus: BsFocus) {
    val layer = store.primary ?: return
    val tracks = when (focus) {
        // As marcas reais da largura (o índice de efeito da trilha de forma vem do motor).
        is BsFocus.StrokeWidth -> store.primaryKeys().shapeTrack(BsParam.SHAPE_STROKE_WIDTH)
            .map { TrackKey(it.property, it.effectIndex, it.paramIndex) }.distinct()
        else -> focusTimelineKeys(store, focus)
    }
    if (tracks.isNotEmpty()) store.deleteAllKeyframes(layer, tracks)
}

/** Curva do trecho sob o cabeçote (fora dos trechos, o primeiro ou o último). */
private fun openFocusCurve(env: PanelEnv, focus: BsFocus) {
    val store = env.store
    val layer = store.primary ?: return
    val track = focusTrack(store, focus)
    val key = store.curveStart(track) ?: store.detail?.localFrame(store.playhead)?.let { track.segmentStart(it) } ?: return
    store.selectKeyframe(layer, key)
    env.onOpenPanel(EditorPanel.Curve)
}
