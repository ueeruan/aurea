package com.aurea.aurea.ui.ds

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.roundToInt

/** Cor RGBA 0..1 (o formato dos parâmetros de cor do motor). */
fun rgbaColor(v: FloatArray): Color =
    Color(v.getOrElse(0) { 0f }.coerceIn(0f, 1f), v.getOrElse(1) { 0f }.coerceIn(0f, 1f), v.getOrElse(2) { 0f }.coerceIn(0f, 1f), v.getOrElse(3) { 1f }.coerceIn(0f, 1f))

/**
 * A AMOSTRA DE COR (`_LinhaDeCor` da A.01): quadrado 30 × 30, raio 6, com o
 * xadrez por baixo para a transparência aparecer.
 */
@Composable
fun ColorWell(color: Color, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Box(
        modifier
            .size(30.dp)
            .clip(RoundedCornerShape(6.dp))
            .tocavel(onClick = onClick),
    ) {
        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) {
            drawChecker()
            drawRect(color)
        }
    }
}

private fun DrawScope.drawChecker() {
    val cell = 6.dp.toPx()
    val a = Color(0xFF3A4150)
    val b = Color(0xFF2A303B)
    drawRect(a)
    var y = 0f
    var row = 0
    while (y < size.height) {
        var x = if (row % 2 == 0) cell else 0f
        while (x < size.width) {
            drawRect(b, Offset(x, y), Size(cell, cell))
            x += cell * 2
        }
        y += cell
        row++
    }
}

private val QuickColors = listOf(
    Color.White, Color.Black, Color(0xFF6FAED9), Color(0xFFA9D3EC), Color(0xFF35C4E7), Color(0xFF2BE3A0),
    Color(0xFFFFB020), Color(0xFFFF6B6B), Color(0xFFFF4FA3), Color(0xFFAAB6C3), Color(0xFF1B2530), Color(0xFFF7F9FB),
)

