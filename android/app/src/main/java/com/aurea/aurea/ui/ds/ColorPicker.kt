package com.aurea.aurea.ui.ds

import com.aurea.aurea.ui.i18n.KeepLtr
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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
import android.content.Context
import android.graphics.Bitmap
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

/** Cor de TELA (sRGB) a partir de RGBA 0..1 já em sRGB. */
fun rgbaColor(v: FloatArray): Color =
    Color(v.getOrElse(0) { 0f }.coerceIn(0f, 1f), v.getOrElse(1) { 0f }.coerceIn(0f, 1f), v.getOrElse(2) { 0f }.coerceIn(0f, 1f), v.getOrElse(3) { 1f }.coerceIn(0f, 1f))

private fun linearToSrgb(c: Float): Float {
    val x = c.coerceIn(0f, 1f)
    return if (x <= 0.0031308f) x * 12.92f else 1.055f * x.pow(1f / 2.4f) - 0.055f
}

private fun srgbToLinear(c: Float): Float {
    val x = c.coerceIn(0f, 1f)
    return if (x <= 0.04045f) x / 12.92f else ((x + 0.055f) / 1.055f).pow(2.4f)
}

/**
 * O motor guarda cor (parâmetro de efeito, fundo da composição) em RGBA
 * LINEAR; o seletor e as amostras trabalham em sRGB, que é o que a tela
 * mostra. Estas duas são a fronteira — alfa não tem curva.
 */
fun engineToDisplay(v: FloatArray): FloatArray =
    floatArrayOf(linearToSrgb(v.getOrElse(0) { 0f }), linearToSrgb(v.getOrElse(1) { 0f }), linearToSrgb(v.getOrElse(2) { 0f }), v.getOrElse(3) { 1f })

fun displayToEngine(r: Float, g: Float, b: Float, a: Float): FloatArray =
    floatArrayOf(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), a.coerceIn(0f, 1f))

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

