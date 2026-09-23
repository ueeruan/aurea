package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.LinkOff
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyLabelChip
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Estado do Editar forma que o PALCO também lê: a corrente de proporção (as
 * alças de canto e as linhas de tamanho respeitam a mesma).
 */
internal object ShapeEditState {
    var linked by mutableStateOf(false)
    /** Parâmetro que o losango do trilho grava (5 = largura, 6 = altura, 1 = raio…). */
    var param by mutableIntStateOf(5)
}

/** As formas simples (SDF do motor, `shape.frag`) na ordem da troca ‹ ›. */
internal val SimpleShapes = intArrayOf(0, 1, 3, 4, 5, 6, 7, 8, 9, 10)

internal fun shapeName(type: Int): String = when (type) {
    0 -> "Retângulo"
    1 -> "Elipse"
    3 -> "Polígono"
    4 -> "Estrela"
    5 -> "Cruz"
    6 -> "Anel"
    7 -> "Fatia"
    8 -> "Flor"
    9 -> "Seta"
    10 -> "Triângulo"
    else -> "Forma"
}

/**
 * EDITAR FORMA (ref18): trilho ‹ ◇ curva; em cima a troca de forma ‹▢› e a
 * corrente de proporção; embaixo só os controles que a forma escolhida tem —
 * Tamanho (x, y) e Raio no retângulo; Pontas e Raio interno na estrela; Lados
 * no polígono… O palco mostra alças de tamanho (cantos e lados) e, no
 * retângulo, a alça do raio. Tudo isto é ANIMÁVEL: a linha escolhida acende e
 * o losango do trilho grava o keyframe dela no cabeçote.
 */
@Composable
internal fun ShapeEditPanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    if (detail.kind != com.aurea.aurea.ui.theme.LayerType.Shape.kind) {
        PanelNotice(stringResource(R.string.panel_escolha_forma_editar_silhueta), Modifier.padding(horizontal = 18.dp))
        return
    }
    if (store.isVectorLayer) {
        PanelNotice(stringResource(R.string.panel_esta_camada_vetorial_edite_caminhos_painel), Modifier.padding(horizontal = 18.dp))
        return
    }
    val type = detail.shapeTypePoints and 0xFFFF
    val points = (detail.shapeTypePoints ushr 16) and 0xFFFF
    // Estado de animação dos parâmetros (valores + bits) no cabeçote.
    val sp by remember(store) { derivedStateOf { store.shapeParams } }
    val sel = ShapeEditState.param
    val animBits = sp?.getOrNull(7)?.toInt() ?: 0
    val keyBits = sp?.getOrNull(8)?.toInt() ?: 0
    val look = when {
        keyBits and (1 shl sel) != 0 -> KeyframeLook.KeyHere
        animBits and (1 shl sel) != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = { store.toggleShapeParamKey(sel) },
            curveAnimated = animBits and (1 shl sel) != 0,
            // Curva do trecho sob o cabeçote, na trilha da linha acesa.
            onCurve = if (store.primaryKeys().shapeTrack(sel).size >= 2) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        store.primaryKeys().shapeTrack(sel).segmentStart(t)?.let { key ->
                            store.selectKeyframe(layer, key)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
        )
        Column(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState()).padding(top = 6.dp, end = 10.dp, bottom = 16.dp)) {
            ShapeSwitcher(store, type)
            Spacer(Modifier.height(4.dp))
            SizeRow(env, detail.sourceWidth.toFloat(), detail.sourceHeight.toFloat())
            val w = detail.sourceWidth.toFloat()
            val h = detail.sourceHeight.toFloat()
            when (type) {
                0 -> ShapeRow(env, 1, stringResource(R.string.panel_raio), detail.shapeCorner, 0.3f, 0f, max(0f, min(w, h) / 2f), "px", 0, 0f, "raio") { store.setShapeParam(1, it) }
                3, 4, 8 -> ShapeRow(env, 2, when (type) { 3 -> stringResource(R.string.panel_lados); 8 -> stringResource(R.string.panel_petalas); else -> stringResource(R.string.panel_pontas) }, points.toFloat(), 0.06f, 3f, 64f, "", 0, 5f, "pontas") {
                    store.setShapeParam(2, it.roundToInt().toFloat())
                }
            }
            when (type) {
                4 -> ShapeRow(env, 3, stringResource(R.string.panel_raio_interno), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 50f, stringResource(R.string.panel_raio_interno_75cf)) { store.setShapeParam(3, it / 100f) }
                5 -> ShapeRow(env, 3, stringResource(R.string.panel_espessura), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 50f, "espessura") { store.setShapeParam(3, it / 100f) }
                // Anel: o motor guarda o FURO; a pessoa pensa na espessura do aro.
                6 -> ShapeRow(env, 3, stringResource(R.string.panel_espessura), (1f - detail.shapeInner) * 100f, 0.3f, 5f, 95f, "%", 0, 50f, "espessura") { store.setShapeParam(3, 1f - it / 100f) }
            }
            Spacer(Modifier.height(6.dp))
            KitHint(
                stringResource(R.string.panel_arraste_alcas_palco_mudar_tamanho) + (if (type == 0) "; a alça azul arredonda os cantos. " else ". ") +
                    stringResource(R.string.panel_losango_trilho_grava_keyframe_linha_acesa),
            )
        }
    }
}

