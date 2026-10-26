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
// Omega aparência (área 05): seletor embutido, balão e paleta única.
import android.content.ClipData
import android.content.ClipboardManager
import android.os.SystemClock
import android.widget.Toast
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.engine.AureaEngine
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin

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
 * Omega aparência (área 05, P0-5): a PALETA ÚNICA, por aparelho
 * (SharedPreferences): cores ARGB sRGB, sem repetir, até [MAX]. É a mesma no
 * seletor embutido, no balão, na tela cheia e na folha antiga. Na primeira
 * leitura a "Paleta salva" de antes vai para o topo e as 14 cores de fábrica do
 * AUREA ([Defaults], 2 fileiras de 7) completam. "+" põe a cor no INÍCIO.
 * [colors] é estado do Compose: mudar a paleta num seletor redesenha os outros.
 */
internal object SavedPalette {
    const val PREFS = "aurea_palette"
    /** Lista antiga (até a 0.3.1): só lida uma vez, na migração. */
    private const val KEY = "colors"
    private const val KEY_OMEGA = "omega.colorpicker.palette"
    /** Faixa de alfa do modo fino ligada (lembrada; iOS usa a mesma chave). */
    const val KEY_ALPHA = "omega.colorpicker.alpha"
    /** Aba da tela cheia: [TAB_WHEEL] ou [TAB_RGB] (lembrada; iOS usa a mesma chave). */
    const val KEY_TAB = "omega.colorpicker.tab"
    const val TAB_WHEEL = "wheel"
    const val TAB_RGB = "rgb"
    const val MAX = 63

    /** As 14 cores de fábrica do AUREA (iguais às do iOS), ARGB sRGB. */
    val Defaults: List<Int> = listOf(
        0xFFFF3B30, 0xFFFF9500, 0xFFFFCC00, 0xFF34C759, 0xFF00C7BE, 0xFF007AFF, 0xFFAF52DE,
        0xFFFFFFFF, 0xFFC7C7CC, 0xFF8E8E93, 0xFF48484A, 0xFF000000, 0xFF5AC8FA, 0xFFA8E063,
    ).map { it.toInt() }

    /** A paleta em uso depois da primeira escrita (nula = ainda não mudou nesta sessão). */
    var colors by mutableStateOf<List<Int>?>(null)
        private set

    private fun parse(text: String): List<Int> =
        text.split(',').mapNotNull { it.trim().toLongOrNull()?.toInt() }

    /** Lê a paleta (migrando a antiga na primeira vez) sem mexer no estado. */
    fun peek(context: Context): List<Int> {
        colors?.let { return it }
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs.getString(KEY_OMEGA, null)?.let { return parse(it).distinct().take(MAX) }
        val migrated = (parse(prefs.getString(KEY, "").orEmpty()) + Defaults).distinct().take(MAX)
        prefs.edit().putString(KEY_OMEGA, migrated.joinToString(",") { (it.toLong() and 0xFFFFFFFFL).toString() }).apply()
        return migrated
    }

    fun load(context: Context): List<Int> = peek(context)

    /** Grava a lista inteira (reordenar, apagar) e publica no estado. */
    fun replace(context: Context, list: List<Int>): List<Int> {
        val clean = list.distinct().take(MAX)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY_OMEGA, clean.joinToString(",") { (it.toLong() and 0xFFFFFFFFL).toString() }).apply()
        colors = clean
        return clean
    }

    fun add(context: Context, current: List<Int>, argb: Int): List<Int> = replace(context, (listOf(argb) + current.filter { it != argb }).take(MAX))
    fun remove(context: Context, current: List<Int>, argb: Int): List<Int> = replace(context, current.filter { it != argb })
}

