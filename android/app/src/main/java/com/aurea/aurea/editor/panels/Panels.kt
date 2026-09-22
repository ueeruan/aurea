package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.theme.AureaColors

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
    Shape,         // cor e preenchimento / editar forma
    Text,          // editar texto
    Parent,        // seguir outra camada (parentesco)
}

/**
 * Conteúdo do painel aberto. `onClose` = voltar (fecha o painel).
 *
 * As duas folhas de ajuste (teclado numérico e seletor de cor) moram AQUI, uma
 * vez só: qualquer linha de qualquer painel pede a folha e ela sobe por cima da
 * área do painel, que é baixa demais para um teclado.
 */
@Composable
fun PanelContent(
    store: EditorStore,
    panel: EditorPanel,
    onClose: () -> Unit,
    onOpenPanel: (EditorPanel) -> Unit,
    onOpenEffectsBrowser: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var color by remember { mutableStateOf<ColorRequest?>(null) }
    // De onde o editor de curva foi aberto: o ‹ do trilho dele volta para lá.
    var returnTo by remember { mutableStateOf<EditorPanel?>(null) }
    LaunchedEffect(panel) { if (panel != EditorPanel.Curve) returnTo = panel }
    // A aba de Transformar sobe até aqui porque o TÍTULO a escreve
    // ("Transformar · Escala"); corpo e título não podem discordar.
    var transformTab by rememberSaveable { mutableStateOf(TransformTab.Mover) }

    val close by rememberUpdatedState(onClose)
    val open by rememberUpdatedState(onOpenPanel)
    val browser by rememberUpdatedState(onOpenEffectsBrowser)
    // Uma instância só: os painéis filhos pulam a recomposição quando a casca
    // recompõe por outro motivo (o playhead, por exemplo).
    val env = remember(store) {
        PanelEnv(
            store = store,
            onClose = { close() },
            onOpenPanel = { open(it) },
            onOpenEffectsBrowser = { browser() },
            openKeypad = { keypad = it },
            openColor = { color = it },
            returnTo = { returnTo },
        )
    }
    // Só a existência da camada importa aqui — não o detalhe que muda a cada quadro.
    val hasLayer by remember(store) { derivedStateOf { store.detail != null } }

    val title = when (panel) {
        EditorPanel.Transform -> "Transformar · ${transformTab.title}"
        EditorPanel.Effects -> "Efeitos"
        EditorPanel.Curve -> "Easing curve"
        EditorPanel.Appearance -> "Mesclagem e opacidade"
        EditorPanel.Speed -> "Tempo e velocidade"
        EditorPanel.Audio -> "Som"
        EditorPanel.Shape -> "Cor e preenchimento"
        EditorPanel.Text -> "Texto"
        EditorPanel.Parent -> "Seguir outra camada"
    }

    Column(modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        PanelHeader(title, onBack = onClose)
        Box(Modifier.fillMaxWidth().weight(1f)) {
            if (hasLayer) {
                when (panel) {
                    EditorPanel.Transform -> TransformPanel(env, transformTab, onTab = { transformTab = it })
                    EditorPanel.Effects -> EffectsPanel(env)
                    EditorPanel.Curve -> CurvePanel(env)
                    EditorPanel.Appearance -> AppearancePanel(env)
                    EditorPanel.Speed -> SpeedPanel(env)
                    EditorPanel.Audio -> AudioPanel(env)
                    EditorPanel.Shape -> ShapePanel(env)
                    EditorPanel.Text -> TextPanel(env)
                    EditorPanel.Parent -> ParentPanel(env)
                }
            }
        }
    }

    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    color?.let { r ->
        DisposableEffect(r) { onDispose { r.finish() } }
        ColorPickerSheet(
            initial = r.initial,
            onChange = r.onChange,
            onDone = {
                color = null
                r.finish()
            },
        )
    }
}

/** Galeria de efeitos (folha modal). Adiciona o efeito às camadas escolhidas. */
@Composable
fun EffectsBrowserSheet(store: EditorStore, onDismiss: () -> Unit) {
    EffectsBrowser(store, onDismiss)
}
