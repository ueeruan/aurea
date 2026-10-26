package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.roundToInt

// =============================================================================
// Omega conteúdo (área 06, P0-4): o EDITOR DE DESENHO.
//
//   ┌───────────────────────────────────────────┬──────────┐
//   │[⟋][====== espessura (42 passos) =====][12]│  Caneta  │ coluna de 60 dp,
//   │ seletor de cor compacto (até 320 dp)       │  Pincel  │ 4 botões de altura
//   │                                            │ Preencher│ igual
//   │                                            │ Borracha │
//   └───────────────────────────────────────────┴──────────┘
// O dedo no palco desenha: cada traço vira um grupo da camada de desenho no
// motor (Engine::add_drawing_stroke), um passo de desfazer por traço.
// Ferramenta, espessura e cor ficam salvas no aparelho (ContentPrefs).
// Par do DrawingPanel do Swift (mesmos ids).
// =============================================================================

/** Os 42 passos da régua de espessura (1…20 de 1 em 1, depois mais largos). */
internal val DrawWidthSteps = intArrayOf(
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    22, 24, 26, 28, 30, 33, 36, 40, 45, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 250, 300,
)

private fun nearestWidthStep(width: Int): Int {
    var best = 0
    for (i in DrawWidthSteps.indices) if (abs(DrawWidthSteps[i] - width) < abs(DrawWidthSteps[best] - width)) best = i
    return best
}

/** O traço em curso no palco (px da composição, x/y intercalados), para o desenho ao vivo. */
internal object DrawingStageState {
    var stroke by mutableStateOf(FloatArray(0))
}

/** O palco está desenhando? (painel Desenho aberto numa camada de desenho escolhida.) */
internal fun drawingActive(store: EditorStore, ui: EditorUi): Boolean {
    if (ui.panel != EditorPanel.Drawing || store.selection.size != 1 || !store.isDrawingLayer) return false
    val d = store.detail ?: return false
    return !d.locked
}

@Composable
internal fun DrawingPanel(env: PanelEnv) {
    val store = env.store
    val context = LocalContext.current
    LaunchedEffect(Unit) { ContentPrefs.load(context) }
    val tool = ContentPrefs.drawTool
    Row(Modifier.fillMaxSize().background(AureaColors.PanelFrameBackground).testTag("draw.panel")) {
        Column(
            Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 6.dp),
        ) {
            WidthRow(store)
            Spacer(Modifier.height(8.dp))
            val c = Color(ContentPrefs.drawColor)
            ContentColorPicker(
                color = c,
                modifier = Modifier.widthIn(max = 320.dp).fillMaxWidth(),
                tag = "draw.color",
                label = stringResource(R.string.om_draw_color_cd),
                onChange = { ContentPrefs.setDrawColor(it.toArgb()) },
            )
        }
        Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        Column(Modifier.width(60.dp).fillMaxHeight()) {
            ToolButton(R.string.om_draw_tool_pen, "draw.tool.pen", tool == 0) { tint -> CupertinoIcon(CupertinoGlyph.Pencil, 22.dp, tint) }
            ToolButton(R.string.om_draw_tool_brush, "draw.tool.brush", tool == 1) { tint -> CupertinoIcon(CupertinoGlyph.Paintbrush, 22.dp, tint) }
            ToolButton(R.string.om_draw_tool_fill, "draw.tool.fill", tool == 2) { tint -> CupertinoIcon(CupertinoGlyph.DropFill, 22.dp, tint) }
            ToolButton(R.string.om_draw_tool_eraser, "draw.tool.eraser", tool == 3) { tint -> ContentIcon(ContentIconKind.Eraser, 22.dp, tint) }
        }
    }
}

@Composable
private fun ColumnScope.ToolButton(label: Int, tag: String, on: Boolean, icon: @Composable (Color) -> Unit) {
    val text = stringResource(label)
    val tool = when (tag) { "draw.tool.brush" -> 1; "draw.tool.fill" -> 2; "draw.tool.eraser" -> 3; else -> 0 }
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .testTag(tag)
            .semantics { contentDescription = text; selected = on }
            .tocavel(shrink = 1f) { ContentPrefs.setDrawTool(tool) },
        contentAlignment = Alignment.Center,
    ) { icon(if (on) AureaColors.Accent else AureaColors.Text) }
}

