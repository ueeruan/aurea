package com.aurea.aurea.editor.timeline

import com.aurea.aurea.ui.theme.AureaTimeline

/**
 * Geometria da timeline em PX — a MESMA conta serve para pintar e para tocar
 * (o que se vê é o que responde ao dedo).
 *
 * Medidas em dp da A.01 (`am_timeline.dart@aba36bb`, spec 03 §1.B) vezes a
 * densidade. Classe pura (só floats) para os testes de JVM rodarem com
 * densidade 1.
 */
internal class TimelineMetrics(val density: Float, val fontScale: Float = 1f) {
    private fun dp(v: Float) = v * density

    // --- Régua e linhas -------------------------------------------------------
    val rulerTicks = dp(AureaTimeline.RulerTicks.value)
    /** Topo da 1ª linha: 20 de riscos + 18 de respiro (o relógio mora no respiro). */
    val rowsTop = dp(AureaTimeline.RulerTicks.value + AureaTimeline.RulerGap.value)
    val row = dp(AureaTimeline.Row.value)
    val bar = dp(AureaTimeline.Bar.value)
    val barRadius = dp(AureaTimeline.BarRadius.value)
    val barMinWidth = dp(AureaTimeline.BarMinWidth.value)
    val track = dp(AureaTimeline.KeyframeTrack.value)
    /** Começo da faixa dos losangos (medido do topo da barra). */
    val trackTop = bar - track
    /** Folga abaixo das linhas: o "+" da casca cobre a ponta de baixo. */
    val bottomPad = dp(56f)

    // --- Coluna das pílulas (olho + quadradinho) -------------------------------
    val headerColumn = dp(AureaTimeline.HeaderColumn.value)
    val pillLeft = dp(4f)
    val pillWidth = dp(AureaTimeline.PillWidth.value)
    val pillHeight = dp(AureaTimeline.PillHeight.value)
    val pillRadius = pillHeight / 2f
    val eyeSlot = dp(26f)
    val eyeGlyph = 16f
    val swatch = dp(18f)
    val swatchRadius = dp(4f)
    /** `spaceEvenly` do Row da A.01: três vãos iguais entre olho (26) e quadradinho (18). */
    private val pillGap = (pillWidth - eyeSlot - swatch) / 3f
    val eyeCenterX = pillLeft + pillGap + eyeSlot / 2f
    val swatchLeft = pillLeft + pillGap * 2f + eyeSlot
    /** O olho responde até o meio do vão que o separa do quadradinho. */
    val eyeHitRight = pillLeft + pillGap * 1.5f + eyeSlot

    // --- Régua ------------------------------------------------------------------
    val tickMajorTop = dp(2f)
    val tickMinorTop = dp(9f)
    val tickBottom = dp(18f)
    val tickMajorWidth = dp(1.4f)
    val tickMinorWidth = dp(1f)
    val tickLabelGap = dp(3f)

    // --- Conteúdo da barra --------------------------------------------------------
    val stripe = dp(4f)
    val padL = dp(14f)
    val padR = dp(10f)
    val padLNarrow = dp(7f)
    val padRNarrow = dp(3f)
    val narrowBar = dp(46f)
    val typeIcon = 11f
    val iconGap = dp(6f)
    val lockIcon = 10f
    val lockGap = dp(5f)
    val rhombusIcon = 12f
    val rhombusGap = dp(6f)
    val arrowSlot = dp(22f)
    val arrowGlyph = 14f
    val menuGlyph = 14f
    val iconMinBar = dp(28f)
    val nameMinBar = dp(52f)
    val lockGapMinBar = dp(70f)
    val rhombusMinBar = dp(120f)
    val menuMinBar = dp(150f)
    val selStroke = dp(1.5f)
    val multiStroke = dp(1.2f)
    val lightLine = dp(1f)
    /** Toque do corpo vai um pouco abaixo da barra (os 10 dp que sobram na linha são do vazio). */
    val bodyHitBottom = bar + dp(4f)
    val arrowTouchPad = dp(6f)

    // --- Alça de trim (A.01: 16 × (36 − 4), top 2, DENTRO das pontas) --------------
    val trimWidth = dp(16f)
    val trimTop = dp(2f)
    val trimInsetStart = dp(3f)     // left = x0 − 3
    val trimInsetEnd = dp(13f)      // left = x1 − 13
    val trimRadius = dp(4f)
    val gripWidth = dp(2f)
    val gripHeight = dp(14f)
    /** Folga de toque para FORA da barra, além do desenho (a alça de 16 é estreita para o dedo). */
    val trimTouchOut = dp(13f)

    // --- Losango ------------------------------------------------------------------
    val diamond = dp(11f)
    val diamondRadius = dp(2f)
    val diamondStroke = dp(1.2f)
    /** Centro do losango: A.01 `top 20, altura 17` (normal) e `top 23, altura 16` (compacto). */
    val diamondCyNormal = dp(28.5f)
    val diamondCyCompact = dp(31f)
    val keyTouchHalf = dp(14f)      // alvo de 28 da A.01
    val keyGlyphHalf = dp(7f)       // o próprio desenho (diagonal ≈ 15,5)
    val keyTouchTop = trackTop - dp(2f)
    val keyMergeGap = dp(4f)        // instantes a menos que isso viram pílula
    val keyPillHeight = dp(10f)
    val keyPillMinWidth = dp(16f)
    val keyDragScale = 1.4f
    val balloonPadH = dp(5f)
    val balloonPadV = dp(2f)
    val balloonRadius = dp(4f)
    val balloonGap = dp(3f)

    // --- Cabeçote e relógio ----------------------------------------------------------
    val playhead = dp(AureaTimeline.Playhead.value)
    val knob = dp(AureaTimeline.PlayheadKnob.value)
    val knobRadius = dp(2f)
    /** Linha de base do relógio: topo dos glifos em y ≈ 11,8 no print (13 sp bold). */
    val timecodeBaseline = dp(21f)
    val underlineTop = dp(28.2f)
    val underlineWidth = dp(58f)
    val underlineHeight = dp(1.5f)
    /** Rótulo da régua perto do relógio some (não disputa leitura com ele). */
    val timecodeZoneHalf = dp(36f)

    // --- Gestos -------------------------------------------------------------------------
    val snapClip = dp(12f)          // ímã de clipe e alça
    val snapKey = dp(8f)            // ímã de losango
    val autoEdge = dp(38f)
    val autoSpeed = dp(120f)        // px/s, constante
    val autoIntent = dp(4f)
    val axisSlop = dp(8f)           // depois do toque longo, o eixo se decide com 8 dp
    val flingMin = dp(50f)          // px/s

    // --- Guias ----------------------------------------------------------------------------
    val guide = dp(1f)
    val reorderLine = dp(2f)
    val reorderDot = dp(3f)
}
