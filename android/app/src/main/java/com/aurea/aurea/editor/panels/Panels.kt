package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaType

/**
 * CONTRATO entre a casca do editor e os painéis.
 *
 * A casca decide QUANDO um painel está aberto e reserva a área (altura pela
 * fórmula da A.01). O painel desenha TUDO dentro dela, inclusive o cabeçalho
 * "‹ Título" e os trilhos laterais.
 */
enum class EditorPanel {
    Transform,     // "Movimento e transformação" (posição, rotação, escala, âncora, opacidade)
    Effects,       // pilha de efeitos da camada
    Curve,         // curva de easing do keyframe escolhido
    Appearance,    // opacidade e mesclagem
    Speed,         // velocidade/tempo
    Audio,         // volume
}

/** Conteúdo do painel aberto. `onClose` = voltar (fecha o painel). */
@Composable
fun PanelContent(
    store: EditorStore,
    panel: EditorPanel,
    onClose: () -> Unit,
    onOpenPanel: (EditorPanel) -> Unit,
    onOpenEffectsBrowser: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text("Painel ${panel.name}", style = AureaType.Body)
    }
}

/** Galeria de efeitos (folha modal). Adiciona o efeito às camadas escolhidas. */
@Composable
fun EffectsBrowserSheet(store: EditorStore, onDismiss: () -> Unit) {
    AureaModalSheet(onDismiss = onDismiss) {
        Text("Efeitos", style = AureaType.TitleLarge)
    }
}
