package com.aurea.aurea.editor.panels

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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlin.math.roundToInt

/** RGBA8 (R no byte baixo) → Color sRGB. */
private fun rgba8(v: Int): Color = Color(
    red = (v and 0xFF) / 255f,
    green = ((v ushr 8) and 0xFF) / 255f,
    blue = ((v ushr 16) and 0xFF) / 255f,
    alpha = ((v ushr 24) and 0xFF) / 255f,
)

/**
 * COR E PREENCHIMENTO da forma: preenchimento (liga/desliga e cor), contorno
 * (cor e largura), os parâmetros que o tipo tem (cantos, pontas, raio interno)
 * e o tamanho. Tudo é o SDF do motor — a forma continua vetorial em qualquer
 * escala.
 */
@Composable
internal fun ShapePanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val type = detail.shapeTypePoints and 0xFFFF
    val points = (detail.shapeTypePoints ushr 16) and 0xFFFF
    val fill = rgba8(detail.shapeFill)
    val stroke = rgba8(detail.shapeStroke)
    val filled = fill.alpha > 0f
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Section("Preenchimento")
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Preencher", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(fill.copy(alpha = 1f)) {
                store.beginGesture("cor da forma")
                env.openColor(ColorRequest(floatArrayOf(fill.red, fill.green, fill.blue, if (filled) fill.alpha else 1f),
                    onChange = { r, g, b, a -> store.setShapeFill(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
            Spacer(Modifier.width(12.dp))
            AureaToggle(checked = filled, onCheckedChange = { on ->
                store.setShapeFill(fill.red, fill.green, fill.blue, if (on) 1f else 0f)
            })
        }
        Section("Contorno")
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Cor do contorno", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(stroke.copy(alpha = 1f)) {
                store.beginGesture("cor do contorno")
                env.openColor(ColorRequest(floatArrayOf(stroke.red, stroke.green, stroke.blue, if (stroke.alpha > 0f) stroke.alpha else 1f),
                    onChange = { r, g, b, a -> store.setShapeStroke(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
        }
        ShapeRuler(store, "Largura do contorno", { store.detail?.shapeStrokeWidth ?: 0f }, "${detail.shapeStrokeWidth.roundToInt()} px",
            0.2f, 0f, 200f, "contorno") { store.setShapeParam(4, it) }
        Section("Forma")
        if (type == 0) {
            val maxR = minOf(detail.sourceWidth, detail.sourceHeight) / 2f
            ShapeRuler(store, "Cantos", { store.detail?.shapeCorner ?: 0f }, "${detail.shapeCorner.roundToInt()} px",
                0.3f, 0f, maxR, "cantos") { store.setShapeParam(1, it) }
        }
        if (type == 3 || type == 4 || type == 8) {
            ShapeRuler(store, if (type == 3) "Lados" else "Pontas", { ((store.detail?.shapeTypePoints ?: 0) ushr 16).toFloat() }, "$points",
                0.05f, 3f, 24f, "pontas") { store.setShapeParam(2, it) }
        }
        if (type == 4 || type == 5 || type == 6) {
            ShapeRuler(store, if (type == 4) "Raio interno" else "Espessura", { (store.detail?.shapeInner ?: 0.5f) * 100f },
                "${(detail.shapeInner * 100f).roundToInt()}%", 0.3f, 5f, 95f, "raio interno") { store.setShapeParam(3, it / 100f) }
        }
        ShapeRuler(store, "Largura", { store.detail?.sourceWidth?.toFloat() ?: 100f }, "${detail.sourceWidth} px",
            1f, 1f, 8192f, "largura") { store.setShapeParam(5, it) }
        ShapeRuler(store, "Altura", { store.detail?.sourceHeight?.toFloat() ?: 100f }, "${detail.sourceHeight} px",
            1f, 1f, 8192f, "altura") { store.setShapeParam(6, it) }
    }
}

@Composable
private fun Section(title: String) {
    Spacer(Modifier.height(8.dp))
    Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(2.dp))
}

@Composable
private fun ShapeRuler(
    store: EditorStore,
    label: String,
    value: () -> Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    gesture: String,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}
