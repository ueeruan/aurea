package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs

/**
 * ECO E RASTRO: cópias da camada em instantes passados (somadas, cada uma mais
 * fraca) e RGB no tempo (vermelho agora, verde e azul atrasados). Seguem o
 * movimento da camada e dos pais.
 */
@Composable
internal fun EchoPanel(env: PanelEnv) {
    val store = env.store
    val v by remember(store) { derivedStateOf { store.echo } }
    val e = v ?: return
    val count = e[0].toInt()
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Title("Eco")
        ChipRow(listOf(0 to "Desligado", 2 to "2 cópias", 4 to "4 cópias", 8 to "8 cópias"), count) {
            store.setEcho(it, e[1], e[2])
        }
        if (count > 0) {
            Spacer(Modifier.height(6.dp))
            ChipRow(listOf(1 to "1 quadro", 2 to "2 quadros", 4 to "4 quadros", 8 to "8 quadros"), e[1].toInt()) {
                store.setEcho(count, it.toFloat(), e[2])
            }
            Spacer(Modifier.height(6.dp))
            ChipRow(listOf(80 to "Longo", 60 to "Médio", 35 to "Curto"), (e[2] * 100).toInt()) {
                store.setEcho(count, e[1], it / 100f)
            }
        }
        Spacer(Modifier.height(16.dp))
        Title("RGB no tempo")
        ChipRow(listOf(0 to "Desligado", 1 to "1 quadro", 2 to "2 quadros", 4 to "4 quadros"), e[3].toInt()) {
            store.setRgbTime(it.toFloat())
        }
    }
}

@Composable
private fun Title(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun ChipRow(items: List<Pair<Int, String>>, current: Int, onPick: (Int) -> Unit) {
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        items.forEach { (value, label) ->
            val on = abs(value - current) == 0
            Box(
                Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { onPick(value) }).padding(horizontal = 12.dp, vertical = 6.dp),
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}
