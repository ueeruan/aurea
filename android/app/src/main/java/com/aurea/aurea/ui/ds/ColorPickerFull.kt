package com.aurea.aurea.ui.ds

import android.content.Context
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

// =============================================================================
// Omega aparência (área 05, P1-8): O SELETOR EM TELA CHEIA
//
//   ┌ Roda │ RGB │ ✓ ┐ 45 dp (a aba usada fica lembrada)
//   │                │  Roda: anel de matiz (marcas a cada 30°, alça 25 / miolo 17,5),
//   │   [atual|nova] │        cartão 70 × 50 com "+", código; réguas S · B · A (44 dp)
//   │     #RRGGBB    │  RGB:  cartão 140 × 70, "#RRGGBB (A%)", réguas R · G · B · A (0–255)
//   ├────────────────┤
//   │ paleta 7 × 30  │ 112 dp, A12: tocar aplica; segurar e arrastar levanta
//   └────────────────┘ (2×, 0,8, 300 ms), reordena; soltar na lixeira apaga.
//
// Fechar (✓ ou Voltar) SEMPRE aplica a cor nova. A cor sai viva por `onChange`
// (só quando muda) e quem chama segura um passo de desfazer até `onDone`.
// Par do `ColorPickerFull` do iOS (fullScreenCover): mesmos ids e textos.
// =============================================================================

private object FullDims {
    val TopBar = 45.dp
    val SliderRow = 44.dp
    val Palette = 112.dp
    val PaletteCell = 30.dp
    val PaletteSwatch = 26.dp
    val Lead = 50.dp
    val Trail = 40.dp
    val RingHandle = 25.dp
    val RingHandleCore = 17.5.dp
    val RingThickness = 22.dp
}

/**
 * O seletor em tela cheia (por cima do editor).
 *
 * @param initial a cor de partida (RGBA sRGB 0..1); a metade "atual" do cartão volta a ela.
 * @param withAlpha sem alfa, as réguas e rótulos de alfa somem.
 * @param onChange a cor nova, ao vivo (só quando muda) e no fechar.
 * @param onDone fechou (✓ ou Voltar); a última cor já foi mandada.
 */
@Composable
fun ColorPickerFull(
    initial: FloatArray,
    withAlpha: Boolean = true,
    onChange: (r: Float, g: Float, b: Float, a: Float) -> Unit,
    onDone: () -> Unit,
) {
    val context = LocalContext.current
    val start = remember { rgba4(initial) }
    val state = remember { ColorPickState(start) }
    val emit by rememberUpdatedState(onChange)
    val done by rememberUpdatedState(onDone)
    val sent = remember { start.copyOf() }
    var closed by remember { mutableStateOf(false) }
    val prefs = remember { context.getSharedPreferences(SavedPalette.PREFS, Context.MODE_PRIVATE) }
    var tab by remember { mutableStateOf(prefs.getString(SavedPalette.KEY_TAB, SavedPalette.TAB_WHEEL) ?: SavedPalette.TAB_WHEEL) }

    fun push() {
        val c = state.rgba()
        if (sameColor(c, sent, 0.1f / 255f)) return
        c.copyInto(sent)
        emit(c[0], c[1], c[2], c[3])
    }

    fun close() {
        if (closed) return
        closed = true
        push()
        done()
    }

    fun pickTab(t: String) {
        tab = t
        prefs.edit().putString(SavedPalette.KEY_TAB, t).apply()
    }

    Dialog(onDismissRequest = { close() }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        FullBody(
            state = state,
            start = start,
            withAlpha = withAlpha,
            tab = tab,
            onTab = { pickTab(it) },
            onEdit = { push() },
            onDone = { close() },
        )
    }
}

