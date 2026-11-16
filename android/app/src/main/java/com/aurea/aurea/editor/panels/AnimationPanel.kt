package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.LayerType

// =============================================================================
// Funções do AUREA, fase F3 (13/11/2026): ANIMAÇÃO, um painel próprio na moldura
// comum, no lugar da face "Animadores" escondida no ⋯ de Transformar e da aba
// Animador das Opções de texto. Abas (≤ 4, de ícone):
//   Camada   · animadores da camada (entrada, saída, wiggle, onda) — qualquer tipo
//   Texto    · animadores de texto (lista → animador aberto)       — texto e texto 3D
//   Prontas  · animações de texto prontas                          — texto e texto 3D
//   Explodir · cada letra, palavra ou linha vira uma camada         — só texto 2D
// O ◆ e a curva do trilho miram o parâmetro em foco (o mesmo foco que a face de
// Transformar usava). Entradas: navegador de Efeitos › Funções do Aurea ›
// Animadores, o atalho do Editar texto e a aba Anim do texto 3D. Par do iOS:
// `AnimationPanelView` (LayerPanels.swift).
// =============================================================================

/** As abas de Animação; [id] vira `animation.tab.<id>`. */
internal enum class AnimationTab(val id: String) { Layer("layer"), Text("text"), Ready("ready"), Explode("explode") }

/** As abas que valem para a camada (conta pura, testada na JVM; gêmea no iOS). */
internal fun animationTabs(kind: Int, text3D: Boolean): List<AnimationTab> = buildList {
    add(AnimationTab.Layer)
    if (kind == LayerType.Text.kind || text3D) {
        add(AnimationTab.Text)
        add(AnimationTab.Ready)
    }
    if (kind == LayerType.Text.kind) add(AnimationTab.Explode)
}

/** Pedido de aba ao abrir Animação (o atalho do texto pede Texto); lido e limpo pelo painel. */
internal object AnimationRequest {
    var tab by mutableStateOf<AnimationTab?>(null)
}

@Composable
internal fun AnimationPanel(env: PanelEnv) {
    val store = env.store
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val text3D = store.text3d != null
    val tabs = animationTabs(kind, text3D)
    var chosen by rememberSaveable(store.primary) { mutableStateOf(AnimationTab.Layer.id) }
    LaunchedEffect(AnimationRequest.tab) {
        AnimationRequest.tab?.let { chosen = it.id }
        AnimationRequest.tab = null
    }
    val tab = tabs.firstOrNull { it.id == chosen } ?: AnimationTab.Layer

    // O trilho: na aba Camada, o parâmetro de animador em foco (como a face de Transformar);
    // na aba Texto, o parâmetro de animador de texto escolhido na linha.
    val layerTrack = if (tab == AnimationTab.Layer) animatorRailTrack(
        store.layerAnimators.size, store.timelineFocus?.singleOrNull(),
        store.transformAnimatorFocus?.takeIf { it.first == store.primary }?.second,
    ) else null
    DisposableEffect(store, store.primary, tab, layerTrack) {
        if (layerTrack != null) store.focusLayerAnimator(layerTrack)
        onDispose { store.timelineFocus = null }
    }
    val textTrack = if (tab == AnimationTab.Text) store.timelineFocus?.singleOrNull()?.takeIf { it.property == TrackProperty.TEXT_ANIM_PARAM } else null
    val track: TrackKey? = layerTrack ?: textTrack
    val keys = track?.let { t ->
        store.primaryKeys().filter { it.property == t.property && it.effectIndex == t.effectIndex && it.paramIndex == t.paramIndex }.sortedBy { it.time }
    }.orEmpty()
    val local = store.detail?.localPlayhead
    val look = when {
        layerTrack != null -> {
            val values = store.layerAnimators.getOrNull(layerTrack.effectIndex)
            val bit = 1 shl layerTrack.paramIndex
            when {
                values != null && values[25].toInt() and bit != 0 -> KeyframeLook.KeyHere
                values != null && values[24].toInt() and bit != 0 -> KeyframeLook.Animated
                else -> KeyframeLook.None
            }
        }
        keys.any { it.time == local } -> KeyframeLook.KeyHere
        keys.isNotEmpty() -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val onKeyframe: (() -> Unit)? = when {
        layerTrack != null -> { { store.toggleLayerAnimKey(layerTrack.effectIndex, layerTrack.paramIndex) } }
        textTrack != null -> { { store.toggleTextAnimKey(textTrack.effectIndex, textTrack.paramIndex) } }
        else -> null
    }
    androidx.compose.foundation.layout.Box(Modifier.fillMaxSize()) {
    PanelFrame(
        rail = PanelRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = onKeyframe,
            onDeleteAllKeyframes = track?.let { t -> { store.primary?.let { id -> store.deleteAllKeyframes(id, listOf(t)) } } },
            onCurve = if (track != null) ({
                val layer = store.primary
                val start = store.curveStart(keys)
                if (layer != null && start != null) {
                    store.selectKeyframe(layer, start)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) else null,
            curveReady = store.curveStart(keys) != null,
        ),
        tabs = if (tabs.size > 1) tabs.map { PanelTab(it.id, stringResource(animationTabLabel(it)), glyph = animationTabGlyph(it)) } else emptyList(),
        selectedTab = tab.id,
        onTab = { id -> chosen = id; store.timelineFocus = null },
    ) {
        Column(Modifier.fillMaxWidth().padding(start = 8.dp, top = 6.dp, end = 12.dp, bottom = 10.dp).testTag("animation.panel")) {
            when (tab) {
                AnimationTab.Layer -> LayerAnimSection(env)
                AnimationTab.Text -> TextAnimSection(env, parts = TEXT_ANIM_LIST)
                AnimationTab.Ready -> TextAnimSection(env, parts = TEXT_ANIM_READY)
                AnimationTab.Explode -> TextAnimSection(env, parts = TEXT_ANIM_EXPLODE)
            }
        }
    }
    // Ids próprios das abas (além de `panel.tab.<i>` da moldura).
    if (tabs.size > 1) PanelTabTags(tabs.map { "animation.tab." + it.id })
    }
}

private fun animationTabLabel(tab: AnimationTab): Int = when (tab) {
    AnimationTab.Layer -> R.string.editor_camada
    AnimationTab.Text -> R.string.ui_text_animators
    AnimationTab.Ready -> R.string.ui_ready_anims
    AnimationTab.Explode -> R.string.tx044_explode
}

private fun animationTabGlyph(tab: AnimationTab): Char = when (tab) {
    AnimationTab.Layer -> CupertinoGlyph.Move
    AnimationTab.Text -> CupertinoGlyph.Textformat
    AnimationTab.Ready -> CupertinoGlyph.WandStars
    AnimationTab.Explode -> CupertinoGlyph.Scissors
}
