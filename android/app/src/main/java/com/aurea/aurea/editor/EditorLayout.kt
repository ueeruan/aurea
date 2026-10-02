package com.aurea.aurea.editor

import kotlin.math.max
import kotlin.math.min

/**
 * As alturas das zonas do editor, resolvidas de uma vez — porte literal de
 * `EditorLayoutMetrics.solve` + a fração do preview de `editor_screen.dart`
 * (A.01). Tudo em dp.
 *
 * A regra que a A.01 cobrava: abrir painel, adicionar ou trocar de aba NUNCA
 * move o preview. O painel tira espaço da timeline, e a timeline nunca fica
 * abaixo do piso. Isso também é o que mantém a SurfaceView do motor parada:
 * a superfície só muda de tamanho na tela cheia ou ao girar o aparelho.
 */
internal data class EditorMetrics(
    val topBar: Float,
    val preview: Float,
    val strip: Float,
    val transport: Float,
    val timeline: Float,
    val sheet: Float,
)

/** O que ocupa a zona do painel contextual (define a fração e o piso). */
internal enum class SheetContent { None, Hint, Dock, Panel, Curve, Batch, Adding, AddBar }

internal object EditorLayout {
    // Redesenho 2026-09-29 (mockup `docs/design/redesenho-2026-09-29/Editor.dc.html`):
    // topo 64, transporte 60, sem a faixa entre prévia e transporte (a divisa
    // arrastável mora no fundo do transporte).
    const val TOP_BAR = 64f
    const val TRANSPORT = 48f
    const val STRIP = 0f
    /** Prévia natural: 360 de 844 no mockup (fração da altura útil). */
    const val PREVIEW_NATURAL_FRACTION = 0.45f
    const val TIMELINE_MIN = 110f
    const val PREVIEW_MIN = 96f
    private const val PREVIEW_FRACTION_MAX = 0.50f   // EditorSession.alturaDoPreview
    private const val PANEL_FRACTION = 0.46f
    private const val ADD_BODY = 280f                // abas 54 + 3 fileiras de ladrilhos + paginação
    private const val SHEET_HANDLE = 12f             // ContextSheet.handleHeight
    /** Lote: 4 + tempo 52 + 8 + tela 48 + 8 + escalonar 48 + respiro (124 cortava o escalonar). */
    private const val BATCH_BODY = 180f
    private const val HINT_BODY = 30f
    /** A barra fixa de adicionar (sem camada escolhida): 6 + categorias de 64 (ícone 23 + nome 11). */
    const val ADD_BAR = 70f
    /** Doca da camada no jeito do AM: fileira rápida e fichas grandes numa folha arredondada. */
    const val DOCK_QUICK = 44f
    const val DOCK_TILE = 72f
    /** Altura inteira da doca: 4 + 10 + rápida + 2 × (10 + ficha) + 12 de respiro. */
    const val DOCK = 14f + DOCK_QUICK + 2f * (10f + DOCK_TILE) + 12f

    /** A doca com [rows] fileiras de fichas (1 ou 2): a altura é a do conteúdo. */
    fun dock(rows: Int): Float = 14f + DOCK_QUICK + rows.coerceIn(1, 2) * (10f + DOCK_TILE) + 12f

    /** Folga do palco em volta do quadro ajustado (as fichas ficam por cima do quadro). */
    const val PREVIEW_FIT_MARGIN = 16f

    fun workspace(totalHeight: Float) = max(0f, totalHeight - TOP_BAR - TRANSPORT - STRIP)

    /** O palco mais alto possível: a timeline nunca fica abaixo do piso. */
    fun maxPreview(totalHeight: Float) = max(PREVIEW_MIN, workspace(totalHeight) - TIMELINE_MIN)