@Composable
private fun FullBody(
    state: ColorPickState,
    start: FloatArray,
    withAlpha: Boolean,
    tab: String,
    onTab: (String) -> Unit,
    onEdit: () -> Unit,
    onDone: () -> Unit,
) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val haptic = LocalHapticFeedback.current
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val palette = rememberSavedPalette()
    val edit by rememberUpdatedState(onEdit)
    val unavailableText = stringResource(R.string.om_ap_cp_unavailable)

    // Arrasto da paleta: a ordem em voo, a amostra levantada e a lixeira.
    var working by remember { mutableStateOf<List<Int>?>(null) }
    var liftIndex by remember { mutableIntStateOf(-1) }
    var liftColor by remember { mutableIntStateOf(0) }
    var liftPos by remember { mutableStateOf(Offset.Zero) }
    var overTrash by remember { mutableStateOf(false) }
    val coords = remember { arrayOfNulls<LayoutCoordinates>(3) }   // 0 raiz, 1 grade, 2 lixeira
    val shown = working ?: palette
    val shownNow by rememberUpdatedState(shown)
    val paletteNow by rememberUpdatedState(palette)
    var notice by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(notice) {
        if (notice != null) {
            kotlinx.coroutines.delay(1800)
            notice = null
        }
    }

    fun applyArgb(argb: Int) {
        val rgba = argbToRgba(argb)
        if (!withAlpha && rgba[3] < 1f) {
            notice = unavailableText
            return
        }
        state.set(if (withAlpha) rgba else floatArrayOf(rgba[0], rgba[1], rgba[2], state.alpha))
        edit()
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(PickerColors.Box)
            .onGloballyPositioned { coords[0] = it }
            .testTag("colorpicker.full.screen"),
    ) {
        Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding()) {
            Row(Modifier.fillMaxWidth().height(FullDims.TopBar)) {
                FullTab(stringResource(R.string.om_ap_cp_wheel), tab != SavedPalette.TAB_RGB, "colorpicker.full.wheel") { onTab(SavedPalette.TAB_WHEEL) }
                FullTab(stringResource(R.string.om_ap_cp_rgb), tab == SavedPalette.TAB_RGB, "colorpicker.full.rgb") { onTab(SavedPalette.TAB_RGB) }
                val doneLabel = stringResource(R.string.om_ap_cp_done)
                Box(
                    Modifier
                        .weight(0.3f)
                        .fillMaxHeight()
                        .background(PickerColors.Side)
                        .testTag("colorpicker.full.done")
                        .semantics { contentDescription = doneLabel }
                        .tocavel(shrink = 1f, haptic = true, onClick = onDone),
                    contentAlignment = Alignment.Center,
                ) { CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 22.dp, AureaColors.Text) }
            }
            Box(Modifier.weight(1f).fillMaxWidth()) {
                if (tab == SavedPalette.TAB_RGB) RgbPage(state, start, withAlpha, palette, onEdit = { edit() })
                else WheelPage(state, start, withAlpha, palette, onEdit = { edit() })
            }
            // Paleta (112 dp, A12): 7 colunas de 30 dp.
            Box(Modifier.fillMaxWidth().height(FullDims.Palette).background(PickerColors.Deepest).testTag("colorpicker.full.palette")) {
                val cellPx = with(density) { FullDims.PaletteCell.toPx() }
                val current = state.rgba()
                val currentArgb = rgbaToArgb(current)
                Box(
                    Modifier
                        .align(Alignment.TopCenter)
                        .padding(top = 4.dp)
                        .fillMaxHeight()
                        .verticalScroll(rememberScrollState()),
                ) {
                    Column(
                        Modifier
                            .width(FullDims.PaletteCell * 7)
                            .onGloballyPositioned { coords[1] = it }
                            .pointerInput(withAlpha) {
                                fun cellAt(p: Offset, count: Int): Int {
                                    val colRaw = (p.x / cellPx).toInt().coerceIn(0, 6)
                                    val col = if (rtl) 6 - colRaw else colRaw
                                    val row = (p.y / cellPx).toInt().coerceAtLeast(0)
                                    return (row * 7 + col).coerceAtMost(count - 1)
                                }
                                fun toRoot(p: Offset): Offset {
                                    val root = coords[0]
                                    val grid = coords[1]
                                    return if (root != null && grid != null && grid.isAttached && root.isAttached) root.localPositionOf(grid, p) else p
                                }
                                fun trashHit(p: Offset): Boolean {
                                    val root = coords[0] ?: return false
                                    val trash = coords[2] ?: return false
                                    if (!trash.isAttached || !root.isAttached) return false
                                    val r: Rect = root.localBoundingBoxOf(trash)
                                    return r.inflate(12.dp.toPx()).contains(p)
                                }
                                awaitEachGesture {
                                    val down = awaitFirstDown(requireUnconsumed = false)
                                    val list = shownNow
                                    if (list.isEmpty()) return@awaitEachGesture
                                    val index = cellAt(down.position, list.size)
                                    if (index !in list.indices) return@awaitEachGesture
                                    // Toque, rolagem ou segurar: até o tempo do toque longo ninguém consome.
                                    val outcome = withTimeoutOrNull(viewConfiguration.longPressTimeoutMillis) {
                                        var result = 0   // 1 toque, 2 rolagem/cancelado
                                        while (result == 0) {
                                            val ev = awaitPointerEvent()
                                            val c: PointerInputChange? = ev.changes.firstOrNull { it.id == down.id }
                                            result = when {
                                                c == null -> 2
                                                !c.pressed -> 1
                                                c.isConsumed || (c.position - down.position).getDistance() > viewConfiguration.touchSlop -> 2
                                                else -> 0
                                            }
                                        }
                                        result
                                    }
                                    when (outcome) {
                                        1 -> applyArgb(list[index])
                                        2 -> Unit
                                        else -> {
                                            // Segurou: levanta a amostra; arrastar reordena, a lixeira apaga.
                                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                            val order = list.toMutableList()
                                            var at = index
                                            liftColor = order[at]
                                            liftIndex = at
                                            working = order.toList()
                                            liftPos = toRoot(down.position)
                                            overTrash = false
                                            try {
                                                while (true) {
                                                    val ev = awaitPointerEvent()
                                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                                    ev.changes.forEach { it.consume() }
                                                    if (!c.pressed) break
                                                    val rootPos = toRoot(c.position)
                                                    liftPos = rootPos
                                                    val trash = trashHit(rootPos)
                                                    if (trash != overTrash) {
                                                        overTrash = trash
                                                        if (trash) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                                    }
                                                    if (!trash) {
                                                        val target = cellAt(c.position, order.size)
                                                        if (target != at && target in order.indices) {
                                                            order.add(target, order.removeAt(at))
                                                            at = target
                                                            liftIndex = at
                                                            working = order.toList()
                                                        }
                                                    }
                                                }
                                            } finally {
                                                if (overTrash) order.removeAt(at)
                                                if (order != paletteNow) SavedPalette.replace(context, order)
                                                working = null
                                                liftIndex = -1
                                                overTrash = false
                                            }
                                        }
                                    }
                                }
                            },
                    ) {
                        shown.chunked(7).forEachIndexed { row, items ->
                            Row {
                                items.forEachIndexed { col, argb ->
                                    val i = row * 7 + col
                                    SwatchCell(
                                        argb = argb,
                                        index = i,
                                        size = FullDims.PaletteSwatch,
                                        selected = argb == currentArgb,
                                        blocked = !withAlpha && (argb ushr 24) < 0xFF,
                                        onTap = { applyArgb(argb) },
                                        modifier = Modifier.graphicsLayer { alpha = if (i == liftIndex) 0f else 1f },
                                        cell = FullDims.PaletteCell,
                                        // O toque da grade é do gesto acima (tocar, segurar, arrastar).
                                        clickable = false,
                                        tagPrefix = "colorpicker.full.swatch",
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
        // Lixeira acima da paleta (só durante o arrasto).
        if (liftIndex >= 0) {
            val trashLabel = stringResource(R.string.om_ap_cp_trash)
            Box(
                Modifier
                    .align(Alignment.BottomCenter)
                    .navigationBarsPadding()
                    .padding(bottom = FullDims.Palette + 12.dp)
                    .size(52.dp)
                    .clip(CircleShape)
                    .background(if (overTrash) AureaColors.Danger else PickerColors.Side)
                    .onGloballyPositioned { coords[2] = it }
                    .testTag("colorpicker.full.trash")
                    .semantics { contentDescription = trashLabel },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Trash, 24.dp, if (overTrash) AureaColors.OnAccent else AureaColors.Text)
            }
            LiftedSwatch(liftColor, liftPos)
        } else {
            coords[2] = null
        }
        notice?.let { text ->
            Text(
                text,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .navigationBarsPadding()
                    .padding(bottom = FullDims.Palette + 12.dp, start = 16.dp, end = 16.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(PickerColors.Side)
                    .padding(horizontal = 12.dp, vertical = 8.dp),
            )
        }
    }
}

/** A amostra levantada: cresce para 2× e fica a 80 % em 300 ms, sob o dedo. */
@Composable
private fun LiftedSwatch(argb: Int, at: Offset) {
    val grow = remember { Animatable(1f) }
    LaunchedEffect(Unit) { grow.animateTo(2f, tween(300)) }
    val density = LocalDensity.current
    val half = with(density) { (FullDims.PaletteSwatch / 2).toPx() }
    Box(
        Modifier
            .offset { IntOffset((at.x - half).roundToInt(), (at.y - half).roundToInt()) }
            .size(FullDims.PaletteSwatch)
            .graphicsLayer {
                scaleX = grow.value
                scaleY = grow.value
                alpha = 1f - (grow.value - 1f) * 0.2f
            }
            .clip(RoundedCornerShape(5.dp))
            .drawBehind {
                drawChecker()
                drawRect(Color(argb))
            },
    )
}

/** Aba do topo (peso 1): texto aceso em destaque com o traço embaixo. */
@Composable
private fun RowScope.FullTab(label: String, on: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .testTag(tag)
            .semantics { selected = on }
            .tocavel(shrink = 1f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            style = AureaType.Base.merge(
                TextStyle(fontSize = 15.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Muted),
            ),
        )
        if (on) Box(Modifier.align(Alignment.BottomCenter).width(36.dp).height(2.dp).background(AureaColors.Accent))
    }
}

/**
 * O cartão "atual | nova" (tocar a metade atual volta à cor de partida) e o
 * "+" do canto, que salva a nova na paleta (some quando ela já está lá).
 */
@Composable
private fun CompareCard(width: Dp, height: Dp, start: FloatArray, state: ColorPickState, palette: List<Int>, onRevert: () -> Unit) {
    val context = LocalContext.current
    val now = state.rgba()
    val argb = rgbaToArgb(now)
    val currentLabel = stringResource(R.string.om_ap_cp_current)
    val newLabel = stringResource(R.string.om_ap_cp_new)
    val addLabel = stringResource(R.string.om_ap_cp_add)
    Box(Modifier.size(width, height)) {
        KeepLtr {
            Row(Modifier.fillMaxSize().clip(RoundedCornerShape(7.5.dp))) {
                Box(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .drawBehind {
                            drawChecker()
                            drawRect(rgbaColor(start))
                        }
                        .testTag("colorpicker.full.current")
                        .semantics { contentDescription = currentLabel }
                        .tocavel(shrink = 1f, onClick = onRevert),
                )
                Box(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .drawBehind {
                            drawChecker()
                            drawRect(rgbaColor(now))
                        }
                        .testTag("colorpicker.full.new")
                        .semantics { contentDescription = newLabel },
                )
            }
        }
        if (argb !in palette) {
            Box(
                Modifier
                    .align(Alignment.BottomEnd)
                    .size(32.dp)
                    .testTag("colorpicker.full.add")
                    .semantics { contentDescription = addLabel }
                    .tocavel(shrink = 1f, haptic = true) { SavedPalette.add(context, palette, argb) },
                contentAlignment = Alignment.BottomEnd,
            ) {
                Box(Modifier.padding(end = 3.dp, bottom = 4.dp)) { CupertinoIcon(CupertinoGlyph.Plus, 14.dp, inkOver(now)) }
            }
        }
    }
}

/** Roda: anel de matiz (toque absoluto) com o cartão no meio; réguas S · B · A embaixo. */
@Composable
private fun WheelPage(state: ColorPickState, start: FloatArray, withAlpha: Boolean, palette: List<Int>, onEdit: () -> Unit) {
    val edit by rememberUpdatedState(onEdit)
    val hueLabel = stringResource(R.string.om_ap_cp_hue)
    Column(Modifier.fillMaxSize()) {
        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(top = 15.dp, bottom = 10.dp),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier
                    .fillMaxSize()
                    .testTag("colorpicker.full.hue")
                    .semantics {
                        contentDescription = hueLabel
                        stateDescription = "${state.hue.roundToInt()}°"
                    }
                    .pointerInput(state) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            val c = Offset(size.width / 2f, size.height / 2f)
                            val outer = min(size.width, size.height) / 2f - FullDims.RingHandle.toPx() / 2f
                            val inner = outer - FullDims.RingThickness.toPx()
                            // O meio é do cartão: só o anel (com folga) escolhe a matiz.
                            if ((down.position - c).getDistance() < inner * 0.7f) return@awaitEachGesture
                            down.consume()
                            fun pick(p: Offset) {
                                var deg = Math.toDegrees(atan2((p.y - c.y).toDouble(), (p.x - c.x).toDouble())).toFloat()
                                if (deg < 0f) deg += 360f
                                state.hue = deg
                                edit()
                            }
                            state.dragging = true
                            pick(down.position)
                            try {
                                while (true) {
                                    val ev = awaitPointerEvent()
                                    val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (!ch.pressed) break
                                    ch.consume()
                                    pick(ch.position)
                                }
                            } finally {
                                state.dragging = false
                            }
                        }
                    }
                    .drawBehind {
                        val c = center
                        val handle = FullDims.RingHandle.toPx()
                        val thick = FullDims.RingThickness.toPx()
                        val outer = min(size.width, size.height) / 2f - handle / 2f
                        val mid = outer - thick / 2f
                        if (mid <= 0f) return@drawBehind
                        drawCircle(Brush.sweepGradient(HueStops, c), mid, c, style = Stroke(thick))
                        // Marcas a cada 30°, por fora do anel.
                        for (i in 0 until 12) {
                            val a = Math.toRadians(i * 30.0)
                            val ca = cos(a).toFloat()
                            val sa = sin(a).toFloat()
                            val r0 = outer + 2.dp.toPx()
                            val r1 = outer + (if (i % 3 == 0) 8.dp else 5.dp).toPx()
                            drawLine(AureaColors.Muted, Offset(c.x + ca * r0, c.y + sa * r0), Offset(c.x + ca * r1, c.y + sa * r1), 1.5.dp.toPx())
                        }
                        val rad = Math.toRadians(state.hue.toDouble())
                        val p = Offset(c.x + cos(rad).toFloat() * mid, c.y + sin(rad).toFloat() * mid)
                        drawCircle(Color.Black.copy(alpha = 0.35f), handle / 2f + 1.dp.toPx(), p)
                        drawCircle(Color.White, handle / 2f, p)
                        drawCircle(hueColor(state.hue), FullDims.RingHandleCore.toPx() / 2f, p)
                    },
            )
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CompareCard(70.dp, 50.dp, start, state, palette) {
                    state.set(start)
                    edit()
                }
                Spacer(Modifier.height(5.dp))
                val now = state.rgba()
                KeepLtr {
                    Text(
                        AureaEngine.formatColorCode(now[0], now[1], now[2], now[3], withAlpha),
                        modifier = Modifier.testTag("colorpicker.full.code"),
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
                    )
                }
            }
        }
        Column(Modifier.fillMaxWidth().padding(bottom = 4.dp)) {
            val now = state.rgba()
            SliderRow(
                lead = { CupertinoIcon(CupertinoGlyph.DropFill, 18.dp, AureaColors.Muted) },
                label = stringResource(R.string.om_ap_cp_saturation),
                value = state.sat,
                text = "${(state.sat * 100).roundToInt()}%",
                tag = "colorpicker.full.saturation",
                thumb = Color(now[0], now[1], now[2]),
                track = listOf(hsvColor(state.hue, 0f, state.value), hsvColor(state.hue, 1f, state.value)),
                checker = false,
                state = state,
            ) {
                state.sat = it
                edit()
            }
            SliderRow(
                lead = { CupertinoIcon(CupertinoGlyph.Lightbulb, 18.dp, AureaColors.Muted) },
                label = stringResource(R.string.om_ap_cp_brightness),
                value = state.value,
                text = "${(state.value * 100).roundToInt()}%",
                tag = "colorpicker.full.brightness",
                thumb = Color(now[0], now[1], now[2]),
                track = listOf(Color.Black, hsvColor(state.hue, state.sat, 1f)),
                checker = false,
                state = state,
            ) {
                state.value = it
                edit()
            }
            if (withAlpha) {
                SliderRow(
                    lead = { CupertinoIcon(CupertinoGlyph.Grid, 18.dp, AureaColors.Muted) },
                    label = stringResource(R.string.om_ap_cp_alpha),
                    value = state.alpha,
                    text = "${(state.alpha * 100).roundToInt()}%",
                    tag = "colorpicker.full.alpha",
                    thumb = Color(now[0], now[1], now[2], now[3]),
                    track = listOf(Color(now[0], now[1], now[2], 0f), Color(now[0], now[1], now[2])),
                    checker = true,
                    state = state,
                ) {
                    state.alpha = it
                    edit()
                }
            }
        }
    }
}

