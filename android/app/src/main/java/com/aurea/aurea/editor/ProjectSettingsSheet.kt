package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.CompositionSettings
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
// ⚙ Projeto — SÓ os ajustes do projeto (ref21)
// =============================================================================

/** Proporções da fileira de caixinhas (largura / altura), na ordem da ref21. */
private val Aspects = listOf("16:9" to 16f / 9f, "9:16" to 9f / 16f, "4:5" to 4f / 5f, "1:1" to 1f, "4:3" to 4f / 3f)

/** Resolução = o lado MENOR (o "1080p" vale para 16:9 e para 9:16). */
private val Resolutions = listOf(480 to "480p (SD)", 720 to "720p (HD)", 1080 to "1080p (FHD)", 1440 to "1440p (QHD)", 2160 to "2160p (4K)")
private val FpsOptions = listOf(24, 25, 30, 50, 60)

/** Cores de fundo de um toque; "Outra cor…" abre o seletor. */
private val Backgrounds = listOf("Preto" to floatArrayOf(0f, 0f, 0f), "Branco" to floatArrayOf(1f, 1f, 1f))

/**
 * A folha da engrenagem: proporção (16:9 · 9:16 · 4:5 · 1:1 · 4:3 · ✎ livre),
 * resolução, quadros por segundo e plano de fundo — nada além do projeto.
 * Lê e escreve no motor (`store.composition`): a proporção mantém o lado
 * menor (e encolhe até caber no teto do aparelho), a resolução mantém a
 * proporção, trocar a taxa preserva os segundos (o motor reescala os tempos).
 */
@Composable
internal fun ProjectSettingsSheet(store: EditorStore, onDismiss: () -> Unit) {
    val comp = store.composition
    val p = store.project
    val w = comp?.width ?: p.width
    val h = comp?.height ?: p.height
    val aspect = if (h > 0) w.toFloat() / h else 0f
    val preset = Aspects.firstOrNull { abs(it.second - aspect) < 0.01f }?.first
    var free by remember { mutableStateOf(preset == null) }
    val shortSide = min(w, h)
    val fpsValue = comp?.fps ?: p.fps.toDouble()
    val bg = comp?.background ?: listOf(0f, 0f, 0f, 1f)
    val bgName = Backgrounds.firstOrNull { (_, c) -> (0..2).all { abs(c[it] - bg[it]) < 0.01f } }?.first ?: stringResource(R.string.editor_personalizada)

    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var pickingBackground by remember { mutableStateOf(false) }
    var menu by remember { mutableStateOf<String?>(null) }

    ShellMenuSheet(onDismiss, maxHeightFraction = 0.7f, scrim = ShellColors.SettingsScrim, handle = ShellColors.SheetHandle) {
        Row(Modifier.fillMaxWidth().padding(start = 6.dp, end = 18.dp), verticalAlignment = Alignment.CenterVertically) {
            ChromeButton(CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), onClick = onDismiss, size = 20.dp, width = 44.dp)
            Text(
                stringResource(R.string.editor_projeto_cbe9),
                style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W700)),
            )
        }

        // Proporção: caixinhas no formato de cada uma; ✎ = tamanho livre.
        Row(
            Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 18.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Aspects.forEach { (label, ratio) ->
                AspectBox(label, ratio, on = !free && preset == label) {
                    free = false
                    if (comp != null && preset != label) applyAspect(store, comp, ratio, shortSide)
                }
            }
            Box(
                Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (free) AureaColors.Accent else AureaColors.Chip)
                    .semantics { contentDescription = "Tamanho livre" }
                    .tocavel(haptic = true) { free = true },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Pencil, 18.dp, if (free) AureaColors.OnAccent else AureaColors.Text)
            }
        }

        if (free && comp != null) {
            SettingLine(stringResource(R.string.editor_tamanho)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    SizeBox("$w", Modifier.weight(1f)) {
                        keypad = KeypadRequest("Largura", w.toFloat(), "px", 16f, 8192f, 0) { v ->
                            setSize(store, comp, v.roundToInt(), h)
                        }
                    }
                    Text("×", modifier = Modifier.padding(horizontal = 10.dp), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
                    SizeBox("$h", Modifier.weight(1f)) {
                        keypad = KeypadRequest("Altura", h.toFloat(), "px", 16f, 8192f, 0) { v ->
                            setSize(store, comp, w, v.roundToInt())
                        }
                    }
                    Text("px", modifier = Modifier.padding(start = 8.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                }
            }
        }

        SettingLine(stringResource(R.string.editor_resolucao)) {
            Dropdown(
                Resolutions.firstOrNull { it.first == shortSide }?.second ?: "${shortSide}p",
                open = menu == "res",
                onOpen = { menu = "res" },
                onDismiss = { menu = null },
                items = Resolutions.map { (short, label) ->
                    PopupItem(label, short == shortSide) {
                        val c = comp ?: return@PopupItem
                        val (nw, nh) = sizeFor(short, if (aspect > 0f) aspect else 16f / 9f)
                        if (c.fits(nw, nh)) store.setCompositionSize(nw, nh)
                        else store.showToast("Este aparelho exporta até ${c.capLong} × ${c.capShort}")
                    }
                },
            )
        }

        SettingLine(stringResource(R.string.editor_quadros_segundo)) {
            val rounded = fpsValue.roundToInt()
            val label = if (abs(fpsValue - rounded) < 0.01) "$rounded fps"
            else String.format(Locale.ROOT, stringResource(R.string.editor_2f_fps), fpsValue).replace('.', ',')
            Dropdown(
                label,
                open = menu == "fps",
                onOpen = { menu = "fps" },
                onDismiss = { menu = null },
                items = FpsOptions.map { f ->
                    PopupItem("$f fps", abs(fpsValue - f) < 0.01) { store.setCompositionFps(f.toDouble()) }
                },
            )
        }

        SettingLine(stringResource(R.string.editor_plano_fundo)) {
            Dropdown(
                bgName,
                swatch = Color(bg[0], bg[1], bg[2]),
                open = menu == "bg",
                onOpen = { menu = "bg" },
                onDismiss = { menu = null },
                items = Backgrounds.map { (name, c) ->
                    PopupItem(name, name == bgName) { store.setCompositionBackground(c[0], c[1], c[2], 1f) }
                } + PopupItem(stringResource(R.string.editor_outra_cor), bgName == stringResource(R.string.editor_personalizada)) { if (comp != null) pickingBackground = true },
            )
        }
        Spacer(Modifier.height(16.dp))
    }

    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    if (pickingBackground && comp != null) {
        // Um passo de desfazer para o seletor inteiro; fecha também se ele
        // sair da tela sem o "Pronto". O fundo já é sRGB no motor.
        val initial = remember { comp.background.toFloatArray() }
        DisposableEffect(Unit) {
            store.beginGesture("fundo da composição")
            onDispose { store.endGesture() }
        }
        ColorPickerSheet(
            initial = initial,
            withAlpha = false,
            onChange = { r, g, b, _ -> store.setCompositionBackground(r, g, b, 1f) },
            onDone = { pickingBackground = false },
        )
    }
}