/** A linha da espessura: ícone, régua de 42 passos e a pílula do valor. */
@Composable
private fun WidthRow(store: EditorStore) {
    val width = ContentPrefs.drawWidth
    val index = nearestWidthStep(width)
    val label = stringResource(R.string.om_draw_width_value_cd, width)
    Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(CupertinoGlyph.Scribble, 20.dp, AureaColors.Text)
        Spacer(Modifier.width(8.dp))
        BoxWithConstraints(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .testTag("draw.width.slider")
                .semantics { contentDescription = label }
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        down.consume()
                        fun at(x: Float) {
                            val i = ((x / size.width.coerceAtLeast(1)) * (DrawWidthSteps.size - 1)).roundToInt().coerceIn(0, DrawWidthSteps.size - 1)
                            ContentPrefs.setDrawWidth(DrawWidthSteps[i])
                        }
                        at(down.position.x)
                        while (true) {
                            val e = awaitPointerEvent()
                            val c = e.changes.firstOrNull { it.id == down.id } ?: break
                            c.consume()
                            if (!c.pressed) break
                            at(c.position.x)
                        }
                    }
                },
        ) {
            val w = maxWidth
            val frac = index / (DrawWidthSteps.size - 1f)
            Box(Modifier.align(Alignment.CenterStart).fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)).background(AureaColors.FxTrack))
            Box(Modifier.align(Alignment.CenterStart).width(w * frac).height(3.dp).clip(RoundedCornerShape(2.dp)).background(AureaColors.Accent))
            Box(Modifier.align(Alignment.CenterStart).offset(x = w * frac - 9.dp).size(18.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Accent))
        }
        Spacer(Modifier.width(8.dp))
        Box(
            Modifier.size(35.dp, 22.dp).clip(RoundedCornerShape(3.75.dp)).background(AureaColors.ContentValuePill).testTag("draw.width.value"),
            contentAlignment = Alignment.Center,
        ) {
            Text("$width", style = AureaType.of(12f, FontWeight.W600, color = AureaColors.Accent))
        }
    }
}

/** O traço ao vivo: a linha do dedo na cor e na espessura da ferramenta. */
internal fun DrawScope.drawDrawingOverlay(m: StageMapper) {
    val s = DrawingStageState.stroke
    if (s.size < 2) return
    val tool = ContentPrefs.drawTool
    val color = if (tool == 3) Color.White.copy(alpha = 0.5f) else Color(ContentPrefs.drawColor)
    val width = (ContentPrefs.drawWidth * m.fit).coerceAtLeast(1.5f)
    val p = Path()
    p.moveTo(m.sx(s[0]), m.sy(s[1]))
    var i = 2
    while (i + 1 < s.size) { p.lineTo(m.sx(s[i]), m.sy(s[i + 1])); i += 2 }
    if (tool == 2) {
        p.close()
        drawPath(p, color.copy(alpha = 0.6f))
        drawPath(p, color, style = Stroke(1.5.dp.toPx()))
    } else {
        drawPath(p, color, style = Stroke(width, cap = StrokeCap.Round, join = StrokeJoin.Round))
    }
}

/** O gesto do desenho: o dedo grava x, y (composição) e a pressão até subir; soltar manda o traço. */
internal suspend fun AwaitPointerEventScope.drawingGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val d = store.detail
    if (d == null || store.playhead < d.startFrame || store.playhead >= d.endFrame) {
        store.showToast(AppText.get(store.getApplication<Application>(), R.string.sh_playhead_into_layer))
        down.consume()
        while (true) {
            val e = awaitPointerEvent()
            val c = e.changes.firstOrNull { it.id == down.id } ?: break
            c.consume()
            if (!c.pressed) break
        }
        return
    }
    if (store.playing) store.pause()
    val xyp = ArrayList<Float>(1024)
    val live = ArrayList<Float>(512)
    fun add(pos: Offset, pressure: Float) {
        val x = m.cx(pos.x)
        val y = m.cy(pos.y)
        xyp += x; xyp += y; xyp += pressure.coerceIn(0f, 1f)
        live += x; live += y
    }
    add(down.position, down.pressure)
    DrawingStageState.stroke = live.toFloatArray()
    down.consume()
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        c.consume()
        // O histórico do movimento (pontos entre dois quadros) entra junto.
        for (h in c.historical) add(h.position, c.pressure)
        if (!c.pressed) break
        add(c.position, c.pressure)
        DrawingStageState.stroke = live.toFloatArray()
    }
    DrawingStageState.stroke = FloatArray(0)
    val argb = ContentPrefs.drawColor
    val rgba = floatArrayOf(
        ((argb shr 16) and 0xFF) / 255f, ((argb shr 8) and 0xFF) / 255f, (argb and 0xFF) / 255f, ((argb ushr 24) and 0xFF) / 255f,
    )
    store.commitDrawingStroke(ContentPrefs.drawTool, rgba, ContentPrefs.drawWidth.toFloat(), xyp.toFloatArray())
}
