package com.aurea.aurea.editor.panels

import android.graphics.Bitmap
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.LayerGeometry
import com.aurea.aurea.editor.StageMapper
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
// Omega aparência (área 05): o que o seletor de cor e o Cor e preenchimento
// pedem ao PALCO (chamado do PreviewStage, por cima dos gestos dele):
//
//  P1-1 · CONTA-GOTAS AO VIVO (`store.eyedropper`): arrastar em qualquer lugar
//         move uma MIRA RELATIVA (do centro da composição ou da última posição),
//         com a lupa sobre o dedo; a cor vem do quadro da prévia capturado no
//         começo do arrasto. `onSample` a cada passo, `onCommit` ao soltar.
//  P1-4 · PONTAS DO DEGRADÊ (painel no Degradê): duas alças nas pontas
//         (relativas à caixa da camada → composição pelos cantos do motor); o
//         toque pega a mais perto, passa da folga e arrasta — um passo de desfazer.
// =============================================================================

@Composable
internal fun ColorStageOverlay(store: EditorStore, mapper: StageMapper) {
    if (store.sceneEditor) return
    val request = store.eyedropper
    if (request != null) {
        EyedropperStage(store, mapper, request)
        return
    }
    if (ColorFillStage.gradientHandles && store.shapeFill != null && store.selection.size == 1) {
        GradientHandlesStage(store, mapper)
    }
}

/** A última posição da mira do conta-gotas (px da composição; NaN = o centro). */
internal object EyedropperAim {
    var x = Float.NaN
    var y = Float.NaN

    fun point(m: StageMapper): Offset =
        if (x.isFinite() && y.isFinite()) Offset(x.coerceIn(0f, m.compW), y.coerceIn(0f, m.compH))
        else Offset(m.compW / 2f, m.compH / 2f)
}

