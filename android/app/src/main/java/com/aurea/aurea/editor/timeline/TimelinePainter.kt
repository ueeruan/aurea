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
    /** Triângulo do cabeçote (camada escolhida), reusado a cada quadro. */
    private val marker = Path()
    /** Contorno branco do clipe escolhido na fileira compacta (ponta esquerda redonda). */
    private val capStroke = Stroke(m.capStroke)
    private val capOutline = Path()
    private val capRadii = FloatArray(8)
    private var capName: NameLayout? = null
    private val timecodeBoxLine = Stroke(m.timecodeBoxStroke)
    private val dividerPaint = Paint().apply { color = android.graphics.Color.argb(64, 0, 0, 0); strokeWidth = 1f }
    private val glyphSrc = android.graphics.Rect()

    // --- Texto ------------------------------------------------------------------------
    private val nameStyle = TextStyle(
        color = Color.White,
        fontSize = 11.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = (-0.1).sp,
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
        fontSize = 16.sp,
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
        st.redrawTick   // lido: as miniaturas que não couberam neste quadro pedem o próximo
        thumbs.beginFrame(THUMB_QUERIES_PER_FRAME)

        if (n > 0) {
            // A pílula da 1ª fileira começa 2 dp acima da barra.
            clipRect(top = m.rowsTop - m.pillInset) {
                drawRows(c, rows, n, w, h, view, ppf, cx, fps, compact)
            }
        }
        drawRuler(w, view, ppf, cx, fps, st.pps)
        drawMarkers(store.markers, w, view, ppf, cx)
        drawTimecode(cx, store.playhead, fps, st.timecodeBox)
        if (thumbs.starved) c.requestRedraw()
        // Redesenho 2026-09-29: camada escolhida (fileira compacta ou relógio em
        // caixa) = cabeçote em destaque com o triângulo no alto da régua
        // (Efeitos.dc.html); sem seleção, o fio branco do editor principal.
        val chosen = compact || st.timecodeBox
        val color = if (chosen) AureaColors.Accent else AureaColors.Playhead
        // Fio de 2 dp do relógio para baixo (mockup): não risca a régua nem os dígitos.
        drawRect(color, Offset(cx - m.playhead / 2f, m.playheadTop), Size(m.playhead, h - m.playheadTop))
        if (chosen) {
            marker.reset()
            marker.moveTo(cx - m.markerWidth / 2f, 0f)
            marker.lineTo(cx + m.markerWidth / 2f, 0f)
            marker.lineTo(cx, m.markerHeight)
            marker.close()
            drawPath(marker, color)
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
            if (segs.size == 1) {
                drawSegment(c, segs[0], top, w, view, ppf, cx, fps, compact, selCount, multi, cache, generation, arrows = true)
                continue
            }
            // FILEIRA COMPARTILHADA: cada trecho da linha no seu tempo, lado a
            // lado. Os escolhidos por cima — contorno e alças passam por cima do
            // vizinho encostado, que é onde o dedo os pega.
            var onScreen = false
            var offLeft = false
            var offRight = false
            for (s in segs) {
                val x0 = TimeAxis.xOf(s.start.toDouble(), view, ppf, cx)
                val x1 = max(TimeAxis.xOf(s.end.toDouble(), view, ppf, cx), x0 + m.barMinWidth)
                when {
                    x1 >= -m.barRadius && x0 <= w + m.barRadius -> onScreen = true
                    x0 > w -> offRight = true
                    else -> offLeft = true
                }
            }
            for (pass in 0..1) for (s in segs) {
                if (c.isSelected(s.id) != (pass == 1)) continue
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

        // Pílulas por cima das barras (as barras passam por baixo): olho + miniatura do tipo.
        for (pass in passes) for (i in indices) {
            val lifted = preview != null && i in preview.sourceStart until preview.sourceEnd
            if (preview != null && lifted != (pass == 1)) continue
            val r = c.rowAt(rows, i) ?: continue
            val rowTop = m.rowsTop + c.rowTop(i) - scroll + (preview?.offsets?.get(i) ?: 0f)
            if (rowTop + c.rowHeight(r) < m.rowsTop || rowTop > h) continue
            val track = r.track
            if (track != null) {
                drawLaneGutter(r, track, rowTop, c.rowHeight(r), c.openGroups.value)
                continue
            }
            // A calha é da FILEIRA: no lote se algum trecho dela está no lote;
            // aberta se as trilhas abertas são de um trecho dela.
            var inBatch = false
            if (multi) for (s in r.segments) if (c.isSelected(s.id)) inBatch = true
            drawGutter(c, r, rowTop, inBatch, c.isExpanded(r), fps)
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
        drawRow(r, top, w, view, ppf, cx, fps, compact, selected, multi, handles, selFrame, dragFrame, cache, generation, picked, keysShown, arrows)
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
            drawDiamonds(r, cy - m.diamondCyNormal, w, view, ppf, cx, false, selFrame, dragFrame, fps, picked)
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
        val x1 = max(TimeAxis.xOf(r.end.toDouble(), view, ppf, cx), x0 + m.barMinWidth)
        val barW = x1 - x0
        val tone = AureaTimeline.tone(r.type)
        // Camada oculta: os clipes da fileira a 40 %.
        val alpha = if (r.visible) 1f else AureaTimeline.HiddenAlpha
        if (x1 >= -m.barRadius && x0 <= w + m.barRadius) {
            // Barra cortada perto da tela: um clipe de minutos não vira um retângulo de 100 mil px.
            val left = max(x0, -m.barRadius * 2f)
            val right = min(x1, w + m.barRadius * 2f)
            val bottom = top + m.bar
            val canvas = drawContext.canvas.nativeCanvas
            rectF.set(left, top, right, bottom)
            barClip.reset()
            // Fileira compacta, clipe escolhido: ponta esquerda bem redonda (raio 14).
            val capped = compact && selected
            if (capped) {
                val l = m.capRadius
                val rr = m.barRadius
                capRadii[0] = l; capRadii[1] = l; capRadii[2] = rr; capRadii[3] = rr
                capRadii[4] = rr; capRadii[5] = rr; capRadii[6] = l; capRadii[7] = l
                barClip.addRoundRect(rectF, capRadii, android.graphics.Path.Direction.CW)
            } else {
                barClip.addRoundRect(rectF, m.barRadius, m.barRadius, android.graphics.Path.Direction.CW)
            }
            canvas.save()
            canvas.clipPath(barClip)
            // Bloco escuro no tom do tipo.
            drawRect(tone.body, Offset(left, top), Size(right - left, m.bar), alpha = alpha)
            bitmapPaint.alpha = (alpha * 255f).roundToInt()
            if (r.hasThumbs && drawThumbs(canvas, r, top, x0, x1, w, view, ppf, cx, fps, cache, generation)) {
                withTransform({
                    translate(x0, top)
                    scale(barW, 1f, pivot = Offset.Zero)
                }) { drawRect(thumbShade, size = Size(1f, m.bar), alpha = alpha) }
            }
            // Trilho dos losangos só quando a linha tem keyframe à vista.
            if (keysShown && r.instants.isNotEmpty()) {
                drawRect(TRACK_SHADE, Offset(left, top + m.trackTop), Size(right - left, m.track), alpha = alpha)
            }
            if (r.track == null && (r.type == LayerType.Audio || r.type == LayerType.Video)) drawWaveform(canvas, r, top, x0, x1, w, view, ppf, cx, tone, alpha)
            // Faixa sólida de 3 dp na borda esquerda só quando a camada tem etiqueta
            // de cor (o mockup não tem faixa: a cor do clipe já diz o tipo).
            val stripe = ShellColors.LabelPalette.getOrNull(r.label - 1)
            if (stripe != null) drawRect(stripe, Offset(x0, top), Size(m.stripe, m.bar), alpha = alpha)
            if (capped) {
                // Tampa branca "‹" de 34 na ponta esquerda VISÍVEL (tocar = voltar).
                val capL = RowHit.capLeft(m, x0)
                val capR = min(capL + m.capWidth, right)
                if (capR > capL) {
                    drawRect(AureaTimeline.ClipSelected, Offset(capL, top), Size(capR - capL, m.bar))
                    drawGlyph(CupertinoGlyph.ChevronLeft, m.capGlyph, CAP_INK, capL + m.capWidth / 2f, top + m.bar / 2f)
                }
            }
            canvas.restore()

            // Setas ‹ › do compacto só no trecho escolhido (os outros da linha também aparecem).
            clipRect(left, top, right, bottom) { drawBarContent(r, top, x0, x1, w, compact && selected, tone, alpha) }

            if (capped) {
                // Fileira compacta: contorno branco de 1,5 com a mesma ponta redonda.
                val s = m.capStroke / 2f
                capOutline.reset()
                capOutline.addRoundRect(
                    androidx.compose.ui.geometry.RoundRect(
                        left + s, top + s, right - s, bottom - s,
                        topLeftCornerRadius = CornerRadius(m.capRadius - s),
                        topRightCornerRadius = CornerRadius(m.barRadius - s),
                        bottomRightCornerRadius = CornerRadius(m.barRadius - s),
                        bottomLeftCornerRadius = CornerRadius(m.capRadius - s),
                    ),
                )
                drawPath(capOutline, AureaTimeline.ClipSelected, style = capStroke)
            } else if (selected) {
                // Escolhido: contorno branco de 2 dp (no lote também).
                val s = m.selStroke
                drawRoundRect(
                    AureaTimeline.ClipSelected,
                    Offset(left + s / 2f, top + s / 2f),
                    Size(right - left - s, m.bar - s),
                    CornerRadius(m.barRadius - s / 2f),
                    style = if (multi) multiStroke else selStroke,
                )
            }
            if (handles && r.track == null) {
                // A tampa "‹" já é a alça branca da ponta esquerda (o dedo ali também apara).
                if (!capped && RowHit.startHandleVisible(m, x0)) drawTrimHandle(x0 - m.trimInsetStart, top)
                if (RowHit.endHandleVisible(x1, w)) drawTrimHandle(x1 - m.trimInsetEnd, top)
            }
        } else if (r.track == null && arrows) {
            // Clipe fora da janela: uma seta na borda diz para que lado ele
            // está (linha vazia parecia camada quebrada). Na fileira
            // compartilhada quem decide a seta é a fileira (uma por lado).
            drawEdgeArrow(x0 > w, top, w, tone.stripe.copy(alpha = if (r.visible) 0.9f else 0.45f))
        }
        // "Keyframes: só as escolhidas" (menu da timeline): as outras linhas ficam limpas.
        if (keysShown) {
            drawDiamonds(r, top, w, view, ppf, cx, compact, selFrame, dragFrame, fps, picked)
        }
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
        val cy = if (r.instants.isEmpty()) top + m.bar / 2f else top + m.trackTop / 2f
        val d = m.density
        val ink = tone.text.copy(alpha = alpha)
        val arrowInk = ARROW_TINT.copy(alpha = ARROW_TINT.alpha * alpha)
        if (compact) return drawCappedContent(r, cy, x0, x1, w, cr, arrowInk, alpha)
        var x = cl
        // O tipo e o cadeado moram na pílula da fileira; a barra leva só o nome.
        // O ≡ mora na ponta REAL da barra (A.01); as setas do compacto grudam na parte visível (print t2).
        val menuRight = x1 - (if (barW < m.narrowBar) m.padRNarrow else m.padR)
        val right = when {
            barW > m.menuMinBar -> min(cr, menuRight - m.menuGlyph * d)
            else -> cr
        }
        val rhombusW = if (r.animated && barW > m.rhombusMinBar) m.rhombusGap + m.rhombusIcon * d else 0f
        if (barW > m.nameMinBar && r.name.isNotEmpty()) {
            val avail = right - rhombusW - x
            if (avail > NAME_MIN_DP * d) {
                val layout = nameLayout(r, avail)
                drawText(layout, color = tone.text, alpha = alpha, topLeft = Offset(x, cy - layout.size.height / 2f))
                x += layout.size.width
            }
        }
        if (rhombusW > 0f && x + rhombusW <= right + 1f) {
            drawGlyph(CupertinoGlyph.Rhombus, m.rhombusIcon, ink, x + m.rhombusGap + m.rhombusIcon * d / 2f, cy)
        }
        if (barW > m.menuMinBar && menuRight <= w + m.menuGlyph * d) {
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
        val capEnd = RowHit.capLeft(m, x0) + m.capWidth
        val next = RowHit.nextArrowLeft(m, x0, x1, w)
        val prev = next - m.arrowSlot
        val arrows = prev >= capEnd
        if (arrows) {
            drawGlyph(CupertinoGlyph.ChevronLeft, m.arrowGlyph, arrowInk, prev + m.arrowSlot / 2f, cy)
            drawGlyph(CupertinoGlyph.ChevronRight, m.arrowGlyph, arrowInk, next + m.arrowSlot / 2f, cy)
        }
        val x = capEnd + m.capNameGap
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

    /** Alça de trim da A.01: 16 × 32 branca DENTRO da ponta, risco central escuro. */
    private fun DrawScope.drawTrimHandle(left: Float, top: Float) {
        drawRoundRect(
            AureaTimeline.ClipSelected,
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
                // Divisa fina entre os quadros da tira (mockup).
                if (l > x0 + 1f) canvas.drawLine(l, top, l, top + m.bar, dividerPaint)
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
        val inst = r.instants
        if (inst.isEmpty()) return
        val cy = top + if (compact) m.diamondCyCompact else m.diamondCyNormal
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
            val fill = if (on) AureaTimeline.KeyframeOn else if (inSelection) KEY_PICKED else KEY_OFF
            if (j == i) {
                val dragging = inst[i] == dragFrame
                if (dragging) dragX = kx
                drawDiamond(kx, cy, fill, on, if (dragging) m.keyDragScale else 1f, inSelection)
            } else {
                drawKeyPill(kx, TimeAxis.xOf(inst[j].toDouble(), view, ppf, cx), cy, fill, inSelection)
            }
        }
        if (!dragX.isNaN()) drawBalloon(dragX, cy, dragFrame, fps)
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
        drawGlyph(
            if (r.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
            m.gutterEye,
            if (r.visible) AureaTimeline.GutterEye else AureaTimeline.GutterIcon,
            m.gutterEyeCx,
            cy,
        )
        val box = m.glyphBox
        val bl = m.glyphBoxLeft
        val bt = cy - box / 2f
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
    private fun DrawScope.drawTimecode(cx: Float, frame: Int, fps: Float, boxed: Boolean) {
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
        if (boxed) {
            // Estilo caixa (camada escolhida / efeitos): borda em destaque em volta dos dígitos.
            val bw = total + 2f * m.timecodeBoxPad
            val s = m.timecodeBoxStroke
            drawRoundRect(
                AureaColors.Accent,
                Offset(cx - bw / 2f + s / 2f, m.timecodeBoxTop + s / 2f),
                Size(bw - s, m.timecodeBoxHeight - s),
                CornerRadius(m.timecodeBoxRadius),
                style = timecodeBoxLine,
            )
        }
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
        if (!boxed) drawRect(Color.White, Offset(cx - total / 2f, m.underlineTop), Size(total, m.underlineHeight))
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
        const val THUMB_MAX_PX = 512
        /** Perguntas de miniatura ao motor por quadro (o resto no quadro seguinte). */
        const val THUMB_QUERIES_PER_FRAME = 6
        const val NAME_STEP_DP = 12f
        const val NAME_MIN_DP = 8f
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
