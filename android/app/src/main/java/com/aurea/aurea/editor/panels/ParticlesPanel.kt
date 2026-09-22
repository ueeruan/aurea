package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
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
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * PARTÍCULAS: presets e os ajustes principais. As partículas são calculadas
 * na GPU a partir do tempo — o mesmo quadro sempre dá o mesmo resultado
 * (prévia = export) e pular no tempo é instantâneo.
 */
@Composable
internal fun ParticlesPanel(env: PanelEnv) {
    val store = env.store
    val v by remember(store) { derivedStateOf { store.particles } }
    val p = v ?: run {
        Text("Selecione uma camada de partículas.", modifier = Modifier.padding(18.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        return
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to "Faíscas", 1 to "Neve", 2 to "Poeira de luz").forEach { (preset, label) ->
                Box(
                    Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip)
                        .tocavel(onClick = { store.applyParticlePreset(preset) }).padding(horizontal = 12.dp, vertical = 6.dp),
                ) {
                    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        ParticleRuler(store, "Partículas por segundo", 0, p[0], "${p[0].roundToInt()}", 1f, 0.1f, 2000f)
        ParticleRuler(store, "Duração de cada uma", 1, p[1], "${com.aurea.aurea.ui.ds.numeroPtBr(p[1], 1)} s", 0.02f, 0.05f, 30f)
        ParticleRuler(store, "Velocidade", 2, p[2], "${p[2].roundToInt()} px/s", 4f, 0f, 5000f)
        ParticleRuler(store, "Abertura", 3, p[3], "${p[3].roundToInt()}°", 1f, 0f, 360f)
        ParticleRuler(store, "Direção", 7, p[7], "${p[7].roundToInt()}°", 1f, -360f, 360f)
        ParticleRuler(store, "Gravidade", 4, p[4], "${(-p[4]).roundToInt()} px/s²", 8f, -5000f, 5000f)
        ParticleRuler(store, "Tamanho inicial", 5, p[5], "${p[5].roundToInt()} px", 0.5f, 0f, 500f)
        ParticleRuler(store, "Tamanho final", 6, p[6], "${p[6].roundToInt()} px", 0.5f, 0f, 500f)
    }
}

@Composable
private fun ParticleRuler(
    store: com.aurea.aurea.state.EditorStore,
    label: String,
    param: Int,
    value: Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
) {
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.particles?.get(param) ?: value },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.particles?.get(param) ?: value },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("partículas") },
                        onValue = { store.setParticleParam(param, it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}
