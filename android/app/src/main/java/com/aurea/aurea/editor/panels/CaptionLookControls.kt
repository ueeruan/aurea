package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionLook
import com.aurea.aurea.captions.CaptionLookTarget
import com.aurea.aurea.captions.CaptionPreview
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/** Nome de cada modelo do motor (o id é estável; o texto, do idioma do app). */
internal val CaptionTemplateNames = mapOf(
    "highlight" to R.string.pn_caption_style_highlight, "classic" to R.string.pn_caption_style_classic,
    "box" to R.string.panel_caixa, "neon" to R.string.pn_caption_style_neon, "karaoke" to R.string.pn_karaoke,
    "pop" to R.string.pn_pop, "soft" to R.string.pack_text_4, "glow" to R.string.pack_text_5,
    "gold" to R.string.cap046_tpl_gold, "bubble" to R.string.cap046_tpl_bubble, "mint" to R.string.cap046_tpl_mint,
    "comic" to R.string.cap046_tpl_comic, "tag" to R.string.cap046_tpl_tag, "minimal" to R.string.cap046_tpl_minimal,
)

/** Paleta rápida (sRGB) e o nome de cada cor para o leitor de tela. */
private val Swatches = listOf(
    0xFFFFFF to R.string.cap046_color_white, 0x000000 to R.string.cap046_color_black, 0xFFE34D to R.string.cap046_color_yellow,
    0xFFC23D to R.string.cap046_color_gold, 0xFF8A00 to R.string.cap046_color_orange, 0xE5252A to R.string.cap046_color_red,
    0xFF3D7F to R.string.cap046_color_pink, 0xA259FF to R.string.cap046_color_purple, 0x2F7BFF to R.string.cap046_color_blue,
    0x3DE0FF to R.string.cap046_color_cyan, 0x4CD964 to R.string.cap046_color_green, 0x7CFFB2 to R.string.cap046_color_mint,
)

private fun rgba(hex: Int, a: Float = 1f) = floatArrayOf(((hex shr 16) and 255) / 255f, ((hex shr 8) and 255) / 255f, (hex and 255) / 255f, a)
private fun FloatArray.color() = Color(this[0], this[1], this[2], getOrElse(3) { 1f })
private fun same(a: FloatArray, hex: Int) = (0..2).all { kotlin.math.abs(a[it] - rgba(hex)[it]) < 0.01f }

/**
 * A amostra "Aa" desenhada com a aparência que o motor resolveu (peso,
 * preenchimento, contorno, fundo, sombra/brilho, cor do destaque no "a").
 */
@Composable
internal fun CaptionSample(p: CaptionPreview, text: String, fontSize: TextUnit, modifier: Modifier = Modifier) {
    val shown = if (p.uppercase) text.uppercase() else text
    val fill = p.fill.color()
    val accent = if (p.highlight) p.highlightColor.color() else fill
    // Destaque na última palavra (ou na última letra, se for uma palavra só).
    val split = shown.lastIndexOf(' ').let { if (it > 0) it + 1 else (shown.length - 1).coerceAtLeast(0) }
    val annotated = buildAnnotatedString {
        append(shown.substring(0, split))
        withStyle(SpanStyle(color = accent)) { append(shown.substring(split)) }
    }
    val base = TextStyle(
        fontSize = fontSize, fontWeight = FontWeight(p.weight.coerceIn(100, 1000)),
        fontStyle = if (p.italic) FontStyle.Italic else FontStyle.Normal, textAlign = TextAlign.Center,
    )
    val px = fontSize.value
    val shadow = when {
        p.glow -> Shadow(p.shadowColor.color(), Offset.Zero, px * 0.5f)
        p.shadow -> Shadow(p.shadowColor.color(), Offset(0f, px * 0.08f), px * 0.15f)
        else -> null
    }
    Box(
        modifier.clip(RoundedCornerShape(6.dp))
            .then(if (p.box) Modifier.background(p.boxColor.color(), RoundedCornerShape(6.dp)) else Modifier)
            .padding(horizontal = 6.dp, vertical = 2.dp),
        contentAlignment = Alignment.Center,
    ) {
        if (p.strokeFrac > 0.001f) {
            Text(shown, style = base.copy(color = p.stroke.color(), drawStyle = Stroke(width = px * p.strokeFrac * 2f)), maxLines = 1)
        }
        Text(annotated, style = base.copy(color = fill, shadow = shadow), maxLines = 1)
    }
}

