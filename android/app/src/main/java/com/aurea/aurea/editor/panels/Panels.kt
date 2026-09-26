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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
    ClipEdit,      // slip / rolling / slide, frame precise
    Audio,         // volume
    Shape,         // cor e preenchimento / editar forma
    Text,          // editar texto
    Font,          // escolher a fonte do texto
    Particles,     // partículas
    Tracking,      // rastreio de ponto / estabilização
    Element3D,     // ambiente 3D (HDRI)
    Captions,      // legendas automáticas da fala
    Presets,       // navegador de presets (efeitos, texto, animação, legenda, curva)
    Mask,          // máscaras (roto) e track matte
    Vector,        // camada vetorial: caminhos, tinta, contorno, aparar, repetidor
    ShapeEdit,     // Frente D (7.2): editar forma — tamanho, raio, pontas; alças no palco
    AiVideo,       // Aurea AI: gerar vídeo remoto (MiniMax H3) e trazer para a timeline
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
        EditorPanel.Transform -> stringResource(R.string.pn_transform_title_tab, transformTab.title)
        EditorPanel.Effects -> stringResource(R.string.panel_efeitos)
        EditorPanel.Curve -> stringResource(R.string.panel_easing_curve)
        EditorPanel.Appearance -> stringResource(R.string.panel_mistura_opacidade)
        EditorPanel.Speed -> stringResource(R.string.panel_tempo_velocidade)
        EditorPanel.ClipEdit -> "Slip · Roll · Slide"
        EditorPanel.Audio -> stringResource(R.string.panel_som)
        EditorPanel.Shape -> stringResource(R.string.panel_cor_preenchimento)
        EditorPanel.Text -> stringResource(R.string.panel_texto)
        EditorPanel.Font -> stringResource(R.string.panel_fonte)
        EditorPanel.Particles -> stringResource(R.string.panel_particulas)
        EditorPanel.Tracking -> stringResource(R.string.panel_rastreio)
        EditorPanel.Element3D -> stringResource(R.string.panel_material_ambiente)
        EditorPanel.Captions -> stringResource(R.string.panel_legendas)
        EditorPanel.Presets -> stringResource(R.string.panel_presets)
        EditorPanel.Mask -> stringResource(R.string.panel_mascara_recorte)
        EditorPanel.Vector -> stringResource(R.string.panel_vetor)
        EditorPanel.ShapeEdit -> stringResource(R.string.panel_editar_forma)
        EditorPanel.AiVideo -> stringResource(R.string.panel_ai_video)
    }

    Column(modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        if (panel != EditorPanel.Curve) PanelHeader(title, onBack = onClose)
        Box(Modifier.fillMaxWidth().weight(1f)) {
            // O painel da Aurea AI gera um video e o poe na timeline: nao ha
            // camada escolhida para ele consultar, entao fica FORA do
            // `hasLayer` — abrir sem nada selecionado e o caso normal.
            if (panel == EditorPanel.AiVideo) {
                AiVideoPanel(env)
            } else if (panel == EditorPanel.Captions) {
                CaptionsPanel(env)
            } else if (hasLayer) {
                when (panel) {
                    EditorPanel.Transform -> TransformPanel(env, transformTab, onTab = { transformTab = it })
                    EditorPanel.Effects -> EffectsPanel(env)
                    EditorPanel.Curve -> CurvePanel(env)
                    EditorPanel.Appearance -> AppearancePanel(env)
                    EditorPanel.Speed -> SpeedPanel(env)
                    EditorPanel.ClipEdit -> ClipEditPanel(env)
                    EditorPanel.Audio -> AudioPanel(env)
                    EditorPanel.Shape -> ShapePanel(env)
                    EditorPanel.Text -> TextPanel(env)
                    EditorPanel.Font -> FontPanel(env)
                    EditorPanel.Particles -> ParticlesPanel(env)
                    EditorPanel.Tracking -> TrackingPanel(env)
                    EditorPanel.Element3D -> Element3DPanel(env)
                    EditorPanel.Captions -> CaptionsPanel(env)
                    EditorPanel.Presets -> PresetsPanel(env)
                    EditorPanel.Mask -> MaskPanel(env)
                    EditorPanel.Vector -> VectorPanel(env)
                    EditorPanel.ShapeEdit -> ShapeEditPanel(env)
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
