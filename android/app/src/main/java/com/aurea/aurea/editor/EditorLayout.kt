package com.aurea.aurea.editor

import kotlin.math.max
import kotlin.math.min

/**
 * As alturas das zonas do editor, resolvidas de uma vez. Tudo em dp.
 *
 * Omega fundação (F6, geometria da referência): no telefone em retrato o topo
 * tem 42, o transporte 46 e embaixo fica uma ZONA FIXA de 280 (timeline +
 * barra de adicionar, ou a timeline com o painel da camada de 200 por cima).
 * A prévia é o que sobra e NUNCA muda de altura ao abrir ou fechar painel,
 * adicionar ou escolher camada — o que também mantém a SurfaceView do motor
 * parada (a superfície só muda na tela cheia ou ao girar o aparelho). Sem a
 * divisa arrastável no retrato (decisão D2). 0.3.3 recursos: a divisa volta
 * (`preferred` em [EditorLayout.solve]). O layout largo continua o do AUREA.
 */
internal data class EditorMetrics(
    val topBar: Float,
    val preview: Float,
    val strip: Float,
    val transport: Float,
    val timeline: Float,
    val sheet: Float,
    /**
     * Omega fundação: a folha é desenhada POR CIMA da timeline (o painel da
     * camada de 200 dp cobre a zona por baixo). Falso = empilhada embaixo dela.
     */
    val overlay: Boolean = false,
) {
    /** A zona inferior inteira: timeline + folha empilhada (com [overlay], só a timeline). */
    val zone: Float get() = timeline + if (overlay) 0f else sheet

    /** A parte da timeline à vista (com [overlay], o que o painel não cobre: 80 dp). */
    val visibleTimeline: Float get() = if (overlay) max(0f, timeline - sheet) else timeline

    /** O que a timeline tem coberto embaixo (o `obscuredBottom` da área 03). */
    val obscuredTimeline: Float get() = if (overlay) min(sheet, timeline) else 0f

    /** Soma das faixas empilhadas: é sempre a altura da área do editor. */
    val total: Float get() = topBar + preview + strip + transport + zone
}

/** O que ocupa a zona do painel contextual. */
internal enum class SheetContent { None, Hint, Dock, Panel, Curve, Batch, Adding, AddBar }

internal object EditorLayout {
    /** Barra superior da referência (botões de 40 com alvo de 44+). */
    const val TOP_BAR = 42f
    /** Barra de reprodução da referência. */
    const val TRANSPORT = 46f
    const val STRIP = 0f
    /** Prévia natural do layout antigo (fica para quem lê o token). */
    const val PREVIEW_NATURAL_FRACTION = 0.45f
    const val TIMELINE_MIN = 110f
    const val PREVIEW_MIN = 96f
    private const val SHEET_HANDLE = 12f             // ContextSheet.handleHeight
    /** Lote: 4 + tempo 52 + 8 + tela 48 + 8 de respiro. */
    private const val BATCH_BODY = 120f
    private const val HINT_BODY = 30f
    /** A barra fixa de adicionar do AUREA (fica, exceção do dono): 6 + categorias de 64. */
    const val ADD_BAR = 70f
    // --- Omega adicionar (área 08, P0-1) ----------------------------------------
    /**
     * O conteúdo do botão aberto na barra de adicionar: a área de conteúdo do
     * popup da referência (251 − 58 de abas = 193). Fica POR CIMA da timeline,
     * logo acima da barra (que não sai do lugar): a zona continua 210 + 70.
     */
    const val ADD_CONTENT = 193f

    /** Altura do conteúdo do adicionar nesta geometria (nunca maior que a timeline à vista). */
    fun addContent(m: EditorMetrics): Float = min(ADD_CONTENT, m.timeline)

    /** Fileira rápida e fichas compactas; todos os alvos têm ao menos 48 dp. */
    const val DOCK_QUICK = 48f
    const val DOCK_TILE = 60f
    /** 8 de topo, rápida, duas fichas com intervalo de 8 e 8 de base. */
    const val DOCK = 8f + DOCK_QUICK + 2f * (8f + DOCK_TILE) + 8f

    // --- Omega fundação (F6): zona inferior fixa -------------------------------
    /** A zona inferior fixa do retrato (`edit_timeline_height` da referência). */
    const val BOTTOM_ZONE = 280f
    /** O painel da camada e os painéis de propriedade (`edit_panel_height`). */
    const val LAYER_PANEL = 200f
    /** O que sobra da timeline com o painel aberto: régua 42 + uma linha. */
    const val TIMELINE_FOCUS = 80f

