package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors

/**
 * CONTRATO da timeline. A casca dá a área; a timeline desenha régua,
 * cabeçote, camadas, keyframes e trata os gestos, lendo e escrevendo SÓ pelo
 * [EditorStore].
 *
 * @param compact modo compacto da A.01 (painel aberto): uma linha, setas
 *   ‹ › trocam de camada, cabeçote vermelho.
 * @param onEmptyTap toque no vazio (a casca fecha o painel ou desseleciona).
 * @param onKeyframeTap toque num losango: a timeline já chamou
 *   `store.selectKeyframe`; a casca decide se abre o editor de curva.
 */
@Composable
fun Timeline(
    store: EditorStore,
    compact: Boolean,
    onEmptyTap: () -> Unit,
    modifier: Modifier = Modifier,
    onKeyframeTap: (layer: Long, key: KeyframeRow) -> Unit = { _, _ -> },
) {
    Box(modifier.background(AureaColors.Stage))
}
