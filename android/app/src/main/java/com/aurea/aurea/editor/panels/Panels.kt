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
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.RowScope
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.unit.LayoutDirection
import com.aurea.aurea.ui.ds.MenuEntry
import kotlinx.coroutines.launch
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
    Filters,       // 0.3.0, modo de edição: looks prontos com intensidade (ColorToolsPanels.kt)
    Adjust,        // 0.3.0, modo de edição: ajustes de cor, HSL, curvas, rodas, scopes
    // Omega conteúdo (área 06): editor de desenho e o modo Editar pontos (no fim: o ordinal fica salvo).
    Drawing,       // caneta, pincel, preencher e borracha numa camada de desenho
    EditPoints,    // três colunas: ações · trackpad · faixa de pontos
    // Omega aparência (área 05).
    BorderShadow,  // Borda e sombra: traço, bordas (decorações) e sombra
    Camera,        // Opções da câmera: visão, desfoque de foco, névoa
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
    /** Interface de Edição: a barra de cima não tem título de seção, então o
     *  painel mostra "‹ Título" mesmo com camada escolhida. */
    alwaysHeader: Boolean = false,
) {
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var color by remember { mutableStateOf<ColorRequest?>(null) }
    // De onde o editor de curva foi aberto: o ‹ do trilho dele volta para lá.
    var returnTo by remember { mutableStateOf<EditorPanel?>(null) }
    LaunchedEffect(panel) { if (panel != EditorPanel.Curve) returnTo = panel }
    // A aba de Transformar sobe até aqui porque o TÍTULO a escreve
    // ("Transformar · Escala"); corpo e título não podem discordar.
    var transformTab by rememberSaveable { mutableStateOf(PanelRequests.takeTransformTab() ?: TransformTab.Mover) }
    // Omega painel: (área 04) "Opções da câmera" do painel da camada pede a aba Lente.
    val requestedTab = PanelRequests.transformTab
    LaunchedEffect(requestedTab) { PanelRequests.takeTransformTab()?.let { transformTab = it } }

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
            // O teclado pertence à camada que o abriu: se a seleção mudar com ele
            // aberto, o OK não grava na camada nova (a folha antiga não tem dono).
            openKeypad = { request ->
                val owner = store.primary
                keypad = KeypadRequest(request.title, request.initial, request.unit, request.min, request.max,
                    request.decimals, request.percentBase) { value -> if (store.primary == owner) request.onConfirm(value) }
            },
            openColor = { color = it },
            returnTo = { returnTo },
            onOpenAnimators = { transformTab = TransformTab.Animadores; open(EditorPanel.Transform) },
        )
    }
    // Só a existência da camada importa aqui — não o detalhe que muda a cada quadro.
    val hasLayer by remember(store) { derivedStateOf { store.detail != null } }

    val title = if (panel == EditorPanel.Transform) {
        stringResource(R.string.pn_transform_title_tab, stringResource(transformTab.title))
    } else {
        panelSectionTitle(store, panel)
    }

    // Omega painel: (área 04, P1-10) o painel de propriedade entra por cima do
    // painel da camada: x +100 dp → 0 em 100 ms com fade de 25 ms. A curva não
    // anima (como na referência). Com "remover animações" do sistema, só o fade.
    val reduceMotion = com.aurea.aurea.effects.rememberReduceMotion()
    // Começam já na posição de partida: o 1º quadro não pisca o painel no lugar.
    val slide = remember { Animatable(if (panel == EditorPanel.Curve || reduceMotion) 0f else 100f) }
    val fade = remember { Animatable(if (panel == EditorPanel.Curve) 1f else 0f) }
    val rtl = androidx.compose.ui.platform.LocalLayoutDirection.current == LayoutDirection.Rtl
    LaunchedEffect(panel) {
        if (panel == EditorPanel.Curve) { slide.snapTo(0f); fade.snapTo(1f); return@LaunchedEffect }
        slide.snapTo(if (reduceMotion) 0f else 100f)
        fade.snapTo(0f)
        launch { fade.animateTo(1f, tween(25)) }
        slide.animateTo(0f, tween(100, easing = FastOutSlowInEasing))
    }
    Column(
        modifier
            .fillMaxSize()
            .clipToBounds()
            .graphicsLayer {
                val dir = if (rtl) -1f else 1f
                translationX = slide.value * density * dir
                alpha = fade.value
            }
            .background(AureaColors.EditorPanel),
    ) {
        // Com camada escolhida, o título da seção mora na barra de cima
        // (SectionTopBar, redesenho 2026-09-29); o cabeçalho próprio só fica
        // para o painel aberto sem camada (Aurea AI, legendas).
        if (panel != EditorPanel.Curve && (!hasLayer || alwaysHeader)) PanelHeader(title, onBack = onClose)
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
                    EditorPanel.Filters -> FiltersPanel(env)
                    EditorPanel.Adjust -> AdjustPanel(env)
                    // Omega conteúdo (área 06).
                    EditorPanel.Drawing -> com.aurea.aurea.editor.DrawingPanel(env)
                    EditorPanel.EditPoints -> com.aurea.aurea.editor.EditPointsPanel(env)
                    // Omega aparência (área 05).
                    EditorPanel.BorderShadow -> BorderShadowPanel(env)
                    EditorPanel.Camera -> CameraOptionsPanel(env)
                }
            }
        }
    }

    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    color?.let { r ->
        DisposableEffect(r) { onDispose { r.finish() } }
        // Omega aparência (P0-5/P1-8): com âncora, o balão com o seletor embutido; sem,
        // o seletor em tela cheia. O `ColorPickerSheet` antigo fica só de reserva.
        androidx.compose.runtime.key(r) {
            ColorRequestHost(store, panel, r, onClose = {
                if (color === r) color = null
                r.finish()
            })
        }
    }
}

