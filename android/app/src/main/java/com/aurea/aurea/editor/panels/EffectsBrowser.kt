package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.state.EditorStore
import java.text.Normalizer

// 1.0.9 (2235: ANR no itel C671L com a main em Pattern.compile dentro de
// EffectScoreFields): o índice de busca chama isto milhares de vezes na
// composição (efeitos × 7 idiomas). O regex compilava a cada chamada.
private val CombiningMarks = Regex("\\p{Mn}+")

/** Busca sem acento e sem caixa ("saturacao" acha "Saturação"). */
internal fun normalizeSearch(s: String): String =
    if (s.all { it.code < 0x80 }) s.lowercase().trim()   // ASCII: nada a decompor
    else Normalizer.normalize(s, Normalizer.Form.NFD).replace(CombiningMarks, "").lowercase().trim()

/**
 * O painel que cada FERRAMENTA-EFEITO abre (legendas, rastreio de câmera,
 * máscara e as funções do AUREA). As ferramentas moram no catálogo de efeitos;
 * o painel delas é o de sempre.
 */
internal fun effectToolPanel(tool: EffectTool): EditorPanel = when (tool) {
    EffectTool.Captions -> EditorPanel.Captions
    EffectTool.CameraTrack -> EditorPanel.CameraTracking
    EffectTool.Mask -> EditorPanel.Mask
    // Funções do AUREA (13/11/2026, decisão 1 do dono: sem página 2 na grade).
    EffectTool.Animation -> EditorPanel.Animation          // F3: painel próprio (camada, texto, prontas, explodir)
    EffectTool.Tracking -> EditorPanel.Tracking            // ponto, planar, cantos, estabilizar
    EffectTool.PreciseCut -> EditorPanel.ClipEdit          // slip/roll/slide e congelar quadro
    EffectTool.Puppet -> EditorPanel.Effects               // o cartão do Fantoche no modo de pinos
    EffectTool.RigMocap -> EditorPanel.Element3D           // a aba Esqueleto
    EffectTool.ShapeOperators -> EditorPanel.VectorPaths   // a aba Operadores
    EffectTool.Layer3D -> EditorPanel.Layer3D
    EffectTool.LayerOptions -> EditorPanel.LayerOptions
    EffectTool.TextLayout -> EditorPanel.TextLayout
}

/**
 * ABRE uma ferramenta-efeito na camada escolhida: a única porta que o resto do
 * app precisa (catálogo, pilha da camada, busca de comandos). A Máscara fica
 * na pilha da camada desde aqui, mesmo que o painel feche sem caminho nenhum.
 */
internal fun openEffectTool(store: EditorStore, onOpenPanel: (EditorPanel) -> Unit, tool: EffectTool) {
    when (tool) {
        EffectTool.Mask -> store.addMaskTool()
        EffectTool.CameraTrack, EffectTool.Tracking -> {
            val layer = store.primary ?: return
            if (store.editingLayers.none { it.id == layer && it.kind == 1 && !it.locked }) return
            if (store.pointPick != null) store.cancelMotionPick()
            if (store.effects.none { it.typeId == tool.typeId }) store.addEffect(tool.typeId, listOf(layer))
        }
        EffectTool.Puppet -> if (!com.aurea.aurea.editor.preparePuppetTool(store)) return
        EffectTool.RigMocap -> { Element3DRequest.face = Face3D.Rig; Element3DRequest.tab = Tab3D.Rig }
        EffectTool.ShapeOperators -> VectorRequest.tab = VectorRequest.OPERATORS
        else -> Unit
    }
    onOpenPanel(effectToolPanel(tool))
}

/**
 * As ferramentas que valem para a camada escolhida (o catálogo e a busca de
 * comandos mostram só estas). Objeto 3D "importado" = o que vem de arquivo
 * (forma, texto e imagem 3D não têm esqueleto).
 */
internal fun toolsForSelection(store: EditorStore): List<EffectTool> {
    val kind = store.detail?.kind ?: 0
    val imported = kind == 10 && store.shape3d == null && store.text3d == null && store.image3d == null
    return com.aurea.aurea.effects.pickableTools(kind, vector = store.isVectorLayer, importedModel = imported)
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
    // Aurea PC: a galeria mora na aba Efeitos da biblioteca; quem pediu só fecha.
    LocalEffectsBrowserHost.current?.let { host ->
        androidx.compose.runtime.LaunchedEffect(Unit) { host(); onDismiss() }
        return
    }
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

/**
 * Quem mostra a galeria no lugar da folha modal. Nulo (celular e tablet) = a
 * folha; o Aurea PC abre a aba Efeitos da biblioteca.
 */
val LocalEffectsBrowserHost = androidx.compose.runtime.staticCompositionLocalOf<(() -> Unit)?> { null }