/**
 * O SELETOR DE COR (`aurea_seletor_de_cor.dart`, modo Quadro): "Cor", amostras
 * Original | Nova (tocar a Original volta a ela), Pronto; quadro S×V de 170,
 * faixa de matiz 26, faixa de alfa 26, código e HSV, cores rápidas.
 *
 * A cor sai VIVA por [onChange] enquanto o dedo mexe; quem chama abre UM passo
 * de desfazer ao abrir a folha e fecha em [onDone] (bug B-07).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ColorPickerSheet(
    initial: FloatArray,
    withAlpha: Boolean = true,
    onChange: (r: Float, g: Float, b: Float, a: Float) -> Unit,
    onDone: () -> Unit,
) {
    val original = remember { rgbaColor(initial) }
    val hsv0 = remember {
        FloatArray(3).also { android.graphics.Color.colorToHSV(original.copy(alpha = 1f).toArgb(), it) }
    }
    var h by remember { mutableFloatStateOf(hsv0[0]) }
    var s by remember { mutableFloatStateOf(hsv0[1]) }
    var v by remember { mutableFloatStateOf(hsv0[2]) }
    var a by remember { mutableFloatStateOf(original.alpha) }
    val emit by rememberUpdatedState(onChange)

    fun current(): Color {
        val argb = android.graphics.Color.HSVToColor(floatArrayOf(h, s, v))
        return Color(argb).copy(alpha = a)
    }

    fun push() {
        val c = current()
        emit(c.red, c.green, c.blue, c.alpha)
    }

    fun setColor(c: Color) {
        val hsv = FloatArray(3)
        android.graphics.Color.colorToHSV(c.copy(alpha = 1f).toArgb(), hsv)
        h = hsv[0]; s = hsv[1]; v = hsv[2]
        a = c.alpha
        push()
    }

    AureaAdjustSheet(onDismiss = onDone) { sheet ->
        Column(Modifier.padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Cor", style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
                Spacer(Modifier.width(12.dp))
                Row(
                    Modifier
                        .size(width = 76.dp, height = 28.dp)
                        .clip(RoundedCornerShape(8.dp)),
                ) {
                    Box(Modifier.weight(1f).fillMaxHeight().tocavel(shrink = 1f) { setColor(original) }) {
                        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) { drawChecker(); drawRect(original) }
                    }
                    Box(Modifier.weight(1f).fillMaxHeight()) {
                        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) { drawChecker(); drawRect(current()) }
                    }
                }
                Spacer(Modifier.weight(1f))
                Text(
                    "Pronto",
                    modifier = Modifier.tocavel { sheet.dismiss() }.padding(vertical = 6.dp, horizontal = 4.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
                )
            }
            Spacer(Modifier.height(10.dp))
            // Quadro saturação × brilho.
            val hueColor = Color(android.graphics.Color.HSVToColor(floatArrayOf(h, 1f, 1f)))
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(170.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .pointerInput(Unit) {
                        fun pick(p: Offset) {
                            s = (p.x / size.width).coerceIn(0f, 1f)
                            v = 1f - (p.y / size.height).coerceIn(0f, 1f)
                            push()
                        }
                        detectDragGestures(onDragStart = { pick(it) }) { change, _ -> change.consume(); pick(change.position) }
                    }
                    .pointerInput(Unit) {
                        detectTapGestures { p ->
                            s = (p.x / size.width).coerceIn(0f, 1f)
                            v = 1f - (p.y / size.height).coerceIn(0f, 1f)
                            push()
                        }
                    },
            ) {
                Canvas(Modifier.fillMaxWidth().fillMaxHeight()) {
                    drawRect(Brush.horizontalGradient(listOf(Color.White, hueColor)))
                    drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.Black)))
                    val c = Offset(s * size.width, (1f - v) * size.height)
                    drawCircle(Color.White, 9.dp.toPx(), c, style = Stroke(2.5.dp.toPx()))
                }
            }
            Spacer(Modifier.height(14.dp))
            Strip(
                value = h / 360f,
                onValue = { h = it * 360f; push() },
                background = { drawRect(Brush.horizontalGradient(List(7) { i -> Color(android.graphics.Color.HSVToColor(floatArrayOf(i * 60f, 1f, 1f))) })) },
            )
            if (withAlpha) {
                Spacer(Modifier.height(10.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.weight(1f)) {
                        Strip(
                            value = a,
                            onValue = { a = it; push() },
                            background = {
                                drawChecker()
                                drawRect(Brush.horizontalGradient(listOf(current().copy(alpha = 0f), current().copy(alpha = 1f))))
                            },
                        )
                    }
                    Spacer(Modifier.width(8.dp))
                    Box(
                        Modifier.size(width = 52.dp, height = 30.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text("${(a * 100).roundToInt()}%", style = AureaType.Value.merge(TextStyle(fontSize = 13.sp)))
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                val c = current()
                val hex = String.format(Locale.ROOT, "%02X%02X%02X", (c.red * 255).roundToInt(), (c.green * 255).roundToInt(), (c.blue * 255).roundToInt())
                Text("#", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
                Spacer(Modifier.width(4.dp))
                Box(
                    Modifier.width(96.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(horizontal = 8.dp, vertical = 6.dp),
                ) {
                    Text(hex, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontFeatureSettings = "tnum")))
                }
                Spacer(Modifier.width(8.dp))
                Text(
                    "H ${h.roundToInt()}°  S ${(s * 100).roundToInt()}%  V ${(v * 100).roundToInt()}%",
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
                )
            }
            Spacer(Modifier.height(12.dp))
            Text("Rápidas", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Spacer(Modifier.height(8.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                QuickColors.forEach { q ->
                    Box(
                        Modifier
                            .size(30.dp)
                            .clip(CircleShape)
                            .background(q)
                            .border(1.dp, AureaColors.Border, CircleShape)
                            .tocavel { setColor(q.copy(alpha = a)) },
                    )
                }
            }
        }
    }
}

/**
 * Uma faixa de 26 dp (matiz ou alfa) com a alça 14 × 30, raio 7, borda branca
 * 2,5 — arrastar ou tocar escolhe.
 */
@Composable
private fun Strip(value: Float, onValue: (Float) -> Unit, background: DrawScope.() -> Unit) {
    val send by rememberUpdatedState(onValue)
    Box(
        Modifier
            .fillMaxWidth()
            .height(30.dp)
            .pointerInput(Unit) {
                detectDragGestures(onDragStart = { send((it.x / size.width).coerceIn(0f, 1f)) }) { change, _ ->
                    change.consume()
                    send((change.position.x / size.width).coerceIn(0f, 1f))
                }
            }
            .pointerInput(Unit) { detectTapGestures { send((it.x / size.width).coerceIn(0f, 1f)) } },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxWidth().height(26.dp).clip(RoundedCornerShape(6.dp))) { background() }
        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) {
            val hw = 7.dp.toPx()
            val x = (value.coerceIn(0f, 1f) * size.width).coerceIn(hw, size.width - hw)
            drawRoundRect(
                color = Color.White,
                topLeft = Offset(x - hw, 0f),
                size = Size(hw * 2, size.height),
                cornerRadius = CornerRadius(hw),
                style = Stroke(2.5.dp.toPx()),
            )
        }
    }
}
