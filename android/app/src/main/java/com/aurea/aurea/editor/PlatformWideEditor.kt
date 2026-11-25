package com.aurea.aurea.editor

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.aurea.aurea.state.EditorStore

/**
 * O layout largo do editor (tablet deitado / paisagem): aqui, o [WideEditor].
 *
 * Ponto de troca do Aurea PC: lá, desktop/src/main/kotlin substitui este
 * arquivo pelo layout de computador (biblioteca, palco, propriedades e a
 * timeline na largura toda). No Android e no iOS nada muda.
 */
@Composable
internal fun PlatformWideEditor(
    store: EditorStore,
    ui: EditorUi,
    content: SheetContent,
    totalHeight: Float,
    sheetWidth: Float,
    stage: @Composable (Modifier) -> Unit,
    floatingText: Boolean = false,
) = WideEditor(store, ui, content, totalHeight, sheetWidth, stage, floatingText)
