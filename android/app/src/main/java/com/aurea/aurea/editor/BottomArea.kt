package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import android.app.Application
import androidx.annotation.StringRes
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalDensity
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.AureaLineIcons
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

/** A borda superior de 1 dp do `ContextSheet` (#273442); o conteúdo começa abaixo dela. */
internal fun Modifier.drawTopHairline(): Modifier =
    this
        .drawBehind { drawRect(AureaColors.Border, size = Size(size.width, 1.dp.toPx())) }
        .padding(top = 1.dp)

/** A linha de dica quando nada está selecionado (`DicaDoPalco`). */
@Composable
internal fun StageHint() {
    Box(Modifier.fillMaxSize().background(AureaColors.EditorPanel), contentAlignment = Alignment.Center) {
        Text(
            stringResource(R.string.editor_toque_num_objeto_tela_editar),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Muted)),
            modifier = Modifier.padding(horizontal = 16.dp),
        )
    }
}

// =============================================================================
// Omega painel (área 04): a doca de fichas saiu. O painel da camada da
// referência (200 dp por cima da timeline) mora em LayerPanel.kt.
// =============================================================================

/**
 * A zona da camada tem a altura do painel (EditorLayout.layerPanel): a conta
 * da geometria pede 2 "fileiras" para chegar aos 200 dp em 1×.
 */
internal fun dockTileRowCount(store: EditorStore): Int {
    store.primary ?: return 1
    return 2
}

/** Uma fileira de largura livre (mantida para quem já media a doca antiga). */
internal fun dockRows(count: Int): List<Int> = listOf(count)

// =============================================================================
// Seleção múltipla — barra de baixo (ref19)
// =============================================================================

/**
 * Com várias camadas: em cima, o tempo (aparar início · dividir · aparar fim
 * no cabeçote | alinhar os inícios · escada · alinhar os fins); embaixo, a
 * tela (alinhar à composição e distribuir). Tudo num passo de desfazer.
 */
@Composable
internal fun MultiSelectionPanel(store: EditorStore, @Suppress("UNUSED_PARAMETER") ui: EditorUi) {
    var grouping by remember { mutableStateOf(false) }
    if (grouping) GroupSelectionSheet(store) { grouping = false }
    val count by remember { derivedStateOf { store.selection.size } }
    val intoLayers = stringResource(R.string.editor_leve_cabecote_dentro_camadas)
    Column(Modifier.fillMaxSize().testTag("timeline.batch.tools").verticalScroll(rememberScrollState()).padding(horizontal = 10.dp)) {
        Spacer(Modifier.height(4.dp))
        Row(
            Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_aparar_inicio_cabecote)) { batchTrim(store, start = true) }
            BatchTool(CupertinoGlyph.Scissors, stringResource(R.string.editor_dividir_cabecote)) {
                val t = store.playhead
                val covered = store.layers.any { it.id in store.selection && !it.locked && t > it.startFrame && t < it.endFrame }
                if (covered) {
                    if (store.playing) store.pause()
                    store.splitAtPlayhead(store.layers.filter { it.id in store.selection && !it.locked }.map { it.id })
                } else {
                    store.showToast(intoLayers)
                }
            }
            BatchTool(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_aparar_fim_cabecote)) { batchTrim(store, start = false) }
            Box(Modifier.width(1.dp).height(24.dp).background(AureaColors.Border))
            if (EditorTimelineRefresh.enabled) {
                BatchTool(CupertinoGlyph.RectangleStack, stringResource(R.string.editor_grupo)) { grouping = true }
                BatchTool(CupertinoGlyph.DocOnDoc, stringResource(R.string.editor_copiar_camada)) { store.copyLayers() }
                BatchTool(CupertinoGlyph.PlusSquareOnSquare, stringResource(R.string.editor_duplicar_camada)) { store.duplicateLayers() }
            } else {
            BatchTool(AureaLineIcons.AlignStarts, stringResource(R.string.editor_alinhar_inicios)) { store.arrangeLayerTimes(0) }
            BatchTool(AureaLineIcons.Stairs, stringResource(R.string.editor_escada_comeca_quando_cima_termina)) { store.arrangeLayerTimes(1) }
            BatchTool(AureaLineIcons.AlignEnds, stringResource(R.string.editor_alinhar_fins)) { store.arrangeLayerTimes(2) }
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(
            Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_alinhar_esquerda_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Left) }
            BatchTool(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.editor_centralizar_horizontal), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterH) }
            BatchTool(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_alinhar_direita_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Right) }
            BatchTool(CupertinoGlyph.ArrowUpToLine, stringResource(R.string.editor_alinhar_topo_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Top) }
            BatchTool(CupertinoGlyph.ArrowUpArrowDown, stringResource(R.string.editor_centralizar_vertical), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterV) }
            BatchTool(CupertinoGlyph.ArrowDownToLine, stringResource(R.string.editor_alinhar_base_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Bottom) }
            val three = count >= 3
            BatchTool(CupertinoGlyph.ArrowLeftRightSquare, stringResource(R.string.editor_distribuir_horizontal_vaos_iguais), 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = true)
            }
            BatchTool(CupertinoGlyph.ArrowUpDownSquare, stringResource(R.string.editor_distribuir_vertical_vaos_iguais), 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = false)
            }
            // Ajustar / preencher a tela (app antigo), junto dos alinhamentos.
            BatchTool(CupertinoGlyph.FullscreenExit, stringResource(R.string.editor_ajustar_tela), 18) { LayerOps.fitToCanvas(store, store.selection, fill = false) }
            BatchTool(CupertinoGlyph.Fullscreen, stringResource(R.string.editor_preencher_tela), 18) { LayerOps.fitToCanvas(store, store.selection, fill = true) }
        }
    }
}

/** Aparar as escolhidas que cobrem o cabeçote (as bloqueadas ficam), num passo só. */
private fun batchTrim(store: EditorStore, start: Boolean) {
    val t = store.playhead
    val rows = store.layers.filter { it.id in store.selection }
    val targets = rows.filter { !it.locked && t > it.startFrame && t < it.endFrame }
    if (targets.isEmpty()) {
        store.showToast(
            store.appText(
                if (rows.isNotEmpty() && rows.all { it.locked }) R.string.sh_layers_locked_unlock_to_edit
                else R.string.editor_leve_cabecote_dentro_camadas,
            ),
        )
        return
    }
    if (store.playing) store.pause()
    store.beginGesture(if (start) "aparar início" else "aparar fim")
    targets.forEach { if (start) store.trimStart(it.id, t) else store.trimEnd(it.id, t) }
    store.endGesture()
}

/** Toast fora do Compose: no idioma escolhido no app (ver [AppText]). */
private fun EditorStore.appText(@StringRes id: Int): String = AppText.get(getApplication<Application>(), id)

private fun EditorStore.toastRes(@StringRes id: Int) = showToast(appText(id))

@Composable
private fun RowScope.BatchTool(glyph: Char, description: String, size: Int = 20, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.weight(1f).fillMaxHeight().semantics { contentDescription = description }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(glyph, size.dp, if (enabled) AureaColors.Text else AureaColors.Disabled) }
}

@Composable
private fun RowScope.BatchTool(icon: ImageVector, description: String, onClick: () -> Unit) {
    Box(
        Modifier.weight(1f).fillMaxHeight().semantics { contentDescription = description }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(22.dp)) }
}
