package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.VECTOR_SHAPE_TYPE
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * TEXTO NO CAMINHO (Fase 7D): escolher uma camada vetorial como guia (o
 * primeiro caminho dela conduz a linha de base), a margem inicial, letras
 * perpendiculares ao caminho e o sentido.
 */
@Composable
internal fun TextPathSection(env: PanelEnv) {
    val store = env.store
    val tp = store.textPath ?: return
    val guide = tp[0]
    val offset = java.lang.Float.intBitsToFloat(tp[1].toInt())
    val perpendicular = tp[2] != 0L
    val reverse = tp[3] != 0L
    // Camadas vetoriais da composição (candidatas a guia).
    val candidates = store.layers.filter { row ->
        row.kind == LayerType.Shape.kind && row.id != store.primary &&
            store.queryDetail(row.id)?.let { (it.shapeTypePoints and 0xFFFF) == VECTOR_SHAPE_TYPE } == true
    }
    Spacer(Modifier.height(6.dp))
    Text("Texto no caminho", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    if (candidates.isEmpty() && guide == 0L) {
        Text("Crie uma camada vetorial (Desenho vetorial) para servir de guia.",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)), modifier = Modifier.padding(vertical = 8.dp))
        return
    }
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        PathChip("Linha reta", guide == 0L) { store.setTextPath(0L, offset, perpendicular, reverse) }
        candidates.forEach { row -> PathChip(row.name.ifEmpty { "Vetor" }, guide == row.id) { store.setTextPath(row.id, offset, perpendicular, reverse) } }
    }
    if (guide == 0L) return
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Margem inicial", modifier = Modifier.width(110.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Box(Modifier.weight(1f).height(40.dp)) {
            val value = { store.textPath?.let { java.lang.Float.intBitsToFloat(it[1].toInt()) } ?: offset }
            TickRuler(
                value = value,
                unitsPerDp = 1f,
                active = true,
                modifier = Modifier.fillMaxSize().valueDrag(
                    enabled = true,
                    start = value,
                    unitsPerDp = { 1f },
                    min = -20000f,
                    max = 20000f,
                    onStart = { store.beginGesture("margem do texto no caminho") },
                    onValue = { x -> store.setTextPath(guide, x, perpendicular, reverse) },
                    onEnd = { store.endGesture() },
                ),
            )
        }
        Spacer(Modifier.width(8.dp))
        ValueBox("${offset.roundToInt()} px", onTap = null)
    }
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Perpendicular ao caminho", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = perpendicular, onCheckedChange = { on -> store.setTextPath(guide, offset, on, reverse) })
    }
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Inverter sentido", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = reverse, onCheckedChange = { on -> store.setTextPath(guide, offset, perpendicular, on) })
    }
}

@Composable
private fun PathChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
