package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
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
 */
@Composable
fun Timeline(
    store: EditorStore,
    compact: Boolean,
    onEmptyTap: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier.background(AureaColors.Stage))
}