    /**
     * [width]/[aspect] (largura ÷ altura do projeto): o palco nunca é mais alto
     * que o quadro na largura da tela + a folga — num projeto deitado as faixas
     * vazias acima e abaixo do quadro roubavam a timeline. Em pé (o quadro já
     * passa da fração) fica como sempre. [preferred] > 0: a altura que a pessoa
     * escolheu arrastando a divisa palco/transporte (vale sobre as duas).
     * [dockRows]: fileiras de fichas da doca da camada (a doca tem a altura dela).
     */
    fun solve(
        totalHeight: Float,
        content: SheetContent,
        fullscreen: Boolean,
        width: Float = 0f,
        aspect: Float = 0f,
        preferred: Float = 0f,
        dockRows: Int = 2,
    ): EditorMetrics {
        if (fullscreen) {
            return EditorMetrics(0f, max(0f, totalHeight - TRANSPORT), 0f, TRANSPORT, 0f, 0f)
        }
        val ws = workspace(totalHeight)
        // Stable preview height while floating add controls open and close.
        val natural = totalHeight * PREVIEW_NATURAL_FRACTION
        val fitted = if (width > 0f && aspect > 0f && aspect.isFinite()) min(natural, width / aspect + PREVIEW_FIT_MARGIN) else natural
        var preview = (if (preferred > 0f && preferred.isFinite()) preferred else fitted)
            .coerceIn(PREVIEW_MIN, maxPreview(totalHeight))

        val sheetFraction = when (content) {
            SheetContent.None -> 0f
            SheetContent.Hint -> if (ws > 0f) (SHEET_HANDLE + HINT_BODY) / ws else 0f
            SheetContent.Batch -> if (ws > 0f) (SHEET_HANDLE + BATCH_BODY) / ws else 0f
            SheetContent.Dock -> if (ws > 0f) DOCK / ws else 0f
            SheetContent.Panel -> PANEL_FRACTION
            SheetContent.Curve -> if (ws > 0f) 280f / ws else 0f
            SheetContent.Adding -> if (ws > 0f) (SHEET_HANDLE + ADD_BODY) / ws else 0f
            SheetContent.AddBar -> if (ws > 0f) ADD_BAR / ws else 0f
        }
        var sheet = if (content != SheetContent.None) ws * sheetFraction.coerceIn(0f, 0.60f) else 0f
        // Camada escolhida ou painel: piso de 90 (uma linha de camada viva).
        // Nada escolhido: 120. Adicionando: o menu pode cobrir a timeline.
        val floor = when (content) {
            SheetContent.Adding, SheetContent.Dock, SheetContent.Panel, SheetContent.Curve, SheetContent.Batch -> TIMELINE_MIN
            else -> 120f
        }
        // Editing controls get usable space first; only the overview keeps
        // the tall preview. Never shrink every button to preserve the preview.
        sheet = max(sheet, when (content) {
            SheetContent.Panel -> 336f
            else -> 0f
        }).coerceAtMost(max(0f, ws - PREVIEW_MIN - floor))
        preview = min(preview, max(PREVIEW_MIN, ws - sheet - floor))
        var timeline = ws - preview - sheet
        if (timeline < floor) {
            sheet = max(0f, sheet - (floor - timeline))
            timeline = ws - preview - sheet
        }
        // Uma tira de timeline menor que o mínimo útil não serve para nada:
        // cede o espaço inteiro ao painel em vez de virar um risco quebrado.
        if (floor == 0f && timeline > 0f && timeline < TIMELINE_MIN) {
            sheet += timeline
            timeline = 0f
        }
        // Doca de uma fileira: o palco fica onde a de duas o deixaria (trocar de
        // camada nunca mexe no palco); a sobra vai inteira para a timeline.
        if (content == SheetContent.Dock && dockRows < 2) {
            val spare = max(0f, sheet - dock(dockRows))
            sheet -= spare
            timeline += spare
        }
        return EditorMetrics(TOP_BAR, preview, STRIP, TRANSPORT, max(0f, timeline), max(0f, sheet))
    }

    /**
     * Layout largo A.01: SÓ em paisagem com largura ≥ 600. Tablet EM PÉ usa o
     * layout do celular (palco, transporte, timeline e doca empilhados): o
     * "≥ 900 mesmo em pé" mandava tablet grande para o largo, com a coluna da
     * direita só com a dica — "a UI inteira sumiu" no tablet (par do iOS).
     */
    fun isWide(width: Float, height: Float) = width >= 600f && width > height

    fun wideTimeline(totalHeight: Float) = ((totalHeight - TOP_BAR - TRANSPORT - STRIP) * 0.34f).coerceIn(88f, 280f)

    fun wideSheetWidth(width: Float) = (width * 0.4f).coerceIn(280f, 380f)
}
