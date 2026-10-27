package com.aurea.aurea.editor.timeline

import android.graphics.Paint
import android.graphics.RectF
import android.util.LongSparseArray
import android.util.SparseArray
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.translate
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
import com.aurea.aurea.ui.theme.ClipTone
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.CupertinoIconsFont
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.aureaTracking
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

    // Waveform: janela por linha em grade fixa do tempo + um array de linhas reusado (nada de alocar por quadro).
    private val waves = WaveStrip(WAVE_MAX)
    private val waveLines = FloatArray(WAVE_MAX * 4)
    private val wavePaint = Paint().apply { isAntiAlias = false; strokeCap = Paint.Cap.BUTT }
    private var waveStore: com.aurea.aurea.state.EditorStore? = null
    private val waveSource = WaveSource { layer, start, fpb, count, out ->
        waveStore?.queryWaveform(layer, start, fpb, count, out) ?: 0
    }
    private val majorPath = Path()
    private val minorPath = Path()
    private val majorStroke = Stroke(m.tickMajorWidth)
    private val minorStroke = Stroke(m.tickMinorWidth)
    private val selStroke = Stroke(m.selStroke)
    private val multiStroke = Stroke(m.multiStroke)
    private val diamondStroke = Stroke(m.diamondStroke)
    private val pickedStroke = Stroke(2f * m.density)
    private val tc = IntArray(4)
    /** Grupos de losangos visíveis de uma linha ([primeiro, último] por grupo), reusado. */
    private var groupBuf = IntArray(256)
    /** Grupos dos losangos esmaecidos (outros parâmetros), separado do [groupBuf]. */
    private var dimGroupBuf = IntArray(256)
    /** Camada de desenho dos esmaecidos (160/255) e do fade de fileira nova. */
    private val dimPaint = androidx.compose.ui.graphics.Paint().apply { alpha = KeyEmphasis.DIM_ALPHA }
    private val fadePaint = androidx.compose.ui.graphics.Paint()
    /** Relógio e cabeçote do quadro em pintura (lidos uma vez em [draw]). */
    private var frameNanos = 0L
    private var playheadFrame = Snap.NONE
    private var steps: RulerSteps? = null
    private var stepsPps = Float.NaN
    private var stepsFps = Float.NaN

    /** Véu sobre a tira (A.01: preto .62 → .22 até 45 % da barra), em espaço 0..1 e escalado. */
    private val thumbShade = Brush.horizontalGradient(
        0f to Color.Black.copy(alpha = 0.45f),
        0.45f to Color.Black.copy(alpha = 0.12f),
        startX = 0f,
        endX = 1f,
    )

    /** Pílula da fileira: raio 14 só à direita (colada na borda esquerda). */
    private val pillPath = Path()
    private val pillRect = androidx.compose.ui.geometry.RoundRect(
        0f, 0f, m.headerColumn, m.pillHeight,
        topLeftCornerRadius = CornerRadius.Zero,
        topRightCornerRadius = CornerRadius(m.pillRadius),
        bottomRightCornerRadius = CornerRadius(m.pillRadius),
        bottomLeftCornerRadius = CornerRadius.Zero,
    ).also { pillPath.addRoundRect(it) }
    private val glyphRing = Stroke(1.5f * m.density)
    /** Contorno branco do clipe escolhido na fileira compacta (ponta esquerda redonda). */
    private val capStroke = Stroke(m.capStroke)
    private val capOutline = Path()
    private val capRadii = FloatArray(8)
    private var capName: NameLayout? = null
    private val dividerPaint = Paint().apply { color = android.graphics.Color.argb(64, 0, 0, 0); strokeWidth = 1f }
    private val glyphSrc = android.graphics.Rect()

    // --- Texto ------------------------------------------------------------------------
    private val nameStyle = TextStyle(
        color = Color.White,
        fontSize = (if (m.referenceTrial) 10 else 11).sp,
        fontWeight = FontWeight.W500,
        letterSpacing = aureaTracking(-0.1f),   // nome de camada em árabe: sem tracking
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    /** Nome do clipe escolhido na fileira compacta: 13 sp semibold (Efeitos.dc.html). */
    private val capNameStyle = TextStyle(
        color = Color.White,
        fontSize = 13.sp,
        fontWeight = FontWeight.W600,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private val digitStyle = TextStyle(
        color = Color.White,
        fontSize = (if (m.referenceTrial) 14 else 16).sp,
        fontWeight = FontWeight.W700,
        letterSpacing = 0.3.sp,
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
    /** O "T" da camada de texto no quadradinho da pílula. */
    private val glyphTextStyle = TextStyle(
        color = AureaTimeline.GlyphText,
        fontSize = 11.sp,
        fontWeight = FontWeight.W700,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private var glyphT: TextLayoutResult? = null
    /** "↳" da camada recortada pela de baixo (desenho próprio do AUREA). */
    private val clipGlyphStyle = TextStyle(
        color = AureaTimeline.GutterIcon,
        fontSize = 11.sp,
        fontWeight = FontWeight.W700,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    private var glyphClip: TextLayoutResult? = null
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
        // Omega 0.3.2 (área 03): a timeline da referência tem o pintor dela.
        if (m.referenceTrial) {
            drawOmega(c)
            return@with
        }
        val w = size.width
        val h = size.height
        val store = c.store
        val st = c.state
        val fps = TimeAxis.safeFps(store.project.fps)
        val ppf = TimeAxis.pxPerFrame(st.pps, m.density, fps)
        val view = c.view()
        val cx = w / 2f
        val playheadX = TimeAxis.xOf(store.playhead.toDouble(), view, ppf, cx)
        val compact = st.compact
        waveStore = store
        val rows = c.rows.value
        val n = c.rowCount(rows)
        st.redrawTick   // lido: as miniaturas que não couberam neste quadro pedem o próximo
        thumbs.beginFrame(THUMB_QUERIES_PER_FRAME)
        frameNanos = System.nanoTime()
        playheadFrame = store.playhead

        if (n > 0) {
            // A pílula da 1ª fileira começa 2 dp acima da barra.
            clipRect(top = m.rowsTop - m.pillInset) {
                drawRows(c, rows, n, w, h, view, ppf, cx, fps, compact)
            }
        }
        drawRuler(w, view, ppf, cx, fps, st.pps)
        drawPreviewBuffer(store.previewBufferRanges, w, view, ppf, cx)
        drawMarkers(store.markers, w, view, ppf, cx)
        drawTimecode(cx, store.playhead, fps)
        if (thumbs.starved) c.requestRedraw()
        c.settleFades(frameNanos)
        // Uma origem temporal para clipes, losangos e fio, inclusive na seleção.
        val color = AureaColors.Playhead
        // Fio de 1 dp interrompido apenas na faixa do relógio.
        drawRect(color, Offset(playheadX - m.playhead / 2f, m.playheadTop), Size(m.playhead, h - m.playheadTop))
        drawRect(color, Offset(playheadX - m.playhead / 2f, 0f), Size(m.playhead, m.rulerTicks))
    }

    /** Completed frames only, using the same temporal axis as clips, ticks and playhead. */
    private fun DrawScope.drawPreviewBuffer(ranges: List<com.aurea.aurea.engine.PreviewBufferRange>, w: Float,
                                           view: Double, ppf: Float, cx: Float) {
        val height = 3f * m.density
        for (range in ranges) {
            val left = TimeAxis.xOf(range.startFrame.toDouble(), view, ppf, cx).coerceAtLeast(0f)
            val right = TimeAxis.xOf(range.endFrame.toDouble(), view, ppf, cx).coerceAtMost(w)
            if (right > left) drawRect(Color(0xFF4DA3FF), Offset(left, m.rulerTicks - height), Size(right - left, height))
        }
    }

    // --- Linhas ---------------------------------------------------------------------------
    private fun DrawScope.drawRows(
        c: TimelineController, rows: List<RowModel>, n: Int, w: Float, h: Float,
        view: Double, ppf: Float, cx: Float, fps: Float, compact: Boolean,
    ) {
        val st = c.state
        val scroll = c.clampedScroll(n)
        val first = max(0, c.rowIndexAt(scroll))
        val last = min(n - 1, c.rowIndexAt(scroll + h - m.rowsTop))
        val selCount = c.selectionSize()
        val multi = selCount >= 2
        val generation = c.store.thumbnailGeneration
        val cache = c.store.thumbnails
        val preview = if (st.reorderSource >= 0 && !compact) Reorder.preview(
            FloatArray(n + 1) { c.rowTop(it) }, timelineGroupKeys(rows),
            st.reorderSource, st.reorderTarget, st.reorderTop - m.rowsTop + scroll,
        ) else null
        val indices = if (preview != null) 0 until n else first..last
        val passes = if (preview != null) 0..1 else 0..0
        if (preview != null && preview.gapTop.isFinite()) {
            val y = m.rowsTop + preview.gapTop - scroll
            drawRect(AureaColors.Accent, Offset(0f, y), Size(w, m.reorderLine))
        }
        for (pass in passes) for (i in indices) {
            val lifted = preview != null && i in preview.sourceStart until preview.sourceEnd
            if (preview != null && lifted != (pass == 1)) continue
            val r = c.rowAt(rows, i) ?: continue
            val top = m.rowsTop + c.rowTop(i) - scroll + (preview?.offsets?.get(i) ?: 0f)
            if (top + c.rowHeight(r) < m.rowsTop || top > h) continue
            if (lifted) {
                drawRect(AureaColors.EditorCanvas, Offset(0f, top), Size(w, c.rowHeight(r)))
                drawRect(AureaColors.Accent.copy(alpha = 0.14f), Offset(0f, top), Size(w, c.rowHeight(r)))
            }
            val segs = r.segments
            if (st.liftRow == i && st.liftKind == RowDrop.JOIN) {
                // A linha que recebe o trecho no ar acende inteira.
                drawRect(AureaColors.Accent.copy(alpha = 0.18f), Offset(m.headerColumn, top), Size(w - m.headerColumn, c.rowHeight(r)))
            }
            // Fileira recém-criada: surge com fade (RowFade, 600 ms).
            val fade = c.rowAlpha(r, frameNanos)
            if (fade < 1f) beginFade(fade, top, c.rowHeight(r), w)
            if (segs.size == 1) {
                // O trecho no ar sai da fileira dele (é desenhado sob o dedo).
                if (segs[0].id != st.liftId) drawSegment(c, segs[0], top, w, view, ppf, cx, fps, compact, selCount, multi, cache, generation, arrows = true)
            } else {
                // FILEIRA COMPARTILHADA: cada trecho da linha no seu tempo, lado a
                // lado. Os escolhidos por cima — contorno e alças passam por cima do
                // vizinho encostado, que é onde o dedo os pega.
                var onScreen = false
                var offLeft = false
                var offRight = false
                for (s in segs) {
                    val x0 = TimeAxis.xOf(s.start.toDouble(), view, ppf, cx)
                    val x1 = TimeAxis.xOf(s.end.toDouble(), view, ppf, cx)
                    when {
                        x1 >= -m.barRadius && x0 <= w + m.barRadius -> onScreen = true
                        x0 > w -> offRight = true
                        else -> offLeft = true
                    }
                }
                for (pass in 0..1) for (s in segs) {
                    if (c.isSelected(s.id) != (pass == 1) || s.id == st.liftId) continue
                    drawSegment(c, s, top, w, view, ppf, cx, fps, compact, selCount, multi, cache, generation, arrows = false)
                }
                // Linha inteira fora da janela: UMA seta por lado (não uma por trecho).
                // Legenda desenha os blocos dela e nunca teve seta.
                if (!onScreen && c.store.captionTracks.none { t -> t.layer == segs[0].id }) {
                    val color = AureaTimeline.tone(r.type).stripe.copy(alpha = if (r.visible) 0.9f else 0.45f)
                    if (offRight) drawEdgeArrow(true, top, w, color)
                    if (offLeft) drawEdgeArrow(false, top, w, color)
                }
            }
            if (fade < 1f) drawContext.canvas.restore()
        }

        // Pílulas por cima das barras (as barras passam por baixo): olho + miniatura do tipo.
        for (pass in passes) for (i in indices) {
            val lifted = preview != null && i in preview.sourceStart until preview.sourceEnd
            if (preview != null && lifted != (pass == 1)) continue
            val r = c.rowAt(rows, i) ?: continue
            val rowTop = m.rowsTop + c.rowTop(i) - scroll + (preview?.offsets?.get(i) ?: 0f)
            if (rowTop + c.rowHeight(r) < m.rowsTop || rowTop > h) continue
            val track = r.track
            val fade = c.rowAlpha(r, frameNanos)
            if (fade < 1f) beginFade(fade, rowTop, c.rowHeight(r), w)
            if (track != null) {
                drawLaneGutter(r, track, rowTop, c.rowHeight(r), c.openGroups.value)
            } else {
                // A calha é da FILEIRA: no lote se algum trecho dela está no lote;
                // aberta se as trilhas abertas são de um trecho dela.
                var inBatch = false
                if (multi) for (s in r.segments) if (c.isSelected(s.id)) inBatch = true
                drawGutter(c, r, rowTop + m.barTop, inBatch, c.isExpanded(r), fps)
            }
            if (fade < 1f) drawContext.canvas.restore()
        }

        // Trecho no ar (arrasto vertical de UM trecho): o traço de inserção entre
        // as fileiras e o trecho sob o dedo, por cima de tudo.
        if (st.liftId != 0L && !compact) {
            if (st.liftKind == RowDrop.INSERT && st.liftLineY.isFinite()) {
                val y = m.rowsTop + st.liftLineY - scroll
                drawRect(AureaColors.Accent, Offset(0f, y - m.reorderLine / 2f), Size(w, m.reorderLine))
                drawCircle(AureaColors.Accent, m.reorderDot, Offset(m.headerColumn, y))
            }
            var lifted: RowModel? = null
            for (r in rows) { lifted = r.segment(st.liftId); if (lifted != null) break }
            if (lifted != null) {
                val x0 = TimeAxis.xOf(lifted.start.toDouble(), view, ppf, cx)
                val x1 = TimeAxis.xOf(lifted.end.toDouble(), view, ppf, cx)
                val hh = c.rowHeight(lifted)
                drawRect(AureaColors.Accent.copy(alpha = 0.22f), Offset(x0, st.liftTop), Size(x1 - x0, hh))
                drawSegment(c, lifted, st.liftTop, w, view, ppf, cx, fps, compact, selCount, multi, cache, generation, arrows = false)
            }
        }

        // Retângulo da seleção de losangos (cantos presos ao conteúdo).
        if (st.boxActive) {
            val xa = TimeAxis.xOf(st.boxFrame0, view, ppf, cx)
            val xb = TimeAxis.xOf(st.boxFrame1, view, ppf, cx)
            val ya = m.rowsTop + st.boxY0 - scroll
            val yb = m.rowsTop + st.boxY1 - scroll
            val topLeft = Offset(min(xa, xb), min(ya, yb))
            val boxSize = Size(abs(xb - xa), abs(yb - ya))
            drawRect(AureaColors.Accent.copy(alpha = 0.14f), topLeft, boxSize)
            drawRect(AureaColors.Accent, topLeft, boxSize, style = Stroke(m.guide))
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

    /** Um TRECHO na fileira (a fileira inteira, quando ela tem um só): estado de seleção e keyframes dele. */
    private fun DrawScope.drawSegment(
        c: TimelineController, r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        compact: Boolean, selCount: Int, multi: Boolean, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
        arrows: Boolean,
    ) {
        val st = c.state
        val selKey = c.store.selectedKeyframe
        val keySel = c.store.keySelection
        val selected = c.isSelected(r.id)
        val handles = selCount == 1 && selected && !r.locked
        val selFrame = if (selKey != null && selKey.first == r.id) r.selectedFrame(selKey.second) else Snap.NONE
        val dragFrame = if (st.dragKeyLayer == r.id) st.dragKeyFrame else Snap.NONE
        // Seleção de keyframes da timeline: quais instantes desta linha têm keyframe escolhido.
        val picked = if (keySel != null && keySel.on(r.id).isNotEmpty()) pickedInstants(r, keySel) else null
        val keysShown = KeyframeVisibility.visible(c.store.showAllKeyframes, r.track != null, selected)
        val keysLocked = !KeyframeVisibility.editable(selected, c.store.keySelectMode, r.track != null)
        drawRow(r, top + if (r.track == null) m.barTop else 0f, w, view, ppf, cx, fps, compact, selected, multi, handles, selFrame, dragFrame, cache, generation, picked, keysShown, arrows, keysLocked)
    }

    /** Seta na borda: o clipe está para aquele lado (linha vazia parecia camada quebrada). */
    private fun DrawScope.drawEdgeArrow(right: Boolean, top: Float, w: Float, color: Color) {
        val tip = if (right) w - 10f * m.density else m.headerColumn + 10f * m.density
        val back = if (right) tip - 7f * m.density else tip + 7f * m.density
        val cy = top + m.bar / 2f
        val arrow = androidx.compose.ui.graphics.Path().apply {
            moveTo(tip, cy)
            lineTo(back, cy - 6f * m.density)
            lineTo(back, cy + 6f * m.density)
            close()
        }
        drawPath(arrow, color)
    }

    private fun DrawScope.drawRow(
        r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        compact: Boolean, selected: Boolean, multi: Boolean, handles: Boolean,
        selFrame: Int, dragFrame: Int, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
        picked: BooleanArray?,
        keysShown: Boolean = true,
        arrows: Boolean = true,
        keysLocked: Boolean = false,
    ) {
        if (r.track != null) {
            // Trilha BAIXA (16 dp): nome e losangos na mesma faixa, centrados;
            // os losangos por cima do nome (o nome é só a legenda).
            val laneH = LANE_HEIGHT_DP * m.density
            val cy = top + laneH / 2f
            drawLine(Color.White.copy(alpha = 0.06f), Offset(m.headerColumn, top + laneH), Offset(w, top + laneH))
            val labelWidth = min(w - m.laneLabelLeft - 8f * m.density, 240f * m.density)
            if (labelWidth > 0f) {
                val label = nameLayout(r, labelWidth)
                val left = m.laneLabelLeft
                drawText(label, alpha = if (r.track.group) 0.9f else 0.6f, topLeft = Offset(left, cy - label.size.height / 2f))
            }
            lockedKeys(keysLocked, top, laneH, w) { drawDiamonds(r, cy - m.diamondCyNormal, w, view, ppf, cx, false, selFrame, dragFrame, fps, picked) }
            return
        }
        val captionTrack = waveStore?.captionTracks?.firstOrNull { it.layer == r.id }
        if (captionTrack != null) {
            for (block in captionTrack.segments) {
                val left = TimeAxis.xOf((block.start + r.start - r.offset).toDouble(), view, ppf, cx)
                val right = TimeAxis.xOf((block.end + r.start - r.offset).toDouble(), view, ppf, cx)
                if (right < 0 || left > w) continue
                val x = max(0f, left); val end = min(w, right - 2f)
                if (end <= x) continue
                val tone = AureaTimeline.tone(r.type)
                drawRoundRect(if (selected) tone.stripe.copy(alpha = .7f) else tone.body, Offset(x, top), Size(end - x, m.bar), CornerRadius(m.barRadius))
                if (end - x > 20f) {
                    val layout = measurer.measure(block.text, nameStyle, overflow = TextOverflow.Ellipsis, maxLines = 1, constraints = Constraints(maxWidth = (end - x - 10f).toInt().coerceAtLeast(1)))
                    clipRect(x, top, end, top + m.bar) { drawText(layout, color = tone.text, topLeft = Offset(x + 5f, top + 5f)) }
                }
            }
            return
        }
        val x0 = TimeAxis.xOf(r.start.toDouble(), view, ppf, cx)
        val x1 = TimeAxis.xOf(r.end.toDouble(), view, ppf, cx)
        val barW = x1 - x0
        val tone = AureaTimeline.tone(r.type)
        // Camada oculta: os clipes da fileira a 40 %.
        val alpha = if (r.visible) 1f else AureaTimeline.HiddenAlpha
        val capped = compact && selected && !m.referenceTrial
        val visualLeft = if (capped) RowHit.capLeft(m, x0) else x0
        if (x1 >= -m.barRadius && visualLeft <= w + m.barRadius) {
            // Barra cortada perto da tela: um clipe de minutos não vira um retângulo de 100 mil px.
            val left = max(visualLeft, -m.capRadius * 2f)
            val right = min(x1, w + m.barRadius * 2f)
            val bodyLeft = min(max(x0, left), right)
            val bottom = top + m.bar
            val canvas = drawContext.canvas.nativeCanvas
            rectF.set(left, top, right, bottom)
            barClip.reset()
            // Fileira compacta, clipe escolhido: ponta esquerda bem redonda (raio 14).
            if (capped) {
                val l = ClipGeometry.cornerRadius(right - left, m.bar, m.capRadius)
                val rr = ClipGeometry.cornerRadius(right - left, m.bar, m.barRadius)
                capRadii[0] = l; capRadii[1] = l; capRadii[2] = rr; capRadii[3] = rr
                capRadii[4] = rr; capRadii[5] = rr; capRadii[6] = l; capRadii[7] = l
                barClip.addRoundRect(rectF, capRadii, android.graphics.Path.Direction.CW)
            } else {
                barClip.addRoundRect(rectF, m.barRadius, m.barRadius, android.graphics.Path.Direction.CW)
            }
            canvas.save()
            canvas.clipPath(barClip)
            canvas.save()
            // A decoração externa não muda a origem de miniaturas, ondas ou keyframes.
            canvas.clipRect(bodyLeft, top, right, bottom)
            // Bloco escuro no tom do tipo.
            drawRect(tone.body, Offset(bodyLeft, top), Size(right - bodyLeft, m.bar), alpha = alpha)
            bitmapPaint.alpha = (alpha * 255f).roundToInt()
            if (r.hasThumbs && drawThumbs(canvas, r, top, x0, x1, w, view, ppf, cx, fps, cache, generation)) {
                withTransform({
                    translate(x0, top)
                    scale(barW, 1f, pivot = Offset.Zero)
                }) { drawRect(thumbShade, size = Size(1f, m.bar), alpha = alpha) }
            }
            // Trilho dos losangos só quando a linha tem keyframe à vista.
            if (!m.referenceTrial && keysShown && r.hasKeys) {
                drawRect(TRACK_SHADE, Offset(left, top + m.trackTop), Size(right - left, m.track), alpha = alpha)
            }
            if (r.track == null && (r.type == LayerType.Audio || r.type == LayerType.Video)) drawWaveform(canvas, r, top, x0, x1, w, view, ppf, cx, tone, alpha)
            // Faixa sólida de 3 dp na borda esquerda só quando a camada tem etiqueta
            // de cor (o mockup não tem faixa: a cor do clipe já diz o tipo).
            val stripe = ShellColors.LabelPalette.getOrNull(r.label - 1)
            if (stripe != null) drawRect(stripe, Offset(x0, top), Size(m.stripe, m.bar), alpha = alpha)
            canvas.restore()
            if (capped) {
                // A borda direita da tampa coincide com o início temporal do clipe.
                val capL = RowHit.capLeft(m, x0)
                val capR = min(x0, right)
                if (capR > capL) {
                    drawRect(AureaTimeline.ClipSelected, Offset(capL, top), Size(capR - capL, m.bar))
                    drawGlyph(CupertinoGlyph.ChevronLeft, m.capGlyph, CAP_INK, capL + m.capWidth / 2f, top + m.bar / 2f)
                }
            }
            canvas.restore()

            // Setas ‹ › do compacto só no trecho escolhido (os outros da linha também aparecem).
            clipRect(bodyLeft, top, right, bottom) { drawBarContent(r, top, x0, x1, w, capped, tone, alpha) }

            // Subpixel bars must not gain duration from centered strokes or
            // corner paths. Only the separate trim controls may be external.
            clipRect(left, top, right, bottom) {
                if (capped) {
                    val stroke = ClipGeometry.strokeWidth(right - left, m.bar, m.capStroke)
                    if (right - left <= stroke) {
                        drawRect(AureaTimeline.ClipSelected, Offset(left, top), Size(right - left, m.bar))
                    } else {
                        val s = stroke / 2f
                        val innerWidth = right - left - stroke
                        val innerHeight = m.bar - stroke
                        capOutline.reset()
                        capOutline.addRoundRect(
                            androidx.compose.ui.geometry.RoundRect(
                                left + s, top + s, right - s, bottom - s,
                                topLeftCornerRadius = CornerRadius(ClipGeometry.cornerRadius(innerWidth, innerHeight, m.capRadius - s)),
                                topRightCornerRadius = CornerRadius(ClipGeometry.cornerRadius(innerWidth, innerHeight, m.barRadius - s)),
                                bottomRightCornerRadius = CornerRadius(ClipGeometry.cornerRadius(innerWidth, innerHeight, m.barRadius - s)),
                                bottomLeftCornerRadius = CornerRadius(ClipGeometry.cornerRadius(innerWidth, innerHeight, m.capRadius - s)),
                            ),
                        )
                        drawPath(capOutline, AureaTimeline.ClipSelected, style = if (stroke == m.capStroke) capStroke else Stroke(stroke))
                    }
                } else if (selected) {
                    val requested = if (multi) m.multiStroke else m.selStroke
                    val stroke = ClipGeometry.strokeWidth(right - left, m.bar, requested)
                    if (right - left <= stroke) {
                        drawRect(AureaTimeline.ClipSelected, Offset(left, top), Size(right - left, m.bar))
                    } else {
                        drawRoundRect(
                            AureaTimeline.ClipSelected,
                            Offset(left + stroke / 2f, top + stroke / 2f),
                            Size(right - left - stroke, m.bar - stroke),
                            CornerRadius(ClipGeometry.cornerRadius(right - left - stroke, m.bar - stroke, m.barRadius - stroke / 2f)),
                            style = if (multi) multiStroke else selStroke,
                        )
                    }
                }
            }
            if (handles && r.track == null) {
                // A tampa "‹" já é a alça branca da ponta esquerda (o dedo ali também apara).
                clipRect(left = m.headerColumn, right = w) {
                    val handleWidth = ClipGeometry.trimPaintWidth(barW, m.trimWidth)
                    if (!capped && RowHit.startHandleVisible(m, x0)) drawTrimHandle(x0 - handleWidth, top, start = true, width = handleWidth)
                    if (RowHit.endHandleVisible(x1, w)) drawTrimHandle(x1 - ClipGeometry.trimPaintEndInset(barW, m.trimWidth), top, start = false, width = handleWidth)
                }
            }
        } else if (r.track == null && arrows) {
            // Clipe fora da janela: uma seta na borda diz para que lado ele
            // está (linha vazia parecia camada quebrada). Na fileira
            // compartilhada quem decide a seta é a fileira (uma por lado).
            drawEdgeArrow(x0 > w, top, w, tone.stripe.copy(alpha = if (r.visible) 0.9f else 0.45f))
        }
        // "Keyframes: só as escolhidas" (menu da timeline): as outras linhas ficam limpas.
        if (keysShown) {
            lockedKeys(keysLocked, top, m.row, w) { drawDiamonds(r, top, w, view, ppf, cx, compact, selFrame, dragFrame, fps, picked) }
        }
    }

    /** Abre a camada de desenho do fade da fileira nova (fecha com `canvas.restore()`). */
    private fun DrawScope.beginFade(alpha: Float, top: Float, height: Float, w: Float) {
        fadePaint.alpha = alpha
        val pad = m.pillInset + m.diamond * 2f
        drawContext.canvas.saveLayer(androidx.compose.ui.geometry.Rect(0f, top - pad, w, top + height + pad), fadePaint)
    }

    /** Losangos só de referência (camada não selecionada): a mesma figura, esmaecida numa camada de desenho. */
    private inline fun DrawScope.lockedKeys(locked: Boolean, top: Float, height: Float, w: Float, block: DrawScope.() -> Unit) {
        if (!locked) { block(); return }
        val paint = androidx.compose.ui.graphics.Paint().apply { alpha = KeyframeVisibility.LOCKED_ALPHA }
        val pad = m.diamond * 2f
        drawContext.canvas.saveLayer(androidx.compose.ui.geometry.Rect(0f, top - pad, w, top + height + pad), paint)
        try { block() } finally { drawContext.canvas.restore() }
    }

    /**
     * Conteúdo do clipe: [‹] · glifo do tipo · cadeado · nome · ◇ · [›] ou ≡, no
     * tom claro do tipo (peso 500). Clipe curto mostra só o glifo.
     */
    private fun DrawScope.drawBarContent(
        r: RowModel, top: Float, x0: Float, x1: Float, w: Float, compact: Boolean, tone: ClipTone, alpha: Float,
    ) {
        val barW = x1 - x0
        val cl = RowHit.contentLeft(m, x0, x1)
        val cr = RowHit.contentRight(m, x0, x1, w)
        if (cr <= cl) return
        // Sem losangos o conteúdo centra na barra; com eles, sobe para a faixa de cima.
        val cy = if (m.referenceTrial || !r.hasKeys) top + m.bar / 2f else top + m.trackTop / 2f
        val d = m.density
        val arrowInk = ARROW_TINT.copy(alpha = ARROW_TINT.alpha * alpha)
        if (compact) return drawCappedContent(r, cy, x0, x1, w, cr, arrowInk, alpha)
        val x = cl
        // O tipo e o cadeado moram na pílula da fileira; a barra leva só o nome.
        // O ≡ mora na ponta REAL da barra (A.01); as setas do compacto grudam na parte visível (print t2).
        val menuRight = x1 - (if (barW < m.narrowBar) m.padRNarrow else m.padR)
        val right = when {
            barW > m.menuMinBar -> min(cr, menuRight - m.menuGlyph * d)
            else -> cr
        }
        if (barW > m.nameMinBar && r.name.isNotEmpty()) {
            val avail = right - x
            if (avail > NAME_MIN_DP * d) {
                val layout = nameLayout(r, avail)
                drawText(layout, color = tone.text, alpha = alpha, topLeft = Offset(x, cy - layout.size.height / 2f))
            }
        }
        if (!m.referenceTrial && barW > m.menuMinBar && menuRight <= w + m.menuGlyph * d) {
            drawGlyph(CupertinoGlyph.LineHorizontal3, m.menuGlyph, AureaTimeline.ClipGrip.copy(alpha = AureaTimeline.ClipGrip.alpha * alpha), menuRight - m.menuGlyph * d / 2f, top + m.bar / 2f)
        }
    }

    /**
     * Conteúdo do clipe escolhido na fileira compacta (Efeitos.dc.html): depois
     * da tampa "‹", o nome 13 sp semibold; as setas de trocar de camada ‹ ›
     * juntas na ponta direita (mesma geometria do [RowHit]).
     */
    private fun DrawScope.drawCappedContent(
        r: RowModel, cy: Float, x0: Float, x1: Float, w: Float, cr: Float, arrowInk: Color, alpha: Float,
    ) {
        val contentStart = max(x0, m.headerColumn)
        val next = RowHit.nextArrowLeft(m, x0, x1, w)
        val prev = next - m.arrowSlot
        val arrows = prev >= contentStart
        if (arrows) {
            drawGlyph(CupertinoGlyph.ChevronLeft, m.arrowGlyph, arrowInk, prev + m.arrowSlot / 2f, cy)
            drawGlyph(CupertinoGlyph.ChevronRight, m.arrowGlyph, arrowInk, next + m.arrowSlot / 2f, cy)
        }
        val x = contentStart + m.capNameGap
        val avail = (if (arrows) prev else cr) - x
        if (r.name.isEmpty() || avail <= NAME_MIN_DP * m.density) return
        val step = NAME_STEP_DP * m.density
        val bucket = (avail / step).toInt()
        val cached = capName
        val layout = if (cached != null && cached.bucket == bucket && cached.name == r.name) cached.layout
        else measurer.measure(
            text = r.name,
            style = capNameStyle,
            overflow = TextOverflow.Ellipsis,
            softWrap = false,
            maxLines = 1,
            constraints = Constraints(maxWidth = max(1, (bucket * step).toInt())),
        ).also { capName = NameLayout(r.name, bucket, it) }
        drawText(layout, alpha = alpha, topLeft = Offset(x, cy - layout.size.height / 2f))
    }

    /** Trim control paint can shrink; the temporal edge and padded hit target stay fixed. */
    private fun DrawScope.drawTrimHandle(left: Float, top: Float, start: Boolean, width: Float = m.trimWidth) {
        if (width <= 0f) return
        if (m.referenceTrial) {
            drawRoundRect(AureaTimeline.ClipSelected, Offset(left, top), Size(width, m.bar), CornerRadius(ClipGeometry.cornerRadius(width, m.bar, m.trimRadius)))
            if (width >= 10f * m.density) drawGlyph(if (start) CupertinoGlyph.ChevronLeft else CupertinoGlyph.ChevronRight, 10f, Color.Black,
                left + width / 2f, top + m.bar / 2f)
            return
        }
        drawRoundRect(
            AureaTimeline.ClipSelected,
            Offset(left, top + m.trimTop),
            Size(width, m.bar - m.trimTop * 2f),
            CornerRadius(ClipGeometry.cornerRadius(width, m.bar - m.trimTop * 2f, m.trimRadius)),
        )
        if (width >= m.gripWidth * 2f) drawRect(
            GRIP,
            Offset(left + (width - m.gripWidth) / 2f, top + (m.bar - m.gripHeight) / 2f),
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
        dividers: Boolean = true,
    ): Boolean {
        val heightPx = m.bar.roundToInt().coerceIn(1, THUMB_MAX_PX)
        val tile = heightPx * thumbs.aspect(cache, r.id)
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
                // Divisa fina entre os quadros da tira (mockup); a Omega não tem.
                if (dividers && l > x0 + 1f) canvas.drawLine(l, top, l, top + m.bar, dividerPaint)
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
        view: Double, ppf: Float, cx: Float, tone: ClipTone, alpha: Float,
    ) {
        val store = waveStore ?: return
        val visL = max(x0 + m.stripe, 0f)
        val visR = min(x1, w)
        if (visR <= visL || ppf <= 0f) return
        // Grade fixa do tempo (WaveGrid): o balde não anda com a vista, e a janela
        // guardada serve várias telas — tocando ou rolando, o motor não é consultado
        // a cada quadro.
        val fpb = WaveGrid.framesPerBucket(max(1f, m.density * 1.5f), ppf)
        val first = WaveGrid.bucketAt(TimeAxis.frameAt(visL, view, ppf, cx), fpb)
        val last = WaveGrid.bucketAt(TimeAxis.frameAt(visR, view, ppf, cx), fpb)
        val e = waves.get(waveSource, r.id, store.layers, store.thumbnailGeneration, fpb, first, last) ?: return
        val step = (fpb * ppf).toFloat()
        val audio = r.type == LayerType.Audio
        // Faixa de baixo da barra (abaixo do nome): no áudio, mais alta.
        val areaTop = top + m.trackTop * (if (audio) 0.62f else 0.8f)
        val areaBottom = top + m.bar - m.lightLine
        val mid = (areaTop + areaBottom) / 2f
        val half = (areaBottom - areaTop) / 2f
        var k = 0
        var b = first
        while (b <= last && k + 4 <= waveLines.size) {
            val v = e.at(b) / 255f * half
            val x = TimeAxis.xOf((b + 0.5) * fpb, view, ppf, cx)
            b++
            if (v < 0.5f || x < visL || x > visR) continue
            waveLines[k++] = x
            waveLines[k++] = mid - v
            waveLines[k++] = x
            waveLines[k++] = mid + v
        }
        if (k == 0) return
        // Onda no tom médio do tipo (a do áudio mais forte que a do vídeo).
        val c = tone.wave
        wavePaint.color = android.graphics.Color.argb(
            ((if (audio) 0.9f else 0.6f) * alpha * 255f).roundToInt(),
            (c.red * 255f).roundToInt(), (c.green * 255f).roundToInt(), (c.blue * 255f).roundToInt(),
        )
        wavePaint.strokeWidth = max(1f, step * 0.72f)
        canvas.drawLines(waveLines, 0, k, wavePaint)
    }

    /**
     * Instantes da linha com algum keyframe da seleção da timeline (paralelo a
     * `instants`). Só roda para as linhas da camada da seleção.
     */
    private fun pickedInstants(r: RowModel, sel: KeySelection): BooleanArray? {
        var any = false
        val out = BooleanArray(r.instants.size) { i -> sel.containsAnyOn(r.id, r.keysAt[i]).also { if (it) any = true } }
        return if (any) out else null
    }

    /**
     * Losangos da A.01: quadrado 11 girado, âmbar se escolhido; vizinhos a < 4 dp
     * viram pílula. Os da seleção da timeline ganham anel branco (e azul quando
     * não são o principal, que continua âmbar).
     */
    private fun DrawScope.drawDiamonds(
        r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float,
        compact: Boolean, selFrame: Int, dragFrame: Int, fps: Float, picked: BooleanArray? = null,
    ) {
        val cy = top + if (compact) m.diamondCyCompact else m.diamondCyNormal
        // Primeiro os dos OUTROS parâmetros, esmaecidos (160/255) e por baixo.
        if (r.dimInstants.isNotEmpty()) drawDimDiamonds(r.dimInstants, cy, w, view, ppf, cx)
        val inst = r.instants
        if (inst.isEmpty()) return
        // Lista de desenho pronta (Keyframes.visibleGroups): só o que está na tela;
        // o buffer cabe o máximo de grupos da largura (grupos distam ≥ keyMergeGap).
        val need = 2 * ((w + 2f * m.keyTouchHalf) / m.keyMergeGap).toInt() + 8
        if (groupBuf.size < need) groupBuf = IntArray(need)
        val groups = Keyframes.visibleGroups(inst, view, ppf, cx, w, m.keyTouchHalf, m.keyMergeGap, groupBuf)
        var dragX = Float.NaN
        for (g in 0 until groups) {
            val i = groupBuf[2 * g]
            val j = groupBuf[2 * g + 1]
            val kx = TimeAxis.xOf(inst[i].toDouble(), view, ppf, cx)
            val on = Keyframes.groupHas(inst, i, j, selFrame)
            var inSelection = false
            if (picked != null) for (k in i..j) if (picked[k]) { inSelection = true; break }
            if (j == i) {
                val dragging = inst[i] == dragFrame
                if (dragging) dragX = kx
                val fill = keyFill(KeyEmphasis.tone(on, inSelection, inst[i] == playheadFrame))
                drawDiamond(kx, cy, fill, on, if (dragging) m.keyDragScale else 1f, inSelection)
            } else {
                drawKeyPill(kx, TimeAxis.xOf(inst[j].toDouble(), view, ppf, cx), cy, if (inSelection) KEY_PICKED else KEY_OFF, inSelection)
                // A faixa resume chaves densas, mas nunca substitui a posição
                // temporal da chave no cabeçote, escolhida ou sendo arrastada.
                // Paint temporal boundaries first and the selected/dragged key
                // last, so adjacent dense anchors cannot cover its diamond.
                val focus = intArrayOf(r.start, r.end, playheadFrame, selFrame, dragFrame)
                for (f in focus.indices) {
                    val frame = focus[f]
                    if ((f + 1 until focus.size).any { focus[it] == frame } || !Keyframes.groupHas(inst, i, j, frame)) continue
                    val index = java.util.Arrays.binarySearch(inst, frame)
                    val keyX = TimeAxis.xOf(frame.toDouble(), view, ppf, cx)
                    val chosen = frame == selFrame
                    val keyPicked = picked?.get(index) == true
                    val dragging = frame == dragFrame
                    if (dragging) dragX = keyX
                    drawDiamond(keyX, cy, keyFill(KeyEmphasis.tone(chosen, keyPicked, frame == playheadFrame)),
                        chosen, if (dragging) m.keyDragScale else 1f, keyPicked)
                }
            }
        }
        if (!dragX.isNaN()) drawBalloon(dragX, cy, dragFrame, fps)
    }

    private fun keyFill(tone: KeyEmphasis.Tone): Color = when (tone) {
        KeyEmphasis.Tone.CHOSEN -> AureaTimeline.KeyframeOn
        KeyEmphasis.Tone.PICKED -> KEY_PICKED
        // O losango sob o cabeçote: na cor de destaque do tema.
        KeyEmphasis.Tone.PLAYHEAD -> AureaColors.Accent
        KeyEmphasis.Tone.OFF -> KEY_OFF
    }

    /**
     * Losangos dos outros parâmetros animados (painel focado noutro): a mesma
     * figura branca, numa camada a 160/255. Sem escolha, seleção nem arrasto —
     * só dizem "aqui também tem animação".
     */
    private fun DrawScope.drawDimDiamonds(inst: IntArray, cy: Float, w: Float, view: Double, ppf: Float, cx: Float) {
        val need = 2 * ((w + 2f * m.keyTouchHalf) / m.keyMergeGap).toInt() + 8
        if (dimGroupBuf.size < need) dimGroupBuf = IntArray(need)
        val groups = Keyframes.visibleGroups(inst, view, ppf, cx, w, m.keyTouchHalf, m.keyMergeGap, dimGroupBuf)
        if (groups == 0) return
        val pad = m.diamond * 2f
        drawContext.canvas.saveLayer(androidx.compose.ui.geometry.Rect(0f, cy - pad, w, cy + pad), dimPaint)
        try {
            for (g in 0 until groups) {
                val i = dimGroupBuf[2 * g]
                val j = dimGroupBuf[2 * g + 1]
                val kx = TimeAxis.xOf(inst[i].toDouble(), view, ppf, cx)
                if (j == i) drawDiamond(kx, cy, KEY_OFF, false, 1f)
                else drawKeyPill(kx, TimeAxis.xOf(inst[j].toDouble(), view, ppf, cx), cy, KEY_OFF)
            }
        } finally {
            drawContext.canvas.restore()
        }
    }

    private fun DrawScope.drawDiamond(x: Float, y: Float, fill: Color, on: Boolean, scale: Float, picked: Boolean = false) {
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
            if (picked) {
                // Anel branco POR FORA do contorno: escolhido se lê em qualquer fundo.
                val ring = pickedStroke.width
                val o = half + ring / 2f + m.diamondStroke / 2f
                drawRoundRect(
                    KEY_PICKED_RING,
                    Offset(x - o, y - o),
                    Size(o * 2f, o * 2f),
                    CornerRadius(m.diamondRadius + ring),
                    style = pickedStroke,
                )
            }
        }
    }

    private fun DrawScope.drawKeyPill(xa: Float, xb: Float, y: Float, fill: Color, picked: Boolean = false) {
        val wPill = max(xb - xa, m.keyPillMinWidth)
        val left = (xa + xb) / 2f - wPill / 2f
        val hPill = m.keyPillHeight
        drawRoundRect(fill, Offset(left, y - hPill / 2f), Size(wPill, hPill), CornerRadius(hPill / 2f))
        val b = m.diamondStroke / 2f
        drawRoundRect(
            if (picked) KEY_PICKED_RING else KEY_BORDER,
            Offset(left + b, y - hPill / 2f + b),
            Size(wPill - m.diamondStroke, hPill - m.diamondStroke),
            CornerRadius(hPill / 2f),
            style = if (picked) pickedStroke else diamondStroke,
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

    /**
     * Calha da fileira (sai a pílula olho + quadradinho): o glifo do TIPO em tom
     * muted (claro com as trilhas abertas, em destaque com a fileira no lote), o
     * OLHO pequeno e apagado no canto de baixo (riscado se oculta) e o cadeado no
     * alto quando travada.
     */
    /**
     * Calha de uma trilha: a de GRUPO mostra ▸/▾ (tocar abre/fecha os eixos),
     * mais claro; um eixo sob o grupo aberto fica sem glifo (o recuo do nome
     * já diz de quem ele é); as outras, o › apagado de sempre.
     */
    private fun DrawScope.drawLaneGutter(r: RowModel, track: TimelineTrack, top: Float, height: Float, open: Set<LaneGroupKey>) {
        val cy = top + height / 2f
        drawRect(AureaColors.EditorCanvas, Offset(0f, top), Size(m.laneLabelLeft - 2f * m.density, height))
        if (track.group) {
            val expanded = LaneGroupKey(r.id, track) in open
            drawGlyph(if (expanded) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 10f, AureaColors.Text, m.gutterIconCx, cy)
            return
        }
        val group = trackGroup(track)
        if (group != null && LaneGroupKey(r.id, group) in open) return
        drawGlyph(CupertinoGlyph.ChevronRight, 10f, AureaTimeline.GutterEye, m.gutterIconCx, cy)
    }

    /**
     * PÍLULA da fileira (mockup 2026-09-29): 78 × 28 colada à esquerda, raio 14
     * só à direita, com o olho (riscado se oculta) e o quadradinho de 22 com a
     * miniatura/tipo: "T" no texto, ponto na forma, quadro na imagem/vídeo, nota
     * no áudio, glifo nos outros. No lote a pílula ganha o contorno em destaque;
     * com as trilhas abertas, o quadradinho. Travada: cadeado na ponta.
     */
    private fun DrawScope.drawGutter(c: TimelineController, r: RowModel, top: Float, inBatch: Boolean, expanded: Boolean, fps: Float) {
        val pillTop = top - m.pillInset
        translate(0f, pillTop) {
            drawPath(pillPath, AureaColors.EditorRowPill)
            if (inBatch) drawPath(pillPath, AureaColors.Accent, style = glyphRing)
        }
        val cy = pillTop + m.pillHeight / 2f
        if (c.store.layerSelectMode) {
            // Escolhendo várias camadas: o olho dá lugar ao ✓ das escolhidas.
            if (r.id in c.store.selection) drawGlyph(CupertinoGlyph.CheckmarkCircleFill, m.gutterEye, AureaColors.Accent, m.gutterEyeCx, cy)
        } else drawGlyph(
            if (r.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
            m.gutterEye,
            if (r.visible) AureaTimeline.GutterEye else AureaTimeline.GutterIcon,
            m.gutterEyeCx,
            cy,
        )
        // Máscara de recorte: o quadradinho encolhe e encosta à direita, "pendurado"
        // na camada de baixo, com o "↳" na frente dele.
        val clipped = r.clipToBelow
        val box = if (clipped) m.glyphBox * CLIP_BOX_SCALE else m.glyphBox
        val bl = if (clipped) m.glyphBoxLeft + m.glyphBox - box else m.glyphBoxLeft
        val bt = cy - box / 2f
        if (clipped) {
            val layout = glyphClip ?: measurer.measure("\u21B3", clipGlyphStyle).also { glyphClip = it }
            drawText(layout, topLeft = Offset(bl - layout.size.width - m.density, cy - layout.size.height / 2f))
        }
        val radius = CornerRadius(m.glyphBoxRadius)
        val boxAlpha = if (r.visible) 1f else AureaTimeline.HiddenAlpha
        drawRoundRect(AureaColors.EditorGlyphBox, Offset(bl, bt), Size(box, box), radius)
        val bcx = bl + box / 2f
        when (r.type) {
            LayerType.Text -> {
                val layout = glyphT ?: measurer.measure("T", glyphTextStyle).also { glyphT = it }
                drawText(layout, alpha = boxAlpha, topLeft = Offset(bcx - layout.size.width / 2f, cy - layout.size.height / 2f))
            }
            LayerType.Shape -> drawCircle(AureaTimeline.GlyphShape.copy(alpha = boxAlpha), m.glyphDot / 2f, Offset(bcx, cy))
            LayerType.Audio -> drawGlyph(CupertinoGlyph.MusicNote, m.glyphIcon, AureaTimeline.GlyphAudio.copy(alpha = boxAlpha), bcx, cy)
            LayerType.Video, LayerType.Image -> {
                if (!drawPillThumb(c, r, bl, bt, box, fps, boxAlpha)) {
                    drawRoundRect(AureaTimeline.tone(r.type).body, Offset(bl, bt), Size(box, box), radius, alpha = boxAlpha)
                }
            }
            else -> drawGlyph(r.type.glyph, m.glyphIcon, AureaTimeline.GutterIcon.copy(alpha = boxAlpha), bcx, cy)
        }
        if (expanded) {
            val s = glyphRing.width
            drawRoundRect(AureaColors.Accent, Offset(bl + s / 2f, bt + s / 2f), Size(box - s, box - s), radius, style = glyphRing)
        }
        if (r.locked) drawGlyph(CupertinoGlyph.LockFill, m.gutterLock, AureaTimeline.GutterIcon, m.gutterLockCx, cy)
    }

    /**
     * Miniatura da camada no quadradinho da pílula, recortada no centro. Usa a
     * mesma tira em cache da barra (mesma altura, mesmo balde do começo da
     * mídia): não custa pergunta extra ao motor quando a barra já está à vista.
     */
    private fun DrawScope.drawPillThumb(c: TimelineController, r: RowModel, left: Float, top: Float, box: Float, fps: Float, alpha: Float): Boolean {
        val heightPx = m.bar.roundToInt().coerceIn(1, THUMB_MAX_PX)
        val image = r.type == LayerType.Image
        val bucket = if (image) -1 else Thumbs.bucketOf(r.offset.toDouble(), fps)
        val frame = if (image) r.start else Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), r.start, r.offset)
        val bmp = thumbs.get(c.store.thumbnails, r.id, bucket, frame, heightPx, c.store.thumbnailGeneration) ?: return false
        val bw = bmp.width
        val bh = bmp.height
        if (bw <= 0 || bh <= 0) return false
        val side = min(bw, bh)
        glyphSrc.set((bw - side) / 2, (bh - side) / 2, (bw + side) / 2, (bh + side) / 2)
        rectF.set(left, top, left + box, top + box)
        val canvas = drawContext.canvas.nativeCanvas
        barClip.reset()
        barClip.addRoundRect(rectF, m.glyphBoxRadius, m.glyphBoxRadius, android.graphics.Path.Direction.CW)
        canvas.save()
        canvas.clipPath(barClip)
        bitmapPaint.alpha = (alpha * 255f).roundToInt()
        canvas.drawBitmap(bmp, glyphSrc, rectF, bitmapPaint)
        canvas.restore()
        return true
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
                    minorPath.lineTo(xm, m.tickMinorBottom)
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
                    minorPath.lineTo(x, m.tickMinorBottom)
                }
                f++
            }
        }
        drawPath(minorPath, AureaTimeline.TickMinor, style = minorStroke)
        drawPath(majorPath, AureaTimeline.TickMajor, style = majorStroke)
    }

    /** Markers belong to the ruler; only the playhead crosses the clip area. */
    private fun DrawScope.drawMarkers(mk: com.aurea.aurea.state.EditorStore.Markers, w: Float, view: Double, ppf: Float, cx: Float) {
        if (mk.size == 0) return
        val half = m.tickBottom * 0.28f
        for (i in 0 until mk.size) {
            val x = TimeAxis.xOf(mk.frames[i].toDouble(), view, ppf, cx)
            // 0.3.2: o nome da marca à direita do triângulo, cortado antes da
            // próxima marca (ou da borda). Desenha mesmo com a marca à esquerda
            // da tela, se o nome ainda aparece.
            val label = mk.labels.getOrNull(i).orEmpty()
            if (label.isNotEmpty()) {
                val nextX = if (i + 1 < mk.size) TimeAxis.xOf(mk.frames[i + 1].toDouble(), view, ppf, cx) else w
                val left = x + half + 3f * m.density
                val avail = min(nextX - half, w) - left - 2f * m.density
                if (avail >= MARKER_NAME_MIN_DP * m.density && left < w) drawMarkerName(label, left, avail)
            }
            if (x < -half || x > w + half) continue
            val beat = mk.kinds[i] == 1
            // 0.3.3 recursos: a cor com a correção da área 03 (R no byte baixo; as
            // marcas gravadas pela 0.3.2 com literais ARGB crus aparecem certas).
            val color = Color(MarkerColors.display(mk.colors[i]))
            val s = max(if (beat) half * 0.7f else half, 3.5f * m.density)
            markerPath.reset()
            markerPath.moveTo(x - s, 0f)
            markerPath.lineTo(x + s, 0f)
            markerPath.lineTo(x, s * 1.4f)
            markerPath.close()
            drawPath(markerPath, color)
            drawLine(color, Offset(x, s * 1.4f), Offset(x, m.tickBottom), strokeWidth = m.density)
        }
    }
    private val markerPath = androidx.compose.ui.graphics.Path()
    private val markerNameStyle = TextStyle(
        color = Color.White,
        fontSize = 10.sp,
        fontWeight = FontWeight.W600,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    /** Layouts dos nomes por (texto, largura em degraus): a régua redesenha a cada quadro. */
    private val markerNames = HashMap<Pair<String, Int>, androidx.compose.ui.text.TextLayoutResult>()

    private fun DrawScope.drawMarkerName(label: String, left: Float, avail: Float) {
        val step = NAME_STEP_DP * m.density
        val bucket = max(1, (avail / step).toInt())
        val key = label to bucket
        val layout = markerNames[key] ?: measurer.measure(
            text = label,
            style = markerNameStyle,
            overflow = TextOverflow.Ellipsis,
            softWrap = false,
            maxLines = 1,
            constraints = Constraints(maxWidth = max(1, (bucket * step).toInt())),
        ).also { if (markerNames.size > 256) markerNames.clear(); markerNames[key] = it }
        drawText(layout, alpha = 0.85f, topLeft = Offset(left, 1f * m.density))
    }

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
        drawRect(Color.White, Offset(cx - total / 2f, m.underlineTop), Size(total, m.underlineHeight))
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
        val cacheKey = if (r.track == null) r.id else r.id xor (r.track.hashCode().toLong() shl 32)
        val cached = names.get(cacheKey)
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
        names.put(cacheKey, NameLayout(r.name, bucket, layout))
        return layout
    }

    /**
     * Glifo da fonte CupertinoIcons numa caixa N×N (como `CupertinoIcon`), centrado em (cx, cy).
     * 0.3.4: no estilo do app (cheio → contorno, `AureaIconStyle`), desenhado uma vez.
     */
    private fun DrawScope.drawGlyph(glyph: Char, sizeDp: Float, tint: Color, cx: Float, cy: Float) {
        val shown = AureaIconStyle.glyph(glyph)
        val key = shown.code * 64 + sizeDp.roundToInt()
        var layout = glyphs.get(key)
        if (layout == null) {
            val fontSize = (sizeDp / m.fontScale).sp
            layout = measurer.measure(
                shown.toString(),
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

    // =================================================================================
    // Omega 0.3.2 (área 03): a timeline da referência, com o desenho do AUREA.
    // Ordem de pintura (P0-2): linhas → cabeçalhos e alças ≡ → régua opaca →
    // tiques, marcadores, aba do tempo e cursor.
    // =================================================================================
    /** Pílula do cabeçalho: x −8..62 (só 62 à vista), ponta direita redonda. */
    private val omegaPill = Path().apply {
        addRoundRect(
            androidx.compose.ui.geometry.RoundRect(
                m.omegaPillLeft, 0f, m.headerColumn, m.pillHeight,
                topLeftCornerRadius = CornerRadius.Zero,
                topRightCornerRadius = CornerRadius(m.pillHeight / 2f),
                bottomRightCornerRadius = CornerRadius(m.pillHeight / 2f),
                bottomLeftCornerRadius = CornerRadius.Zero,
            ),
        )
    }
    private val omegaPillStroke = Stroke(m.omegaPillBorder)
    private val omegaTabStroke = Stroke(m.omegaTabStroke)
    private val omegaKeyStroke = Stroke(m.omegaKeyStroke, join = StrokeJoin.Round)
    private val omegaKeyAltStroke = Stroke(m.omegaKeyAltStroke, join = StrokeJoin.Round)
    private val omegaKeyRingStroke = Stroke(m.omegaKeyRing, join = StrokeJoin.Round)
    private val omegaHandleStroke = Stroke(m.omegaHandleStroke, cap = StrokeCap.Round, join = StrokeJoin.Round)
    private val omegaTickStroke = Stroke(m.omegaTick)
    /** Tiques por classe: 0 quadro, 1 meio segundo, 2 segundo. */
    private val omegaTicks = arrayOf(Path(), Path(), Path())
    private val omegaTabPath = Path()
    private val omegaFlag = Path()
    private val omegaTriangle = Path()
    private val omegaHandlePath = Path()
    private val omegaChevron = Path()
    private val omegaLayerPaint = androidx.compose.ui.graphics.Paint()
    /** Losango arredondado (2:3) por altura em px (poucos tamanhos: em edição, outros, empilhado). */
    private val omegaDiamonds = HashMap<Int, Path>()
    private val omegaNames = LongSparseArray<NameLayout>()
    private val omegaLabelStyle = TextStyle(
        fontSize = 11.sp,
        fontWeight = FontWeight.W500,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
    )
    /** As quatro aparências dos marcadores em ARGB (para achar a de cada marca). */
    private val omegaRoles = IntArray(AureaTimeline.Bookmarks.size) { AureaTimeline.Bookmarks[it].toArgb() }

    private fun markerColor(packed: Int): Color =
        AureaTimeline.Bookmarks[MarkerColors.appearance(packed, omegaRoles).coerceIn(0, AureaTimeline.Bookmarks.lastIndex)]

    private fun DrawScope.drawOmega(c: TimelineController) {
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
        st.redrawTick   // lido: as miniaturas que não couberam neste quadro pedem o próximo
        thumbs.beginFrame(THUMB_QUERIES_PER_FRAME)
        frameNanos = System.nanoTime()
        playheadFrame = store.playhead
        if (n > 0) omegaRows(c, rows, n, w, h, view, ppf, cx, fps, compact)
        // A régua é opaca: cobre os clipes que rolam para cima dela.
        drawRect(AureaColors.EditorCanvas, Offset.Zero, Size(w, m.rowsTop))
        omegaRuler(st, w, view, ppf, cx, fps)
        val buffer = 3f * m.density
        for (range in store.previewBufferRanges) {
            val left = TimeAxis.xOf(range.startFrame.toDouble(), view, ppf, cx).coerceAtLeast(0f)
            val right = TimeAxis.xOf(range.endFrame.toDouble(), view, ppf, cx).coerceAtMost(w)
            if (right > left) drawRect(PREVIEW_BUFFER, Offset(left, m.rowsTop - buffer), Size(right - left, buffer))
        }
        val mk = store.markers
        // 0.3.3 recursos: o marcador antigo da 0.3.2 na régua — triângulo na cor da
        // marca, fio até os tiques e o NOME à direita (cortado antes da próxima). A
        // bandeirola, o contorno da aba e o cursor na cor da marca saíram.
        drawMarkers(mk, w, view, ppf, cx)
        omegaTimeTab(cx, store.playhead, fps, false)
        if (thumbs.starved) c.requestRedraw()
        c.settleFades(frameNanos)
        // Pulso de 130 ms do clipe levantado: mais quadros enquanto ele dura.
        if (st.movingIds.isNotEmpty() && (frameNanos - st.liftStartNanos) / 1_000_000L < LiftPulse.DURATION_MS) c.requestRedraw()
        // Cursor de 1,5 dp, interrompido na aba; a 50 % movendo.
        val playheadX = TimeAxis.xOf(store.playhead.toDouble(), view, ppf, cx)
        var color = AureaColors.Playhead
        if (st.moveOrigStart != Snap.NONE) color = color.copy(alpha = color.alpha * 0.5f)
        val half = m.playhead / 2f
        drawRect(color, Offset(playheadX - half, 0f), Size(m.playhead, m.omegaTabTop + m.density))
        val resume = m.omegaTabTop + m.omegaTabHeight - m.density
        drawRect(color, Offset(playheadX - half, resume), Size(m.playhead, max(0f, h - resume)))
    }

    /** A câmera principal: a 1ª câmera visível de cima para baixo (P2-2; o AUREA não expõe outra). */
    private fun omegaMainCamera(rows: List<RowModel>): Long {
        for (r in rows) if (r.track == null && r.type == LayerType.Camera && r.visible) return r.id
        return TimelineController.NO_ID
    }

    private fun DrawScope.omegaRows(
        c: TimelineController, rows: List<RowModel>, n: Int, w: Float, h: Float,
        view: Double, ppf: Float, cx: Float, fps: Float, compact: Boolean,
    ) {
        val st = c.state
        val store = c.store
        val scroll = c.clampedScroll(n)
        val first = max(0, c.rowIndexAt(scroll))
        val last = min(n - 1, c.rowIndexAt(scroll + h - m.rowsTop))
        val preview = if (st.reorderSource >= 0 && !compact) Reorder.preview(
            FloatArray(n + 1) { c.rowTop(it) }, timelineGroupKeys(rows, byLine = false),
            st.reorderSource, st.reorderTarget, st.reorderTop - m.rowsTop + scroll,
        ) else null
        val indices = if (preview != null) 0 until n else first..last
        val passes = if (preview != null) 0..1 else 0..0
        val cache = store.thumbnails
        val generation = store.thumbnailGeneration
        val multi = omegaMultiSelection(store.layerSelectMode, c.selectionSize())
        val hideEyes = multi && c.selectionSize() > 0
        val camera = if (compact) TimelineController.NO_ID else omegaMainCamera(rows)
        fun topOf(i: Int): Float = m.rowsTop + c.rowTop(i) - scroll + (preview?.offsets?.get(i) ?: 0f)

        // 1) Fundo da linha da câmera principal (vermelho 10 %).
        if (camera != TimelineController.NO_ID) for (i in indices) {
            val r = c.rowAt(rows, i) ?: continue
            if (r.track != null || r.id != camera) continue
            val top = topOf(i)
            val rh = c.rowHeight(r)
            if (top + rh < m.rowsTop || top > h) continue
            drawRect(AureaTimeline.MainCameraRow, Offset(0f, top), Size(w, rh))
        }
        // 2) Os clipes; a linha arrastada (reordenar) por último, sobre o fundo dela.
        for (pass in passes) for (i in indices) {
            val lifted = preview != null && i in preview.sourceStart until preview.sourceEnd
            if (preview != null && lifted != (pass == 1)) continue
            val r = c.rowAt(rows, i) ?: continue
            val top = topOf(i)
            val rh = c.rowHeight(r)
            if (top + rh < m.rowsTop || top > h) continue
            if (lifted) drawRect(AureaTimeline.OmegaDragRow, Offset(0f, top), Size(w, rh))
            val fade = c.rowAlpha(r, frameNanos)
            if (fade < 1f) beginFade(fade, top, rh, w)
            if (r.track != null) omegaLane(c, r, top, w, view, ppf, cx, fps)
            else for (s in r.segments) omegaClip(c, s, top, w, view, ppf, cx, fps, compact, cache, generation)
            if (fade < 1f) drawContext.canvas.restore()
        }
        // 3) Cabeçalhos e alças ≡ por cima: as barras passam por baixo deles.
        for (pass in passes) for (i in indices) {
            val lifted = preview != null && i in preview.sourceStart until preview.sourceEnd
            if (preview != null && lifted != (pass == 1)) continue
            val r = c.rowAt(rows, i) ?: continue
            val top = topOf(i)
            val rh = c.rowHeight(r)
            if (top + rh < m.rowsTop || top > h) continue
            val fade = c.rowAlpha(r, frameNanos)
            if (fade < 1f) beginFade(fade, top, rh, w)
            val track = r.track
            if (track != null) {
                drawLaneGutter(r, track, top, rh, c.openGroups.value)
            } else {
                val activated = !compact && multi && c.isSelected(r.id)
                omegaHeader(c, r, top, activated, hideEyes && !compact, fps)
                if (!compact) omegaGrip(top, rh, w)
            }
            if (fade < 1f) drawContext.canvas.restore()
        }
        // 4) Retângulo da seleção de losangos (do AUREA) e fio do ímã.
        if (st.boxActive) {
            val xa = TimeAxis.xOf(st.boxFrame0, view, ppf, cx)
            val xb = TimeAxis.xOf(st.boxFrame1, view, ppf, cx)
            val ya = m.rowsTop + st.boxY0 - scroll
            val yb = m.rowsTop + st.boxY1 - scroll
            val topLeft = Offset(min(xa, xb), min(ya, yb))
            val boxSize = Size(abs(xb - xa), abs(yb - ya))
            drawRect(AureaColors.Accent.copy(alpha = 0.14f), topLeft, boxSize)
            drawRect(AureaColors.Accent, topLeft, boxSize, style = Stroke(m.guide))
        }
        val g = st.guideFrame
        if (g != Snap.NONE) {
            val gx = TimeAxis.xOf(g.toDouble(), view, ppf, cx)
            if (gx >= m.headerColumn && gx <= w) {
                drawRect(AureaColors.Accent, Offset(gx - m.guide / 2f, m.rowsTop), Size(m.guide, h - m.rowsTop))
            }
        }
    }

    /** Trilha de propriedade aberta (só do AUREA, D-1): nome e losangos editáveis. */
    private fun DrawScope.omegaLane(c: TimelineController, r: RowModel, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float) {
        val laneH = LANE_HEIGHT_DP * m.density
        val cy = top + laneH / 2f
        drawLine(AureaTimeline.OmegaHeaderBorder, Offset(m.headerColumn, top + laneH), Offset(w, top + laneH))
        val labelWidth = min(w - m.laneLabelLeft - 8f * m.density, 240f * m.density)
        if (labelWidth > 0f) {
            val label = nameLayout(r, labelWidth)
            drawText(label, alpha = if (r.track?.group == true) 0.9f else 0.6f, topLeft = Offset(m.laneLabelLeft, cy - label.size.height / 2f))
        }
        omegaKeys(c, r, cy, w, view, ppf, cx, fps, editing = true, editH = m.omegaKeyAlt * 1.2f, altH = m.omegaKeyAlt)
    }

    /** Cartão do clipe (P0-4): degradê por tipo e id, zebra, tira, onda, etiqueta e rótulo. */
    private fun DrawScope.omegaClip(
        c: TimelineController, r: RowModel, rowTop: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        compact: Boolean, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
    ) {
        val st = c.state
        val store = c.store
        val top = rowTop + m.barTop
        val selected = c.isSelected(r.id)
        // Legenda (só do AUREA): os blocos dela, como antes.
        if (store.captionTracks.any { it.layer == r.id }) {
            drawRow(r, top, w, view, ppf, cx, fps, compact, selected, false, false, Snap.NONE, Snap.NONE, cache, generation, null, keysShown = false)
            return
        }
        // Prévia da alça de velocidade: a largura nova enquanto o dedo arrasta.
        val stretching = st.stretchId == r.id && st.stretchStart != Snap.NONE
        val startF = if (stretching) st.stretchStart else r.start
        val endF = if (stretching) st.stretchEnd else r.end
        val x0 = TimeAxis.xOf(startF.toDouble(), view, ppf, cx)
        val x1 = TimeAxis.xOf(endF.toDouble(), view, ppf, cx)
        val pad = m.barRadius * 2f
        if (x1 >= -pad && x0 <= w + pad) {
            val left = max(x0, -pad)
            val right = min(x1, w + pad)
            val bottom = top + m.bar
            val radius = ClipGeometry.cornerRadius(right - left, m.bar, m.barRadius)
            val moving = st.isMoving(r.id)
            val pulse = if (moving) LiftPulse.scale((frameNanos - st.liftStartNanos) / 1_000_000L) else 1f
            // Sombra: leve sempre; mais forte e espalhada com o clipe levantado.
            if (moving) {
                val layer = AureaTimeline.ClipLiftShadow.copy(alpha = AureaTimeline.ClipLiftShadow.alpha / 3f)
                for (k in 1..3) {
                    val g = m.omegaLiftShadow * k / 3f
                    drawRoundRect(layer, Offset(left - g / 2f, top - g / 4f + m.omegaShadow * 2f), Size(right - left + g, m.bar + g / 2f), CornerRadius(radius + g / 2f))
                }
            } else {
                drawRoundRect(AureaTimeline.ClipShadow, Offset(left, top + m.omegaShadow), Size(right - left, m.bar), CornerRadius(radius))
            }
            withTransform({ scale(pulse, pulse, Offset((left + right) / 2f, top + m.bar / 2f)) }) {
                val canvas = drawContext.canvas.nativeCanvas
                rectF.set(left, top, right, bottom)
                barClip.reset()
                barClip.addRoundRect(rectF, radius, radius, android.graphics.Path.Direction.CW)
                canvas.save()
                canvas.clipPath(barClip)
                // 1. 0.3.4 (bugs-034, cores): cor SÓLIDA do tipo, um pouco dessaturada (ClipColors.solid).
                drawRect(Color(ClipColors.solid(r.type.color.toArgb())), Offset(left, top), Size(right - left, m.bar))
                // 2. Zebra de quadros.
                omegaZebra(startF, top, left, right, view, ppf, cx)
                // 3. Tira de miniaturas, sem véu nem divisórias.
                bitmapPaint.alpha = 255
                if (r.hasThumbs) drawThumbs(canvas, r, top, x0, x1, w, view, ppf, cx, fps, cache, generation, dividers = false)
                // 4. Onda de 1 dp.
                if (r.type == LayerType.Audio || r.type == LayerType.Video) omegaWave(canvas, r, top, x0, x1, w, view, ppf, cx)
                // 0.3.4 (bugs-034, cores): realce sutil no topo do cartão (brilho que some até 45 % da
                // altura e um fio de 1 dp), por cima das miniaturas e da onda.
                drawRect(
                    Brush.verticalGradient(listOf(AureaTimeline.ClipSheen, Color.Transparent), startY = top, endY = top + m.bar * AureaTimeline.ClipSheenDepth),
                    Offset(left, top), Size(right - left, m.bar * AureaTimeline.ClipSheenDepth),
                )
                drawRect(AureaTimeline.ClipSheenLine, Offset(left, top), Size(right - left, m.omegaSheenLine))
                // 5. Triângulo da etiqueta no canto de cima à esquerda (catetos de meia barra).
                val tag = ShellColors.LabelPalette.getOrNull(r.label - 1)
                if (tag != null) {
                    val leg = m.bar / 2f
                    omegaTriangle.reset()
                    omegaTriangle.moveTo(x0, top)
                    omegaTriangle.lineTo(x0 + leg, top)
                    omegaTriangle.lineTo(x0, top + leg)
                    omegaTriangle.close()
                    drawPath(omegaTriangle, tag)
                }
                // 6. Rótulo de 11 sp na borda do clipe (14 dp para dentro), reticências no meio.
                omegaLabel(r, top, x0, x1)
                canvas.restore()
            }
            // Escolhida na visão geral: o contorno branco de 2 dp do AUREA.
            if (selected && !compact) {
                clipRect(left, top, right, bottom) {
                    val stroke = ClipGeometry.strokeWidth(right - left, m.bar, m.selStroke)
                    if (right - left <= stroke) {
                        drawRect(AureaTimeline.ClipSelected, Offset(left, top), Size(right - left, m.bar))
                    } else {
                        drawRoundRect(
                            AureaTimeline.ClipSelected,
                            Offset(left + stroke / 2f, top + stroke / 2f),
                            Size(right - left - stroke, m.bar - stroke),
                            CornerRadius(ClipGeometry.cornerRadius(right - left - stroke, m.bar - stroke, m.barRadius - stroke / 2f)),
                            style = selStroke,
                        )
                    }
                }
            }
            // Alças do foco (P1-7; com o painel Velocidade, as de velocidade, P1-8).
            if (compact && selected && c.selectionSize() == 1 && !r.locked) omegaHandles(st, x0, x1, top, w)
        }
        // Losangos (P0-6): em edição na camada escolhida; os outros, pequenos e a 160/255.
        if (KeyframeVisibility.visible(store.showAllKeyframes, false, selected)) {
            val editing = KeyframeVisibility.editable(selected, store.keySelectMode) && c.selectionSize() < 2
            omegaKeys(c, r, top + m.bar / 2f, w, view, ppf, cx, fps, editing, m.omegaKeyEdit, m.omegaKeyAlt)
        }
    }

    /** Zebra de quadros: faixas brancas de meio quadro, uma sim e uma não (dobra até ter 4 dp). */
    private fun DrawScope.omegaZebra(startF: Int, top: Float, left: Float, right: Float, view: Double, ppf: Float, cx: Float) {
        if (!(ppf > 0f)) return
        var band = 0.5
        while (band * ppf < m.omegaZebraMin && band < 1e7) band *= 2.0
        val bandPx = (band * ppf).toFloat()
        val fromK = max(0L, floor((TimeAxis.frameAt(left, view, ppf, cx) - startF) / band).toLong())
        val toK = ceil((TimeAxis.frameAt(right, view, ppf, cx) - startF) / band).toLong()
        var k = fromK + (fromK and 1L)
        var count = 0
        while (k <= toK && count < ZEBRA_MAX) {
            val xa = TimeAxis.xOf(startF + k * band, view, ppf, cx)
            drawRect(AureaTimeline.ClipZebra, Offset(xa, top), Size(bandPx, m.bar))
            k += 2
            count++
        }
    }

    /** Onda (P0-4): barras de 1 dp centradas; no áudio a barra toda, no vídeo a faixa de baixo. */
    private fun omegaWave(
        canvas: android.graphics.Canvas, r: RowModel, top: Float, x0: Float, x1: Float, w: Float,
        view: Double, ppf: Float, cx: Float,
    ) {
        val store = waveStore ?: return
        val visL = max(x0, 0f)
        val visR = min(x1, w)
        if (visR <= visL || ppf <= 0f) return
        val fpb = WaveGrid.framesPerBucket(max(1f, m.density * 1.5f), ppf)
        val first = WaveGrid.bucketAt(TimeAxis.frameAt(visL, view, ppf, cx), fpb)
        val last = WaveGrid.bucketAt(TimeAxis.frameAt(visR, view, ppf, cx), fpb)
        val e = waves.get(waveSource, r.id, store.layers, store.thumbnailGeneration, fpb, first, last) ?: return
        val audio = r.type == LayerType.Audio
        val areaTop = if (audio) top else top + m.bar * 0.6f
        val areaBottom = top + m.bar
        val mid = (areaTop + areaBottom) / 2f
        val half = (areaBottom - areaTop) / 2f
        var k = 0
        var b = first
        while (b <= last && k + 4 <= waveLines.size) {
            val v = e.at(b) / 256f * half
            val x = TimeAxis.xOf((b + 0.5) * fpb, view, ppf, cx)
            b++
            if (v < 0.5f || x < visL || x > visR) continue
            waveLines[k++] = x
            waveLines[k++] = mid - v
            waveLines[k++] = x
            waveLines[k++] = mid + v
        }
        if (k == 0) return
        wavePaint.color = AureaTimeline.ClipWave.toArgb()
        wavePaint.strokeWidth = m.omegaWave
        canvas.drawLines(waveLines, 0, k, wavePaint)
    }

    /** Rótulo do clipe (D-3): na borda do clipe, não acompanha a rolagem. */
    private fun DrawScope.omegaLabel(r: RowModel, top: Float, x0: Float, x1: Float) {
        if (r.name.isEmpty()) return
        val avail = (x1 - x0) - 2f * m.omegaLabelInset
        if (avail < NAME_MIN_DP * m.density) return
        val layout = omegaNameLayout(r, avail) ?: return
        // 0.3.4 (bugs-034, cores): branco em todos os tipos, com sombra curta (legível sobre as miniaturas).
        drawText(
            layout, color = AureaTimeline.ClipLabel,
            topLeft = Offset(x0 + m.omegaLabelInset, top + m.bar / 2f - layout.size.height / 2f),
            shadow = Shadow(AureaTimeline.ClipLabelShadow, Offset(0f, m.omegaShadow), m.omegaLabelShadow),
        )
    }

    /** Nome com reticências no MEIO na largura disponível (em degraus de 12 dp). */
    private fun omegaNameLayout(r: RowModel, availPx: Float): TextLayoutResult? {
        val step = NAME_STEP_DP * m.density
        val bucket = (availPx / step).toInt()
        if (bucket <= 0) return null
        val cached = omegaNames.get(r.id)
        if (cached != null && cached.bucket == bucket && cached.name == r.name) return cached.layout
        val limit = bucket * step
        fun measure(text: String) = measurer.measure(text, omegaLabelStyle, softWrap = false, maxLines = 1)
        val fitted = MiddleEllipsis.fit(r.name) { measure(it).size.width <= limit }
        if (fitted.isEmpty()) return null
        val layout = measure(fitted)
        if (omegaNames.size() > NAME_CACHE) omegaNames.clear()
        omegaNames.put(r.id, NameLayout(r.name, bucket, layout))
        return layout
    }

    /** Losango arredondado de altura [h] px e largura 2/3, centrado na origem. */
    private fun omegaDiamond(h: Float): Path {
        val key = (h * 4f).roundToInt()
        return omegaDiamonds.getOrPut(key) {
            val hh = h / 2f
            val hw = h / 3f
            val px = floatArrayOf(0f, hw, 0f, -hw)
            val py = floatArrayOf(-hh, 0f, hh, 0f)
            val k = 0.22f
            Path().apply {
                for (i in 0 until 4) {
                    val p = (i + 3) % 4
                    val q = (i + 1) % 4
                    val inX = px[i] + (px[p] - px[i]) * k
                    val inY = py[i] + (py[p] - py[i]) * k
                    val outX = px[i] + (px[q] - px[i]) * k
                    val outY = py[i] + (py[q] - py[i]) * k
                    if (i == 0) moveTo(inX, inY) else lineTo(inX, inY)
                    quadraticTo(px[i], py[i], outX, outY)
                }
                close()
            }
        }
    }

    /** Losango dos outros parâmetros (e de toda camada fora de edição): branco a 160/255, traço de 0,5 dp. */
    private fun DrawScope.omegaAltKey(x: Float, cy: Float, h: Float) {
        val path = omegaDiamond(h)
        translate(x, cy) {
            drawPath(path, AureaTimeline.KeyAlt)
            drawPath(path, AureaTimeline.KeyAltStroke, style = omegaKeyAltStroke)
        }
    }

    /**
     * Losango em edição: branco com traço preto de 1 dp. Realçado (escolhido, na
     * seleção ou sob o cabeçote): 85 % com o contorno em destaque por trás.
     * Empilhado (vários parâmetros, ou só parte deles, no instante): dois a ±20 %
     * da altura, o de baixo cinza no parcial. No arrasto: só o contorno.
     */
    private fun DrawScope.omegaEditKey(x: Float, cy: Float, h: Float, highlighted: Boolean, dragging: Boolean, stacked: Boolean, partial: Boolean) {
        if (dragging) {
            translate(x, cy) { drawPath(omegaDiamond(h), AureaTimeline.KeyFill, style = omegaKeyAltStroke) }
            return
        }
        if (highlighted) translate(x, cy) { drawPath(omegaDiamond(h), AureaColors.Accent, style = omegaKeyRingStroke) }
        val s = if (highlighted) 0.85f else 1f
        if (stacked) {
            val small = omegaDiamond(h * 0.75f * s)
            val dy = h * 0.2f
            translate(x, cy + dy) {
                drawPath(small, if (partial) AureaTimeline.KeyPartial else AureaTimeline.KeyFill)
                drawPath(small, AureaTimeline.KeyStroke, style = omegaKeyStroke)
            }
            translate(x, cy - dy) {
                drawPath(small, AureaTimeline.KeyFill)
                drawPath(small, AureaTimeline.KeyStroke, style = omegaKeyStroke)
            }
        } else {
            val path = omegaDiamond(h * s)
            translate(x, cy) {
                drawPath(path, AureaTimeline.KeyFill)
                drawPath(path, AureaTimeline.KeyStroke, style = omegaKeyStroke)
            }
        }
    }

    /** Instantes densos (a menos de 4 dp) viram uma pílula no estilo da linha (do AUREA). */
    private fun DrawScope.omegaKeyPill(xa: Float, xb: Float, cy: Float, h: Float, editing: Boolean) {
        val ph = h * 2f / 3f
        val wPill = max(xb - xa, ph)
        val left = (xa + xb) / 2f - wPill / 2f
        val corner = CornerRadius(ph / 2f)
        drawRoundRect(if (editing) AureaTimeline.KeyFill else AureaTimeline.KeyAlt, Offset(left, cy - ph / 2f), Size(wPill, ph), corner)
        drawRoundRect(
            if (editing) AureaTimeline.KeyStroke else AureaTimeline.KeyAltStroke,
            Offset(left, cy - ph / 2f), Size(wPill, ph), corner,
            style = if (editing) omegaKeyStroke else omegaKeyAltStroke,
        )
    }

    /** Quantas trilhas diferentes têm keyframe no instante (sem alocar). */
    private fun distinctTracks(keys: List<com.aurea.aurea.engine.KeyframeRow>): Int {
        var n = 0
        for (i in keys.indices) {
            val k = keys[i]
            var seen = false
            for (j in 0 until i) {
                val o = keys[j]
                if (o.property == k.property && o.effectIndex == k.effectIndex && o.paramIndex == k.paramIndex) {
                    seen = true
                    break
                }
            }
            if (!seen) n++
        }
        return n
    }

    /** Os losangos de uma linha (P0-6), centrados em [cy]. */
    private fun DrawScope.omegaKeys(
        c: TimelineController, r: RowModel, cy: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        editing: Boolean, editH: Float, altH: Float,
    ) {
        val store = c.store
        val st = c.state
        // Os dos outros parâmetros (sem toque) primeiro, sempre pequenos.
        val dim = r.dimInstants
        if (dim.isNotEmpty()) {
            val need = 2 * ((w + 2f * m.keyTouchHalf) / m.keyMergeGap).toInt() + 8
            if (dimGroupBuf.size < need) dimGroupBuf = IntArray(need)
            val groups = Keyframes.visibleGroups(dim, view, ppf, cx, w, m.keyTouchHalf, m.keyMergeGap, dimGroupBuf)
            for (g in 0 until groups) {
                val i = dimGroupBuf[2 * g]
                val j = dimGroupBuf[2 * g + 1]
                val xa = TimeAxis.xOf(dim[i].toDouble(), view, ppf, cx)
                if (i == j) omegaAltKey(xa, cy, altH)
                else omegaKeyPill(xa, TimeAxis.xOf(dim[j].toDouble(), view, ppf, cx), cy, altH, false)
            }
        }
        val inst = r.instants
        if (inst.isEmpty()) return
        val selKey = store.selectedKeyframe
        val selFrame = if (editing && selKey != null && selKey.first == r.id) r.selectedFrame(selKey.second) else Snap.NONE
        val dragFrame = if (st.dragKeyLayer == r.id) st.dragKeyFrame else Snap.NONE
        val keySel = store.keySelection
        val picked = if (editing && keySel != null && keySel.on(r.id).isNotEmpty()) pickedInstants(r, keySel) else null
        val focusCount = if (editing && r.track == null && r.id == store.primary) store.timelineFocus?.size ?: 0 else 0
        val need = 2 * ((w + 2f * m.keyTouchHalf) / m.keyMergeGap).toInt() + 8
        if (groupBuf.size < need) groupBuf = IntArray(need)
        val groups = Keyframes.visibleGroups(inst, view, ppf, cx, w, m.keyTouchHalf, m.keyMergeGap, groupBuf)
        var dragX = Float.NaN
        for (g in 0 until groups) {
            val i = groupBuf[2 * g]
            val j = groupBuf[2 * g + 1]
            val xa = TimeAxis.xOf(inst[i].toDouble(), view, ppf, cx)
            if (j == i) {
                val f = inst[i]
                if (!editing) {
                    omegaAltKey(xa, cy, altH)
                    continue
                }
                val dragging = f == dragFrame
                if (dragging) dragX = xa
                val highlighted = f == selFrame || picked?.get(i) == true || f == playheadFrame
                val distinct = if (focusCount >= 2) distinctTracks(r.keysAt[i]) else 1
                val stacked = focusCount >= 2 && (distinct >= 2 || distinct < focusCount)
                omegaEditKey(xa, cy, editH, highlighted, dragging, stacked, stacked && distinct < focusCount)
            } else {
                omegaKeyPill(xa, TimeAxis.xOf(inst[j].toDouble(), view, ppf, cx), cy, if (editing) editH else altH, editing)
                if (!editing) continue
                // A pílula resume as chaves densas; as em foco ficam no tempo delas, por cima.
                for (f in intArrayOf(selFrame, dragFrame, playheadFrame)) {
                    if (!Keyframes.groupHas(inst, i, j, f)) continue
                    val x = TimeAxis.xOf(f.toDouble(), view, ppf, cx)
                    if (f == dragFrame) dragX = x
                    omegaEditKey(x, cy, editH, f != dragFrame, f == dragFrame, false, false)
                }
            }
        }
        if (!dragX.isNaN()) drawBalloon(dragX, cy, dragFrame, fps)
    }

    /**
     * Cabeçalho da linha (P0-3): pílula de x −8 a 62 (destaque na seleção
     * múltipla), faixa da etiqueta de 2 dp, olho em x 17 (✓ nas escolhidas;
     * some em todas enquanto há seleção múltipla), miniatura 20 × 23 em x 34..54,
     * recorte com o "↳" e o cadeado no canto da miniatura.
     */
    private fun DrawScope.omegaHeader(c: TimelineController, r: RowModel, rowTop: Float, activated: Boolean, hideEye: Boolean, fps: Float) {
        val pillTop = rowTop + m.omegaPillTop
        translate(0f, pillTop) {
            drawPath(omegaPill, if (activated) AureaColors.Accent else AureaTimeline.OmegaHeader)
            drawPath(omegaPill, AureaTimeline.OmegaHeaderBorder, style = omegaPillStroke)
        }
        val cy = pillTop + m.pillHeight / 2f
        if (!activated) {
            val tag = ShellColors.LabelPalette.getOrNull(r.label - 1)
            if (tag != null) {
                val tagTop = rowTop + m.omegaTagTop
                drawRect(tag, Offset(0f, tagTop), Size(m.omegaTagWidth, max(0f, c.rowHeight(r) - m.omegaTagTop - m.omegaTagBottomGap)))
            }
        }
        if (activated) {
            drawGlyph(CupertinoGlyph.CheckmarkCircleFill, m.omegaCheckGlyph, AureaColors.OnAccent.copy(alpha = 0.8f), m.gutterEyeCx, cy)
        } else if (!hideEye) {
            drawGlyph(
                if (r.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
                m.omegaEyeGlyph,
                AureaTimeline.GutterEye.copy(alpha = if (r.visible) 0.9f else 0.5f),
                m.gutterEyeCx, cy,
            )
        }
        val ink = if (activated) AureaColors.OnAccent else AureaTimeline.GutterEye
        val alpha = if (r.visible) 1f else 0.6f
        val clipped = r.clipToBelow
        val scale = if (clipped) m.omegaClipScale else 1f
        val bw = m.omegaThumbWidth * scale
        val bh = m.omegaThumbHeight * scale
        val bcx = m.omegaThumbLeft + m.omegaThumbWidth / 2f - (if (clipped) m.omegaClipShift else 0f)
        val tinted = ink.copy(alpha = ink.alpha * alpha)
        when (r.type) {
            LayerType.Video, LayerType.Image -> if (!omegaThumb(c, r, bcx - bw / 2f, cy - bh / 2f, bw, bh, fps, alpha)) {
                drawGlyph(r.type.glyph, m.glyphIcon, tinted, bcx, cy)
            }
            LayerType.Audio -> drawGlyph(CupertinoGlyph.MusicNote, 10f, ink.copy(alpha = 0.8f * alpha), bcx, cy)
            LayerType.Text -> {
                val layout = glyphT ?: measurer.measure("T", glyphTextStyle).also { glyphT = it }
                drawText(layout, color = ink, alpha = alpha, topLeft = Offset(bcx - layout.size.width / 2f, cy - layout.size.height / 2f))
            }
            LayerType.Shape -> drawCircle(tinted, m.glyphDot / 2f * scale, Offset(bcx, cy))
            else -> drawGlyph(r.type.glyph, m.glyphIcon, tinted, bcx, cy)
        }
        if (clipped) {
            val layout = glyphClip ?: measurer.measure("↳", clipGlyphStyle).also { glyphClip = it }
            drawText(
                layout, color = ink, alpha = if (r.visible) 1f else 0.5f,
                topLeft = Offset(m.omegaClipIndicatorCx - layout.size.width / 2f, rowTop + m.omegaClipIndicatorCy - layout.size.height / 2f),
            )
        }
        if (r.locked) drawGlyph(CupertinoGlyph.LockFill, 9f, AureaColors.Muted, m.omegaLockCx, rowTop + m.omegaLockCy)
    }

    /** Miniatura da mídia AJUSTADA na caixa (sem cortar), da mesma tira em cache da barra. */
    private fun DrawScope.omegaThumb(c: TimelineController, r: RowModel, left: Float, top: Float, bw: Float, bh: Float, fps: Float, alpha: Float): Boolean {
        val heightPx = m.bar.roundToInt().coerceIn(1, THUMB_MAX_PX)
        val image = r.type == LayerType.Image
        val bucket = if (image) -1 else Thumbs.bucketOf(r.offset.toDouble(), fps)
        val frame = if (image) r.start else Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), r.start, r.offset)
        val bmp = thumbs.get(c.store.thumbnails, r.id, bucket, frame, heightPx, c.store.thumbnailGeneration) ?: return false
        if (bmp.width <= 0 || bmp.height <= 0) return false
        val s = min(bw / bmp.width, bh / bmp.height)
        val dw = bmp.width * s
        val dh = bmp.height * s
        rectF.set(left + (bw - dw) / 2f, top + (bh - dh) / 2f, left + (bw + dw) / 2f, top + (bh + dh) / 2f)
        bitmapPaint.alpha = (alpha * 255f).roundToInt()
        drawContext.canvas.nativeCanvas.drawBitmap(bmp, null, rectF, bitmapPaint)
        return true
    }

    /** Alça ≡ da linha: três barrinhas de 8 dp a 5 dp da borda direita (o toque longo reordena). */
    private fun DrawScope.omegaGrip(rowTop: Float, rh: Float, w: Float) {
        val cy = rowTop + rh / 2f
        val right = w - m.omegaGripRight
        val left = right - m.omegaGripWidth
        val d = m.density
        for (k in -1..1) {
            val y = cy + k * 3f * d - d / 2f
            drawRect(AureaTimeline.OmegaGripShadow, Offset(left, y + d), Size(m.omegaGripWidth, d))
            drawRect(AureaTimeline.OmegaGrip, Offset(left, y), Size(m.omegaGripWidth, d))
        }
    }

    /** Alças do foco (P1-7): pílulas brancas por fora das bordas, alfa pelo estado do gesto. */
    private fun DrawScope.omegaHandles(st: TimelineState, x0: Float, x1: Float, top: Float, w: Float) {
        val alpha = when (st.handleState) {
            1 -> 0.3f
            2 -> 0.5f
            3 -> 0f
            else -> 0.2f
        }
        if (alpha <= 0f) return
        clipRect(left = m.headerColumn, right = w) {
            omegaHandle(x0 - m.omegaHandle, top, start = true, alpha = alpha, speed = st.speedHandles)
            omegaHandle(x1, top, start = false, alpha = alpha, speed = st.speedHandles)
        }
    }

    /** Uma alça: a ponta de fora redonda e o chevron vazado (‹ ou ›; ≪ ≫ na velocidade). */
    private fun DrawScope.omegaHandle(left: Float, top: Float, start: Boolean, alpha: Float, speed: Boolean) {
        val hw = m.omegaHandle
        val bar = m.bar
        val round = CornerRadius(bar / 2f)
        val flat = CornerRadius(m.omegaTabRadius)
        omegaHandlePath.reset()
        omegaHandlePath.addRoundRect(
            androidx.compose.ui.geometry.RoundRect(
                left, top, left + hw, top + bar,
                topLeftCornerRadius = if (start) round else flat,
                topRightCornerRadius = if (start) flat else round,
                bottomRightCornerRadius = if (start) flat else round,
                bottomLeftCornerRadius = if (start) round else flat,
            ),
        )
        omegaLayerPaint.alpha = alpha
        drawContext.canvas.saveLayer(androidx.compose.ui.geometry.Rect(left, top, left + hw, top + bar), omegaLayerPaint)
        drawPath(omegaHandlePath, AureaTimeline.TrimGrip)
        val cx = left + hw / 2f
        val cy = top + bar / 2f
        val s = m.omegaHandleChevron
        omegaChevron.reset()
        val dir = if (start) 1f else -1f
        val offsets = if (speed) floatArrayOf(-s * 0.55f, s * 0.55f) else floatArrayOf(0f)
        for (o in offsets) {
            omegaChevron.moveTo(cx + o + dir * s / 2f, cy - s)
            omegaChevron.lineTo(cx + o - dir * s / 2f, cy)
            omegaChevron.lineTo(cx + o + dir * s / 2f, cy + s)
        }
        drawPath(omegaChevron, AureaTimeline.KeyStroke, style = omegaHandleStroke, blendMode = BlendMode.Clear)
        drawContext.canvas.restore()
    }

    /** Régua (P0-5): tiques de 1 dp pendurados do alto — segundo 18, meio 12, quadro 5 —, sem rótulos. */
    private fun DrawScope.omegaRuler(st: TimelineState, w: Float, view: Double, ppf: Float, cx: Float, fps: Float) {
        for (p in omegaTicks) p.reset()
        val t0 = TimeAxis.frameAt(0f, view, ppf, cx)
        val t1 = TimeAxis.frameAt(w, view, ppf, cx)
        if (t1 < 0.0) return
        val fFirst = max(0L, floor(t0).toLong())
        val fLast = ceil(t1).toLong()
        val pps = st.pps
        val step = OmegaRuler.frameStep(pps, fps)
        val secStep = OmegaRuler.secondStep(pps)
        val sec0 = max(0L, floor(fFirst / fps.toDouble()).toLong())
        val sec1 = ceil(fLast / fps.toDouble()).toLong()
        fun tick(path: Path, frame: Long, heightDp: Float) {
            val x = TimeAxis.xOf(frame.toDouble(), view, ppf, cx)
            if (x < -1f || x > w + 1f) return
            path.moveTo(x, 0f)
            path.lineTo(x, heightDp * m.density)
        }
        var s = (sec0 / secStep) * secStep
        while (s <= sec1) {
            tick(omegaTicks[OmegaRuler.SECOND], OmegaRuler.frameOf(s.toDouble(), fps), OmegaRuler.heightDp(OmegaRuler.SECOND))
            s += secStep
        }
        if (secStep == 1 && pps / 2f >= OmegaRuler.MIN_TICK_DP) {
            for (k in sec0..sec1) tick(omegaTicks[OmegaRuler.HALF], OmegaRuler.frameOf(k + 0.5, fps), OmegaRuler.heightDp(OmegaRuler.HALF))
        }
        if (secStep == 1 && step < fps / 2f) {
            var f = (fFirst / step) * step
            while (f <= fLast) {
                if (OmegaRuler.tickClass(f, fps) == OmegaRuler.FRAME) tick(omegaTicks[OmegaRuler.FRAME], f, OmegaRuler.heightDp(OmegaRuler.FRAME))
                f += step
            }
        }
        for (p in omegaTicks) drawPath(p, AureaTimeline.OmegaTick, style = omegaTickStroke)
        // Mover (P1-5): os tiques do início e do fim originais em branco, os novos em destaque.
        if (st.moveOrigStart != Snap.NONE) {
            omegaHighlightTick(st.moveOrigStart, AureaColors.Playhead, view, ppf, cx, fps)
            omegaHighlightTick(st.moveOrigEnd, AureaColors.Playhead, view, ppf, cx, fps)
            omegaHighlightTick(st.moveNewStart, AureaColors.Accent, view, ppf, cx, fps)
            omegaHighlightTick(st.moveNewEnd, AureaColors.Accent, view, ppf, cx, fps)
        }
    }

    private fun DrawScope.omegaHighlightTick(frame: Int, color: Color, view: Double, ppf: Float, cx: Float, fps: Float) {
        if (frame == Snap.NONE) return
        val x = TimeAxis.xOf(frame.toDouble(), view, ppf, cx)
        val hDp = OmegaRuler.heightDp(OmegaRuler.tickClass(frame.toLong(), fps))
        drawRect(color, Offset(x - m.omegaTick / 2f, 0f), Size(m.omegaTick, hDp * m.density))
    }

    /** Marcadores (P1-3): fio de 1 dp pela régua (≈ 59 %) e a bandeirola cheia. Devolve o do quadro atual (−1 = nenhum). */
    private fun DrawScope.omegaMarkers(mk: com.aurea.aurea.state.EditorStore.Markers, w: Float, view: Double, ppf: Float, cx: Float): Int {
        var atPlayhead = -1
        for (i in 0 until mk.size) {
            val f = mk.frames[i]
            if (f == playheadFrame) atPlayhead = i
            val x = TimeAxis.xOf(f.toDouble(), view, ppf, cx)
            if (x < -m.omegaMarkerFlagLarge || x > w + m.omegaTick) continue
            val color = markerColor(mk.colors[i])
            drawRect(color.copy(alpha = MARKER_LINE_ALPHA), Offset(x - m.omegaTick / 2f, 0f), Size(m.omegaTick, m.rowsTop))
            val d = if (f == playheadFrame) m.omegaMarkerFlagLarge else m.omegaMarkerFlagSmall
            omegaFlag.reset()
            omegaFlag.moveTo(x, 0f)
            omegaFlag.lineTo(x, m.omegaMarkerFlag)
            omegaFlag.lineTo(x - d, m.omegaMarkerFlag)
            omegaFlag.lineTo(x, m.omegaMarkerFlagTip)
            omegaFlag.close()
            drawPath(omegaFlag, color)
        }
        return atPlayhead
    }

    /**
     * Aba do tempo atual (P0-5): 70 × 18 dp centrada no cursor, cantos de 2 e o
     * de cima à esquerda chanfrado; `MM:SS:FF` branco tabular. Num marcador, o
     * contorno de 1 dp na cor do primeiro papel.
     */
    private fun DrawScope.omegaTimeTab(cx: Float, frame: Int, fps: Float, marked: Boolean) {
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
        val tabW = max(m.omegaTabMinWidth, total + 2f * m.omegaTabPad)
        val left = cx - tabW / 2f
        val right = cx + tabW / 2f
        val top = m.omegaTabTop
        val bottom = top + m.omegaTabHeight
        val r = m.omegaTabRadius
        val ch = m.omegaTabChamfer
        omegaTabPath.reset()
        omegaTabPath.moveTo(left + ch, top)
        omegaTabPath.lineTo(right - r, top)
        omegaTabPath.quadraticTo(right, top, right, top + r)
        omegaTabPath.lineTo(right, bottom - r)
        omegaTabPath.quadraticTo(right, bottom, right - r, bottom)
        omegaTabPath.lineTo(left + r, bottom)
        omegaTabPath.quadraticTo(left, bottom, left, bottom - r)
        omegaTabPath.lineTo(left, top + ch)
        omegaTabPath.close()
        drawPath(omegaTabPath, AureaTimeline.OmegaTimeTab)
        if (marked) drawPath(omegaTabPath, AureaTimeline.Bookmarks[0], style = omegaTabStroke)
        val textTop = m.omegaTimecodeBaseline - digitBaseline
        var x = cx - total / 2f
        if (hourDigits > 0) {
            x = drawNumber(tc[0], hourDigits, x, textTop)
            x = drawColon(x, textTop)
        }
        x = drawNumber(tc[1], 2, x, textTop)
        x = drawColon(x, textTop)
        x = drawNumber(tc[2], 2, x, textTop)
        x = drawColon(x, textTop)
        drawNumber(tc[3], ffDigits, x, textTop)
    }

    private companion object {
        /** Faixa da prévia já calculada (a mesma cor do desenho de antes). */
        val PREVIEW_BUFFER = Color(0xFF4DA3FF)
        /** Fio do marcador: alfa 150/255 da referência. */
        const val MARKER_LINE_ALPHA = 150f / 255f
        /** Teto de faixas da zebra por clipe e quadro (uma tela larga cabe com folga). */
        const val ZEBRA_MAX = 4096
        const val THUMB_MAX_PX = 512
        /** Quadradinho da camada recortada: um pouco menor, encostado à direita. */
        const val CLIP_BOX_SCALE = 0.82f
        /** Perguntas de miniatura ao motor por quadro (o resto no quadro seguinte). */
        const val THUMB_QUERIES_PER_FRAME = 6
        const val NAME_STEP_DP = 12f
        const val NAME_MIN_DP = 8f
        const val MARKER_NAME_MIN_DP = 18f
        const val NAME_CACHE = 256
        const val LABEL_CACHE = 96
        /** Diagonal do quadrado girado (√2). */
        const val DIAGONAL = 1.4142135f
        val TRACK_SHADE = Color.Black.copy(alpha = 0.22f)
        val GRIP = Color.Black.copy(alpha = 0.38f)
        val ARROW_TINT = Color.White.copy(alpha = 0.7f)
        /** Chevron escuro da tampa branca da fileira compacta (#161C2A do mockup). */
        val CAP_INK = Color(0xFF161C2A)
        val KEY_OFF = Color.White.copy(alpha = 0.9f)
        val KEY_BORDER = Color.Black.copy(alpha = 0.85f)
        /** Keyframe na seleção da timeline (não principal): azul com anel branco — o mesmo no iOS. */
        val KEY_PICKED = Color(0xFF4DA3FF)
        val KEY_PICKED_RING = Color.White
        val BALLOON = Color.Black.copy(alpha = 0.82f)
    }
}
