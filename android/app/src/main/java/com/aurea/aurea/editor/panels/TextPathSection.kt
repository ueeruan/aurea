package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.VECTOR_SHAPE_TYPE
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * TEXTO NO CAMINHO (Fase 7D; opções completas no 0.2.7, as Path Options do
 * After Effects): o caminho vem de uma camada vetorial OU de uma máscara da
 * própria camada (que, usada como caminho, não recorta o texto). Margem
 * inicial e final animáveis, forçar alinhamento, letras perpendiculares e o
 * sentido. As margens seguem o alinhamento do parágrafo — a regra mora no
 * motor (Renderer + vector::path_line_positions).
 */
@Composable
internal fun TextPathSection(env: PanelEnv) {
    val store = env.store
    val tp = store.textPathState ?: return
    // Camadas vetoriais da composição (candidatas a guia).
    val candidates = store.layers.filter { row ->
        row.kind == LayerType.Shape.kind && row.id != store.primary &&
            store.queryDetail(row.id)?.let { (it.shapeTypePoints and 0xFFFF) == VECTOR_SHAPE_TYPE } == true
    }
    val ownMasks = store.masks?.takeIf { it.layer == store.primary }?.masks.orEmpty()
    Spacer(Modifier.height(6.dp))
    Text(stringResource(R.string.panel_texto_caminho), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    if (candidates.isEmpty() && ownMasks.isEmpty() && !tp.onPath) {
        Text(stringResource(R.string.text027_path_hint),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)), modifier = Modifier.padding(vertical = 8.dp))
        return
    }
    fun apply(next: EditorStore.TextPathState) = store.setTextPathOptions(next)
    Row(
        Modifier.fillMaxWidth().testTag("text.path.sources").horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        PathChip(stringResource(R.string.panel_linha_reta), !tp.onPath, "text.path.none") { apply(tp.copy(guide = 0L, mask = -1)) }
        ownMasks.forEachIndexed { i, m ->
            PathChip(stringResource(R.string.text027_path_mask, i + 1), tp.mask == m.id, "text.path.mask.$i") { apply(tp.copy(guide = 0L, mask = m.id)) }
        }
        candidates.forEach { row ->
            PathChip(row.name.ifEmpty { stringResource(R.string.panel_vetor) }, tp.mask < 0 && tp.guide == row.id, "text.path.layer.${row.id}") {
                apply(tp.copy(guide = row.id, mask = -1))
            }
        }
    }
    if (!tp.onPath) return
    val first = stringResource(R.string.panel_margem_inicial)
    TextRuler(env, first, { store.textPathState?.firstMargin ?: tp.firstMargin }, "${tp.firstMargin.roundToInt()} px", 1f, -20000f, 20000f,
        "margem inicial do texto no caminho", keyProperty = TrackProperty.TEXT_PATH_FIRST_MARGIN, keyTag = "text.path.first.key") { x ->
        store.textPathState?.let { apply(it.copy(firstMargin = x)) }
    }
    val last = stringResource(R.string.text027_path_last_margin)
    TextRuler(env, last, { store.textPathState?.lastMargin ?: tp.lastMargin }, "${tp.lastMargin.roundToInt()} px", 1f, -20000f, 20000f,
        "margem final do texto no caminho", keyProperty = TrackProperty.TEXT_PATH_LAST_MARGIN, keyTag = "text.path.last.key") { x ->
        store.textPathState?.let { apply(it.copy(lastMargin = x)) }
    }
    PathToggle(stringResource(R.string.text027_path_force_align), tp.forceAlign, "text.path.force") { on -> apply(tp.copy(forceAlign = on)) }
    PathToggle(stringResource(R.string.panel_perpendicular_caminho), tp.perpendicular, "text.path.perpendicular") { on -> apply(tp.copy(perpendicular = on)) }
    PathToggle(stringResource(R.string.panel_inverter_sentido), tp.reverse, "text.path.reverse") { on -> apply(tp.copy(reverse = on)) }
    Text(stringResource(R.string.text027_path_alignment_note),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)), modifier = Modifier.padding(vertical = 6.dp))
}

@Composable
private fun PathToggle(label: String, checked: Boolean, tag: String, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Box(Modifier.testTag(tag)) { AureaToggle(checked = checked, onCheckedChange = onChange) }
    }
}

@Composable
private fun PathChip(label: String, on: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.RadioButton; selected = on }
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
