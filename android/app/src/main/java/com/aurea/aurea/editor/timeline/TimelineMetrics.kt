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
internal class TimelineMetrics(val density: Float, val fontScale: Float = 1f, val referenceTrial: Boolean = false) {
    private fun dp(v: Float) = v * density

    // --- Régua e linhas -------------------------------------------------------
    val rulerTicks = dp(if (referenceTrial) 14f else AureaTimeline.RulerTicks.value)
    /** Rows and clip padding are distinct; timestamps never include the visual gutter. */
    val rowsTop = if (referenceTrial) dp(42f + 14f * (fontScale.coerceIn(1f, 2f) - 1f)) else dp(AureaTimeline.RulerTicks.value + AureaTimeline.RulerGap.value)
    val barTop = dp(if (referenceTrial) 2.5f else 0f)
    // Reference density, expanded for larger system text; drawing and hits share it.
    val row = dp(if (referenceTrial) 28f + 24f * (fontScale.coerceIn(1f, 2f) - 1f) else AureaTimeline.Row.value)
    val bar = dp(if (referenceTrial) 23f + 20f * (fontScale.coerceIn(1f, 2f) - 1f) else AureaTimeline.Bar.value)
    // Omega (área 03): cartão com cantos de 3 dp (antes 1,5).
    val barRadius = dp(if (referenceTrial) 3f else AureaTimeline.BarRadius.value)
    val track = dp(AureaTimeline.KeyframeTrack.value)
    /** Começo da faixa dos losangos (medido do topo da barra). */
    val trackTop = bar - track
    /** Folga abaixo das linhas (a última não cola na borda). */
    val bottomPad = dp(if (referenceTrial) 0f else 24f)   // Omega: sem sobra (os +74 eram do "+" excluído)

    // --- Pílula da fileira (olho + miniatura/tipo), colada à esquerda -----------
    /** Largura da pílula; as barras passam por baixo dela. */
    // Omega: a pílula vai de x −8 a 62; só 62 dp ficam à vista e as barras passam por baixo.
    val headerColumn = dp(if (referenceTrial) 62f else AureaTimeline.HeaderColumn.value)
    /** A pílula: 28 de altura, começa 2 dp acima da barra, raio 14 só à direita. */
    val pillInset = if (referenceTrial) dp(1.5f) else dp(AureaTimeline.RowPillInset.value)
    val pillHeight = if (referenceTrial) row - dp(2f) else dp(AureaTimeline.RowPillHeight.value)
    val pillRadius = pillHeight / 2f
    /** Centro vertical da pílula, medido do topo da barra. */
    val pillCy = pillHeight / 2f - pillInset
    /** Chevron das trilhas de propriedade (coluna estreita à esquerda). */
    val gutterIconCx = dp(17f)
    val gutterEye = AureaTimeline.GutterEyeSize.value
    /** Olho de 20, centrado em x 16. */
    val gutterEyeCx = dp(if (referenceTrial) 17f else 16f)
    /** Miniatura / "T" / ponto / nota de 20, de x 38 a 58. */
    val glyphBox = dp(AureaTimeline.GlyphBox.value)
    val glyphBoxLeft = dp(if (referenceTrial) 34f else 38f)
    val glyphBoxRadius = dp(3f)
    val glyphIcon = 12f
    val glyphDot = dp(10f)
    val gutterLock = 10f
    val gutterLockCx = dp(66f)
    /** Toque do olho: o começo da pílula até antes do quadradinho; o resto é do tipo (abre as trilhas). */
    val eyeHitRight = dp(if (referenceTrial) 36f else 28f)
    /** Onde começa o nome de uma trilha de propriedade (depois do chevron). */
    val laneLabelLeft = dp(30f)

    // --- Régua ------------------------------------------------------------------
    val tickMajorTop = 0f
    val tickMinorTop = 0f
    val tickBottom = dp(12f)
    val tickMinorBottom = dp(6f)
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
    /** O toque do corpo inclui mais 4 dp abaixo da barra. */
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