/** Modelos prontos (antes ou depois de gerar, da faixa ou dos blocos marcados): o cartão desenha o próprio estilo. */
@Composable
internal fun CaptionTemplateRow(cap: CaptionLookTarget) {
    val enabled = cap.lookEnabled
    Text(stringResource(R.string.cap046_templates), Modifier.padding(top = 8.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        cap.templates.forEach { t ->
            val name = stringResource(CaptionTemplateNames[t.id] ?: R.string.panel_estilo)
            val on = cap.templateId == t.id
            Column(
                Modifier.width(92.dp).clip(RoundedCornerShape(10.dp))
                    .background(AureaColors.Chip)
                    .border(2.dp, if (on) AureaColors.Accent else Color.Transparent, RoundedCornerShape(10.dp))
                    .tocavel(enabled = enabled, role = Role.RadioButton, onClick = { if (enabled) cap.applyTemplate(t.id) })
                    .semantics { selected = on; contentDescription = name }
                    .padding(vertical = 10.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(Modifier.height(40.dp).fillMaxWidth().background(Color(0xFF3A3F48)), contentAlignment = Alignment.Center) {
                    CaptionSample(t.preview, "Aa", 22.sp)
                }
                Spacer(Modifier.height(6.dp))
                Text(name, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp,
                    color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}

/**
 * "Personalizar estilo": fonte, cor do texto, contorno, fundo, sombra e cor
 * do destaque. Cada toque é um passo de desfazer na faixa pronta; antes de
 * gerar, vale para as legendas que vão nascer.
 */
@Composable
internal fun CaptionCustomize(cap: CaptionLookTarget, store: EditorStore) {
    var open by remember { mutableStateOf(false) }
    var fonts by remember { mutableStateOf(false) }
    val s = cap.settings
    val fromStyle = stringResource(R.string.cap046_from_style)
    Row(
        Modifier.fillMaxWidth().height(48.dp).tocavel(onClick = { open = !open }).semantics { selected = open },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(stringResource(R.string.cap046_customize), Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)))
        Text(if (open) "▴" else "▾", style = AureaType.BodySmall.copy(color = AureaColors.Muted))
    }
    if (!open) return
    if (fonts) {
        com.aurea.aurea.editor.FontBrowserDialog(store, onPick = { f ->
            cap.update { it.with(CaptionLook.FONT, true).copy(fontFamily = f.family, fontWeight = f.weight, fontItalic = f.italic,
                fontPath = if (f.imported) f.path else "") }
            com.aurea.aurea.editor.FontPrefs.recordUse(f)
            fonts = false
        }, onDismiss = { fonts = false })
    }
    cap.preview?.let { p ->
        Box(Modifier.fillMaxWidth().padding(vertical = 6.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFF3A3F48)).padding(vertical = 14.dp),
            contentAlignment = Alignment.Center) {
            CaptionSample(p, stringResource(R.string.cap046_sample), 20.sp)
        }
    }
    // Fonte
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.panel_fonte), Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        if (s.has(CaptionLook.FONT)) {
            LookChip(fromStyle, false) { cap.update { it.with(CaptionLook.FONT, false) } }
            Spacer(Modifier.width(6.dp))
        }
        Text((if (s.has(CaptionLook.FONT)) s.fontFamily.ifEmpty { null } else null) ?: fromStyle,
            Modifier.tocavel(onClick = { fonts = true }).padding(vertical = 8.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)))
        Text("  ›", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
    }
    // Cor do texto
    SectionLabel(stringResource(R.string.cap046_text_color))
    SwatchRow(if (s.has(CaptionLook.FILL)) s.fill else null, fromStyle) { hex ->
        cap.update { if (hex == null) it.with(CaptionLook.FILL, false) else it.with(CaptionLook.FILL, true).copy(fill = rgba(hex)) }
    }
    // Contorno: do estilo, sem, fino, médio, grosso (+ cor)
    SectionLabel(stringResource(R.string.panel_contorno))
    val widths = listOf(0f, 0.05f, 0.09f, 0.14f)
    val strokeIndex = if (!s.has(CaptionLook.STROKE)) -1 else widths.indexOfFirst { kotlin.math.abs(it - s.strokeFrac) < 0.005f }
    ChipRow(listOf(fromStyle, stringResource(R.string.cap046_none), stringResource(R.string.cap046_thin),
        stringResource(R.string.cap046_medium), stringResource(R.string.cap046_thick)), strokeIndex + 1) { i ->
        cap.update { if (i == 0) it.with(CaptionLook.STROKE, false) else it.with(CaptionLook.STROKE, true).copy(strokeFrac = widths[i - 1]) }
    }
    if (s.has(CaptionLook.STROKE) && s.strokeFrac > 0f) {
        SwatchRow(s.stroke, null) { hex -> hex?.let { h -> cap.update { it.copy(stroke = rgba(h)) } } }
    }
    // Fundo: do estilo, sem, com (+ cor)
    SectionLabel(stringResource(R.string.panel_fundo))
    ChipRow(listOf(fromStyle, stringResource(R.string.cap046_none), stringResource(R.string.cap046_on)),
        if (!s.has(CaptionLook.BOX)) 0 else if (s.box) 2 else 1) { i ->
        cap.update { if (i == 0) it.with(CaptionLook.BOX, false) else it.with(CaptionLook.BOX, true).copy(box = i == 2) }
    }
    if (s.has(CaptionLook.BOX) && s.box) {
        SwatchRow(s.boxColor, null) { hex -> hex?.let { h -> cap.update { it.copy(boxColor = rgba(h, if (h == 0x000000) 0.7f else 0.9f)) } } }
    }
    // Sombra: do estilo, sem, com
    SectionLabel(stringResource(R.string.panel_sombra))
    ChipRow(listOf(fromStyle, stringResource(R.string.cap046_none), stringResource(R.string.cap046_on)),
        if (!s.has(CaptionLook.SHADOW)) 0 else if (s.shadow) 2 else 1) { i ->
        cap.update { if (i == 0) it.with(CaptionLook.SHADOW, false) else it.with(CaptionLook.SHADOW, true).copy(shadow = i == 2) }
    }
    // Cor do destaque (palavra falada)
    if (s.highlight) {
        SectionLabel(stringResource(R.string.cap046_highlight_color))
        SwatchRow(floatArrayOf(s.highlightColor[0], s.highlightColor[1], s.highlightColor[2], 1f), null) { hex ->
            hex?.let { h -> cap.update { it.copy(highlightColor = rgba(h).copyOf(3)) } }
        }
    }
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun SectionLabel(text: String) {
    Spacer(Modifier.height(6.dp))
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
}

@Composable
private fun ChipRow(options: List<String>, selected: Int, onPick: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) { options.forEachIndexed { i, o -> LookChip(o, i == selected) { onPick(i) } } }
}

@Composable
private fun LookChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(role = Role.RadioButton, onClick = onClick).semantics { selected = on }
            .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/** Paleta: `current` nulo = "do estilo" (o primeiro chip, quando [styleLabel] existe). */
@Composable
private fun SwatchRow(current: FloatArray?, styleLabel: String?, onPick: (Int?) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(48.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (styleLabel != null) LookChip(styleLabel, current == null) { onPick(null) }
        Swatches.forEach { (hex, name) ->
            val on = current != null && same(current, hex)
            val label = stringResource(name)
            Box(
                Modifier.size(32.dp).clip(CircleShape).background(Color(rgba(hex)[0], rgba(hex)[1], rgba(hex)[2]))
                    .border(if (on) 3.dp else 1.dp, if (on) AureaColors.Accent else Color(0x55FFFFFF), CircleShape)
                    .tocavel(role = Role.RadioButton, onClick = { onPick(hex) })
                    .semantics { selected = on; contentDescription = label },
            )
        }
    }
}