/** A paleta única para desenhar (relê quando qualquer seletor muda a lista). */
@Composable
internal fun rememberSavedPalette(): List<Int> {
    val context = LocalContext.current
    val first = remember { SavedPalette.peek(context) }
    return SavedPalette.colors ?: first
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

// =============================================================================
// Omega aparência (área 05, P0-5 · P1-7): CONTAS DE COR, SELETOR EMBUTIDO e BALÃO
//
//   ┌──────────── área esquerda ────────────┬59─┐  127,5 dp, raio 5, fundo A11
//   │ faixa 26: cor · #RRGGBB · +           │ 💧│  conta-gotas (palco ao vivo)
//   │ grade 7 × (27 + 1,5/3)                │ ≡ │  ajuste fino (S×V · anel · alfa)
//   │                                       │ ◐ │  mais cores (tela cheia)
//   └───────────────────────────────────────┴───┘
//
// Par do `EmbeddedColorPicker`/`ColorPopover` do iOS (ColorPickerKit.swift):
// mesmos ids (`colorpicker.*`), textos (`om_ap_cp_*`) e contas (motor).
// =============================================================================

/** Medidas do seletor embutido (as da referência). */
internal object PickerDims {
    val Height = 127.5.dp
    val Radius = 5.dp
    val SideWidth = 59.dp
    val TitleHeight = 26.dp
    val GridWidth = 210.dp
    val Cell = 30.dp
    val Swatch = 27.dp
    val HueWidth = 108.dp
    val AlphaWidth = 29.dp
    val PopoverWidth = 290.dp
    val ArrowHeight = 9.dp
    val ArrowWidth = 18.dp
    /** Espera depois da última cor mandada antes de aceitar a cor que volta do motor. */
    const val ECHO_MS = 350L
}

/** Papéis de cor do seletor (todos por token). */
internal object PickerColors {
    /** «fundo-fundo» (A11): caixa do seletor, tela cheia, seta do balão. */
    val Box: Color get() = AureaColors.PanelFrameDivider
    /** «fundo-mais-fundo» (A12): rodapé da paleta na tela cheia. */
    val Deepest: Color get() = AureaColors.FxDarkest
    /** «painel» (A9): botões da coluna direita, ✓ da tela cheia. */
    val Side: Color get() = AureaColors.PanelFrameBackground
}

/** RGBA 0..1 com 4 componentes (cópia; alfa 1 se faltar). */
internal fun rgba4(v: FloatArray): FloatArray = floatArrayOf(
    v.getOrElse(0) { 0f }.coerceIn(0f, 1f), v.getOrElse(1) { 0f }.coerceIn(0f, 1f),
    v.getOrElse(2) { 0f }.coerceIn(0f, 1f), v.getOrElse(3) { 1f }.coerceIn(0f, 1f),
)

/** RGBA 0..1 → ARGB 8 bits (a forma da paleta). */
internal fun rgbaToArgb(v: FloatArray): Int {
    fun b(x: Float) = (x.coerceIn(0f, 1f) * 255f).roundToInt()
    return (b(v.getOrElse(3) { 1f }) shl 24) or (b(v.getOrElse(0) { 0f }) shl 16) or (b(v.getOrElse(1) { 0f }) shl 8) or b(v.getOrElse(2) { 0f })
}

internal fun argbToRgba(argb: Int): FloatArray = floatArrayOf(
    ((argb shr 16) and 0xFF) / 255f, ((argb shr 8) and 0xFF) / 255f, (argb and 0xFF) / 255f, ((argb ushr 24) and 0xFF) / 255f,
)

/** Mesma cor (até [tolerance] por componente). */
internal fun sameColor(a: FloatArray, b: FloatArray, tolerance: Float = 0.5f / 255f): Boolean {
    for (i in 0 until 4) if (abs(a.getOrElse(i) { 1f } - b.getOrElse(i) { 1f }) > tolerance) return false
    return true
}

/** RGB 0..1 → [matiz 0..360, saturação, brilho] em [out]. */
internal fun rgbToHsv(r: Float, g: Float, b: Float, out: FloatArray) {
    val mx = max(r, max(g, b))
    val mn = min(r, min(g, b))
    val d = mx - mn
    var h = 0f
    if (d > 1e-6f) {
        h = when (mx) {
            r -> ((g - b) / d) % 6f
            g -> (b - r) / d + 2f
            else -> (r - g) / d + 4f
        } * 60f
        if (h < 0f) h += 360f
    }
    out[0] = h
    out[1] = if (mx <= 1e-6f) 0f else d / mx
    out[2] = mx
}

/** HSV → RGB 0..1 em [out] (0..2). */
internal fun hsvToRgb(h: Float, s: Float, v: Float, out: FloatArray) {
    val hh = ((h % 360f) + 360f) % 360f / 60f
    val c = v * s
    val x = c * (1f - abs(hh % 2f - 1f))
    val m = v - c
    var r = 0f
    var g = 0f
    var b = 0f
    when (hh.toInt()) {
        0 -> { r = c; g = x }
        1 -> { r = x; g = c }
        2 -> { g = c; b = x }
        3 -> { g = x; b = c }
        4 -> { r = x; b = c }
        else -> { r = c; b = x }
    }
    out[0] = (r + m).coerceIn(0f, 1f)
    out[1] = (g + m).coerceIn(0f, 1f)
    out[2] = (b + m).coerceIn(0f, 1f)
}

/** Cor pura da matiz (saturação e brilho cheios). */
internal fun hueColor(h: Float): Color {
    val o = FloatArray(4)
    hsvToRgb(h, 1f, 1f, o)
    return Color(o[0], o[1], o[2])
}

/** As 7 paradas do anel/faixa de matiz (0°, 60°, … 360°). */
internal val HueStops: List<Color> = List(7) { hueColor(it * 60f) }

/**
 * Estado do seletor: matiz/saturação/brilho/alfa locais (a matiz não se perde
 * em cinza nem a saturação no preto). [dragging] e [lastEmitAt] seguram a cor
 * que volta do motor enquanto o dedo mexe (ela chega atrasada).
 */
@Stable
internal class ColorPickState(rgba: FloatArray) {
    var hue by mutableFloatStateOf(0f)
    var sat by mutableFloatStateOf(0f)
    var value by mutableFloatStateOf(1f)
    var alpha by mutableFloatStateOf(1f)
    var dragging = false
    var lastEmitAt = 0L

    init { set(rgba) }

    fun set(rgba: FloatArray) {
        val c = rgba4(rgba)
        val hsv = FloatArray(3)
        rgbToHsv(c[0], c[1], c[2], hsv)
        if (hsv[2] > 1e-4f) {
            if (hsv[1] > 1e-4f) hue = hsv[0]
            sat = hsv[1]
        }
        value = hsv[2]
        alpha = c[3]
    }

    fun rgba(): FloatArray {
        val out = FloatArray(4)
        hsvToRgb(hue, sat, value, out)
        out[3] = alpha.coerceIn(0f, 1f)
        return out
    }

    fun matches(rgba: FloatArray): Boolean = sameColor(rgba(), rgba4(rgba), 1f / 255f)
}

/** O estado do seletor seguindo a cor de fora (desfazer, cabeçote, conta-gotas). */
@Composable
internal fun rememberPickState(color: FloatArray): ColorPickState {
    val state = remember { ColorPickState(color) }
    val latest by rememberUpdatedState(color)
    val c = rgba4(color)
    LaunchedEffect(c[0], c[1], c[2], c[3]) {
        val wait = state.lastEmitAt + PickerDims.ECHO_MS - SystemClock.uptimeMillis()
        if (wait > 0) delay(wait)
        if (!state.dragging && !state.matches(latest)) state.set(latest)
    }
    return state
}

/** Abre o passo de desfazer na primeira cor mandada e fecha no fim do gesto. */
internal class GestureGate(private val start: () -> Unit, private val end: () -> Unit) {
    private var open = false
    fun touch() {
        if (!open) {
            open = true
            start()
        }
    }
    fun close() {
        if (open) {
            open = false
            end()
        }
    }
}

/** Texto da área de transferência (ou nulo). */
internal fun readClipboardText(context: Context): String? = runCatching {
    val cb = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    cb.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(context)?.toString()
}.getOrNull()

internal fun writeClipboardText(context: Context, text: String) {
    runCatching {
        val cb = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cb.setPrimaryClip(ClipData.newPlainText("color", text))
    }
}

/** "#RRGGBB" de uma cor da paleta (rótulo de acessibilidade). */
internal fun argbLabel(argb: Int): String = String.format(Locale.ROOT, "#%06X", argb and 0xFFFFFF)

/** Tinta legível sobre a cor (a conta é do motor: igual no iOS). */
internal fun inkOver(rgba: FloatArray): Color =
    if (AureaEngine.colorWantsDarkText(rgba[0], rgba[1], rgba[2], rgba.getOrElse(3) { 1f })) Color.Black else Color.White

private enum class FineLabel { None, Hue, SatVal, Alpha }

/**
 * O SELETOR DE COR EMBUTIDO (Cor e preenchimento, balão): 127,5 dp.
 *
 * @param color a cor atual (RGBA sRGB 0..1); mudanças de fora (desfazer, cabeçote, conta-gotas) entram.
 * @param onColor cada passo do dedo (RGBA sRGB 0..1).
 * @param withAlpha o campo aceita alfa; sem alfa, amostra transparente fica indisponível e a faixa de alfa some.
 * @param onGestureStart abre o passo de desfazer na primeira cor de um gesto (arrasto, toque numa
 *   amostra, colar, a tela cheia aberta); [onGestureEnd] fecha no fim dele. Quem já segura um passo
 *   (o balão) passa vazio.
 * @param eyedropperActive o conta-gotas do palco está ligado por este seletor.
 * @param onEyedropper liga/desliga o conta-gotas; nulo = sem o botão (fora do editor).
 * @param onMessage avisos curtos (o toast do editor); nulo = toast do sistema.
 */
@Composable
fun EmbeddedColorPicker(
    color: FloatArray,
    onColor: (FloatArray) -> Unit,
    modifier: Modifier = Modifier,
    withAlpha: Boolean = true,
    onGestureStart: () -> Unit = {},
    onGestureEnd: () -> Unit = {},
    eyedropperActive: Boolean = false,
    onEyedropper: (() -> Unit)? = null,
    onMessage: ((String) -> Unit)? = null,
) {
    val context = LocalContext.current
    val state = rememberPickState(color)
    val send by rememberUpdatedState(onColor)
    val startGesture by rememberUpdatedState(onGestureStart)
    val endGesture by rememberUpdatedState(onGestureEnd)
    val message by rememberUpdatedState(onMessage)
    val gate = remember { GestureGate({ startGesture() }, { endGesture() }) }
    DisposableEffect(Unit) { onDispose { gate.close() } }
    var fine by rememberSaveable { mutableStateOf(false) }
    var full by remember { mutableStateOf(false) }
    val unavailableText = stringResource(R.string.om_ap_cp_unavailable)
    val copiedText = stringResource(R.string.om_ap_cp_code_copied)

    fun notify(text: String) {
        val m = message
        if (m != null) m(text) else Toast.makeText(context, text, Toast.LENGTH_SHORT).show()
    }

    fun emit() {
        state.lastEmitAt = SystemClock.uptimeMillis()
        gate.touch()
        send(state.rgba())
    }

    // Um toque = um passo (amostra, colar).
    fun applyOnce(rgba: FloatArray) {
        state.set(rgba)
        emit()
        gate.close()
    }

    Row(
        modifier
            .fillMaxWidth()
            .height(PickerDims.Height)
            .clip(RoundedCornerShape(PickerDims.Radius))
            .background(PickerColors.Box)
            .testTag("colorpicker"),
    ) {
        Box(Modifier.weight(1f).fillMaxHeight()) {
            if (fine) {
                FineArea(state, withAlpha, onEmit = { emit() }, onEnd = { gate.close() })
            } else {
                SwatchArea(
                    state = state,
                    withAlpha = withAlpha,
                    onSwatch = { argb ->
                        val rgba = argbToRgba(argb)
                        if (!withAlpha && rgba[3] < 1f) notify(unavailableText)
                        else applyOnce(if (withAlpha) rgba else floatArrayOf(rgba[0], rgba[1], rgba[2], state.alpha))
                    },
                    onPaste = { rgba -> applyOnce(if (withAlpha) rgba else floatArrayOf(rgba[0], rgba[1], rgba[2], state.alpha)) },
                    onCopied = { notify(copiedText) },
                )
            }
        }
        Column(Modifier.width(PickerDims.SideWidth).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(1.dp)) {
            if (onEyedropper != null) {
                SideButton(CupertinoGlyph.Eyedropper, stringResource(R.string.om_ap_cp_eyedropper), eyedropperActive, "colorpicker.eyedropper", onEyedropper)
            }
            SideButton(CupertinoGlyph.SliderHorizontal3, stringResource(R.string.om_ap_cp_fine), fine, "colorpicker.fine") { fine = !fine }
            SideButton(CupertinoGlyph.ColorFilter, stringResource(R.string.om_ap_cp_full), false, "colorpicker.full") {
                gate.close()
                full = true
            }
        }
    }
    if (full) {
        ColorPickerFull(
            initial = state.rgba(),
            withAlpha = withAlpha,
            onChange = { r, g, b, a ->
                state.set(floatArrayOf(r, g, b, a))
                emit()
            },
            onDone = {
                full = false
                gate.close()
            },
        )
    }
}

/** Um botão da coluna direita (reparte a altura; aceso = destaque). */
@Composable
private fun ColumnScope.SideButton(glyph: Char, label: String, active: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .background(if (active) AureaColors.AccentDim else PickerColors.Side)
            .testTag(tag)
            .semantics {
                contentDescription = label
                selected = active
            }
            .tocavel(shrink = 1f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 20.dp, if (active) AureaColors.Accent else AureaColors.Text)
    }
}