/** Troca a proporção mantendo o lado menor; não cabendo no teto, encolhe na proporção pedida. */
private fun applyAspect(store: EditorStore, c: CompositionSettings, ratio: Float, shortSide: Int) {
    var (nw, nh) = sizeFor(shortSide, ratio)
    if (!c.fits(nw, nh)) {
        val k = min(c.capLong.toFloat() / max(nw, nh), c.capShort.toFloat() / min(nw, nh))
        nw = even(nw * k)
        nh = even(nh * k)
    }
    store.setCompositionSize(nw, nh)
}

/** Tamanho livre: par e dentro do teto do aparelho (senão diz o teto). */
private fun setSize(store: EditorStore, c: CompositionSettings, width: Int, height: Int) {
    val nw = even(width.toFloat())
    val nh = even(height.toFloat())
    if (c.fits(nw, nh)) store.setCompositionSize(nw, nh)
    else store.showToast("Este aparelho exporta até ${c.capLong} × ${c.capShort}")
}

/** Tamanho par com lado menor `short` na proporção `ratio` (largura / altura). */
private fun sizeFor(short: Int, ratio: Float): Pair<Int, Int> =
    if (ratio >= 1f) even(short * ratio) to even(short.toFloat()) else even(short.toFloat()) to even(short / ratio)

private fun even(v: Float): Int = max(2, (v / 2f).roundToInt() * 2)

/** Caixinha no formato da proporção (lado maior 44), rótulo dentro. */
@Composable
private fun AspectBox(label: String, ratio: Float, on: Boolean, onClick: () -> Unit) {
    val long = 44f
    val bw = if (ratio >= 1f) long else long * ratio.coerceAtLeast(0.5f) + 6f
    val bh = if (ratio >= 1f) long / ratio + 6f else long
    Box(
        Modifier
            .size(bw.dp, bh.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(if (on) AureaColors.Accent else AureaColors.Chip)
            .semantics { contentDescription = "Proporção $label" }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            style = AureaType.Base.merge(
                TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W700, color = if (on) AureaColors.OnAccent else AureaColors.Text),
            ),
        )
    }
}

/** Rótulo à esquerda (coluna de 132) e o controle à direita. */
@Composable
private fun SettingLine(label: String, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            maxLines = 2,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
            modifier = Modifier.width(132.dp).padding(end = 8.dp),
        )
        Box(Modifier.weight(1f)) { content() }
    }
}

/** A pílula com o valor e ⌄; tocar abre as opções logo abaixo. */
@Composable
private fun Dropdown(
    value: String,
    open: Boolean,
    onOpen: () -> Unit,
    onDismiss: () -> Unit,
    items: List<PopupItem>,
    swatch: Color? = null,
) {
    Box {
        Row(
            Modifier
                .fillMaxWidth()
                .height(46.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Chip)
                .tocavel(shrink = 1f, haptic = true, onClick = onOpen)
                .padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (swatch != null) {
                Box(
                    Modifier
                        .size(24.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .background(swatch)
                        .border(1.dp, AureaColors.Border, RoundedCornerShape(5.dp)),
                )
                Spacer(Modifier.width(10.dp))
            }
            Text(
                value,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)),
            )
            CupertinoIcon(CupertinoGlyph.ChevronDown, 15.dp, AureaColors.Muted)
        }
        if (open) ShellPopupMenu(items, onDismiss = onDismiss, width = 220.dp)
    }
}

@Composable
private fun SizeBox(value: String, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier
            .height(46.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .tocavel(shrink = 1f, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(value, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)).merge(AureaType.Tabular))
    }
}
