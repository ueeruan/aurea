package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin

// =============================================================================
//  Linhas próprias do pacote de áudio no painel de efeitos:
//
//   · "Camada de áudio" (parâmetro de referência a camada) — o nome da camada
//     escolhida; o toque abre a lista das camadas da composição. O valor que
//     vai para o motor é o ÍNDICE da camada (parte baixa do id); −1 = nenhuma.
//   · o gráfico da RESPOSTA do EQ paramétrico, com a MESMA conta do filtro que
//     toca (biquad de pico, `audio::eq_band` no motor).
// =============================================================================

/** Índice de camada (a parte baixa do LayerId empacotado) — o que o parâmetro guarda. */
internal fun layerIndexOf(id: Long): Int = (id and 0xFFFFFFFFL).toInt()

@Composable
internal fun EffectLayerRow(env: PanelEnv, effectId: Int, s: ParamSlot, label: String, onMenu: () -> Unit) {
    val store = env.store
    val chosen by remember(store, effectId, s.index) {
        derivedStateOf { store.paramOf(effectId, s.index)?.value?.getOrNull(0)?.roundToInt() ?: -1 }
    }
    val none = stringResource(R.string.afx_layer_none)
    val self = store.primary
    val layers = store.layers
    val name = if (chosen < 0) none else layers.firstOrNull { layerIndexOf(it.id) == chosen }?.name?.ifEmpty { none } ?: none
    var picking by remember { mutableStateOf(false) }
    PropertyCustomRow(label, selected = false, onSelect = { picking = true }, onExpression = onMenu) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Spacer(Modifier.weight(1f))
            Text(
                name,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Action)),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .testTag("effects.layer_ref.$effectId.${s.index}")
                    .tocavel(shrink = 1f) { picking = true }
                    .padding(horizontal = 6.dp, vertical = 8.dp),
            )
            Spacer(Modifier.width(4.dp))
        }
    }
    if (picking) {
        AureaActionSheet(
            title = label,
            actions = buildList {
                add(SheetAction(none) { write(env, effectId, s.index, -1) })
                layers.filter { it.id != self }.forEach { l ->
                    add(SheetAction(l.name.ifEmpty { "#${layerIndexOf(l.id)}" }) { write(env, effectId, s.index, layerIndexOf(l.id)) })
                }
            },
            onDismiss = { picking = false },
        )
    }
}

private fun write(env: PanelEnv, effectId: Int, index: Int, layerIndex: Int) {
    val p = env.store.paramOf(effectId, index) ?: return
    env.store.setEffectParam(effectId, p, layerIndex.toFloat())
}

// -----------------------------------------------------------------------------
// EQ paramétrico: o gráfico
// -----------------------------------------------------------------------------
private const val EQ_RATE = 48000.0
private const val EQ_RANGE_DB = 24.0

/** Pico/vale RBJ (o `audio::eq_band` do motor): b0, b1, b2, a1, a2. */
internal fun eqBand(hz: Double, bandwidthPercent: Double, gainDb: Double): DoubleArray {
    val f = hz.coerceIn(10.0, EQ_RATE * 0.49)
    val q = 100.0 / bandwidthPercent.coerceIn(0.5, 1000.0)
    val a = 10.0.pow(gainDb.coerceIn(-60.0, 60.0) / 40.0)
    val w0 = 2.0 * PI * f / EQ_RATE
    val alpha = sin(w0) / (2.0 * q)
    val c = cos(w0)
    val a0 = 1.0 + alpha / a
    return doubleArrayOf((1.0 + alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha / a) / a0)
}

/** |H| em dB do biquad em `hz`. */
internal fun biquadDb(b: DoubleArray, hz: Double): Double {
    val w = 2.0 * PI * hz.coerceIn(0.0, EQ_RATE * 0.5) / EQ_RATE
    val c1 = cos(w); val s1 = -sin(w); val c2 = cos(2 * w); val s2 = -sin(2 * w)
    val nr = b[0] + b[1] * c1 + b[2] * c2
    val ni = b[1] * s1 + b[2] * s2
    val dr = 1.0 + b[3] * c1 + b[4] * c2
    val di = b[3] * s1 + b[4] * s2
    val mag = hypot(nr, ni) / max(hypot(dr, di), 1e-30)
    return 20.0 * log10(max(mag, 1e-12))
}

/** Resposta somada das bandas ligadas (12 valores: ativar, Hz, largura %, dB × 3). */
internal fun eqResponseDb(values: FloatArray, hz: Double): Double {
    var db = 0.0
    for (band in 0 until 3) {
        val o = band * 4
        if (values.getOrElse(o) { 0f } < 0.5f) continue
        db += biquadDb(eqBand(values[o + 1].toDouble(), values[o + 2].toDouble(), values[o + 3].toDouble()), hz)
    }
    return db
}

@Composable
internal fun EqResponseGraph(env: PanelEnv, effectId: Int) {
    val store = env.store
    val values by remember(store, effectId) {
        derivedStateOf {
            val v = FloatArray(12) { i -> store.paramOf(effectId, i)?.value?.getOrNull(0) ?: 0f }
            v.toList()
        }
    }
    val lineColor = AureaColors.Action
    val gridColor = AureaColors.Hairline
    Column(Modifier.fillMaxWidth().padding(start = 12.dp, end = 4.dp, top = 6.dp, bottom = 6.dp)) {
        Text(
            stringResource(R.string.afx_eq_response),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(4.dp))
        Canvas(
            Modifier
                .fillMaxWidth()
                .height(96.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.SurfaceHigh)
                .testTag("effects.eq.response.$effectId"),
        ) {
            val w = size.width
            val h = size.height
            val mid = h / 2f
            // Grade: 0 dB e ±12 dB; 100 Hz, 1 kHz e 10 kHz (escala log 20 Hz–20 kHz).
            for (db in listOf(-12.0, 0.0, 12.0)) {
                val y = (mid - db / EQ_RANGE_DB * mid).toFloat()
                drawLine(gridColor, Offset(0f, y), Offset(w, y), strokeWidth = if (db == 0.0) 1.5f else 1f)
            }
            for (hz in listOf(100.0, 1000.0, 10000.0)) {
                val x = (log10(hz / 20.0) / 3.0 * w).toFloat()
                drawLine(gridColor, Offset(x, 0f), Offset(x, h), strokeWidth = 1f)
            }
            val arr = values.toFloatArray()
            val path = Path()
            val steps = 160
            for (i in 0..steps) {
                val t = i.toDouble() / steps
                val hz = 20.0 * 1000.0.pow(t)
                val db = eqResponseDb(arr, hz).coerceIn(-EQ_RANGE_DB, EQ_RANGE_DB)
                val x = (t * w).toFloat()
                val y = (mid - db / EQ_RANGE_DB * mid).toFloat()
                if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
            }
            drawPath(path, lineColor, style = Stroke(width = 2.dp.toPx()))
        }
    }
}