@Composable
private fun EyedropperStage(store: EditorStore, m: StageMapper, request: EditorStore.EyedropperRequest) {
    val scope = rememberCoroutineScope()
    var frame by remember { mutableStateOf<Bitmap?>(null) }
    val image = remember(frame) { frame?.asImageBitmap() }
    var aim by remember { mutableStateOf<Offset?>(null) }
    var finger by remember { mutableStateOf<Offset?>(null) }
    var picked by remember { mutableStateOf(Color.Transparent) }
    val frameNow by rememberUpdatedState(frame)

    // O quadro da prévia (captura síncrona na GPU: fora da linha principal).
    fun capture() {
        scope.launch {
            val bmp = withContext(Dispatchers.Default) { runCatching { store.captureBitmap(720, preview = true) }.getOrNull() }
            if (bmp != null) frame = bmp
        }
    }
    LaunchedEffect(request) { capture() }

    val hint = stringResource(R.string.om_ap_cp_eyedropper_hint)
    Box(
        Modifier
            .fillMaxSize()
            .testTag("stage.eyedropper")
            .semantics { contentDescription = hint }
            .pointerInput(request) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    down.consume()
                    if (!m.valid || m.fit <= 0f) return@awaitEachGesture
                    if (store.playing) store.pause()
                    capture()
                    fun sampleAt(p: Offset): FloatArray? {
                        val f = frameNow ?: return null
                        if (f.width <= 0 || f.height <= 0 || m.compW <= 0f || m.compH <= 0f) return null
                        val px = (p.x / m.compW * f.width).toInt().coerceIn(0, f.width - 1)
                        val py = (p.y / m.compH * f.height).toInt().coerceIn(0, f.height - 1)
                        val argb = runCatching { f.getPixel(px, py) }.getOrNull() ?: return null
                        val c = Color(argb)
                        picked = c.copy(alpha = 1f)
                        return floatArrayOf(c.red, c.green, c.blue, 1f)
                    }
                    var cur = EyedropperAim.point(m)
                    var last = down.position
                    var sample: FloatArray? = null
                    aim = cur
                    finger = down.position
                    sampleAt(cur)?.let { s ->
                        sample = s
                        request.onSample(s)
                    }
                    try {
                        while (true) {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            ev.changes.forEach { it.consume() }
                            if (!c.pressed) break
                            val dx = c.position.x - last.x
                            val dy = c.position.y - last.y
                            last = c.position
                            finger = c.position
                            if (dx == 0f && dy == 0f) continue
                            cur = Offset((cur.x + dx / m.fit).coerceIn(0f, m.compW), (cur.y + dy / m.fit).coerceIn(0f, m.compH))
                            aim = cur
                            sampleAt(cur)?.let { s ->
                                sample = s
                                request.onSample(s)
                            }
                        }
                    } finally {
                        EyedropperAim.x = cur.x
                        EyedropperAim.y = cur.y
                        finger = null
                        sample?.let { request.onCommit(it) }
                    }
                }
            }
            .drawBehind {
                if (!m.valid) return@drawBehind
                val p = aim ?: EyedropperAim.point(m)
                val s = Offset(m.sx(p.x), m.sy(p.y))
                // A mira: anel e cruz (sombra escura por baixo).
                val ring = 10.dp.toPx()
                val arm = 18.dp.toPx()
                val under = Color.Black.copy(alpha = 0.5f)
                drawCircle(under, ring, s, style = Stroke(4.dp.toPx()))
                drawLine(under, s - Offset(arm, 0f), s + Offset(arm, 0f), 3.5.dp.toPx())
                drawLine(under, s - Offset(0f, arm), s + Offset(0f, arm), 3.5.dp.toPx())
                drawCircle(Color.White, ring, s, style = Stroke(2.dp.toPx()))
                drawLine(Color.White, s - Offset(arm, 0f), s + Offset(arm, 0f), 1.5.dp.toPx())
                drawLine(Color.White, s - Offset(0f, arm), s + Offset(0f, arm), 1.5.dp.toPx())
                // A lupa sobre o dedo: 11 × 11 pixels ampliados em volta da mira.
                val img = image ?: return@drawBehind
                val f = finger ?: return@drawBehind
                val r = 36.dp.toPx()
                val c = Offset(f.x.coerceIn(r, max(r, size.width - r)), (f.y - r - 44.dp.toPx()).coerceAtLeast(r))
                val px = (p.x / m.compW * img.width).toInt().coerceIn(0, img.width - 1)
                val py = (p.y / m.compH * img.height).toInt().coerceIn(0, img.height - 1)
                val n = 11
                val sx = (px - n / 2).coerceIn(0, max(0, img.width - n))
                val sy = (py - n / 2).coerceIn(0, max(0, img.height - n))
                val lens = Path().apply { addOval(Rect(c, r)) }
                clipPath(lens) {
                    drawRect(Color.Black, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
                    drawImage(
                        img,
                        srcOffset = IntOffset(sx, sy),
                        srcSize = IntSize(min(n, img.width), min(n, img.height)),
                        dstOffset = IntOffset((c.x - r).roundToInt(), (c.y - r).roundToInt()),
                        dstSize = IntSize((2 * r).roundToInt(), (2 * r).roundToInt()),
                        filterQuality = FilterQuality.None,
                    )
                    val cell = 2 * r / n
                    drawRect(Color.White, Offset(c.x - cell / 2, c.y - cell / 2), Size(cell, cell), style = Stroke(1.5.dp.toPx()))
                }
                drawCircle(picked, r, c, style = Stroke(5.dp.toPx()))
                drawCircle(Color.White, r + 2.5.dp.toPx(), c, style = Stroke(1.5.dp.toPx()))
            },
    ) {
        Text(
            hint,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
            modifier = Modifier
                .align(Alignment.TopCenter)
                .testTag("stage.eyedropper.hint")
                .padding(top = 8.dp, start = 8.dp, end = 8.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(AureaColors.EditorPanelHigh)
                .padding(horizontal = 12.dp, vertical = 6.dp),
        )
    }
}

/** Ponto relativo à caixa (0..1; TL, TR, BR, BL em [b]) → composição (bilinear). */
private fun relToComp(b: FloatArray, u: Float, v: Float): Offset {
    val tx = b[0] + (b[2] - b[0]) * u
    val ty = b[1] + (b[3] - b[1]) * u
    val bx = b[6] + (b[4] - b[6]) * u
    val by = b[7] + (b[5] - b[7]) * u
    return Offset(tx + (bx - tx) * v, ty + (by - ty) * v)
}

/** Composição → relativo à caixa (pelos eixos TL→TR e TL→BL; −2..3 como o motor). */
private fun compToRel(b: FloatArray, p: Offset): Offset? {
    val ex = b[2] - b[0]
    val ey = b[3] - b[1]
    val fx = b[6] - b[0]
    val fy = b[7] - b[1]
    val det = ex * fy - fx * ey
    if (abs(det) < 1e-6f) return null
    val dx = p.x - b[0]
    val dy = p.y - b[1]
    val u = (dx * fy - fx * dy) / det
    val v = (ex * dy - ey * dx) / det
    return Offset(u.coerceIn(-2f, 3f), v.coerceIn(-2f, 3f))
}

