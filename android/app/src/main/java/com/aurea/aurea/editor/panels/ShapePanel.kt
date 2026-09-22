package com.aurea.aurea.editor.panels

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
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.theme.AureaType

/** RGBA8 (R no byte baixo) → Color sRGB. */
private fun rgba8(v: Int): Color = Color(
    red = (v and 0xFF) / 255f,
    green = ((v ushr 8) and 0xFF) / 255f,
    blue = ((v ushr 16) and 0xFF) / 255f,
    alpha = ((v ushr 24) and 0xFF) / 255f,
)

/**
 * COR E PREENCHIMENTO da forma: preenchimento (liga/desliga e cor) e contorno
 * (cor e largura). Os parâmetros da silhueta (tamanho, raio, pontas…) moram em
 * EDITAR FORMA ([ShapeEditPanel]). Tudo é o SDF do motor — a forma continua
 * vetorial em qualquer escala.
 */
@Composable
internal fun ShapePanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val fill = rgba8(detail.shapeFill)
    val stroke = rgba8(detail.shapeStroke)
    val filled = fill.alpha > 0f
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 8.dp, end = 12.dp, bottom = 24.dp)) {
        KitTitle("Preenchimento")
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Preencher", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
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
        KitTitle("Contorno")
        ColorLine("Cor do contorno", stroke.copy(alpha = 1f)) {
            store.beginGesture("cor do contorno")
            env.openColor(ColorRequest(floatArrayOf(stroke.red, stroke.green, stroke.blue, if (stroke.alpha > 0f) stroke.alpha else 1f),
                onChange = { r, g, b, a -> store.setShapeStroke(r, g, b, a) },
                onDone = { store.endGesture() }))
        }
        HumanRow(
            env, "Largura", detail.shapeStrokeWidth, 0.2f, 0f, 500f, "px", 0, 0f,
            onStart = { store.beginGesture("contorno") },
            onValue = { store.setShapeParam(4, it) },
            onEnd = { store.endGesture() },
            onCommit = { v ->
                store.beginGesture("contorno")
                // Largura sem cor: o contorno nasce visível (senão o número mente).
                if (v > 0f && stroke.alpha <= 0f) store.setShapeStroke(stroke.red, stroke.green, stroke.blue, 1f)
                store.setShapeParam(4, v)
                store.endGesture()
            },
        )
        if (detail.shapeStrokeWidth > 0f && stroke.alpha <= 0f) {
            KitHint("O contorno está sem cor: escolha uma cor acima para ele aparecer.")
        }
    }
}
