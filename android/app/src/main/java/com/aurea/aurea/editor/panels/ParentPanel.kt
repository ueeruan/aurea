package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

/**
 * SEGUIR OUTRA CAMADA: escolhe o pai. A camada fica onde está na tela (o motor
 * compensa o transform) e passa a acompanhar posição, rotação e escala do pai.
 * Ela mesma e as filhas dela não aparecem (seria um ciclo).
 */
@Composable
internal fun ParentPanel(env: PanelEnv) {
    val store = env.store
    val layerId = store.primary ?: return
    val current by remember(store) { derivedStateOf { store.detail?.parentId ?: 0L } }
    val candidates by remember(store, layerId) { derivedStateOf { store.parentCandidates(layerId) } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp)) {
        ParentRow(CupertinoGlyph.Xmark, AureaColors.Chip, "Nenhuma (solta)", current == 0L) { store.setParent(layerId, 0L) }
        candidates.forEach { row ->
            val type = LayerType.of(row.kind)
            val name = row.name.ifEmpty { type.label }
            ParentRow(type.glyph, type.color, name, current == row.id) { store.setParent(layerId, row.id) }
        }
        if (candidates.isEmpty()) {
            Text(
                "Nenhuma outra camada para seguir. Crie um Nulo em Adicionar › Objeto.",
                modifier = Modifier.padding(8.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
        }
    }
}

@Composable
private fun ParentRow(glyph: Char, color: Color, label: String, on: Boolean, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(48.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.AccentDim else Color.Transparent)
            .tocavel(onClick = onClick)
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(24.dp).clip(RoundedCornerShape(7.dp)).background(color), contentAlignment = Alignment.Center) {
            CupertinoIcon(glyph, 14.dp, Color.White)
        }
        Spacer(Modifier.width(12.dp))
        Text(
            label,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = if (on) AureaColors.Accent else AureaColors.Text)),
        )
        if (on) CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, AureaColors.Accent)
    }
}
