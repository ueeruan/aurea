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
import androidx.compose.ui.platform.testTag
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.RoundRect
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
import kotlin.math.abs
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
internal val SimpleShapes = intArrayOf(0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24)

/**
 * Parâmetros das formas (shape::Param do motor, ShapeGeometry.hpp): o índice
 * do vetor de `queryShapeParams` e da trilha ShapeParam.
 */
internal object ShapeParam {
    const val CORNER = 1
    const val COUNT = 2
    const val INNER = 3
    const val DEPTH = 7
    const val TIP = 8
    const val THICKNESS = 9
    const val SWEEP = 10
    const val HEAD = 11
    const val SHAFT = 12
    const val AMPLITUDE = 13
    const val SEED = 14
}

/** Bits de "animado" e de "keyframe aqui": os dois últimos números do vetor de parâmetros. */
internal fun shapeAnimBits(sp: FloatArray?): Int = sp?.let { it.getOrNull(it.size - 2) }?.toInt() ?: 0
internal fun shapeKeyBits(sp: FloatArray?): Int = sp?.let { it.getOrNull(it.size - 1) }?.toInt() ?: 0

@Composable
internal fun shapeName(type: Int): String = stringResource(when (type) {
    0 -> R.string.editor_retangulo
    1 -> R.string.editor_elipse
    3 -> R.string.editor_poligono
    4 -> R.string.editor_estrela
    5 -> R.string.sh_shape_cross
    6 -> R.string.sh_shape_ring
    7 -> R.string.sh_shape_slice
    8 -> R.string.sh_shape_flower
    9 -> R.string.sh_shape_arrow
    10 -> R.string.panel_triangulo
    12 -> R.string.sh_shape_trapezoid
    13 -> R.string.sh_shape_parallelogram
    14 -> R.string.sh_shape_gear
    15 -> R.string.sh_shape_double_arrow
    16 -> R.string.sh_shape_line
    17 -> R.string.sh_shape_diamond
    18 -> R.string.sh_shape_heart
    19 -> R.string.sh_shape_seal
    20 -> R.string.sh_shape_arc
    21 -> R.string.sh_shape_bubble
    22 -> R.string.sh_shape_bolt
    23 -> R.string.sh_shape_wave
    24 -> R.string.sh_shape_blob
    else -> R.string.target_shape
})

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
    androidx.compose.runtime.DisposableEffect(store, store.primary, sel) {
        store.timelineFocus = shapePanelTimelineFocus(sel)
        onDispose { store.timelineFocus = null }
    }
    val curveKeys = curveTrack((listOf(sel) + listOf(5, 6, 1, 2, 3, 4) + (7..14)).distinct()
        .map { store.primaryKeys().shapeTrack(it) })
    val animBits = shapeAnimBits(sp)
    val keyBits = shapeKeyBits(sp)
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
            onCurve = if (curveKeys.isNotEmpty()) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        (curveKeys.segmentStart(t) ?: curveKeys.firstOrNull())?.let { key ->
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
            val w = sp?.getOrNull(5) ?: detail.sourceWidth.toFloat()
            val h = sp?.getOrNull(6) ?: detail.sourceHeight.toFloat()
            SizeRow(env, w, h)
            // Valor no cabeçote dos parâmetros 7..14 (o motor já resolve o padrão da forma).
            fun pv(param: Int, fallback: Float): Float = sp?.getOrNull(param) ?: fallback
            when (type) {
                0, 16, 21 -> ShapeRow(env, 1, stringResource(R.string.panel_raio), detail.shapeCorner, 0.3f, 0f, max(0f, min(w, h) / 2f), "px", 0, 0f, "raio") { store.setShapeParam(1, it) }
                3, 4, 8, 14 -> ShapeRow(env, 2, when (type) { 3 -> stringResource(R.string.panel_lados); 8 -> stringResource(R.string.panel_petalas); 14 -> stringResource(R.string.panel_dentes); else -> stringResource(R.string.panel_pontas) }, points.toFloat(), 0.06f, 3f, 64f, "", 0, 5f, "pontas") {
                    store.setShapeParam(2, it.roundToInt().toFloat())
                }
                // Selo: saliências; onda: ondas; mancha: lóbulos.
                19, 23, 24 -> ShapeRow(
                    env, 2,
                    stringResource(when (type) { 19 -> R.string.shp_bumps; 23 -> R.string.shp_waves; else -> R.string.shp_lobes }),
                    points.toFloat(), 0.06f,
                    when (type) { 19 -> 3f; 23 -> 1f; else -> 2f }, when (type) { 19 -> 48f; else -> 12f }, "", 0,
                    when (type) { 19 -> 14f; 23 -> 3f; else -> 4f }, "contagem",
                ) { store.setShapeParam(2, it.roundToInt().toFloat()) }
            }
            when (type) {
                4 -> ShapeRow(env, 3, stringResource(R.string.panel_raio_interno), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 50f, stringResource(R.string.panel_raio_interno_75cf)) { store.setShapeParam(3, it / 100f) }
                5 -> ShapeRow(env, 3, stringResource(R.string.panel_espessura), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 50f, "espessura") { store.setShapeParam(3, it / 100f) }
                // Anel: o motor guarda o FURO; a pessoa pensa na espessura do aro.
                6 -> ShapeRow(env, 3, stringResource(R.string.panel_espessura), (1f - detail.shapeInner) * 100f, 0.3f, 5f, 95f, "%", 0, 50f, "espessura") { store.setShapeParam(3, 1f - it / 100f) }
                12 -> ShapeRow(env, 3, stringResource(R.string.panel_largura_topo), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 60f, "topo") { store.setShapeParam(3, it / 100f) }
                13 -> ShapeRow(env, 3, stringResource(R.string.panel_inclinacao), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 50f, "inclinacao") { store.setShapeParam(3, it / 100f) }
                14 -> {
                    ShapeRow(env, 3, stringResource(R.string.panel_cubo), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 30f, "cubo") { store.setShapeParam(3, it / 100f) }
                    PercentRow(env, ShapeParam.DEPTH, stringResource(R.string.shp_depth), pv(ShapeParam.DEPTH, 0.22f), 5f, 95f, 22f, "profundidade")
                }
                15 -> {
                    ShapeRow(env, 3, stringResource(R.string.panel_espessura), detail.shapeInner * 100f, 0.3f, 5f, 95f, "%", 0, 22f, "espessura") { store.setShapeParam(3, it / 100f) }
                    PercentRow(env, ShapeParam.HEAD, stringResource(R.string.shp_head), pv(ShapeParam.HEAD, 0.275f), 10f, 90f, 27.5f, "ponta")
                }
                // Fatia e arco: abertura em graus (270° = a fatia de sempre).
                7 -> SweepRow(env, pv(ShapeParam.SWEEP, 270f))
                8 -> PercentRow(env, ShapeParam.DEPTH, stringResource(R.string.shp_depth), pv(ShapeParam.DEPTH, 0.28f), 5f, 95f, 28f, "profundidade")
                9 -> {
                    PercentRow(env, ShapeParam.HEAD, stringResource(R.string.shp_head), pv(ShapeParam.HEAD, 0.45f), 10f, 90f, 45f, "ponta")
                    PercentRow(env, ShapeParam.SHAFT, stringResource(R.string.shp_shaft), pv(ShapeParam.SHAFT, 0.44f), 5f, 100f, 44f, "haste")
                }
                16 -> PercentRow(env, ShapeParam.THICKNESS, stringResource(R.string.panel_espessura), pv(ShapeParam.THICKNESS, 1f), 2f, 100f, 100f, "espessura")
                17 -> PercentRow(env, ShapeParam.TIP, stringResource(R.string.shp_waist), pv(ShapeParam.TIP, 0.5f), 5f, 95f, 50f, "cintura")
                18 -> PercentRow(env, ShapeParam.DEPTH, stringResource(R.string.shp_depth), pv(ShapeParam.DEPTH, 0.4f), 5f, 95f, 40f, "profundidade")
                19 -> PercentRow(env, ShapeParam.DEPTH, stringResource(R.string.shp_depth), pv(ShapeParam.DEPTH, 0.6f), 5f, 95f, 60f, "profundidade")
                20 -> {
                    PercentRow(env, ShapeParam.THICKNESS, stringResource(R.string.panel_espessura), pv(ShapeParam.THICKNESS, 0.2f), 2f, 50f, 20f, "espessura")
                    SweepRow(env, pv(ShapeParam.SWEEP, 270f))
                }
                21 -> PercentRow(env, ShapeParam.TIP, stringResource(R.string.shp_tail), pv(ShapeParam.TIP, 0.3f), 0f, 100f, 30f, "ponta do balão")
                22 -> PercentRow(env, ShapeParam.TIP, stringResource(R.string.panel_inclinacao), pv(ShapeParam.TIP, 0.5f), 0f, 100f, 50f, "inclinacao")
                23 -> {
                    PercentRow(env, ShapeParam.THICKNESS, stringResource(R.string.panel_espessura), pv(ShapeParam.THICKNESS, 0.4f), 2f, 100f, 40f, "espessura")
                    PercentRow(env, ShapeParam.AMPLITUDE, stringResource(R.string.shp_amplitude), pv(ShapeParam.AMPLITUDE, 1f), 0f, 100f, 100f, "amplitude")
                }
                24 -> {
                    PercentRow(env, ShapeParam.DEPTH, stringResource(R.string.shp_variation), pv(ShapeParam.DEPTH, 0.5f), 5f, 95f, 50f, "variacao")
                    ShapeRow(env, ShapeParam.SEED, stringResource(R.string.shp_seed), pv(ShapeParam.SEED, 0f), 0.06f, 0f, 99f, "", 0, 0f, "variante") {
                        store.setShapeParam(ShapeParam.SEED, it.roundToInt().toFloat())
                    }
                }
            }
            Spacer(Modifier.height(6.dp))
            KitHint(
                stringResource(R.string.panel_arraste_alcas_palco_mudar_tamanho) + (if (type == 0) stringResource(R.string.edt_shape_corner_hint) + " " else ". ") +
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
    val prevLabel = stringResource(R.string.panel_forma_anterior)
    val nextLabel = stringResource(R.string.panel_proxima_forma)
    val keepRatioLabel = stringResource(R.string.panel_manter_proporcao)
    val freeRatioLabel = stringResource(R.string.panel_soltar_proporcao)
    Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(44.dp).semantics { contentDescription = prevLabel }.tocavel(shrink = 1f) { go(-1) },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.ChevronLeft, 18.dp, AureaColors.Text) }
        Box(
            Modifier.size(44.dp, 40.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip),
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(26.dp)) { drawShapeGlyph(type, AureaColors.Text) }
        }
        Box(
            Modifier.size(44.dp).semantics { contentDescription = nextLabel }.tocavel(shrink = 1f) { go(1) },
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
                .semantics { contentDescription = if (linked) freeRatioLabel else keepRatioLabel }
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
                .testTag("shape.size.ruler")
                .valueDrag(
                    enabled = true,
                    start = { store.shapeParams?.getOrNull(if (axis == 0) 5 else 6) ?: if (axis == 0) w else h },
                    unitsPerDp = { 1f },
                    min = 1f,
                    max = 16384f,
                    onStart = {
                        startW[0] = store.shapeParams?.getOrNull(5) ?: w
                        startW[1] = store.shapeParams?.getOrNull(6) ?: h
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
            val keypadLabel = stringResource(if (a == 0) R.string.panel_largura else R.string.panel_altura)
            ValueBox(
                numeroPtBr(v, 0),
                width = 58.dp,
                label = if (a == 0) stringResource(R.string.panel_x_largura) else stringResource(R.string.panel_y_altura),
                color = if (a == axis) AureaColors.Accent else Color.White,
                onTap = {
                    if (axis != a) {
                        axis = a
                    } else {
                        env.openKeypad(KeypadRequest(keypadLabel, v, "px", 1f, 16384f, 0) {
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

/** Linha de parâmetro em porcentagem (o motor guarda a fração 0..1). */
@Composable
private fun PercentRow(env: PanelEnv, param: Int, label: String, fraction: Float, min: Float, max: Float, default: Float, gesture: String) {
    ShapeRow(env, param, label, fraction * 100f, 0.3f, min, max, "%", 0, default, gesture) { env.store.setShapeParam(param, it / 100f) }
}

/** Abertura da fatia e do arco (graus; 270° = a fatia de 3/4 de sempre). */
@Composable
private fun SweepRow(env: PanelEnv, degrees: Float) {
    ShapeRow(env, ShapeParam.SWEEP, stringResource(R.string.shp_sweep), degrees, 0.6f, 1f, 360f, "°", 0, 270f, "abertura") {
        env.store.setShapeParam(ShapeParam.SWEEP, it)
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
    val anim = shapeAnimBits(sp) and (1 shl param) != 0
    val here = shapeKeyBits(sp) and (1 shl param) != 0
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
        12 -> drawPath(quad(c, r, -0.6f, -0.7f, 0.6f, -0.7f, 1f, 0.7f, -1f, 0.7f), color)
        13 -> drawPath(quad(c, r, -0.5f, -0.7f, 1f, -0.7f, 0.5f, 0.7f, -1f, 0.7f), color)
        14 -> drawPath(gearGlyph(c, r, 10, 0.3f), color)
        15 -> drawPath(doubleArrowGlyph(c, r), color)
        16 -> drawRoundRect(color, Offset(c.x - r, c.y - r * 0.14f), Size(2 * r, 0.28f * r), CornerRadius(r * 0.06f))
        17 -> drawPath(diamondGlyph(c, r), color)
        18 -> drawPath(heartGlyph(c, r), color)
        19 -> drawPath(sealGlyph(c, r, 12), color)
        20 -> drawArc(color, 0f, 270f, false, Offset(c.x - r * 0.82f, c.y - r * 0.82f), Size(1.64f * r, 1.64f * r), style = Stroke(r * 0.36f))
        21 -> drawPath(bubbleGlyph(c, r), color)
        22 -> drawPath(boltGlyph(c, r), color)
        23 -> drawPath(waveGlyph(c, r, 2), color, style = Stroke(r * 0.3f))
        24 -> drawPath(blobGlyph(c, r), color)
        else -> drawRect(color, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
    }
}

// --- Silhuetas das formas paramétricas (mesmas proporções do motor; também
// na grade de Adicionar › Forma) -------------------------------------------------

internal fun diamondGlyph(c: Offset, r: Float): Path = quad(c, r, 0f, -1f, 0.8f, 0f, 0f, 1f, -0.8f, 0f)

/** Raio: os 7 vértices do motor numa caixa 0,6 : 1. */
internal fun boltGlyph(c: Offset, r: Float): Path =
    quad(c, r, -0.03f, -1f, 0.36f, -1f, 0.072f, -0.12f, 0.42f, -0.12f, -0.252f, 1f, -0.012f, 0.14f, -0.372f, 0.14f)

internal fun heartGlyph(c: Offset, r: Float): Path = Path().apply {
    moveTo(c.x, c.y + 0.9f * r)
    cubicTo(c.x - 0.35f * r, c.y + 0.55f * r, c.x - r, c.y + 0.15f * r, c.x - r, c.y - 0.35f * r)
    cubicTo(c.x - r, c.y - 0.85f * r, c.x - 0.2f * r, c.y - 0.95f * r, c.x, c.y - 0.5f * r)
    cubicTo(c.x + 0.2f * r, c.y - 0.95f * r, c.x + r, c.y - 0.85f * r, c.x + r, c.y - 0.35f * r)
    cubicTo(c.x + r, c.y + 0.15f * r, c.x + 0.35f * r, c.y + 0.55f * r, c.x, c.y + 0.9f * r)
    close()
}

/** Selo: círculo com `bumps` saliências em arco, uma em cima. */
internal fun sealGlyph(c: Offset, r: Float, bumps: Int): Path = Path().apply {
    val steps = bumps * 12
    for (i in 0..steps) {
        val t = i.toFloat() / steps
        val a = -PI.toFloat() / 2f + t * 2f * PI.toFloat()
        val rr = r * (0.84f + 0.16f * abs(cos(bumps * t * PI.toFloat())))
        val x = c.x + rr * cos(a)
        val y = c.y + rr * sin(a)
        if (i == 0) moveTo(x, y) else lineTo(x, y)
    }
    close()
}

/** Onda (linha do meio, para desenhar com traço): começa descendo, como no motor. */
internal fun waveGlyph(c: Offset, r: Float, waves: Int): Path = Path().apply {
    val steps = 48
    for (i in 0..steps) {
        val t = i.toFloat() / steps
        val x = c.x - r + 2f * r * t
        val y = c.y + 0.4f * r * sin(t * waves * 2f * PI.toFloat())
        if (i == 0) moveTo(x, y) else lineTo(x, y)
    }
}

/** Mancha: raio polar com três harmônicos (4, 5 e 2), como o blob do motor. */
internal fun blobGlyph(c: Offset, r: Float): Path = Path().apply {
    val steps = 96
    for (i in 0..steps) {
        val a = i.toFloat() / steps * 2f * PI.toFloat()
        val rr = r * (1f + 0.18f * (0.55f * cos(4f * a + 0.9f) + 0.3f * cos(5f * a + 2.4f) + 0.15f * cos(2f * a + 4.1f))) / 1.18f
        val x = c.x + rr * cos(a)
        val y = c.y + rr * sin(a)
        if (i == 0) moveTo(x, y) else lineTo(x, y)
    }
    close()
}

/** Balão de fala: corpo arredondado + rabicho à esquerda (unidos num caminho só). */
internal fun bubbleGlyph(c: Offset, r: Float): Path {
    val body = Path().apply { addRoundRect(RoundRect(c.x - r, c.y - 0.8f * r, c.x + r, c.y + 0.42f * r, CornerRadius(0.35f * r))) }
    val tail = quad(c, r, -0.5f, 0.3f, -0.6f, 0.88f, -0.05f, 0.3f)
    return Path().apply { op(body, tail, androidx.compose.ui.graphics.PathOperation.Union) }
}

/** Quadrilátero em frações de `r` em volta de `c` (trapézio, paralelogramo). */
internal fun quad(c: Offset, r: Float, vararg xy: Float): Path = Path().apply {
    moveTo(c.x + xy[0] * r, c.y + xy[1] * r)
    var i = 2
    while (i + 1 < xy.size) { lineTo(c.x + xy[i] * r, c.y + xy[i + 1] * r); i += 2 }
    close()
}

/** Engrenagem (mesma proporção do motor: raiz 0,78; cubo = furo). */
internal fun gearGlyph(c: Offset, r: Float, teeth: Int, hub: Float): Path {
    val root = r * 0.78f
    val p = Path()
    val steps = teeth * 4
    for (i in 0 until steps) {
        val a0 = -PI.toFloat() / 2f + i * 2f * PI.toFloat() / steps
        val rr = if (i % 4 == 1 || i % 4 == 2) r else root
        val a1 = a0 + 2f * PI.toFloat() / steps
        if (i == 0) p.moveTo(c.x + rr * cos(a0), c.y + rr * sin(a0)) else p.lineTo(c.x + rr * cos(a0), c.y + rr * sin(a0))
        p.lineTo(c.x + rr * cos(a1), c.y + rr * sin(a1))
    }
    p.close()
    val hole = Path().apply { addOval(androidx.compose.ui.geometry.Rect(c, root * hub)) }
    return Path().apply { op(p, hole, androidx.compose.ui.graphics.PathOperation.Difference) }
}

internal fun doubleArrowGlyph(c: Offset, r: Float): Path =
    quad(c, r, -1f, 0f, -0.45f, -0.8f, -0.45f, -0.22f, 0.45f, -0.22f, 0.45f, -0.8f, 1f, 0f, 0.45f, 0.8f, 0.45f, 0.22f, -0.45f, 0.22f, -0.45f, 0.8f)

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