/** Modo amostras: faixa da cor (código, "+") e a grade da paleta única. */
@Composable
private fun SwatchArea(
    state: ColorPickState,
    withAlpha: Boolean,
    onSwatch: (Int) -> Unit,
    onPaste: (FloatArray) -> Unit,
    onCopied: () -> Unit,
) {
    val context = LocalContext.current
    val palette = rememberSavedPalette()
    val cur = state.rgba()
    val argb = rgbaToArgb(if (withAlpha) cur else floatArrayOf(cur[0], cur[1], cur[2], 1f))
    val ink = inkOver(cur)
    val code = AureaEngine.formatColorCode(cur[0], cur[1], cur[2], cur[3], withAlpha)
    Column(Modifier.fillMaxSize()) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(PickerDims.TitleHeight)
                .drawBehind {
                    drawChecker()
                    drawRect(rgbaColor(cur))
                },
        ) {
            var menu by remember { mutableStateOf(false) }
            val codeLabel = stringResource(R.string.om_ap_cp_code, code)
            Box(
                Modifier
                    .fillMaxSize()
                    .testTag("colorpicker.hex")
                    .semantics { contentDescription = codeLabel }
                    .tocavel(shrink = 1f, onLongClick = { menu = true }) {},
                contentAlignment = Alignment.Center,
            ) {
                KeepLtr {
                    Text(code, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = ink, fontFeatureSettings = "tnum")))
                }
                // P1-7: copiar / colar o código (o colar mostra a cor da área de transferência).
                val clip = remember(menu) { if (menu) readClipboardText(context) else null }
                val parsed = remember(clip) { clip?.let { AureaEngine.parseColorCode(it) }?.takeIf { it.size >= 4 } }
                AnchoredMenu(
                    expanded = menu,
                    onDismiss = { menu = false },
                    tag = "colorpicker.code.menu",
                    entries = listOf(
                        MenuEntry.Item(stringResource(R.string.om_ap_cp_copy_code), glyph = CupertinoGlyph.DocOnDoc, tag = "colorpicker.code.copy") {
                            writeClipboardText(context, code)
                            onCopied()
                        },
                        MenuEntry.Item(
                            stringResource(R.string.om_ap_cp_paste_code),
                            swatch = parsed?.let { rgbaColor(it) },
                            glyph = if (parsed == null) CupertinoGlyph.DocOnClipboard else null,
                            enabled = parsed != null,
                            tag = "colorpicker.code.paste",
                        ) { parsed?.let { onPaste(rgba4(it)) } },
                    ),
                )
            }
            if (argb !in palette) {
                val addLabel = stringResource(R.string.om_ap_cp_add)
                Box(
                    Modifier
                        .align(Alignment.CenterEnd)
                        .width(44.dp)
                        .fillMaxHeight()
                        .testTag("colorpicker.add")
                        .semantics { contentDescription = addLabel }
                        .tocavel(shrink = 1f, haptic = true) { SavedPalette.add(context, palette, argb) },
                    contentAlignment = Alignment.CenterEnd,
                ) {
                    Box(Modifier.size(PickerDims.TitleHeight), contentAlignment = Alignment.Center) {
                        CupertinoIcon(CupertinoGlyph.Plus, 15.dp, ink)
                    }
                }
            }
        }
        Box(Modifier.fillMaxWidth().weight(1f)) {
            Column(
                Modifier
                    .width(PickerDims.GridWidth)
                    .fillMaxHeight()
                    .verticalScroll(rememberScrollState())
                    .padding(vertical = 3.dp),
            ) {
                palette.chunked(7).forEachIndexed { row, items ->
                    Row {
                        items.forEachIndexed { col, swatch ->
                            SwatchCell(
                                argb = swatch,
                                index = row * 7 + col,
                                size = PickerDims.Swatch,
                                selected = swatch == argb,
                                blocked = !withAlpha && (swatch ushr 24) < 0xFF,
                                onTap = { onSwatch(swatch) },
                            )
                        }
                    }
                }
            }
        }
    }
}

