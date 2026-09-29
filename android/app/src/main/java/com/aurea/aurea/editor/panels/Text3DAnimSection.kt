package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlin.math.roundToInt

/** Presets do motor (mesma ordem de `scene3d::kText3DAnimPresetCount`). */
private val Text3DAnimPresets = listOf(
    R.string.t3a_fade, R.string.t3a_rise, R.string.t3a_drop, R.string.t3a_pop, R.string.t3a_spin_y, R.string.t3a_flip_x,
    R.string.t3a_typewriter, R.string.t3a_wave, R.string.t3a_cascade, R.string.t3a_zoom, R.string.t3a_swing,
)

/** Floats por modo em `queryText3DAnim` (Engine::kText3DAnimFloats). */
private const val AnimFloats = 5

/**
 * ANIMAÇÃO DE TEXTO do texto 3D: grade de presets (um toque aplica), entrada /
 * saída / loop, unidade (letra, palavra, linha), duração e atraso entre
 * unidades. São os animadores de camada por letra do motor — os mesmos do
 * texto 2D — aplicados em cada letra da malha (preview = export).
 *
 * [parts] = forma 3D: a MESMA seção, com cada parte no papel de uma letra
 * (o motor força a unidade "parte"; palavra e linha não existem na forma).
 */
@Composable
internal fun Text3DAnimSection(env: PanelEnv, parts: Boolean = false) {
    val store = env.store
    val anim = remember(store.layerAnimators, store.text3d, store.shape3d) { store.queryText3DAnim() } ?: return
    var mode by remember { mutableIntStateOf(0) }
    val row = mode * AnimFloats
    val current = anim[row].roundToInt()
    var unit by remember { mutableIntStateOf(1) }
    var duration by remember { mutableFloatStateOf(0.6f) }
    var stagger by remember { mutableFloatStateOf(60f) }
    // O modo escolhido mostra o que já está aplicado nele.
    LaunchedEffect(mode, anim[row], anim[row + 1], anim[row + 2], anim[row + 3]) {
        if (anim[row] >= 0f) {
            unit = anim[row + 1].roundToInt().coerceIn(1, 3)
            duration = anim[row + 2].coerceIn(0.1f, 3f)
            stagger = anim[row + 3].coerceIn(0f, 500f)
        }
    }
    val reapply = { if (current >= 0) store.applyText3DAnim(current, mode, unit, duration, stagger) }

    Spacer(Modifier.height(14.dp))
    Text(stringResource(if (parts) R.string.s3a_title else R.string.t3a_title), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Text(
        stringResource(if (parts) R.string.s3a_hint else R.string.t3a_hint),
        modifier = Modifier.padding(top = 4.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
    )
    // Entrada / saída / loop: o ponto marca o modo que já tem preset.
    val modes = listOf(R.string.t3a_in, R.string.t3a_out, R.string.t3a_loop).mapIndexed { i, res ->
        stringResource(res) + if (anim[i * AnimFloats] >= 0f) " •" else ""
    }
    ChipRow("", modes, mode) { mode = it }
    if (parts) {
        // Forma 3D: uma unidade só — cada parte.
        ChipRow(stringResource(R.string.panel_anima_cada), listOf(stringResource(R.string.shape3d_parts)), 0) { }
    } else ChipRow(
        stringResource(R.string.panel_anima_cada),
        listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha)),
        unit - 1,
    ) { unit = it + 1; reapply() }
    // Grade de presets: três por linha; um toque aplica (ou troca) o do modo.
    val names = listOf(R.string.t3a_none) + Text3DAnimPresets
    Column(verticalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(vertical = 4.dp)) {
        names.chunked(3).forEachIndexed { r, chunk ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                chunk.forEachIndexed { c, res ->
                    val preset = r * 3 + c - 1
                    AnimChip(stringResource(res), preset == current) {
                        store.applyText3DAnim(preset, mode, unit, duration, stagger)
                    }
                }
            }
        }
    }
    AnimSlider(stringResource(R.string.t3a_duration), duration, 0.1f..3f, "${com.aurea.aurea.ui.ds.numeroPtBr(duration, 1)} s",
        onChange = { duration = it }, onDone = reapply)
    AnimSlider(stringResource(R.string.t3a_stagger), stagger, 0f..500f, "${stagger.roundToInt()} ms",
        onChange = { stagger = it }, onDone = reapply)
}

@Composable
private fun AnimSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, shown: String, onChange: (Float) -> Unit, onDone: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.width(118.dp), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
        Slider(value = value, onValueChange = onChange, valueRange = range, onValueChangeFinished = onDone, modifier = Modifier.weight(1f))
        Spacer(Modifier.width(8.dp))
        Text(shown, modifier = Modifier.width(52.dp), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}
