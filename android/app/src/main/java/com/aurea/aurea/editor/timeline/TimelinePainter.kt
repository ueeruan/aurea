package com.aurea.aurea.editor.timeline

import android.graphics.Paint
import android.graphics.RectF
import android.util.LongSparseArray
import android.util.SparseArray
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.ShellColors
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaTimeline
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIconsFont
import com.aurea.aurea.ui.theme.LayerType
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Pinta a timeline da A.01 num único Canvas. Tudo que depende do tempo (vista,
 * zoom, playhead) é lido AQUI, na fase de desenho: o relógio anda e só isto
 * repinta, nada recompõe (spec §9.1).
 *
 * Regra de desempenho: nenhum objeto por quadro. Paths, pincéis, traços,
 * layouts de texto (nome, glifos, dígitos do relógio) e a tira de miniaturas
 * ficam em cache; o que muda por quadro são só números.
 */
/** Baldes de waveform por linha e quadro (tela de 4K a 1,5 dp com folga). */
private const val WAVE_MAX = 2048

internal class TimelinePainter(
    private val m: TimelineMetrics,
    private val measurer: TextMeasurer,
) {
    private val thumbs = ThumbStrip()

    // --- Objetos reaproveitados ---------------------------------------------------
    private val barClip = android.graphics.Path()
    private val rectF = RectF()
    private val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    // Waveform: um buffer direto e um array de linhas reusados (nada de alocar por quadro).
    private val waveBuf: java.nio.ByteBuffer = java.nio.ByteBuffer.allocateDirect(WAVE_MAX)
    private val waveLines = FloatArray(WAVE_MAX * 4)
    private val wavePaint = Paint().apply { isAntiAlias = false; strokeCap = Paint.Cap.BUTT }
    private var waveStore: com.aurea.aurea.state.EditorStore? = null
    private val majorPath = Path()
    private val minorPath = Path()
    private val majorStroke = Stroke(m.tickMajorWidth)
    private val minorStroke = Stroke(m.tickMinorWidth)
    private val selStroke = Stroke(m.selStroke)
    private val multiStroke = Stroke(m.multiStroke)
    private val diamondStroke = Stroke(m.diamondStroke)
    private val tc = IntArray(4)
    private var steps: RulerSteps? = null
    private var stepsPps = Float.NaN
    private var stepsFps = Float.NaN

    /** Véu sobre a tira (A.01: preto .62 → .22 até 45 % da barra), em espaço 0..1 e escalado. */
    private val thumbShade = Brush.horizontalGradient(
        0f to Color.Black.copy(alpha = 0.62f),
        0.45f to Color.Black.copy(alpha = 0.22f),
        startX = 0f,
        endX = 1f,
    )

    /** Coluna das pílulas: as barras passam POR BAIXO (A.01: bg → bg .95 a 78 % → transparente). */
    private val headerShade = Brush.horizontalGradient(
        0f to AureaColors.Stage,
        0.78f to AureaColors.Stage.copy(alpha = 0.95f),
        1f to AureaColors.Stage.copy(alpha = 0f),
        startX = 0f,
        endX = m.headerColumn,
    )

    /**
     * Corpo da barra: `lerp(#151C24, cor do tipo, .46 normal / .66 escolhida /
     * .22 oculta)` em sRGB linear por canal, como o `Color.lerp` do Flutter
     * (confere com o print: imagem escolhida = #2F539B).
     */
    private val bodyColors: Array<Color> = Array(LayerType.entries.size * 3) { i ->
        val type = LayerType.entries[i / 3]
        val t = when (i % 3) {
            STATE_HIDDEN -> 0.22f
            STATE_SELECTED -> 0.66f
            else -> 0.46f
        }
        lerpSrgb(AureaColors.Surface, type.color, t)
    }

    // --- Texto ------------------------------------------------------------------------
    private val nameStyle = TextStyle(
        color = Color.White,
        fontSize = 12.sp,
        fontWeight = FontWeight.W600,
        letterSpacing = (-0.1).sp,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private val digitStyle = TextStyle(
        color = Color.White,
        fontSize = 13.sp,
        fontWeight = FontWeight.W700,
        letterSpacing = 0.5.sp,
        fontFeatureSettings = "tnum",
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private val balloonStyle = TextStyle(
        color = Color.White,
        fontSize = 10.sp,
        fontWeight = FontWeight.W700,
        fontFeatureSettings = "tnum",
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private val labelStyle = TextStyle(
        color = AureaTimeline.TickMajor,
        fontSize = 9.sp,
        fontWeight = FontWeight.W500,
        fontFeatureSettings = "tnum",
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )

    private class NameLayout(val name: String, val bucket: Int, val layout: TextLayoutResult)

    private val names = LongSparseArray<NameLayout>()
    private val glyphs = SparseArray<TextLayoutResult>()
    private val labels = SparseArray<TextLayoutResult>()
    private val digits = arrayOfNulls<TextLayoutResult>(11)
    private var digitWidth = 0f
    private var colonWidth = 0f
    private var digitBaseline = 0f
    private var balloonFrame = Snap.NONE
    private var balloonLayout: TextLayoutResult? = null

    // =================================================================================
    fun draw(scope: DrawScope, c: TimelineController) = with(scope) {
        val w = size.width
        val h = size.height
        val store = c.store
        val st = c.state
        val fps = TimeAxis.safeFps(store.project.fps)
        val ppf = TimeAxis.pxPerFrame(st.pps, m.density, fps)
        val view = c.view()
        val cx = w / 2f
        val compact = st.compact
        waveStore = store
        val rows = c.rows.value
        val n = c.rowCount(rows)

        if (n > 0) {
            clipRect(top = m.rowsTop) {
                drawRows(c, rows, n, w, h, view, ppf, cx, fps, compact)
            }
        }
        drawRuler(w, view, ppf, cx, fps, st.pps)
        drawMarkers(store.markers, w, view, ppf, cx)
        drawTimecode(cx, store.playhead, fps)
        val color = if (compact) AureaColors.Danger else AureaColors.Playhead
        drawRect(color, Offset(cx - m.playhead / 2f, 0f), Size(m.playhead, h))
        // Cabeçote vermelho do compacto tem o "botão" 8×8 no alto (A.01, painel aberto).
        if (compact) drawRoundRect(color, Offset(cx - m.knob / 2f, 0f), Size(m.knob, m.knob), CornerRadius(m.knobRadius))
    }

    // --- Linhas ---------------------------------------------------------------------------
    private fun DrawScope.drawRows(
        c: TimelineController, rows: List<RowModel>, n: Int, w: Float, h: Float,
        view: Double, ppf: Float, cx: Float, fps: Float, compact: Boolean,
    ) {
        val st = c.state
        val scroll = c.clampedScroll(n)
        val first = max(0, floor(scroll / m.row).toInt())
        val last = min(n - 1, floor((scroll + h - m.rowsTop) / m.row).toInt())
        val selCount = c.selectionSize()
        val multi = selCount >= 2
        val selKey = c.store.selectedKeyframe
        val generation = c.store.thumbnailGeneration
        val cache = c.store.thumbnails

        for (i in first..last) {
            val r = c.rowAt(rows, i) ?: continue
            val top = m.rowsTop + i * m.row - scroll
            val selected = c.isSelected(r.id)
            val handles = !compact && selCount == 1 && selected && !r.locked
            val selFrame = if (selKey != null && selKey.first == r.id) Keyframes.toTimeline(selKey.second.time, r.start, r.offset) else Snap.NONE
            val dragFrame = if (st.dragKeyLayer == r.id) st.dragKeyFrame else Snap.NONE
            drawRow(r, top, w, view, ppf, cx, fps, compact, selected, multi, handles, selFrame, dragFrame, cache, generation)
        }

        // Reordenar: véu na linha segurada + traço de destino (a linha não sai do lugar).
        val src = st.reorderSource
        if (src >= 0) {
            val srcTop = m.rowsTop + src * m.row - scroll
            drawRect(AureaColors.Accent.copy(alpha = 0.14f), Offset(0f, srcTop), Size(w, m.row))
            val y = Reorder.dropLineY(src, st.reorderTarget, m.rowsTop, scroll, m.row)
            if (!y.isNaN()) {
                drawRect(AureaColors.Accent, Offset(0f, y - m.reorderLine / 2f), Size(w, m.reorderLine))
                drawCircle(AureaColors.Accent, m.reorderDot, Offset(m.pillLeft, y))
            }
        }

        // Coluna das pílulas por cima das barras.
        drawRect(headerShade, Offset(0f, m.rowsTop), Size(m.headerColumn, h - m.rowsTop))
        for (i in first..last) {
            val r = c.rowAt(rows, i) ?: continue
            val cy = m.rowsTop + i * m.row - scroll + m.row / 2f
            drawHeaderPill(r, cy, multi && c.isSelected(r.id))
        }

        // Fio do ímã.
        val g = st.guideFrame
        if (g != Snap.NONE) {
            val gx = TimeAxis.xOf(g.toDouble(), view, ppf, cx)
            if (gx >= m.headerColumn && gx <= w) {
                drawRect(AureaColors.Accent, Offset(gx - m.guide / 2f, m.rowsTop), Size(m.guide, h - m.rowsTop))
            }
        }
    }

    private fun DrawScope.drawRow(
        r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        compact: Boolean, selected: Boolean, multi: Boolean, handles: Boolean,
        selFrame: Int, dragFrame: Int, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
    ) {
        val x0 = TimeAxis.xOf(r.start.toDouble(), view, ppf, cx)
        val x1 = max(TimeAxis.xOf(r.end.toDouble(), view, ppf, cx), x0 + m.barMinWidth)
        val barW = x1 - x0
        if (x1 >= -m.barRadius && x0 <= w + m.barRadius) {
            // Barra cortada perto da tela: um clipe de minutos não vira um retângulo de 100 mil px.
            val left = max(x0, -m.barRadius * 2f)
            val right = min(x1, w + m.barRadius * 2f)
            val bottom = top + m.bar
            val state = if (!r.visible) STATE_HIDDEN else if (selected) STATE_SELECTED else STATE_NORMAL
            val canvas = drawContext.canvas.nativeCanvas
            rectF.set(left, top, right, bottom)
            barClip.reset()
            barClip.addRoundRect(rectF, m.barRadius, m.barRadius, android.graphics.Path.Direction.CW)
            canvas.save()
            canvas.clipPath(barClip)
            drawRect(bodyColors[r.type.ordinal * 3 + state], Offset(left, top), Size(right - left, m.bar))
            if (r.hasThumbs && drawThumbs(canvas, r, top, x0, x1, w, view, ppf, cx, fps, cache, generation)) {
                withTransform({
                    translate(x0, top)
                    scale(barW, 1f, pivot = Offset.Zero)
                }) { drawRect(thumbShade, size = Size(1f, m.bar)) }
            }
            // Trilho dos losangos, a faixa da cor do tipo e o fio de luz no alto.
            drawRect(TRACK_SHADE, Offset(left, top + m.trackTop), Size(right - left, m.track))
            if (r.type == LayerType.Audio || r.type == LayerType.Video) drawWaveform(canvas, r, top, x0, x1, w, view, ppf, cx)
            // A faixa da ponta: a cor da etiqueta, quando a camada tem uma; senão a do tipo.
            val stripe = ShellColors.LabelPalette.getOrNull(r.label - 1) ?: r.type.color
            drawRect(if (r.visible) stripe else stripe.copy(alpha = 0.5f), Offset(x0, top), Size(m.stripe, m.bar))
            drawLine(
                Color.White.copy(alpha = if (selected) 0.24f else 0.10f),
                Offset(max(x0 + m.stripe, left), top + m.lightLine / 2f),
                Offset(right, top + m.lightLine / 2f),
                strokeWidth = m.lightLine,
            )
            canvas.restore()

            clipRect(left, top, right, bottom) { drawBarContent(r, top, x0, x1, w, compact) }

            if (selected) {
                val s = if (multi) m.multiStroke else m.selStroke
                drawRoundRect(
                    Color.White,
                    Offset(left + s / 2f, top + s / 2f),
                    Size(right - left - s, m.bar - s),
                    CornerRadius(m.barRadius - s / 2f),
                    style = if (multi) multiStroke else selStroke,
                )
            }
            if (handles) {
                if (RowHit.startHandleVisible(m, x0)) drawTrimHandle(x0 - m.trimInsetStart, top)
                if (RowHit.endHandleVisible(x1, w)) drawTrimHandle(x1 - m.trimInsetEnd, top)
            }
        }
        drawDiamonds(r, top, w, view, ppf, cx, compact, selFrame, dragFrame, fps)
    }

    /** Conteúdo da barra (A.01): [‹] · ícone do tipo · cadeado · nome · ◇ · [›] ou ≡. */
    private fun DrawScope.drawBarContent(r: RowModel, top: Float, x0: Float, x1: Float, w: Float, compact: Boolean) {
        val barW = x1 - x0
        val cl = RowHit.contentLeft(m, x0, x1)
        val cr = RowHit.contentRight(m, x0, x1, w)
        if (cr <= cl) return
        val cy = top + m.trackTop / 2f
        val d = m.density
        var x = cl
        if (compact) {
            drawGlyph(CupertinoGlyph.ChevronLeft, m.arrowGlyph, ARROW_TINT, x + m.arrowSlot / 2f, cy)
            x += m.arrowSlot
        }
        if (barW > m.iconMinBar) {
            val a = if (r.visible) 0.85f else 0.45f
            drawGlyph(r.type.glyph, m.typeIcon, Color.White.copy(alpha = a), x + m.typeIcon * d / 2f, cy)
            x += m.typeIcon * d + m.iconGap
        }
        if (r.locked) {
            drawGlyph(CupertinoGlyph.LockFill, m.lockIcon, Color.White, x + m.lockIcon * d / 2f, cy)
            x += m.lockIcon * d + if (barW > m.lockGapMinBar) m.lockGap else 0f
        }
        // O ≡ mora na ponta REAL da barra (A.01); as setas do compacto grudam na parte visível (print t2).
        val menuRight = x1 - (if (barW < m.narrowBar) m.padRNarrow else m.padR)
        val right = when {
            compact -> cr - m.arrowSlot
            barW > m.menuMinBar -> min(cr, menuRight - m.menuGlyph * d)
            else -> cr
        }
        val rhombusW = if (r.animated && barW > m.rhombusMinBar) m.rhombusGap + m.rhombusIcon * d else 0f
        if (barW > m.nameMinBar && r.name.isNotEmpty()) {
            val avail = right - rhombusW - x
            if (avail > NAME_MIN_DP * d) {
                val layout = nameLayout(r, avail)
                drawText(layout, topLeft = Offset(x, cy - layout.size.height / 2f))
                x += layout.size.width
            }
        }
        if (rhombusW > 0f && x + rhombusW <= right + 1f) {
            drawGlyph(CupertinoGlyph.Rhombus, m.rhombusIcon, Color.White, x + m.rhombusGap + m.rhombusIcon * d / 2f, cy)
        }
        if (compact) {
            drawGlyph(CupertinoGlyph.ChevronRight, m.arrowGlyph, ARROW_TINT, cr - m.arrowSlot / 2f, cy)
        } else if (barW > m.menuMinBar && menuRight <= w + m.menuGlyph * d) {
            drawGlyph(CupertinoGlyph.LineHorizontal3, m.menuGlyph, ARROW_TINT, menuRight - m.menuGlyph * d / 2f, cy)
        }
    }

    /** Alça de trim da A.01: 16 × 32 branca DENTRO da ponta, risco central escuro. */
    private fun DrawScope.drawTrimHandle(left: Float, top: Float) {
        drawRoundRect(
            AureaTimeline.TrimHandle,
            Offset(left, top + m.trimTop),
            Size(m.trimWidth, m.bar - m.trimTop * 2f),
            CornerRadius(m.trimRadius),
        )
        drawRect(
            GRIP,
            Offset(left + (m.trimWidth - m.gripWidth) / 2f, top + (m.bar - m.gripHeight) / 2f),
            Size(m.gripWidth, m.gripHeight),
        )
    }

    /**
     * Tira de miniaturas: ladrilhos com índice ABSOLUTO a partir do instante 0 da
     * mídia (a tira não "anda" quando se apara o início), um pedido por balde.
     * Devolve se desenhou algum (o véu só entra por cima de imagem).
     */
    private fun drawThumbs(
        canvas: android.graphics.Canvas, r: RowModel, top: Float, x0: Float, x1: Float, w: Float,
        view: Double, ppf: Float, cx: Float, fps: Float,
        cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
    ): Boolean {
        val heightPx = m.bar.roundToInt().coerceIn(1, THUMB_MAX_PX)
        val tile = heightPx * thumbs.aspect(r.id)
        val origin = TimeAxis.xOf((r.start - r.offset).toDouble(), view, ppf, cx)
        val visL = max(x0, 0f)
        val visR = min(x1, w)
        if (visR <= visL) return false
        val image = r.type == LayerType.Image
        var i = max(0, floor((visL - origin) / tile).toInt())
        val iEnd = floor((visR - origin) / tile).toInt()
        var drew = false
        while (i <= iEnd) {
            val bucket = if (image) -1 else Thumbs.bucketOf(i * tile / ppf.toDouble(), fps)
            val frame = if (image) r.start else Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), r.start, r.offset)
            val bmp = thumbs.get(cache, r.id, bucket, frame, heightPx, generation)
            if (bmp != null) {
                val l = origin + i * tile
                rectF.set(l, top, l + tile, top + m.bar)
                canvas.drawBitmap(bmp, null, rectF, bitmapPaint)
                drew = true
            }
            i++
        }
        return drew
    }

    /**
     * Waveform do som da camada, só na parte visível: um balde a cada ~1,5 dp,
     * pedido ao motor (picos já calculados, o nível certo para o zoom — pinça
     * não recalcula nada). Fica na faixa de baixo da barra, sob o nome; no
     * vídeo, por cima das miniaturas.
     */
    private fun drawWaveform(
        canvas: android.graphics.Canvas, r: RowModel, top: Float, x0: Float, x1: Float, w: Float,
        view: Double, ppf: Float, cx: Float,
    ) {
        val store = waveStore ?: return
        val visL = max(x0 + m.stripe, 0f)
        val visR = min(x1, w)
        if (visR <= visL || ppf <= 0f) return
        val step = max(1f, m.density * 1.5f)
        val count = min(WAVE_MAX, ceil((visR - visL) / step).toInt())
        if (count <= 0) return
        val startFrame = TimeAxis.frameAt(visL, view, ppf, cx)
        val n = store.queryWaveform(r.id, startFrame, (step / ppf).toDouble(), count, waveBuf)
        if (n <= 0) return
        val audio = r.type == LayerType.Audio
        // Faixa de baixo da barra (abaixo do nome): no áudio, mais alta.
        val areaTop = top + m.trackTop * (if (audio) 0.62f else 0.8f)
        val areaBottom = top + m.bar - m.lightLine
        val mid = (areaTop + areaBottom) / 2f
        val half = (areaBottom - areaTop) / 2f
        var k = 0
        for (i in 0 until n) {
            val v = (waveBuf.get(i).toInt() and 0xFF) / 255f * half
            if (v < 0.5f) continue
            val x = visL + i * step + step / 2f
            waveLines[k++] = x
            waveLines[k++] = mid - v
            waveLines[k++] = x
            waveLines[k++] = mid + v
        }
        if (k == 0) return
        wavePaint.color = android.graphics.Color.argb(if (audio) 170 else 150, 255, 255, 255)
        wavePaint.strokeWidth = max(1f, step * 0.72f)
        canvas.drawLines(waveLines, 0, k, wavePaint)
    }

    /** Losangos da A.01: quadrado 11 girado, âmbar se escolhido; vizinhos a < 4 dp viram pílula. */
    private fun DrawScope.drawDiamonds(
        r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float,
        compact: Boolean, selFrame: Int, dragFrame: Int, fps: Float,
    ) {
        val inst = r.instants
        if (inst.isEmpty()) return
        val cy = top + if (compact) m.diamondCyCompact else m.diamondCyNormal
        var i = Keyframes.firstAtOrAfter(inst, TimeAxis.frameAt(-m.keyTouchHalf, view, ppf, cx))
        var dragX = Float.NaN
        while (i < inst.size) {
            val kx = TimeAxis.xOf(inst[i].toDouble(), view, ppf, cx)
            if (kx > w + m.keyTouchHalf) break
            var j = i
            var lastX = kx
            var on = inst[i] == selFrame
            while (j + 1 < inst.size) {
                val nx = TimeAxis.xOf(inst[j + 1].toDouble(), view, ppf, cx)
                if (nx - lastX >= m.keyMergeGap) break
                j++
                lastX = nx
                if (inst[j] == selFrame) on = true
            }
            val fill = if (on) AureaTimeline.KeyframeOn else KEY_OFF
            if (j == i) {
                val dragging = inst[i] == dragFrame
                if (dragging) dragX = kx
                drawDiamond(kx, cy, fill, on, if (dragging) m.keyDragScale else 1f)
            } else {
                drawKeyPill(kx, lastX, cy, fill)
            }
            i = j + 1
        }
        if (!dragX.isNaN()) drawBalloon(dragX, cy, dragFrame, fps)
    }

    private fun DrawScope.drawDiamond(x: Float, y: Float, fill: Color, on: Boolean, scale: Float) {
        val s = m.diamond * scale
        val half = s / 2f
        rotate(45f, Offset(x, y)) {
            // Brilho do aceso (A.01: sombra âmbar .5) / sombra leve do apagado, sem blur.
            val glow = half * 1.3f
            drawRoundRect(
                if (on) AureaTimeline.KeyframeOn.copy(alpha = 0.35f) else Color.Black.copy(alpha = 0.3f),
                Offset(x - glow, y - glow),
                Size(glow * 2f, glow * 2f),
                CornerRadius(m.diamondRadius * 1.5f),
            )
            drawRoundRect(fill, Offset(x - half, y - half), Size(s, s), CornerRadius(m.diamondRadius))
            val b = m.diamondStroke / 2f
            drawRoundRect(
                KEY_BORDER,
                Offset(x - half + b, y - half + b),
                Size(s - m.diamondStroke, s - m.diamondStroke),
                CornerRadius(m.diamondRadius),
                style = diamondStroke,
            )
        }
    }

    private fun DrawScope.drawKeyPill(xa: Float, xb: Float, y: Float, fill: Color) {
        val wPill = max(xb - xa, m.keyPillMinWidth)
        val left = (xa + xb) / 2f - wPill / 2f
        val hPill = m.keyPillHeight
        drawRoundRect(fill, Offset(left, y - hPill / 2f), Size(wPill, hPill), CornerRadius(hPill / 2f))
        val b = m.diamondStroke / 2f
        drawRoundRect(
            KEY_BORDER,
            Offset(left + b, y - hPill / 2f + b),
            Size(wPill - m.diamondStroke, hPill - m.diamondStroke),
            CornerRadius(hPill / 2f),
            style = diamondStroke,
        )
    }

    /**
     * Balão de tempo do losango na mão (A.01: `MM:SS:FF` 10 sp sobre preto .82),
     * logo acima do losango crescido — dentro da linha, para não ser cortado na 1ª.
     */
    private fun DrawScope.drawBalloon(x: Float, diamondCy: Float, frame: Int, fps: Float) {
        if (balloonFrame != frame || balloonLayout == null) {
            balloonFrame = frame
            balloonLayout = measurer.measure(Timecode.format(frame, fps), balloonStyle)
        }
        val layout = balloonLayout ?: return
        val bw = layout.size.width + m.balloonPadH * 2f
        val bh = layout.size.height + m.balloonPadV * 2f
        val left = x - bw / 2f
        val diamondTop = diamondCy - m.diamond * m.keyDragScale * DIAGONAL / 2f
        val top = diamondTop - m.balloonGap - bh
        drawRoundRect(BALLOON, Offset(left, top), Size(bw, bh), CornerRadius(m.balloonRadius))
        drawText(layout, topLeft = Offset(left + m.balloonPadH, top + m.balloonPadV))
    }

    /** Pílula 58×28 da A.01: olho + quadradinho (cadeado se travada, visto se no lote). */
    private fun DrawScope.drawHeaderPill(r: RowModel, cy: Float, inBatch: Boolean) {
        drawRoundRect(
            AureaTimeline.HeaderPill,
            Offset(m.pillLeft, cy - m.pillHeight / 2f),
            Size(m.pillWidth, m.pillHeight),
            CornerRadius(m.pillRadius),
        )
        drawGlyph(if (r.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, m.eyeGlyph, EYE_TINT, m.eyeCenterX, cy)
        drawRoundRect(
            AureaTimeline.Swatch,
            Offset(m.swatchLeft, cy - m.swatch / 2f),
            Size(m.swatch, m.swatch),
            CornerRadius(m.swatchRadius),
        )
        val sx = m.swatchLeft + m.swatch / 2f
        when {
            r.locked -> drawGlyph(CupertinoGlyph.LockFill, 11f, AureaTimeline.SwatchGlyph, sx, cy)
            inBatch -> drawGlyph(CupertinoGlyph.CheckmarkAlt, 13f, AureaTimeline.SwatchGlyph, sx, cy)
        }
    }

    // --- Régua --------------------------------------------------------------------------
    /**
     * Riscos a partir do instante 0 (nada em tempo negativo): forte de y 2 a 18,
     * fino de 9 a 18 (A.01). Dois Paths, duas chamadas de desenho.
     */
    private fun DrawScope.drawRuler(w: Float, view: Double, ppf: Float, cx: Float, fps: Float, pps: Float) {
        if (steps == null || stepsPps != pps || stepsFps != fps) {
            steps = RulerSteps.of(pps, fps)
            stepsPps = pps
            stepsFps = fps
        }
        val s = steps ?: return
        val t0 = TimeAxis.frameAt(0f, view, ppf, cx) / fps
        val t1 = TimeAxis.frameAt(w, view, ppf, cx) / fps
        if (t1 < 0.0) return
        val major = s.majorSeconds
        majorPath.reset()
        minorPath.reset()
        var k = max(0L, floor(max(0.0, t0) / major).toLong())
        val kEnd = ceil(t1 / major).toLong()
        while (k <= kEnd) {
            val t = k * major
            val x = TimeAxis.xOf(t * fps, view, ppf, cx)
            majorPath.moveTo(x, m.tickMajorTop)
            majorPath.lineTo(x, m.tickBottom)
            if (!s.frameMinors && s.subdivisions > 1) {
                for (j in 1 until s.subdivisions) {
                    val xm = TimeAxis.xOf((t + j * major / s.subdivisions) * fps, view, ppf, cx)
                    if (xm < -1f || xm > w + 1f) continue
                    minorPath.moveTo(xm, m.tickMinorTop)
                    minorPath.lineTo(xm, m.tickBottom)
                }
            }
            if (s.labels) drawRulerLabel(t.roundToInt(), x, cx)
            k++
        }
        if (s.frameMinors) {
            var f = max(0L, ceil(t0 * fps).toLong())
            val fEnd = floor(t1 * fps).toLong()
            while (f <= fEnd) {
                val sec = f / fps.toDouble()
                val nearMajor = Math.round(sec / major) * major
                if (abs(sec - nearMajor) * fps >= 0.5) {
                    val x = TimeAxis.xOf(f.toDouble(), view, ppf, cx)
                    minorPath.moveTo(x, m.tickMinorTop)
                    minorPath.lineTo(x, m.tickBottom)
                }
                f++
            }
        }
        drawPath(minorPath, AureaTimeline.TickMinor, style = minorStroke)
        drawPath(majorPath, AureaTimeline.TickMajor, style = majorStroke)
    }

    /** Marcas na régua: triângulo no alto + fio até a base dos riscos; batidas em laranja, menores. */
    private fun DrawScope.drawMarkers(mk: com.aurea.aurea.state.EditorStore.Markers, w: Float, view: Double, ppf: Float, cx: Float) {
        if (mk.size == 0) return
        val half = m.tickBottom * 0.28f
        for (i in 0 until mk.size) {
            val x = TimeAxis.xOf(mk.frames[i].toDouble(), view, ppf, cx)
            if (x < -half || x > w + half) continue
            val beat = mk.kinds[i] == 1
            val c = mk.colors[i]
            val color = Color(red = (c and 0xFF) / 255f, green = ((c shr 8) and 0xFF) / 255f, blue = ((c shr 16) and 0xFF) / 255f)
            val s = if (beat) half * 0.7f else half
            markerPath.reset()
            markerPath.moveTo(x - s, 0f)
            markerPath.lineTo(x + s, 0f)
            markerPath.lineTo(x, s * 1.4f)
            markerPath.close()
            drawPath(markerPath, color)
            drawLine(color, Offset(x, s * 1.4f), Offset(x, m.tickBottom), strokeWidth = if (beat) 1f else 1.5f * m.density)
        }
    }
    private val markerPath = androidx.compose.ui.graphics.Path()

    private fun DrawScope.drawRulerLabel(seconds: Int, x: Float, cx: Float) {
        var layout = labels.get(seconds)
        if (layout == null) {
            if (labels.size() > LABEL_CACHE) labels.clear()
            layout = measurer.measure(Timecode.rulerLabel(seconds), labelStyle)
            labels.put(seconds, layout)
        }
        val lx = x + m.tickLabelGap
        // O rótulo não disputa leitura com o relógio.
        if (lx + layout.size.width > cx - m.timecodeZoneHalf && lx < cx + m.timecodeZoneHalf) return
        drawText(layout, topLeft = Offset(lx, 0f))
    }

    // --- Relógio ------------------------------------------------------------------------
    /**
     * `MM:SS:FF` 13 sp bold, centrado no cabeçote, sublinhado 58×1,5 (A.01).
     * Desenhado dígito a dígito com layouts em cache (algarismos tabulares têm
     * a mesma largura): o relógio repinta a cada quadro sem alocar.
     */
    private fun DrawScope.drawTimecode(cx: Float, frame: Int, fps: Float) {
        if (digits[0] == null) {
            for (dgt in 0..9) digits[dgt] = measurer.measure(dgt.toString(), digitStyle)
            digits[10] = measurer.measure(":", digitStyle)
            digitWidth = (0..9).maxOf { digits[it]!!.size.width }.toFloat()
            colonWidth = digits[10]!!.size.width.toFloat()
            digitBaseline = digits[0]!!.firstBaseline
        }
        Timecode.split(frame, fps, tc)
        val ffDigits = if (fps > 100f) 3 else 2
        var hourDigits = 0
        var hv = tc[0]
        while (hv > 0) {
            hourDigits++
            hv /= 10
        }
        val colons = if (hourDigits > 0) 3 else 2
        val total = (hourDigits + 4 + ffDigits) * digitWidth + colons * colonWidth
        val top = m.timecodeBaseline - digitBaseline
        var x = cx - total / 2f
        if (hourDigits > 0) {
            x = drawNumber(tc[0], hourDigits, x, top)
            x = drawColon(x, top)
        }
        x = drawNumber(tc[1], 2, x, top)
        x = drawColon(x, top)
        x = drawNumber(tc[2], 2, x, top)
        x = drawColon(x, top)
        drawNumber(tc[3], ffDigits, x, top)
        drawRect(Color.White, Offset(cx - m.underlineWidth / 2f, m.underlineTop), Size(m.underlineWidth, m.underlineHeight))
    }

    private fun DrawScope.drawNumber(value: Int, count: Int, x0: Float, top: Float): Float {
        var x = x0
        var div = 1
        repeat(count - 1) { div *= 10 }
        while (div > 0) {
            val layout = digits[(value / div) % 10]!!
            drawText(layout, topLeft = Offset(x + (digitWidth - layout.size.width) / 2f, top))
            x += digitWidth
            div /= 10
        }
        return x
    }

    private fun DrawScope.drawColon(x: Float, top: Float): Float {
        drawText(digits[10]!!, topLeft = Offset(x, top))
        return x + colonWidth
    }

    // --- Texto em cache -------------------------------------------------------------------
    /** Nome com reticências na largura disponível (em degraus de 12 dp: rolar não refaz o layout a cada px). */
    private fun nameLayout(r: RowModel, availPx: Float): TextLayoutResult {
        val step = NAME_STEP_DP * m.density
        val bucket = (availPx / step).toInt()
        val cached = names.get(r.id)
        if (cached != null && cached.bucket == bucket && cached.name == r.name) return cached.layout
        val layout = measurer.measure(
            text = r.name,
            style = nameStyle,
            overflow = TextOverflow.Ellipsis,
            softWrap = false,
            maxLines = 1,
            constraints = Constraints(maxWidth = max(1, (bucket * step).toInt())),
        )
        if (names.size() > NAME_CACHE) names.clear()
        names.put(r.id, NameLayout(r.name, bucket, layout))
        return layout
    }

    /** Glifo da fonte CupertinoIcons numa caixa N×N (como `CupertinoIcon`), centrado em (cx, cy). */
    private fun DrawScope.drawGlyph(glyph: Char, sizeDp: Float, tint: Color, cx: Float, cy: Float) {
        val key = glyph.code * 64 + sizeDp.roundToInt()
        var layout = glyphs.get(key)
        if (layout == null) {
            val fontSize = (sizeDp / m.fontScale).sp
            layout = measurer.measure(
                glyph.toString(),
                TextStyle(
                    fontFamily = CupertinoIconsFont,
                    fontSize = fontSize,
                    lineHeight = fontSize,
                    platformStyle = PlatformTextStyle(includeFontPadding = false),
                    lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both),
                ),
            )
            glyphs.put(key, layout)
        }
        drawText(layout, color = tint, topLeft = Offset(cx - layout.size.width / 2f, cy - layout.size.height / 2f))
    }

    private companion object {
        const val STATE_NORMAL = 0
        const val STATE_SELECTED = 1
        const val STATE_HIDDEN = 2
        const val THUMB_MAX_PX = 512
        const val NAME_STEP_DP = 12f
        const val NAME_MIN_DP = 8f
        const val NAME_CACHE = 256
        const val LABEL_CACHE = 96
        /** Diagonal do quadrado girado (√2). */
        const val DIAGONAL = 1.4142135f
        val TRACK_SHADE = Color.Black.copy(alpha = 0.22f)
        val GRIP = Color.Black.copy(alpha = 0.38f)
        val ARROW_TINT = Color.White.copy(alpha = 0.7f)
        val EYE_TINT = Color.White.copy(alpha = 0.7f)
        val KEY_OFF = Color.White.copy(alpha = 0.9f)
        val KEY_BORDER = Color.Black.copy(alpha = 0.85f)
        val BALLOON = Color.Black.copy(alpha = 0.82f)

        fun lerpSrgb(a: Color, b: Color, t: Float) = Color(
            red = a.red + (b.red - a.red) * t,
            green = a.green + (b.green - a.green) * t,
            blue = a.blue + (b.blue - a.blue) * t,
            alpha = 1f,
        )
    }
}
