package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GridNode
import com.aurea.aurea.state.gridControlChoices
import com.aurea.aurea.ui.ds.AureaDropdown
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph

// =============================================================================
// Omega grid (0.3.5): "Controlado por" no painel da grade
//
// No topo dos controles do efeito da grade (a camada controladora): a caixa
// suspensa da interface nova (AureaDropdown → menu ancorado) com Nenhum, os
// nulos da composição e "Criar nulo de controle". Vincular faz a grade inteira
// seguir o nulo (posição, rotação X/Y/Z, escala, âncora) e a cadeia de pais
// dele; a regra e a recusa do ciclo são do motor. Par do iOS: GridControlRow
// (no fim de EffectsView.swift). Ids `grid.control`, `grid.control.menu`,
// `grid.control.none`, `grid.control.null.<id>`, `grid.control.create`.
// =============================================================================

@Composable
internal fun GridControlRow(store: EditorStore) {
    val grid = store.primary ?: return
    val rows = store.layers
    val choices = remember(rows, grid) {
        gridControlChoices(rows.map { GridNode(it.id, it.kind, it.name, it.parentIndex) }, grid)
    }
    val locked = store.layers.firstOrNull { it.id == grid }?.locked == true
    val label = stringResource(R.string.om_grid_controlled_by)
    val none = stringResource(R.string.om_grid_controlled_none)
    val header = stringResource(R.string.om_grid_nulls_header)
    val create = stringResource(R.string.om_grid_create_control_null)
    val entries = buildList {
        add(MenuEntry.Item(none, selected = choices.current == 0L, tag = "grid.control.none") {
            if (choices.current != 0L) store.setGridControlNull(grid, 0L)
        })
        if (choices.nulls.isNotEmpty()) {
            add(MenuEntry.Divider)
            add(MenuEntry.Header(header))
            choices.nulls.forEach { n ->
                add(MenuEntry.Item(n.name.ifBlank { header }, glyph = CupertinoGlyph.SmallcircleCircle,
                    selected = n.id == choices.current, tag = "grid.control.null.${n.id}") {
                    if (n.id != choices.current) store.setGridControlNull(grid, n.id)
                })
            }
        }
        add(MenuEntry.Divider)
        add(MenuEntry.Item(create, glyph = CupertinoGlyph.Plus, tag = "grid.control.create") {
            store.createGridControlNull(grid)
        })
    }
    Column(Modifier.fillMaxWidth().padding(horizontal = 2.dp, vertical = 2.dp)) {
        Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                label,
                modifier = Modifier.weight(1f).padding(start = 8.dp, end = 8.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
            )
            AureaDropdown(
                value = choices.currentName?.ifBlank { header } ?: none,
                entries = entries,
                modifier = Modifier.weight(1.3f),
                width = null,
                tone = AureaSurfaceTone.Editor,
                enabled = !locked,
                tag = "grid.control",
                contentDescription = label,
            )
        }
        FxBodyNotice(stringResource(R.string.om_grid_controlled_hint))
    }
}
