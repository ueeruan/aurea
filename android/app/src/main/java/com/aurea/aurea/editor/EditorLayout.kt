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
internal enum class SheetContent { None, Hint, Dock, Panel, Curve, Batch, Adding }

internal object EditorLayout {
    const val TOP_BAR = 44f
    const val TRANSPORT = 46f
    const val STRIP = 8f
    const val TIMELINE_MIN = 110f
    const val PREVIEW_MIN = 96f
    private const val PREVIEW_FRACTION_MAX = 0.50f   // EditorSession.alturaDoPreview
    private const val DOCK_FRACTION = 0.40f          // EditorSession.alturaDaFolha
    private const val PANEL_FRACTION = 0.46f
    private const val ADD_BODY = 280f                // abas 54 + 3 fileiras de ladrilhos + paginação
    private const val SHEET_HANDLE = 12f             // ContextSheet.handleHeight
    private const val BATCH_BODY = 124f
    private const val HINT_BODY = 30f

    fun workspace(totalHeight: Float) = max(0f, totalHeight - TOP_BAR - TRANSPORT - STRIP)

    fun solve(totalHeight: Float, content: SheetContent, fullscreen: Boolean): EditorMetrics {
        if (fullscreen) {
            return EditorMetrics(0f, max(0f, totalHeight - TRANSPORT), 0f, TRANSPORT, 0f, 0f)
        }
        val ws = workspace(totalHeight)
        // Stable preview height while floating add controls open and close.
        var preview = (totalHeight * 0.54f)
            .coerceIn(PREVIEW_MIN, max(PREVIEW_MIN, ws - TIMELINE_MIN))

        val sheetFraction = when (content) {
            SheetContent.None -> 0f
            SheetContent.Hint -> if (ws > 0f) (SHEET_HANDLE + HINT_BODY) / ws else 0f
            SheetContent.Batch -> if (ws > 0f) (SHEET_HANDLE + BATCH_BODY) / ws else 0f
            SheetContent.Dock -> DOCK_FRACTION
            SheetContent.Panel -> PANEL_FRACTION
            SheetContent.Curve -> if (ws > 0f) 280f / ws else 0f
            SheetContent.Adding -> if (ws > 0f) (SHEET_HANDLE + ADD_BODY) / ws else 0f
        }
        var sheet = if (content != SheetContent.None) ws * sheetFraction.coerceIn(0f, 0.60f) else 0f
        // Camada escolhida ou painel: piso de 90 (uma linha de camada viva).
        // Nada escolhido: 120. Adicionando: o menu pode cobrir a timeline.
        val floor = when (content) {
            SheetContent.Adding, SheetContent.Dock, SheetContent.Panel, SheetContent.Curve, SheetContent.Batch -> 90f
            else -> 120f
        }
        // Editing controls get usable space first; only the overview keeps
        // the tall preview. Never shrink every button to preserve the preview.
        sheet = max(sheet, when (content) {
            SheetContent.Panel -> 336f
            SheetContent.Dock -> 240f
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
        return EditorMetrics(TOP_BAR, preview, STRIP, TRANSPORT, max(0f, timeline), max(0f, sheet))
    }

    /** Layout largo A.01: largura ≥ 600 em paisagem, ou ≥ 900. */
    fun isWide(width: Float, height: Float) = (width >= 600f && width > height) || width >= 900f

    fun wideTimeline(totalHeight: Float) = ((totalHeight - TOP_BAR - TRANSPORT - STRIP) * 0.34f).coerceIn(88f, 280f)

    fun wideSheetWidth(width: Float) = (width * 0.4f).coerceIn(280f, 380f)
}
