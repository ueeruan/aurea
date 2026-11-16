package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.editor.panels.panelSectionTitle
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * A JANELA FLUTUANTE de painéis do Aurea PC. No PC o cartão da direita é só dos
 * Controles de efeitos (como no AE) e as propriedades ficam na timeline; as
 * ferramentas da camada (a grade do Aurea: Cor e preenchimento, Borda e sombra,
 * Mistura e opacidade, Máscara, Editar forma, Presets...) e os painéis que elas
 * abrem aparecem aqui, num cartão do Aurea que se arrasta pelo título.
 *
 * O painel aberto continua sendo `EditorUi.panel` (o palco lê dele: desenho,
 * pontos, máscara, rastreio); esta janela só o mostra. Sem painel, a janela
 * mostra a grade da camada ([LayerPanel]) ou, com várias camadas, as
 * ferramentas do lote. Efeitos e Curva nunca abrem aqui ([route]).
 */
object PcPanelWindow {
    var open by mutableStateOf(false)
    /** Canto superior esquerdo (dp); NaN = ainda não arrastada. */
    var x by mutableStateOf(Float.NaN)
    var y by mutableStateOf(Float.NaN)

    /**
     * O `PanelRouting.panelHost` do PC: Efeitos ficam no cartão da direita
     * (Controles de efeitos); o resto segue o caminho normal (o painel aberto
     * aparece nesta janela).
     */
    fun route(panel: EditorPanel): Boolean = panel == EditorPanel.Effects

    /** Abre a janela com a grade de ferramentas da camada. */
    internal fun showTools(ui: EditorUi) {
        ui.panel = null
        open = true
    }
}

/** Abre um painel do Aurea na janela flutuante (pelo caminho normal de `openPanel`). */
internal fun pcOpenPanel(store: EditorStore, ui: EditorUi, panel: EditorPanel) {
    openPanel(store, ui, panel)
    if (panel != EditorPanel.Effects && panel != EditorPanel.Curve) PcPanelWindow.open = true
}

/** O que a janela mostra: painel aberto (fora Efeitos/Curva) ou nulo (a grade). */
private fun windowPanel(ui: EditorUi): EditorPanel? = ui.panel?.takeIf { it != EditorPanel.Effects && it != EditorPanel.Curve }

@Composable
internal fun PcPanelWindowHost(store: EditorStore, ui: EditorUi, maxW: Float, maxH: Float) {
    val panel = windowPanel(ui)
    // Abrir um painel (menu da timeline, grade, palco, assistente) mostra a janela.
    LaunchedEffect(panel) { if (panel != null) PcPanelWindow.open = true }
    val free = panel == EditorPanel.AiVideo || panel == EditorPanel.Captions
    val layer = store.primary
    // Sem camada (e sem um painel que dispensa camada) a janela fecha.
    LaunchedEffect(layer == null, free) { if (layer == null && !free) PcPanelWindow.open = false }
    if (!PcPanelWindow.open || store.screen != com.aurea.aurea.state.Screen.Editor) return
    if (layer == null && !free) return
    val density = LocalDensity.current
    val w = 380f
    val h = if (panel != null) 560f else 300f
    if (PcPanelWindow.x.isNaN()) {
        PcPanelWindow.x = (maxW - w - 380f).coerceAtLeast(0f)
        PcPanelWindow.y = 70f
    }
    val x = PcPanelWindow.x.coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
    val y = PcPanelWindow.y.coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
    val shape = RoundedCornerShape(12.dp)
    val multi = store.selection.size > 1
    val layerName = store.layers.firstOrNull { it.id == layer }?.name.orEmpty()
    val title = when {
        panel != null -> panelSectionTitle(store, panel)
        multi -> stringResource(R.string.pc_layer_tools) + " · ${store.selection.size}"
        else -> stringResource(R.string.pc_layer_tools)
    }
    Column(
        Modifier.offset { IntOffset((x * density.density).roundToInt(), (y * density.density).roundToInt()) }
            .size(w.dp, h.dp).shadow(18.dp, shape).clip(shape)
            .background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape),
    ) {
        // Título: ‹ volta à grade (com painel aberto), nome do painel e da camada, ✕ fecha.
        Row(
            Modifier.fillMaxWidth().height(40.dp)
                .pointerInput(Unit) {
                    detectDragGestures { c, d ->
                        c.consume()
                        PcPanelWindow.x = (PcPanelWindow.x + d.x / density.density).coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
                        PcPanelWindow.y = (PcPanelWindow.y + d.y / density.density).coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
                    }
                }
                .padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (panel != null && layer != null) {
                Box(Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).tocavel(shrink = 1f) { ui.panel = null }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronLeft, 15.dp, AureaColors.Text)
                }
            } else {
                Box(Modifier.size(28.dp), contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.SquareGrid2x2, 14.dp, AureaColors.Muted)
                }
            }
            Spacer(Modifier.width(4.dp))
            Column(Modifier.weight(1f)) {
                Text(title, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (layerName.isNotEmpty() && !multi) {
                    Text(layerName, color = AureaColors.Subtle, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
            Box(
                Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).tocavel(shrink = 1f) {
                    ui.panel = null
                    PcPanelWindow.open = false
                },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Xmark, 13.dp, AureaColors.Muted)
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
        Box(Modifier.fillMaxWidth().weight(1f)) {
            when {
                panel != null -> PanelContent(
                    store = store,
                    panel = panel,
                    onClose = { ui.panel = null },
                    onOpenPanel = { pcOpenPanel(store, ui, it) },
                    onOpenEffectsBrowser = { PcLibraryState.effects = true },
                    modifier = Modifier.fillMaxSize(),
                )
                multi -> MultiSelectionPanel(store, ui)
                layer != null -> LayerPanel(store, ui, layer)
            }
        }
    }
}