// =============================================================================
// Omega painel (área 04): pedidos ao painel aberto, ações da barra (P0-6) e
// "remover animações" do sistema (P1-10).
// =============================================================================

/** Pedidos de fora para o painel que vai abrir (ex.: "Opções da câmera" → aba Lente). */
internal object PanelRequests {
    var transformTab by mutableStateOf<TransformTab?>(null)

    /** Lê e limpa o pedido de aba de Transformar. */
    fun takeTransformTab(): TransformTab? = transformTab?.also { transformTab = null }
}

/**
 * As ações que o painel aberto publica na barra de cima (`PanelBar` da
 * seção): o ⋮ ([more], menu ancorado) e um [trailing] à direita do título. O
 * painel publica com [PublishBarActions]; a barra lê daqui (EditorScreen).
 */
internal object PanelBarActions {
    var more by mutableStateOf<List<MenuEntry>?>(null)
    var trailing by mutableStateOf<(@Composable RowScope.() -> Unit)?>(null)
}

/** Publica as ações da barra enquanto o painel estiver na tela (some ao sair). */
@Composable
internal fun PublishBarActions(more: List<MenuEntry>?, trailing: (@Composable RowScope.() -> Unit)? = null) {
    androidx.compose.runtime.SideEffect {
        PanelBarActions.more = more
        PanelBarActions.trailing = trailing
    }
    DisposableEffect(Unit) {
        onDispose {
            PanelBarActions.more = null
            PanelBarActions.trailing = null
        }
    }
}

/** Galeria de efeitos (folha modal). Adiciona o efeito às camadas escolhidas. */
@Composable
fun EffectsBrowserSheet(store: EditorStore, onOpenPanel: (EditorPanel) -> Unit = {}, onDismiss: () -> Unit) {
    EffectsBrowser(store, onOpenPanel, onDismiss)
}

/** O título da seção aberta (a barra de cima e o cabeçalho do painel sem camada). */
@Composable
internal fun panelSectionTitle(store: EditorStore, panel: EditorPanel): String = when (panel) {
    EditorPanel.Transform -> stringResource(R.string.sh_dock_transform)
    EditorPanel.Effects -> stringResource(R.string.panel_efeitos)
    EditorPanel.Curve -> stringResource(R.string.panel_easing_curve)
    EditorPanel.Appearance -> stringResource(R.string.panel_mistura_opacidade)
    EditorPanel.Speed -> stringResource(R.string.panel_tempo_velocidade)
    EditorPanel.ClipEdit -> stringResource(R.string.i18n_clip_modes)
    EditorPanel.Audio -> stringResource(R.string.panel_som)
    EditorPanel.Shape -> stringResource(R.string.panel_cor_preenchimento)
    EditorPanel.Text -> stringResource(R.string.text_options)
    EditorPanel.Font -> stringResource(R.string.panel_fonte)
    EditorPanel.Particles -> stringResource(R.string.panel_particulas)
    EditorPanel.Tracking -> stringResource(R.string.panel_rastreio)
    EditorPanel.Element3D -> stringResource(if (store.text3d != null) R.string.text_options else R.string.panel_material_ambiente)
    EditorPanel.Captions -> stringResource(R.string.panel_legendas)
    EditorPanel.Presets -> stringResource(R.string.panel_presets)
    EditorPanel.Mask -> stringResource(R.string.panel_mascara_recorte)
    EditorPanel.Vector -> stringResource(R.string.panel_vetor)
    EditorPanel.ShapeEdit -> stringResource(R.string.panel_editar_forma)
    EditorPanel.AiVideo -> stringResource(R.string.panel_ai_video)
    EditorPanel.Filters -> stringResource(R.string.grade_title_filters)
    EditorPanel.Adjust -> stringResource(R.string.grade_title_adjust)
    // Omega conteúdo (área 06).
    EditorPanel.Drawing -> stringResource(R.string.om_draw_title)
    EditorPanel.EditPoints -> stringResource(R.string.om_pts_title)
    // Omega aparência (área 05).
    EditorPanel.BorderShadow -> stringResource(R.string.om_ap_bs_title)
    EditorPanel.Camera -> stringResource(R.string.om_ap_cam_title)
}