/** Uma amostra da paleta (célula de [cell]; amostra de [size], raio 5). */
@Composable
internal fun SwatchCell(
    argb: Int,
    index: Int,
    size: Dp,
    selected: Boolean,
    blocked: Boolean,
    onTap: () -> Unit,
    modifier: Modifier = Modifier,
    cell: Dp = PickerDims.Cell,
    clickable: Boolean = true,
    tagPrefix: String = "colorpicker.swatch",
) {
    val label = stringResource(R.string.om_ap_cp_swatch, argbLabel(argb))
    val unavailable = stringResource(R.string.om_ap_cp_unavailable)
    Box(
        modifier
            .size(cell)
            .testTag("$tagPrefix.$index")
            .semantics {
                contentDescription = label
                this.selected = selected
                if (blocked) stateDescription = unavailable
                // Sem o clique próprio (grade da tela cheia): o leitor de tela ainda aplica.
                if (!clickable) onClick { onTap(); true }
            }
            .then(if (clickable) Modifier.tocavel(shrink = 0.9f, onClick = onTap) else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        val shape = RoundedCornerShape(5.dp)
        Box(
            Modifier
                .size(size)
                .clip(shape)
                .drawBehind {
                    drawChecker()
                    drawRect(Color(argb))
                }
                .then(if (selected) Modifier.border(1.5.dp, AureaColors.Accent, shape) else Modifier),
        )
        if (blocked) CupertinoIcon(CupertinoGlyph.XmarkCircle, 14.dp, inkOver(argbToRgba(argb)))
    }
}

/**
 * Modo fino: quadrado S×V (arrasto RELATIVO, Δ/(lado × 1,1)), anel de matiz de
 * 108 dp (giro relativo, ímã de 60° do motor, háptico) e a faixa de alfa (29 dp,
 * ligada pela setinha; a escolha fica lembrada). No meio do anel, durante o
 * arrasto: "H °" ou "S % / V %"; parado, o código.
 */
@Composable
private fun FineArea(state: ColorPickState, withAlpha: Boolean, onEmit: () -> Unit, onEnd: () -> Unit) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences(SavedPalette.PREFS, Context.MODE_PRIVATE) }
    var showAlpha by remember { mutableStateOf(prefs.getBoolean(SavedPalette.KEY_ALPHA, false)) }
    var label by remember { mutableStateOf(FineLabel.None) }
    val emit by rememberUpdatedState(onEmit)
    val end by rememberUpdatedState(onEnd)
    val haptic = LocalHapticFeedback.current
    val svLabel = stringResource(R.string.om_ap_cp_saturation) + " · " + stringResource(R.string.om_ap_cp_brightness)
    val hueLabel = stringResource(R.string.om_ap_cp_hue)
    val alphaLabel = stringResource(R.string.om_ap_cp_alpha)
    Row(Modifier.fillMaxSize()) {
        // Quadrado saturação × brilho (relativo: o dedo não cobre a alça).
        Box(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .testTag("colorpicker.sv")
                .semantics {
                    contentDescription = svLabel
                    stateDescription = "${(state.sat * 100).roundToInt()}% · ${(state.value * 100).roundToInt()}%"
                }
                .pointerInput(state) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        down.consume()
                        state.dragging = true
                        label = FineLabel.SatVal
                        var last = down.position
                        try {
                            while (true) {
                                val ev = awaitPointerEvent()
                                val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                if (!c.pressed) break
                                c.consume()
                                val dx = c.position.x - last.x
                                val dy = c.position.y - last.y
                                last = c.position
                                if (dx == 0f && dy == 0f) continue
                                state.sat = (state.sat + dx / (size.width * 1.1f)).coerceIn(0f, 1f)
                                state.value = (state.value - dy / (size.height * 1.1f)).coerceIn(0f, 1f)
                                emit()
                            }
                        } finally {
                            state.dragging = false
                            label = FineLabel.None
                            end()
                        }
                    }
                }
                .drawBehind {
                    drawRect(Brush.horizontalGradient(listOf(Color.White, hueColor(state.hue))))
                    drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.Black)))
                    val p = Offset(state.sat * size.width, (1f - state.value) * size.height)
                    drawCircle(Color.Black.copy(alpha = 0.35f), 8.dp.toPx(), p, style = Stroke(3.5.dp.toPx()))
                    drawCircle(Color.White, 8.dp.toPx(), p, style = Stroke(2.dp.toPx()))
                },
        )
        // Anel de matiz (108 dp): giro relativo, ímã de 60° com háptico ao entrar.
        Box(
            Modifier
                .width(PickerDims.HueWidth)
                .fillMaxHeight()
                .testTag("colorpicker.hue")
                .semantics {
                    contentDescription = hueLabel
                    stateDescription = "${state.hue.roundToInt()}°"
                }
                .pointerInput(state) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val c = Offset(size.width / 2f, size.height / 2f)
                        val outer = min(size.width, size.height) / 2f - 3.dp.toPx()
                        // O miolo (¼ do raio) não gira: o ângulo ali pula à toa.
                        if ((down.position - c).getDistance() < outer / 4f) return@awaitEachGesture
                        down.consume()
                        fun angleOf(p: Offset) = Math.toDegrees(atan2((p.y - c.y).toDouble(), (p.x - c.x).toDouble())).toFloat()
                        var lastAngle = angleOf(down.position)
                        var accum = AureaEngine.hueTouchStart(state.hue, lastAngle)
                        var snapped = false
                        state.dragging = true
                        label = FineLabel.Hue
                        fun applyHue() {
                            val res = AureaEngine.hueSnap(accum)
                            val nh = res.getOrElse(0) { accum }
                            val sn = res.getOrElse(1) { 0f } > 0.5f
                            if (sn && !snapped) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                            snapped = sn
                            if (abs(nh - state.hue) > 1e-3f) {
                                state.hue = nh
                                emit()
                            }
                        }
                        applyHue()
                        try {
                            while (true) {
                                val ev = awaitPointerEvent()
                                val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                                if (!ch.pressed) break
                                ch.consume()
                                val a = angleOf(ch.position)
                                var d = a - lastAngle
                                if (d > 180f) d -= 360f
                                if (d < -180f) d += 360f
                                lastAngle = a
                                if (d == 0f) continue
                                accum += d
                                applyHue()
                            }
                        } finally {
                            state.dragging = false
                            label = FineLabel.None
                            end()
                        }
                    }
                }
                .drawBehind {
                    val c = center
                    val outer = min(size.width, size.height) / 2f - 3.dp.toPx()
                    val thick = 12.dp.toPx()
                    val mid = outer - thick / 2f
                    drawCircle(Brush.sweepGradient(HueStops, c), mid, c, style = Stroke(thick))
                    val rad = Math.toRadians(state.hue.toDouble())
                    val p = Offset(c.x + cos(rad).toFloat() * mid, c.y + sin(rad).toFloat() * mid)
                    drawCircle(hueColor(state.hue), thick * 0.62f, p)
                    drawCircle(Color.White, thick * 0.62f, p, style = Stroke(2.dp.toPx()))
                },
            contentAlignment = Alignment.Center,
        ) {
            val cur = state.rgba()
            val centerText = when (label) {
                FineLabel.Hue -> "${state.hue.roundToInt()}°"
                FineLabel.SatVal -> "${(state.sat * 100).roundToInt()}% / ${(state.value * 100).roundToInt()}%"
                FineLabel.Alpha -> "${(state.alpha * 100).roundToInt()}%"
                FineLabel.None -> AureaEngine.formatColorCode(cur[0], cur[1], cur[2], cur[3], withAlpha)
            }
            KeepLtr {
                Text(
                    centerText,
                    textAlign = TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 13.sp, color = AureaColors.Text, fontFeatureSettings = "tnum")),
                )
            }
            if (withAlpha) {
                val toggleLabel = stringResource(R.string.om_ap_cp_alpha_toggle)
                Box(
                    Modifier
                        .align(Alignment.TopEnd)
                        .size(30.dp)
                        .testTag("colorpicker.alpha.toggle")
                        .semantics {
                            contentDescription = toggleLabel
                            selected = showAlpha
                        }
                        .tocavel(shrink = 1f) {
                            showAlpha = !showAlpha
                            prefs.edit().putBoolean(SavedPalette.KEY_ALPHA, showAlpha).apply()
                        },
                    contentAlignment = Alignment.TopEnd,
                ) {
                    Box(Modifier.padding(top = 5.dp).size(20.dp), contentAlignment = Alignment.Center) {
                        CupertinoIcon(if (showAlpha) CupertinoGlyph.ChevronRight else CupertinoGlyph.ChevronLeft, 14.dp, AureaColors.Muted)
                    }
                }
            }
        }
        // Faixa de alfa (29 dp): arrasto vertical relativo; em cima, opaco.
        if (withAlpha && showAlpha) {
            Box(
                Modifier
                    .width(PickerDims.AlphaWidth)
                    .fillMaxHeight()
                    .testTag("colorpicker.alpha")
                    .semantics {
                        contentDescription = alphaLabel
                        stateDescription = "${(state.alpha * 100).roundToInt()}%"
                    }
                    .pointerInput(state) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            down.consume()
                            state.dragging = true
                            label = FineLabel.Alpha
                            var last = down.position
                            try {
                                while (true) {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (!c.pressed) break
                                    c.consume()
                                    val dy = c.position.y - last.y
                                    last = c.position
                                    if (dy == 0f) continue
                                    state.alpha = (state.alpha - dy / (size.height * 1.1f)).coerceIn(0f, 1f)
                                    emit()
                                }
                            } finally {
                                state.dragging = false
                                label = FineLabel.None
                                end()
                            }
                        }
                    }
                    .drawBehind {
                        drawChecker()
                        val rgb = state.rgba()
                        val opaque = Color(rgb[0], rgb[1], rgb[2])
                        drawRect(Brush.verticalGradient(listOf(opaque, opaque.copy(alpha = 0f))))
                        val y = (1f - state.alpha) * size.height
                        drawLine(Color.Black.copy(alpha = 0.4f), Offset(0f, y), Offset(size.width, y), 4.dp.toPx())
                        drawLine(Color.White, Offset(0f, y), Offset(size.width, y), 2.dp.toPx())
                    },
            )
        }
    }
}