/** Xadrez de "transparente" (também o swatch do fundo transparente do projeto). */
internal fun DrawScope.drawChecker() {
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
    /** Conta-gotas (app antigo): o quadro do cabeçote renderizado; nulo = sem o botão. */
    pickFromPreview: (() -> Bitmap?)? = null,
    /** Replacing a project/panel cancels a pending eyedropper result. */
    previewIdentity: Any? = null,
    onChange: (r: Float, g: Float, b: Float, a: Float) -> Unit,
    onDone: () -> Unit,
) {
    val context = LocalContext.current
    var palette by remember { mutableStateOf(SavedPalette.load(context)) }
    var picking by remember(previewIdentity) { mutableStateOf(false) }
    var frame by remember(previewIdentity) { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(picking, previewIdentity) {
        if (picking && frame == null && pickFromPreview != null) {
            var captured: Bitmap? = null
            try {
                withContext(Dispatchers.Default) { captured = runCatching { pickFromPreview() }.getOrNull() }
                frame = captured
                captured = null
                if (frame == null) picking = false
            } finally {
                // Native capture may finish after this sheet was dismissed.
                captured?.recycle()
            }
        }
    }
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
                Text(stringResource(R.string.ds_cor), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
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
                    stringResource(R.string.ds_pronto),
                    modifier = Modifier.tocavel { sheet.dismiss() }.padding(vertical = 6.dp, horizontal = 4.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
                )
            }
            Spacer(Modifier.height(10.dp))
            val shot = frame
            if (picking && shot != null) {
                // Conta-gotas: arrastar a lupa sobre a prévia amostra a cor da composição.
                PreviewEyedropper(shot) { c -> setColor(c.copy(alpha = a)) }
            } else {
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
            // "# RRGGBB · H S V" é código: LTR também em árabe (o # antes do hex).
            KeepLtr { Row(verticalAlignment = Alignment.CenterVertically) {
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
            } }
            if (pickFromPreview != null) {
                Spacer(Modifier.height(10.dp))
                Row(
                    Modifier
                        .clip(RoundedCornerShape(15.dp))
                        .background(if (picking) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.Chip)
                        .tocavel { picking = !picking; if (!picking) frame = null }
                        .padding(horizontal = 12.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CupertinoIcon(CupertinoGlyph.Eyedropper, 15.dp, if (picking) AureaColors.Accent else AureaColors.Text)
                    Spacer(Modifier.width(6.dp))
                    Text(
                        stringResource(if (picking) R.string.ds_arraste_para_pegar else R.string.ds_pegar_da_previa),
                        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (picking) AureaColors.Accent else AureaColors.Text)),
                    )
                }
            }
            Spacer(Modifier.height(12.dp))
            // Paleta salva (por aparelho): + guarda a cor atual, toque reusa, segurar apaga.
            Text(stringResource(R.string.ds_paleta_salva), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Spacer(Modifier.height(8.dp))
            val saveLabel = stringResource(R.string.ds_salvar_cor)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(
                    Modifier
                        .size(30.dp)
                        .clip(CircleShape)
                        .background(AureaColors.Chip)
                        .border(1.dp, AureaColors.Border, CircleShape)
                        .semantics { contentDescription = saveLabel }
                        .tocavel { palette = SavedPalette.add(context, palette, current().toArgb()) },
                    contentAlignment = Alignment.Center,
                ) { CupertinoIcon(CupertinoGlyph.Plus, 14.dp, AureaColors.Text) }
                palette.forEach { argb ->
                    val q = Color(argb)
                    Box(
                        Modifier
                            .size(30.dp)
                            .clip(CircleShape)
                            .border(1.dp, AureaColors.Border, CircleShape)
                            .pointerInput(argb) {
                                detectTapGestures(
                                    onTap = { setColor(q) },
                                    onLongPress = { palette = SavedPalette.remove(context, palette, argb) },
                                )
                            },
                    ) {
                        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) { drawChecker(); drawRect(q) }
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            Text(stringResource(R.string.ds_rapidas), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
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
 * Paleta salva, POR APARELHO (SharedPreferences): cores ARGB sRGB, a mais nova
 * primeiro, sem repetir, até 24.
 */
internal object SavedPalette {
    private const val PREFS = "aurea_palette"
    private const val KEY = "colors"
    private const val MAX = 24

    fun load(context: Context): List<Int> =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, "").orEmpty()
            .split(',').mapNotNull { it.trim().toLongOrNull()?.toInt() }.take(MAX)

    private fun save(context: Context, colors: List<Int>): List<Int> {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY, colors.joinToString(",") { (it.toLong() and 0xFFFFFFFFL).toString() }).apply()
        return colors
    }

    fun add(context: Context, current: List<Int>, argb: Int): List<Int> = save(context, (listOf(argb) + current.filter { it != argb }).take(MAX))
    fun remove(context: Context, current: List<Int>, argb: Int): List<Int> = save(context, current.filter { it != argb })
}

/** Pixel da imagem encaixada (centralizada, escala `k`) sob o ponto `p` da caixa. */
private fun framePixel(frame: Bitmap, boxW: Float, boxH: Float, p: Offset): Pair<Int, Int> {
    val k = min(boxW / frame.width, boxH / frame.height)
    val ox = (boxW - frame.width * k) / 2f
    val oy = (boxH - frame.height * k) / 2f
    return ((p.x - ox) / k).toInt().coerceIn(0, frame.width - 1) to ((p.y - oy) / k).toInt().coerceIn(0, frame.height - 1)
}

/**
 * O CONTA-GOTAS: a prévia (o quadro do cabeçote) numa caixa de 200 dp;
 * arrastar mostra a lupa (pixels ampliados, mira e anel da cor) e manda a cor
 * ao vivo; tocar pega na hora.
 */
@Composable
private fun PreviewEyedropper(frame: Bitmap, onColor: (Color) -> Unit) {
    val image = remember(frame) { frame.asImageBitmap() }
    var touch by remember { mutableStateOf<Pair<Int, Int>?>(null) }
    var picked by remember { mutableStateOf(Color.Transparent) }
    val send by rememberUpdatedState(onColor)
    val label = stringResource(R.string.ds_arraste_para_pegar)
    fun pick(px: Int, py: Int) {
        picked = Color(frame.getPixel(px, py)).copy(alpha = 1f)
        send(picked)
    }
    Box(
        Modifier
            .fillMaxWidth()
            .height(200.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(Color.Black)
            .semantics { contentDescription = label }
            .pointerInput(frame) {
                detectDragGestures(
                    onDragStart = { p -> framePixel(frame, size.width.toFloat(), size.height.toFloat(), p).also { touch = it; pick(it.first, it.second) } },
                    onDragEnd = { touch = null },
                    onDragCancel = { touch = null },
                ) { change, _ ->
                    change.consume()
                    framePixel(frame, size.width.toFloat(), size.height.toFloat(), change.position).also { touch = it; pick(it.first, it.second) }
                }
            }
            .pointerInput(frame) {
                detectTapGestures { p -> framePixel(frame, size.width.toFloat(), size.height.toFloat(), p).also { pick(it.first, it.second) } }
            },
    ) {
        Canvas(Modifier.fillMaxWidth().fillMaxHeight()) {
            val k = min(size.width / frame.width, size.height / frame.height)
            val dw = (frame.width * k).roundToInt()
            val dh = (frame.height * k).roundToInt()
            val ox = ((size.width - dw) / 2f).roundToInt()
            val oy = ((size.height - dh) / 2f).roundToInt()
            drawImage(image, dstOffset = IntOffset(ox, oy), dstSize = IntSize(dw, dh))
            val (px, py) = touch ?: return@Canvas
            // Lupa acima do dedo: 11 × 11 pixels ampliados, mira no centro, anel da cor.
            val tx = ox + (px + 0.5f) * k
            val ty = oy + (py + 0.5f) * k
            val r = 34.dp.toPx()
            val c = Offset(tx.coerceIn(r, size.width - r), (ty - r - 18.dp.toPx()).coerceAtLeast(r))
            val n = 11
            val sx = (px - n / 2).coerceIn(0, max(0, frame.width - n))
            val sy = (py - n / 2).coerceIn(0, max(0, frame.height - n))
            val lens = androidx.compose.ui.graphics.Path().apply { addOval(androidx.compose.ui.geometry.Rect(c, r)) }
            clipPath(lens) {
                drawRect(Color.Black, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
                drawImage(
                    image,
                    srcOffset = IntOffset(sx, sy),
                    srcSize = IntSize(min(n, frame.width), min(n, frame.height)),
                    dstOffset = IntOffset((c.x - r).roundToInt(), (c.y - r).roundToInt()),
                    dstSize = IntSize((2 * r).roundToInt(), (2 * r).roundToInt()),
                    filterQuality = FilterQuality.None,
                )
                val cell = 2 * r / n
                drawRect(Color.White, Offset(c.x - cell / 2, c.y - cell / 2), Size(cell, cell), style = Stroke(1.5.dp.toPx()))
            }
            drawCircle(picked, r, c, style = Stroke(5.dp.toPx()))
            drawCircle(Color.White, r + 2.5.dp.toPx(), c, style = Stroke(1.5.dp.toPx()))
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