/** RGB: cartão 140 × 70, "#RRGGBB (A%)" e réguas R · G · B · A de 0 a 255. */
@Composable
private fun RgbPage(state: ColorPickState, start: FloatArray, withAlpha: Boolean, palette: List<Int>, onEdit: () -> Unit) {
    val edit by rememberUpdatedState(onEdit)
    val now = state.rgba()
    fun setChannel(i: Int, v: Float) {
        val c = state.rgba()
        c[i] = v.coerceIn(0f, 1f)
        state.set(c)
        edit()
    }
    Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CompareCard(140.dp, 70.dp, start, state, palette) {
                    state.set(start)
                    edit()
                }
                Spacer(Modifier.height(5.dp))
                val hex = AureaEngine.formatColorCode(now[0], now[1], now[2], 1f, false)
                val text = if (withAlpha) String.format(Locale.ROOT, "%s (%d%%)", hex, (now[3] * 100).roundToInt()) else hex
                KeepLtr {
                    Text(
                        text,
                        modifier = Modifier.testTag("colorpicker.full.code"),
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
                    )
                }
            }
        }
        Column(Modifier.fillMaxWidth().padding(bottom = 4.dp)) {
            val thumb = Color(now[0], now[1], now[2])
            val names = listOf(R.string.om_ap_cp_red, R.string.om_ap_cp_green, R.string.om_ap_cp_blue)
            val letters = listOf("R", "G", "B")
            val tags = listOf("red", "green", "blue")
            for (i in 0 until 3) {
                val lo = now.copyOf().also { it[i] = 0f }
                val hi = now.copyOf().also { it[i] = 1f }
                SliderRow(
                    lead = { ChannelLetter(letters[i]) },
                    label = stringResource(names[i]),
                    value = now[i],
                    text = "${(now[i] * 255f).roundToInt()}",
                    tag = "colorpicker.full.${tags[i]}",
                    thumb = thumb,
                    track = listOf(Color(lo[0], lo[1], lo[2]), Color(hi[0], hi[1], hi[2])),
                    checker = false,
                    state = state,
                ) { setChannel(i, it) }
            }
            if (withAlpha) {
                SliderRow(
                    lead = { ChannelLetter("A") },
                    label = stringResource(R.string.om_ap_cp_alpha),
                    value = now[3],
                    text = "${(now[3] * 255f).roundToInt()}",
                    tag = "colorpicker.full.alphaByte",
                    thumb = Color(now[0], now[1], now[2], now[3]),
                    track = listOf(Color(now[0], now[1], now[2], 0f), Color(now[0], now[1], now[2])),
                    checker = true,
                    state = state,
                ) {
                    state.alpha = it.coerceIn(0f, 1f)
                    edit()
                }
            }
        }
    }
}