// -----------------------------------------------------------------------------
// Balão (P0-5): o seletor embutido preso à amostra tocada, com seta 9 × 18.
// -----------------------------------------------------------------------------

/** Onde o balão abre (conta pura, testável na JVM). Tudo em px da janela. */
object ColorPopoverMath {
    data class Placement(val x: Int, val y: Int, val below: Boolean, val arrowX: Int)

    /**
     * Abaixo da âncora se couber [need] (127,5 + 25 dp), senão acima. O balão fica
     * a [arrowInset] do centro da âncora pelo lado da metade da tela em que ela
     * está, preso a [margin] das bordas; a seta aponta para o centro da âncora.
     */
    fun place(
        anchorLeft: Int, anchorTop: Int, anchorRight: Int, anchorBottom: Int,
        windowWidth: Int, windowHeight: Int, popupWidth: Int, popupHeight: Int,
        need: Int, margin: Int, arrowInset: Int, arrowHalf: Int, radius: Int,
    ): Placement {
        val cx = (anchorLeft + anchorRight) / 2
        val below = anchorBottom + need <= windowHeight
        val rawY = if (below) anchorBottom else anchorTop - popupHeight
        val y = rawY.coerceIn(0, max(0, windowHeight - popupHeight))
        val rawX = if (cx < windowWidth / 2) cx - arrowInset else cx - (popupWidth - arrowInset)
        val x = rawX.coerceIn(margin, max(margin, windowWidth - popupWidth - margin))
        val lo = radius + arrowHalf
        val hi = max(lo, popupWidth - radius - arrowHalf)
        return Placement(x, y, below, (cx - x).coerceIn(lo, hi))
    }
}

