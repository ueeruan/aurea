package com.aurea.aurea.editor.timeline

import com.aurea.aurea.ui.theme.AureaTimeline
import kotlin.math.min

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
    /** Topo da 1ª BARRA: 30 de riscos + 30 do relógio + 8 de respiro + 2 (a pílula começa 2 dp acima da barra). */
    val rowsTop = dp(AureaTimeline.RulerTicks.value + AureaTimeline.RulerGap.value)
    val row = dp(AureaTimeline.Row.value)
    val bar = dp(AureaTimeline.Bar.value)
    val barRadius = dp(AureaTimeline.BarRadius.value)
    val barMinWidth = dp(AureaTimeline.BarMinWidth.value)
    val track = dp(AureaTimeline.KeyframeTrack.value)
    /** Começo da faixa dos losangos (medido do topo da barra). */
    val trackTop = bar - track
    /** Folga abaixo das linhas (a última não cola na borda). */
    val bottomPad = dp(24f)

    // --- Pílula da fileira (olho + miniatura/tipo), colada à esquerda -----------
    /** Largura da pílula; as barras passam por baixo dela. */
    val headerColumn = dp(AureaTimeline.HeaderColumn.value)
    /** A pílula: 28 de altura, começa 2 dp acima da barra, raio 14 só à direita. */
    val pillInset = dp(AureaTimeline.RowPillInset.value)
    val pillHeight = dp(AureaTimeline.RowPillHeight.value)
    val pillRadius = pillHeight / 2f
    /** Centro vertical da pílula, medido do topo da barra. */
    val pillCy = pillHeight / 2f - pillInset
    /** Chevron das trilhas de propriedade (coluna estreita à esquerda). */
    val gutterIconCx = dp(17f)
    val gutterEye = AureaTimeline.GutterEyeSize.value
    /** Olho de 16 com 8 de margem: centro em x 16. */
    val gutterEyeCx = dp(16f)
    /** Quadradinho de 22 (miniatura / "T" / ponto / nota) de x 32 a 54. */
    val glyphBox = dp(AureaTimeline.GlyphBox.value)
    val glyphBoxLeft = dp(32f)
    val glyphBoxRadius = dp(3f)
    val glyphIcon = 12f
    val glyphDot = dp(10f)
    val gutterLock = 10f
    val gutterLockCx = dp(66f)
    /** Toque do olho: o começo da pílula até antes do quadradinho; o resto é do tipo (abre as trilhas). */
    val eyeHitRight = dp(28f)
    /** Onde começa o nome de uma trilha de propriedade (depois do chevron). */
    val laneLabelLeft = dp(30f)

    // --- Régua ------------------------------------------------------------------
    val tickMajorTop = dp(8f)
    val tickMinorTop = dp(14f)
    val tickBottom = dp(22f)
    val tickMajorWidth = dp(1f)
    val tickMinorWidth = dp(1f)
    val tickLabelGap = dp(3f)

    // --- Conteúdo da barra --------------------------------------------------------
    val stripe = dp(AureaTimeline.ClipStripe.value)
    val padL = dp(8f)
    val padR = dp(10f)
    val padLNarrow = dp(6f)
    val padRNarrow = dp(3f)
    val narrowBar = dp(46f)
    val typeIcon = AureaTimeline.ClipIcon.value
    val iconGap = dp(6f)
    val lockIcon = 10f
    val lockGap = dp(5f)
    val arrowSlot = dp(22f)
    val arrowGlyph = 14f
    val menuGlyph = 14f
    val iconMinBar = dp(28f)
    val nameMinBar = dp(52f)
    val lockGapMinBar = dp(70f)
    val menuMinBar = dp(64f)
    val selStroke = dp(AureaTimeline.ClipSelStroke.value)
    val multiStroke = dp(AureaTimeline.ClipSelStroke.value)
    val lightLine = dp(1f)
    /** Toque do corpo vai um pouco abaixo da barra (os 10 dp que sobram na linha são do vazio). */
    val bodyHitBottom = bar + dp(4f)
    val arrowTouchPad = dp(6f)
    /**
     * Fileira compacta, clipe escolhido (Efeitos.dc.html): tampa branca "‹" de
     * 34 antes do início real do clipe (tocar = voltar), contorno branco 1,5 e a
     * ponta esquerda arredondada (raio 14, preso à meia altura da barra).
     */
    val capWidth = dp(34f)
    val capStroke = dp(1.5f)
    val capRadius = min(dp(14f), bar / 2f)
    val capGlyph = 14f
    val capNameGap = dp(10f)

    // --- Alça de trim (16 × (24 − 4), top 2, DENTRO das pontas) -----------------------
    val trimWidth = dp(16f)
    val trimTop = dp(2f)
    val trimInsetStart = dp(3f)     // left = x0 − 3
    val trimInsetEnd = dp(13f)      // left = x1 − 13
    val trimRadius = dp(4f)
    val gripWidth = dp(2f)
    val gripHeight = dp(10f)
    /** Folga de toque para FORA da barra, além do desenho (a alça de 16 é estreita para o dedo). */
    val trimTouchOut = dp(13f)

    // --- Losango ------------------------------------------------------------------
    val diamond = dp(11f)
    val diamondRadius = dp(2f)
    val diamondStroke = dp(1.2f)
    /** O losango inteiro cabe na barra; X continua vindo exclusivamente do tempo. */
    val diamondCyNormal = bar / 2f
    val diamondCyCompact = bar / 2f
    val keyTouchHalf = dp(14f)      // alvo de 28 da A.01
    val keyGlyphHalf = dp(7f)       // o próprio desenho (diagonal ≈ 15,5)
    val keyTouchTop = 0f
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
    /** Triângulo do cabeçote no alto da régua (camada escolhida): 10 × 8, em destaque. */
    val markerWidth = dp(10f)
    val markerHeight = dp(8f)
    /** Relógio 16 sp bold na faixa de 30 abaixo dos riscos; sublinhado branco de 2 na base (y 52). */
    val timecodeTop = dp(30f)
    val timecodeBaseline = dp(46f)
    val underlineTop = dp(52f)
    val underlineHeight = dp(2f)
    /** Estilo caixa (camada escolhida / efeitos): 26 de altura, borda 1,5 em destaque, raio 4. */
    val timecodeBoxTop = dp(31f)
    val timecodeBoxHeight = dp(26f)
    val timecodeBoxPad = dp(6f)
    val timecodeBoxStroke = dp(1.5f)
    val timecodeBoxRadius = dp(4f)
    /** Rótulo da régua perto do relógio some (não disputa leitura com ele). */
    val timecodeZoneHalf = dp(44f)
    /** O fio do cabeçote começa logo abaixo do relógio e desce até a barra de adicionar. */
    val playheadTop = dp(64f)

    // --- Gestos -------------------------------------------------------------------------
    val snapClip = dp(12f)          // ímã de clipe e alça
    val snapKey = dp(8f)            // ímã de losango
    /** Faixa da auto-rolagem: 48 dp no máximo (menos numa janela baixa, ver `AutoScroll.zone`). */
    val autoEdge = dp(48f)
    /** Velocidade no fundo da faixa (px/s); a rampa começa em 0 na entrada dela. */
    val autoSpeed = dp(360f)
    /** Trilha baixa: a vizinha também responde até este tanto do centro dela (alvo ≥ 32 dp). */
    val laneTouchReach = dp(16f)
    val autoIntent = dp(4f)
    val axisSlop = dp(8f)           // depois do toque longo, o eixo se decide com 8 dp
    val holdJitter = dp(2f)         // tremor de um dedo parado (mais que isto é movimento)
    val flingMin = dp(50f)          // px/s

    // --- Guias ----------------------------------------------------------------------------
    val guide = dp(1f)
    val reorderLine = dp(2f)
    val reorderDot = dp(3f)
}