    // --- Alças externas: a borda editável continua em x(início) e x(fim) ----------
    val trimWidth = dp(if (referenceTrial) 30f else 16f)
    val trimTop = dp(if (referenceTrial) 0f else 2f)
    val trimInsetStart = trimWidth
    val trimInsetEnd = 0f
    val trimRadius = dp(if (referenceTrial) 1.5f else 4f)
    val gripWidth = dp(2f)
    val gripHeight = dp(10f)
    /** Folga de toque para FORA da barra, além do desenho (a alça de 16 é estreita para o dedo). */
    val trimTouchOut = dp(13f)

    // --- Losango ------------------------------------------------------------------
    val diamond = dp(if (referenceTrial) 10f else 9f)
    val diamondRadius = dp(2f)
    val diamondStroke = dp(1.2f)
    /** O losango inteiro cabe na barra; X continua vindo exclusivamente do tempo. */
    val diamondCyNormal = bar / 2f
    val diamondCyCompact = bar / 2f
    val keyTouchHalf = dp(14f)      // alvo de 28 da A.01 (núcleo: ganha até das alças)
    /** Folga do losango até o alvo mínimo de 48 dp; cede às alças, tampa e setas. */
    val keyHitHalf = dp(24f)
    /** Histerese do arrasto de losango: o frame só troca depois de passar meio frame + isto. */
    val keyDragHysteresis = dp(3f)
    val keyGlyphHalf = dp(6f)       // desenho compacto; alvo de toque continua 48 dp
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
    val playhead = dp(if (referenceTrial) 1.5f else AureaTimeline.Playhead.value)
    val knob = dp(AureaTimeline.PlayheadKnob.value)
    val knobRadius = dp(2f)
    /** Triângulo do cabeçote no alto da régua (camada escolhida): 10 × 8, em destaque. */
    val markerWidth = dp(10f)
    val markerHeight = dp(8f)
    /** Relógio 16 sp bold abaixo dos riscos; sublinhado branco em y 30. */
    val timecodeTop = dp(14f)
    val timecodeBaseline = dp(28f + if (referenceTrial) 14f * (fontScale.coerceIn(1f, 2f) - 1f) else 0f)
    val underlineTop = dp(30f + if (referenceTrial) 14f * (fontScale.coerceIn(1f, 2f) - 1f) else 0f)
    val underlineHeight = dp(1.5f)
    /** Estilo caixa (camada escolhida / efeitos): 26 de altura, borda 1,5 em destaque, raio 4. */
    val timecodeBoxTop = dp(13f)
    val timecodeBoxHeight = dp(26f)
    val timecodeBoxPad = dp(6f)
    val timecodeBoxStroke = dp(1.5f)
    val timecodeBoxRadius = dp(4f)
    /** Rótulo da régua perto do relógio some (não disputa leitura com ele). */
    val timecodeZoneHalf = dp(44f)
    /** O fio começa abaixo do relógio e atravessa toda a área livre da timeline. */
    val playheadTop = if (referenceTrial) dp(35f + 14f * (fontScale.coerceIn(1f, 2f) - 1f)) else dp(36f)

    // --- Gestos -------------------------------------------------------------------------
    val snapClip = dp(12f)          // ímã de clipe e alça
    val snapKey = dp(8f)            // ímã de losango
    /** Faixa da auto-rolagem: 48 dp no máximo (menos numa janela baixa, ver `AutoScroll.zone`). */
    val autoEdge = dp(if (referenceTrial) 38f else 48f)
    /** Velocidade no fundo da faixa (px/s); a rampa começa em 0 na entrada dela. */
    val autoSpeed = dp(if (referenceTrial) 120f else 360f)
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