private class ColorPopoverProvider(
    private val anchor: Rect,
    private val need: Int,
    private val margin: Int,
    private val arrowInset: Int,
    private val arrowHalf: Int,
    private val radius: Int,
    private val onPlaced: (ColorPopoverMath.Placement) -> Unit,
) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        // A âncora é a AMOSTRA (px da janela, `boundsInWindow`), não o layout pai do Popup.
        val p = ColorPopoverMath.place(
            anchor.left.roundToInt(), anchor.top.roundToInt(), anchor.right.roundToInt(), anchor.bottom.roundToInt(),
            windowSize.width, windowSize.height, popupContentSize.width, popupContentSize.height,
            need, margin, arrowInset, arrowHalf, radius,
        )
        onPlaced(p)
        return IntOffset(p.x, p.y)
    }
}

/**
 * O BALÃO DO SELETOR: preso à amostra ([anchor], px da janela), seta 9 × 18, sem
 * animação; abaixo se couber, senão acima; toque fora ou Voltar fecha. Com o
 * conta-gotas ligado ele deixa o toque passar (o dedo vai para o palco). Quem
 * chama segura o passo de desfazer do balão inteiro.
 */
@Composable
fun ColorPopover(
    anchor: Rect,
    color: FloatArray,
    onColor: (FloatArray) -> Unit,
    onDismiss: () -> Unit,
    withAlpha: Boolean = true,
    eyedropperActive: Boolean = false,
    onEyedropper: (() -> Unit)? = null,
    onMessage: ((String) -> Unit)? = null,
) {
    val density = LocalDensity.current
    var placement by remember { mutableStateOf<ColorPopoverMath.Placement?>(null) }
    val provider = remember(anchor, density) {
        with(density) {
            ColorPopoverProvider(
                anchor = anchor,
                need = (PickerDims.Height + 25.dp).roundToPx(),
                margin = 8.dp.roundToPx(),
                arrowInset = 30.dp.roundToPx(),
                arrowHalf = (PickerDims.ArrowWidth / 2).roundToPx(),
                radius = PickerDims.Radius.roundToPx(),
                onPlaced = { placement = it },
            )
        }
    }
    val screen = LocalConfiguration.current.screenWidthDp.dp
    val width = if (screen - 16.dp < PickerDims.PopoverWidth) screen - 16.dp else PickerDims.PopoverWidth
    Popup(
        popupPositionProvider = provider,
        onDismissRequest = onDismiss,
        properties = PopupProperties(
            focusable = !eyedropperActive,
            dismissOnBackPress = true,
            dismissOnClickOutside = !eyedropperActive,
        ),
    ) {
        val p = placement
        Column(Modifier.width(width).alpha(if (p == null) 0f else 1f).testTag("colorpicker.popover")) {
            PopoverArrow(up = true, visible = p?.below == true, arrowX = p?.arrowX)
            EmbeddedColorPicker(
                color = color,
                onColor = onColor,
                modifier = Modifier.shadow(8.dp, RoundedCornerShape(PickerDims.Radius)),
                withAlpha = withAlpha,
                eyedropperActive = eyedropperActive,
                onEyedropper = onEyedropper,
                onMessage = onMessage,
            )
            PopoverArrow(up = false, visible = p?.below == false, arrowX = p?.arrowX)
        }
    }
}

/** A seta do balão (9 × 18, a cor da caixa). Sempre ocupa a faixa: o tamanho do balão não muda. */
@Composable
private fun PopoverArrow(up: Boolean, visible: Boolean, arrowX: Int?) {
    Canvas(Modifier.fillMaxWidth().height(PickerDims.ArrowHeight)) {
        if (!visible || arrowX == null) return@Canvas
        val half = PickerDims.ArrowWidth.toPx() / 2f
        val x = arrowX.toFloat()
        val path = Path().apply {
            if (up) {
                moveTo(x - half, size.height)
                lineTo(x, 0f)
                lineTo(x + half, size.height)
            } else {
                moveTo(x - half, 0f)
                lineTo(x, size.height)
                lineTo(x + half, 0f)
            }
            close()
        }
        drawPath(path, PickerColors.Box)
    }
}