/** A ponta [which] (3 início, 4 fim) na tela, ou nula. */
private fun handleOnScreen(store: EditorStore, m: StageMapper, which: Int, box: FloatArray): Offset? {
    val d = store.detail ?: return null
    val f = store.shapeFill ?: return null
    if (!m.valid || f.size < 19 || !LayerGeometry.corners(d, box)) return null
    val i = if (which == 3) 15 else 17
    val c = relToComp(box, f[i], f[i + 1])
    return Offset(m.sx(c.x), m.sy(c.y))
}

@Composable
private fun GradientHandlesStage(store: EditorStore, m: StageMapper) {
    val box = remember { FloatArray(8) }
    val startLabel = stringResource(R.string.om_ap_fill_grad_start)
    val endLabel = stringResource(R.string.om_ap_fill_grad_end)
    Box(
        Modifier
            .fillMaxSize()
            .pointerInput(store) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val d = store.detail ?: return@awaitEachGesture
                    if (d.locked) return@awaitEachGesture
                    val corners = FloatArray(8)
                    val s = handleOnScreen(store, m, 3, corners) ?: return@awaitEachGesture
                    val e = handleOnScreen(store, m, 4, corners) ?: return@awaitEachGesture
                    down.consume()
                    // A ponta mais perto do toque; o dedo carrega a ponta (sem salto).
                    val which = if ((down.position - s).getDistance() <= (down.position - e).getDistance()) 3 else 4
                    val grab = (if (which == 3) s else e) - down.position
                    val slop = 8.dp.toPx()
                    var dragging = false
                    try {
                        while (true) {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!c.pressed) break
                            if (!dragging && (c.position - down.position).getDistance() > slop) {
                                dragging = true
                                if (store.playing) store.pause()
                                store.beginGesture("pontas do degradê")
                                ColorFillStage.dragging = which
                            }
                            if (!dragging) continue
                            c.consume()
                            val t = c.position + grab
                            val rel = compToRel(corners, Offset(m.cx(t.x), m.cy(t.y))) ?: continue
                            store.setShapeFillValue(which, floatArrayOf(rel.x, rel.y))
                        }
                    } finally {
                        if (dragging) {
                            store.endGesture()
                            ColorFillStage.dragging = 0
                        }
                    }
                }
            }
            .drawBehind {
                val f = store.shapeFill ?: return@drawBehind
                if (f.size < 19) return@drawBehind
                val s = handleOnScreen(store, m, 3, box) ?: return@drawBehind
                val e = handleOnScreen(store, m, 4, box) ?: return@drawBehind
                val under = Color.Black.copy(alpha = 0.45f)
                drawLine(under, s, e, 3.5.dp.toPx())
                drawLine(Color.White, s, e, 1.5.dp.toPx())
                val active = ColorFillStage.dragging
                for (which in 3..4) {
                    val p = if (which == 3) s else e
                    val k = if (which == 3) 7 else 11
                    val r = (if (active == which) 12.dp else 10.dp).toPx()
                    drawCircle(under, r + 2.dp.toPx(), p)
                    drawCircle(if (active == which) AureaColors.Accent else Color.White, r, p)
                    drawCircle(Color(f[k].coerceIn(0f, 1f), f[k + 1].coerceIn(0f, 1f), f[k + 2].coerceIn(0f, 1f)), r - 3.dp.toPx(), p)
                }
                // A ponta inicial ganha um ponto no meio (início × fim sem depender da cor).
                drawCircle(Color.White, 2.dp.toPx(), s)
            },
    ) {
        // Alvos de acessibilidade/testes nas pontas (o arrasto é do gesto acima).
        for (which in 3..4) {
            Box(
                Modifier
                    .offset {
                        val p = handleOnScreen(store, m, which, FloatArray(8)) ?: Offset(-1000f, -1000f)
                        val half = 22.dp.toPx()
                        IntOffset((p.x - half).roundToInt(), (p.y - half).roundToInt())
                    }
                    .size(44.dp)
                    .testTag(if (which == 3) "stage.gradient.start" else "stage.gradient.end")
                    .semantics { contentDescription = if (which == 3) startLabel else endLabel },
            )
        }
    }
}