/**
 * A TROCA DE FORMA ‹▢›: miniatura da forma atual com o nome; ‹ e › passam
 * para a anterior/próxima (um passo de desfazer cada). À direita, a corrente
 * de proporção.
 */
@Composable
private fun ShapeSwitcher(store: EditorStore, type: Int) {
    val idx = SimpleShapes.indexOf(type).coerceAtLeast(0)
    fun go(delta: Int) {
        val next = SimpleShapes[(idx + delta + SimpleShapes.size) % SimpleShapes.size]
        store.beginGesture("trocar forma")
        store.setShapeParam(0, next.toFloat())
        store.endGesture()
    }
    Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(44.dp).semantics { contentDescription = "Forma anterior" }.tocavel(shrink = 1f) { go(-1) },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.ChevronLeft, 18.dp, AureaColors.Text) }
        Box(
            Modifier.size(44.dp, 40.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip),
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(26.dp)) { drawShapeGlyph(type, AureaColors.Text) }
        }
        Box(
            Modifier.size(44.dp).semantics { contentDescription = "Próxima forma" }.tocavel(shrink = 1f) { go(1) },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.ChevronRight, 18.dp, AureaColors.Text) }
        Spacer(Modifier.width(6.dp))
        Text(shapeName(type), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700)))
        val linked = ShapeEditState.linked
        Box(
            Modifier
                .size(44.dp, 36.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(if (linked) AureaColors.AccentDim else AureaColors.Chip)
                .then(if (linked) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(8.dp)) else Modifier)
                .semantics { contentDescription = if (linked) "Soltar proporção" else "Manter proporção" }
                .tocavel(shrink = 1f) { ShapeEditState.linked = !linked },
            contentAlignment = Alignment.Center,
        ) {
            Icon(if (linked) Icons.Rounded.Link else Icons.Rounded.LinkOff, contentDescription = null, tint = if (linked) AureaColors.Accent else AureaColors.Text, modifier = Modifier.size(20.dp))
        }
    }
}

/**
 * TAMANHO (x, y) numa linha só (ref18): a régua mexe no eixo aceso (o outro
 * acompanha se a corrente estiver travada). Tocar na caixa do outro eixo o
 * acende; tocar na caixa acesa abre o teclado.
 */
@Composable
private fun SizeRow(env: PanelEnv, w: Float, h: Float) {
    val store = env.store
    var axis by remember { mutableIntStateOf(0) }
    LaunchedEffect(axis) { ShapeEditState.param = if (axis == 0) 5 else 6 }
    var dragging by remember { mutableStateOf(false) }
    var live by remember { mutableFloatStateOf(0f) }
    val cur = if (axis == 0) w else h
    fun write(v: Float, fromW: Float, fromH: Float) {
        val nv = v.coerceIn(1f, 16384f)
        if (ShapeEditState.linked) {
            val from = if (axis == 0) fromW else fromH
            val k = if (from > 0f) nv / from else 1f
            val nw = if (axis == 0) nv else (fromW * k).coerceIn(1f, 16384f)
            val nh = if (axis == 1) nv else (fromH * k).coerceIn(1f, 16384f)
            store.setShapeParam(5, nw)
            store.setShapeParam(6, nh)
        } else {
            store.setShapeParam(if (axis == 0) 5 else 6, nv)
        }
    }
    val startW = remember { FloatArray(2) }
    Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
        PropertyLabelChip(stringResource(R.string.panel_tamanho), selected = true)
        Spacer(Modifier.width(6.dp))
        TickRuler(
            value = { if (dragging) live else cur },
            unitsPerDp = 1f,
            active = true,
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .padding(vertical = 6.dp)
                .valueDrag(
                    enabled = true,
                    start = { if (axis == 0) (store.detail?.sourceWidth ?: 1).toFloat() else (store.detail?.sourceHeight ?: 1).toFloat() },
                    unitsPerDp = { 1f },
                    min = 1f,
                    max = 16384f,
                    onStart = {
                        startW[0] = (store.detail?.sourceWidth ?: 1).toFloat()
                        startW[1] = (store.detail?.sourceHeight ?: 1).toFloat()
                        live = if (axis == 0) startW[0] else startW[1]
                        dragging = true
                        store.beginGesture("tamanho da forma")
                    },
                    onValue = { v ->
                        live = v
                        write(v, startW[0], startW[1])
                    },
                    onEnd = {
                        dragging = false
                        store.endGesture()
                    },
                ),
        )
        Spacer(Modifier.width(6.dp))
        for (a in 0..1) {
            val v = if (a == axis && dragging) live else if (a == 0) w else h
            ValueBox(
                numeroPtBr(v, 0),
                width = 58.dp,
                label = if (a == 0) stringResource(R.string.panel_x_largura) else stringResource(R.string.panel_y_altura),
                color = if (a == axis) AureaColors.Accent else Color.White,
                onTap = {
                    if (axis != a) {
                        axis = a
                    } else {
                        env.openKeypad(KeypadRequest(if (a == 0) "Largura" else "Altura", v, "px", 1f, 16384f, 0) {
                            store.beginGesture("tamanho da forma")
                            write(it, w, h)
                            store.endGesture()
                        })
                    }
                },
            )
            if (a == 0) Spacer(Modifier.width(4.dp))
        }
    }
}