@Composable
private fun ChannelLetter(text: String) {
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)))
}

/** HSV → Color de tela (alfa 1). */
private fun hsvColor(h: Float, s: Float, v: Float): Color {
    val o = FloatArray(4)
    hsvToRgb(h, s, v, o)
    return Color(o[0], o[1], o[2])
}

/**
 * Uma régua de 44 dp: ícone/letra de 50 dp à esquerda, trilha com o degradê do
 * canal (12 dp, xadrez sob alfa) e o polegar com a cor; valor de 40 dp à direita.
 * Tocar ou arrastar escolhe (posição absoluta).
 */
@Composable
private fun SliderRow(
    lead: @Composable () -> Unit,
    label: String,
    value: Float,
    text: String,
    tag: String,
    thumb: Color,
    track: List<Color>,
    checker: Boolean,
    state: ColorPickState,
    onValue: (Float) -> Unit,
) {
    val send by rememberUpdatedState(onValue)
    val v = value.coerceIn(0f, 1f)
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(FullDims.SliderRow), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(FullDims.Lead).fillMaxHeight(), contentAlignment = Alignment.Center) { lead() }
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .testTag(tag)
                    .semantics {
                        contentDescription = label
                        stateDescription = text
                    }
                    .pointerInput(state) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            down.consume()
                            val pad = 11.dp.toPx()
                            fun at(x: Float) = ((x - pad) / (size.width - 2 * pad).coerceAtLeast(1f)).coerceIn(0f, 1f)
                            state.dragging = true
                            send(at(down.position.x))
                            try {
                                while (true) {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (!c.pressed) break
                                    c.consume()
                                    send(at(c.position.x))
                                }
                            } finally {
                                state.dragging = false
                            }
                        }
                    }
                    .drawBehind {
                        val pad = 11.dp.toPx()
                        val h = 12.dp.toPx()
                        val top = (size.height - h) / 2f
                        val w = size.width - 2 * pad
                        if (w <= 0f) return@drawBehind
                        val radius = CornerRadius(h / 2f)
                        if (checker) {
                            val path = Path().apply { addRoundRect(RoundRect(pad, top, pad + w, top + h, radius)) }
                            clipPath(path) { drawChecker() }
                        }
                        drawRoundRect(
                            Brush.horizontalGradient(track, startX = pad, endX = pad + w),
                            topLeft = Offset(pad, top),
                            size = Size(w, h),
                            cornerRadius = radius,
                        )
                        val x = pad + v * w
                        val cy = size.height / 2f
                        drawCircle(Color.Black.copy(alpha = 0.35f), 11.dp.toPx(), Offset(x, cy))
                        drawCircle(Color.White, 10.dp.toPx(), Offset(x, cy))
                        drawCircle(thumb.copy(alpha = 1f), 7.5.dp.toPx(), Offset(x, cy))
                    },
            )
            Box(Modifier.width(FullDims.Trail).fillMaxHeight(), contentAlignment = Alignment.Center) {
                Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")))
            }
        }
    }
}