    /** O painel cresce com a fonte do sistema: 200 em 1×, 240 em 2×. */
    fun layerPanel(fontScale: Float = 1f): Float = LAYER_PANEL + 40f * (fontScale.coerceIn(1f, 2f) - 1f)

    /**
     * 0.3.6: o teto dos painéis de propriedade. Com a divisa no alto (ou a Edição numa
     * tela alta) o painel ocupava a zona inteira — 600 dp e mais para meia dúzia de
     * linhas. Passando disso, a timeline fica com o resto. Cresce com a fonte.
     */
    const val PANEL_MAX = 360f
    fun panelMax(fontScale: Float = 1f): Float = PANEL_MAX + 60f * (fontScale.coerceIn(1f, 2f) - 1f)

    // 0.3.4 painéis: o painel de propriedade sobe até logo abaixo da linha em foco.
    /** Régua da timeline (`TimelineMetrics.rowsTop`): 42 em 1×, +14 por 1× de fonte. */
    const val FOCUS_RULER = 42f
    /** A linha da camada em foco (`TimelineMetrics.row`): 28 em 1×, +24 por 1× de fonte. */
    const val FOCUS_ROW = 28f
    /** Folga entre a linha em foco e o topo do painel. */
    const val FOCUS_GAP = 10f

    /**
     * A faixa de timeline que fica à vista com o painel aberto: régua + a linha
     * da camada em foco + a folga ([TIMELINE_FOCUS] = 80 em 1×). Cresce com a
     * fonte junto com a régua e a linha (senão a linha some atrás do painel).
     */
    fun timelineFocus(fontScale: Float = 1f): Float {
        val s = fontScale.coerceIn(1f, 2f) - 1f
        return FOCUS_RULER + 14f * s + FOCUS_ROW + 24f * s + FOCUS_GAP
    }

    /** A zona inferior: o painel + a faixa de foco da timeline (280 em 1×). */
    fun bottomZone(fontScale: Float = 1f): Float = layerPanel(fontScale) + timelineFocus(fontScale)

    fun dockTile(fontScale: Float = 1f): Float = DOCK_TILE + 28f * (fontScale.coerceIn(1f, 2f) - 1f)

    /** A doca com [rows] fileiras de fichas (1 ou 2): a altura é a do conteúdo. */
    fun dock(rows: Int, fontScale: Float = 1f): Float = 8f + DOCK_QUICK + rows.coerceIn(1, 2) * (8f + dockTile(fontScale)) + 8f

    /** Folga do palco em volta do quadro ajustado (layout antigo; fica para quem lê o token). */
    const val PREVIEW_FIT_MARGIN = 16f

    fun workspace(totalHeight: Float) = max(0f, totalHeight - TOP_BAR - TRANSPORT - STRIP)

    // 0.3.3 recursos: a divisa palco/timeline volta (arrastar o transporte na vertical).
    /** A menor zona inferior que a divisa deixa: timeline mínima (110) + barra de adicionar (70). */
    const val ZONE_MIN = TIMELINE_MIN + ADD_BAR

    /** O palco mais alto possível arrastando a divisa: a zona nunca fica abaixo de [ZONE_MIN]. */
    fun maxPreview(totalHeight: Float) = max(PREVIEW_MIN, workspace(totalHeight) - ZONE_MIN)