/** Uma linha de parâmetro da forma (um arrasto = um desfazer). */
@Composable
private fun ShapeRow(
    env: PanelEnv,
    param: Int,
    label: String,
    value: Float,
    step: Float,
    min: Float,
    max: Float,
    unit: String,
    decimals: Int,
    default: Float,
    gesture: String,
    set: (Float) -> Unit,
) {
    val store = env.store
    val sp = store.shapeParams
    val anim = (sp?.getOrNull(7)?.toInt() ?: 0) and (1 shl param) != 0
    val here = (sp?.getOrNull(8)?.toInt() ?: 0) and (1 shl param) != 0
    HumanRow(
        env, label, value, step, min, max, unit, decimals, default,
        onStart = { ShapeEditState.param = param; store.beginGesture(gesture) },
        onValue = set,
        onEnd = { store.endGesture() },
        onCommit = { v ->
            ShapeEditState.param = param
            store.beginGesture(gesture)
            set(v)
            store.endGesture()
        },
        selected = ShapeEditState.param == param,
        onSelect = { ShapeEditState.param = param },
        keyframe = if (here) KeyframeLook.KeyHere else if (anim) KeyframeLook.Animated else KeyframeLook.None,
    )
}

/** Silhueta de cada forma simples (miniatura da troca). */
internal fun DrawScope.drawShapeGlyph(type: Int, color: Color) {
    val s = min(size.width, size.height)
    val c = Offset(size.width / 2f, size.height / 2f)
    val r = s / 2f
    when (type) {
        0 -> drawRoundRect(color, Offset(c.x - r, c.y - r * 0.8f), Size(2 * r, 1.6f * r), CornerRadius(r * 0.25f))
        1 -> drawOval(color, Offset(c.x - r, c.y - r * 0.8f), Size(2 * r, 1.6f * r))
        3 -> drawPath(polygon(c, r, 5, 1f), color)
        4 -> drawPath(polygon(c, r, 5, 0.45f), color)
        5 -> {
            val t = r * 0.36f
            drawRect(color, Offset(c.x - t, c.y - r), Size(2 * t, 2 * r))
            drawRect(color, Offset(c.x - r, c.y - t), Size(2 * r, 2 * t))
        }
        6 -> drawCircle(color, r * 0.78f, c, style = Stroke(r * 0.36f))
        7 -> drawArc(color, -0f, 270f, true, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
        8 -> {
            val p = Path()
            val n = 5
            for (i in 0..72) {
                val a = i / 72f * 2f * PI.toFloat()
                val rr = r * (0.72f + 0.28f * cos(n * a))
                val x = c.x + rr * cos(a)
                val y = c.y + rr * sin(a)
                if (i == 0) p.moveTo(x, y) else p.lineTo(x, y)
            }
            p.close()
            drawPath(p, color)
        }
        9 -> {
            val p = Path()
            p.moveTo(c.x - r, c.y - r * 0.22f)
            p.lineTo(c.x + r * 0.1f, c.y - r * 0.22f)
            p.lineTo(c.x + r * 0.1f, c.y - r * 0.8f)
            p.lineTo(c.x + r, c.y)
            p.lineTo(c.x + r * 0.1f, c.y + r * 0.8f)
            p.lineTo(c.x + r * 0.1f, c.y + r * 0.22f)
            p.lineTo(c.x - r, c.y + r * 0.22f)
            p.close()
            drawPath(p, color)
        }
        10 -> {
            val p = Path()
            p.moveTo(c.x - r, c.y - r)
            p.lineTo(c.x - r, c.y + r)
            p.lineTo(c.x + r, c.y + r)
            p.close()
            drawPath(p, color)
        }
        else -> drawRect(color, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
    }
}

private fun polygon(c: Offset, r: Float, n: Int, inner: Float): Path {
    val p = Path()
    val steps = if (inner < 1f) n * 2 else n
    for (i in 0 until steps) {
        val a = -PI.toFloat() / 2f + i * 2f * PI.toFloat() / steps
        val rr = if (inner < 1f && i % 2 == 1) r * inner else r
        val x = c.x + rr * cos(a)
        val y = c.y + rr * sin(a)
        if (i == 0) p.moveTo(x, y) else p.lineTo(x, y)
    }
    p.close()
    return p
}
