package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.state.EditorStore
import java.text.Normalizer

/** Busca sem acento e sem caixa ("saturacao" acha "Saturação"). */
internal fun normalizeSearch(s: String): String =
    Normalizer.normalize(s, Normalizer.Form.NFD).replace(Regex("\\p{Mn}+"), "").lowercase().trim()

/**
 * O painel que cada FERRAMENTA-EFEITO abre (legendas, rastreio de câmera,
 * máscara). As ferramentas moram no catálogo de efeitos; o painel delas é o de
 * sempre.
 */
internal fun effectToolPanel(tool: EffectTool): EditorPanel = when (tool) {
    EffectTool.Captions -> EditorPanel.Captions
    EffectTool.CameraTrack -> EditorPanel.Tracking
    EffectTool.Mask -> EditorPanel.Mask
}

/**
 * ABRE uma ferramenta-efeito na camada escolhida: a única porta que o resto do
 * app precisa (catálogo, pilha da camada, busca de comandos). A Máscara fica
 * na pilha da camada desde aqui, mesmo que o painel feche sem caminho nenhum.
 */
internal fun openEffectTool(store: EditorStore, onOpenPanel: (EditorPanel) -> Unit, tool: EffectTool) {
    if (tool == EffectTool.Mask) store.addMaskTool()
    if (tool == EffectTool.CameraTrack) store.cameraTrackerVisible = true
    onOpenPanel(effectToolPanel(tool))
}

/** Mesma porta, de dentro de um painel. */
internal fun openEffectTool(env: PanelEnv, tool: EffectTool) = openEffectTool(env.store, env.onOpenPanel, tool)

/**
 * A TELA "ADICIONAR EFEITO" pedida de fora do painel Efeitos (o caminho normal
 * é o "+ Adicionar efeito" da própria pilha). Um toque no cartão adiciona às
 * camadas escolhidas e fecha; uma ferramenta fecha e abre o painel dela;
 * segurar favorita.
 */
@Composable
internal fun EffectsBrowser(store: EditorStore, onOpenPanel: (EditorPanel) -> Unit, onDismiss: () -> Unit) {
    val hasAudio by remember(store) { derivedStateOf { store.detail?.hasAudio == true } }
    EffectAddSheet(
        store = store,
        layerHasAudio = hasAudio,
        onPick = { e ->
            store.addEffect(e.typeId)
            onDismiss()
        },
        onTool = { tool ->
            onDismiss()
            openEffectTool(store, onOpenPanel, tool)
        },
        onDismiss = onDismiss,
    )
}