    /**
     * O retrato do telefone (e do tablet em pé): prévia = área − 42 − 46 − zona,
     * igual em todos os [SheetContent]. Dentro da zona:
     * - [SheetContent.AddBar]: timeline (210) e a barra de adicionar (70) embaixo;
     * - [SheetContent.Dock], [SheetContent.Panel], [SheetContent.Curve]: timeline na
     *   zona inteira e a folha (200) POR CIMA ([EditorMetrics.overlay]); ficam 80 de timeline;
     * - [SheetContent.None]: só a timeline; Hint, Batch e Adding: empilhados embaixo dela.
     * Tela baixa demais: a prévia fica no piso e a zona encolhe (a soma é sempre a altura).
     *
     * 0.3.3 recursos: [preferred] > 0 é a altura da prévia que a pessoa escolheu
     * arrastando a divisa (guardada no aparelho), de [PREVIEW_MIN] até
     * [maxPreview]: a zona cresce (mais timeline) ou encolhe até [ZONE_MIN]. O painel
     * da camada (Dock/Panel/Curve) e o adicionar aberto precisam da zona inteira:
     * com a prévia maior que a automática, ela devolve o espaço enquanto eles estão
     * abertos (como na 0.3.2), e o painel continua com 200 + 80 de timeline.
     * Sem preferência, a prévia é a automática (área − 42 − 46 − 280).
     * 0.3.4 painéis: Panel e Curve ocupam a zona inteira menos [timelineFocus]
     * (régua + a linha em foco + folga): com a divisa no alto o painel cresce em vez
     * de deixar um vazio de timeline por cima dele (200 + 80 só na zona padrão).
     * [width] e [aspect] ficam na assinatura para quem já chamava.
     */
    @Suppress("UNUSED_PARAMETER")
    fun solve(
        totalHeight: Float,
        content: SheetContent,
        fullscreen: Boolean,
        width: Float = 0f,
        aspect: Float = 0f,
        preferred: Float = 0f,
        dockRows: Int = 2,
        fontScale: Float = 1f,
    ): EditorMetrics {
        if (fullscreen) {
            return EditorMetrics(0f, max(0f, totalHeight - TRANSPORT), 0f, TRANSPORT, 0f, 0f)
        }
        val ws = workspace(totalHeight)
        val panel = layerPanel(fontScale)
        val focus = timelineFocus(fontScale)
        val previewFloor = min(PREVIEW_MIN, ws * 0.25f)
        val automatic = max(previewFloor, ws - bottomZone(fontScale)).coerceAtMost(ws)
        // 0.3.3 recursos: a altura arrastada na divisa (0 = automática).
        val chosen = if (preferred > 0f && preferred.isFinite()) {
            preferred.coerceIn(previewFloor, max(previewFloor, ws - ZONE_MIN)).coerceAtMost(ws)
        } else automatic
        val needsWholeZone = content == SheetContent.Dock || content == SheetContent.Panel ||
            content == SheetContent.Curve || content == SheetContent.Adding
        val preview = if (needsWholeZone) min(chosen, automatic) else chosen
        val zone = ws - preview

        fun stacked(body: Float): EditorMetrics {
            val sheet = body.coerceIn(0f, zone)
            return EditorMetrics(TOP_BAR, preview, STRIP, TRANSPORT, zone - sheet, sheet)
        }
        fun over(body: Float): EditorMetrics {
            // Ao menos a faixa de foco da timeline fica à vista (menos numa tela baixa).
            // 0.3.4 painéis: o teto é metade da zona (com 0,3 a faixa de foco da fonte grande cortava a linha).
            val sheet = min(body, max(0f, zone - min(focus, zone * 0.5f)))
            return EditorMetrics(TOP_BAR, preview, STRIP, TRANSPORT, zone, max(0f, sheet), overlay = true)
        }
        return when (content) {
            SheetContent.None -> stacked(0f)
            SheetContent.AddBar -> stacked(ADD_BAR)
            SheetContent.Hint -> stacked(SHEET_HANDLE + HINT_BODY)
            SheetContent.Batch -> stacked(SHEET_HANDLE + BATCH_BODY)
            // Omega adicionar: a mesma zona da barra (210 + 70); o conteúdo fica por cima da timeline.
            SheetContent.Adding -> stacked(ADD_BAR)
            SheetContent.Dock -> over(min(dock(dockRows, fontScale), panel))
            // 0.3.4 painéis: o painel de propriedade ocupa a zona inteira até logo abaixo
            // da linha em foco (régua + linha + folga), também com a divisa no alto:
            // nada de 200 fixo com um vazio de timeline por cima.
            // 0.3.6: até [panelMax]; o que passa disso fica com a timeline.
            SheetContent.Panel, SheetContent.Curve -> over(min(zone, panelMax(fontScale)))
        }
    }

    /**
     * Layout largo A.01: SÓ em paisagem com largura ≥ 600. Tablet EM PÉ usa o
     * layout do celular (palco, transporte, timeline e doca empilhados): o
     * "≥ 900 mesmo em pé" mandava tablet grande para o largo, com a coluna da
     * direita só com a dica — "a UI inteira sumiu" no tablet (par do iOS).
     */
    fun isWide(width: Float, height: Float) = width >= 520f && width > height && height < 600f || width >= 900f && width > height

    fun wideTimeline(totalHeight: Float) = ((totalHeight - TOP_BAR - TRANSPORT - STRIP) * 0.34f).coerceIn(88f, 280f)

    fun wideSheetWidth(width: Float) = (width * 0.4f).coerceIn(240f, 420f)
}