    // --- Omega 0.3.2 (área 03) ----------------------------------------------------------
    private val textScale = fontScale.coerceIn(1f, 2f) - 1f
    /** Pílula do cabeçalho: começa 8 dp fora da tela (x −8) e vai até [headerColumn]. */
    val omegaPillLeft = dp(-8f)
    /** Topo da pílula medido do topo da LINHA (1 dp de margem em cima e embaixo). */
    val omegaPillTop = dp(1f)
    val omegaPillBorder = dp(0.5f)
    /** Faixa da etiqueta à esquerda: 2 dp à vista, de y 1,8 até 2 dp antes do fim da linha. */
    val omegaTagWidth = dp(2f)
    val omegaTagTop = dp(1.8f)
    val omegaTagBottomGap = dp(2f)
    val omegaEyeGlyph = 18f
    val omegaCheckGlyph = 16f
    /** Miniatura da camada no cabeçalho: 20 × 23 em x 34..54 (ajustada). */
    val omegaThumbLeft = dp(34f)
    val omegaThumbWidth = dp(20f)
    val omegaThumbHeight = dp(23f)
    /** Recorte: a miniatura anda 7 dp para a esquerda e fica com 83 %. */
    val omegaClipShift = dp(7f)
    val omegaClipScale = 0.83f
    /** Indicador "↳" de 14 × 16 no canto de baixo à direita do cabeçalho. */
    val omegaClipIndicatorCx = dp(55f)
    val omegaClipIndicatorCy = dp(20f)
    /** Cadeado no canto de cima à direita da miniatura. */
    val omegaLockCx = dp(50f)
    val omegaLockCy = dp(7f)
    /** Alça ≡ da linha: 8 × 7 dp a 5 dp da borda direita; o toque longo nela reordena. */
    val omegaGripRight = dp(5f)
    val omegaGripWidth = dp(8f)
    val omegaGripZone = dp(30f)
    /** Tempo atual: aba de 70 × 18 com o canto de cima à esquerda chanfrado. */
    val omegaTabTop = dp(18f + 7f * textScale)
    val omegaTabHeight = dp(18f + 7f * textScale)
    val omegaTabMinWidth = dp(70f)
    val omegaTabPad = dp(5f)
    val omegaTabRadius = dp(2f)
    val omegaTabChamfer = dp(6f)
    val omegaTabStroke = dp(1f)
    val omegaTimecodeBaseline = omegaTabTop + dp(13f + 6f * textScale)
    /** Zona do toque no tempo atual: 100 × 38 centrada, de y 5 a 43. */
    val omegaTimeTouchHalf = dp(50f)
    val omegaTimeTouchTop = dp(5f)
    val omegaTimeTouchBottom = dp(43f)
    /** Tiques: segundo 18, meio segundo 12, quadro 5 (dp); 1 dp de largura. */
    val omegaTick = dp(1f)
    /** Marcador: fio de 1 dp pela régua e bandeirola de 5 dp (2 dp; 4 dp no quadro atual). */
    val omegaMarkerFlag = dp(5f)
    val omegaMarkerFlagTip = dp(1f)
    val omegaMarkerFlagSmall = dp(2f)
    val omegaMarkerFlagLarge = dp(4f)
    /** Toque na régua: até 21 dp de um marcador vai até ele. */
    val omegaMarkerNear = dp(21f)
    /** Rótulo do clipe: 14 dp para dentro do cartão. */
    val omegaLabelInset = dp(14f)
    /** Losangos: 0,65 × barra em edição, 0,4 × barra nos outros parâmetros (proporção 2:3). */
    val omegaKeyEdit = bar * 0.65f
    val omegaKeyAlt = bar * 0.4f
    val omegaKeyStroke = dp(1f)
    val omegaKeyAltStroke = dp(0.5f)
    val omegaKeyRing = dp(4f)
    /** Zebra de quadros: cada faixa tem pelo menos 4 dp (≈ 10 px na referência). */
    val omegaZebraMin = dp(4f)
    /** Onda: barras de 1 dp. */
    val omegaWave = dp(1f)
    /** Sombra do cartão (0,5 dp) e a do clipe levantado (8 dp). */
    val omegaShadow = dp(0.5f)
    val omegaLiftShadow = dp(8f)
    /** Alças do foco: pílula de 26 × barra com a ponta de fora redonda. */
    val omegaHandle = dp(26f)
    val omegaHandleChevron = dp(5f)
    val omegaHandleStroke = dp(2f)
}
