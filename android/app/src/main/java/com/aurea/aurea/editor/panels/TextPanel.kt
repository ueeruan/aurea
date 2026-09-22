package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.TextFieldValue
import com.aurea.aurea.ui.ds.AureaToggle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * TEXTO: o conteúdo (campo de várias linhas, aplicado enquanto digita), tamanho,
 * cor, alinhamento, contorno, entrelinha e espaçamento. O motor rasteriza com a
 * fonte do sistema na escala da tela — o texto fica nítido ampliado.
 */
@Composable
internal fun TextPanel(env: PanelEnv) {
    val store = env.store
    val t by remember(store) { derivedStateOf { store.textDetail } }
    val td = t ?: run {
        Text("Selecione uma camada de texto.", modifier = Modifier.padding(18.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        return
    }
    var field by remember(store.primary) { mutableStateOf(TextFieldValue(td.content)) }
    val draft = field.text
    LaunchedEffect(td.content) {
        if (td.content != field.text && !store.textEditing) {
            field = TextFieldValue(td.content, TextRange(field.selection.start.coerceAtMost(td.content.length), field.selection.end.coerceAtMost(td.content.length)))
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Box(
            Modifier.fillMaxWidth().heightIn(min = 56.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        ) {
            BasicTextField(
                value = field,
                onValueChange = {
                    val changed = it.text != field.text
                    field = it
                    if (changed) store.setTextContent(it.text)
                },
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth(),
            )
            if (draft.isEmpty()) Text("Digite o texto", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
        }
        // Trecho selecionado no campo: estilo próprio (rich text).
        val sel = field.selection
        if (!sel.collapsed) {
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Trecho escolhido", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                SpanChip("Cor") {
                    val a = sel.min
                    val b = sel.max
                    env.openColor(ColorRequest(td.color.copyOf(), onChange = { r, g, bl, _ -> store.setTextSpan(a, b, floatArrayOf(r, g, bl), 0, 1f) },
                        onDone = {}))
                }
                SpanChip("Negrito") { store.setTextSpan(sel.min, sel.max, null, 700, 1f) }
                SpanChip("Maior") { store.setTextSpan(sel.min, sel.max, null, 0, 1.35f) }
                SpanChip("Normal") { store.clearTextSpans(sel.min, sel.max) }
            }
        }
        Spacer(Modifier.height(10.dp))
        val font by remember(store) { derivedStateOf { store.textFont } }
        Row(
            Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).tocavel(onClick = { env.onOpenPanel(EditorPanel.Font) }),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Fonte", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Text(
                font?.family?.ifEmpty { null } ?: "Padrão do aparelho",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)),
            )
            Text("  ›", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
        }
        TextRuler(store, "Tamanho", { store.textDetail?.size ?: 72f }, "${td.size.roundToInt()} px", 0.5f, 4f, 1000f, "tamanho do texto") {
            store.setTextSize(it)
        }
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Cor", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(Color(td.color[0], td.color[1], td.color[2])) {
                store.beginGesture("cor do texto")
                env.openColor(ColorRequest(td.color.copyOf(), onChange = { r, g, b, a -> store.setTextColor(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
        }
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Alinhamento", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(0 to "Esquerda", 1 to "Centro", 2 to "Direita").forEach { (a, label) ->
                    val on = td.alignment == a
                    Box(
                        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                            .tocavel(onClick = { store.setTextAlignment(a) }).padding(horizontal = 12.dp, vertical = 6.dp),
                    ) {
                        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                    }
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Text("Contorno", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Cor do contorno", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(Color(td.strokeColor[0], td.strokeColor[1], td.strokeColor[2])) {
                store.beginGesture("cor do contorno do texto")
                env.openColor(ColorRequest(td.strokeColor.copyOf(), onChange = { r, g, b, a -> store.setTextStrokeColor(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
        }
        TextRuler(store, "Largura do contorno", { store.textDetail?.strokeWidth ?: 0f }, "${td.strokeWidth.roundToInt()} px",
            0.1f, 0f, 60f, "contorno do texto") { store.setTextStrokeWidth(it) }
        TextStyleSections(env)
        TextPathSection(env)
        TextAnimSection(env)
    }
}

@Composable
private fun SpanChip(label: String, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
    }
}

/** Caixa de parágrafo, fundo e sombra (ver `set_text_style`). */
@Composable
private fun TextStyleSections(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.textStyle } }
    val v = st ?: return
    Spacer(Modifier.height(6.dp))
    Text("Caixa", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to "Livre", 1 to "Parágrafo", 2 to "Tamanho fixo", 3 to "Encolher para caber").forEach { (m, label) ->
            val on = v[0].toInt() == m
            Box(
                Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { store.setTextStyleValue(0, m.toFloat()) }).padding(horizontal = 10.dp, vertical = 6.dp),
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
    if (v[0] >= 1f) {
        TextRuler(store, "Largura da caixa", { store.textStyle?.get(1) ?: 800f }, "${v[1].roundToInt()} px", 2f, 10f, 20000f, "largura da caixa") {
            store.setTextStyleValue(1, it)
        }
        if (v[0] >= 2f) {
            TextRuler(store, "Altura da caixa", { store.textStyle?.get(2) ?: 200f }, "${v[2].roundToInt()} px", 2f, 10f, 20000f, "altura da caixa") {
                store.setTextStyleValue(2, it)
            }
        }
    }
    Spacer(Modifier.height(6.dp))
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Fundo", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
        if (v[3] > 0.5f) {
            ColorWell(Color(v[4], v[5], v[6])) {
                store.beginGesture("cor do fundo do texto")
                env.openColor(ColorRequest(floatArrayOf(v[4], v[5], v[6], v[7]),
                    onChange = { r, g, b, a -> store.setTextStyleValues(mapOf(4 to r, 5 to g, 6 to b, 7 to a)) }, onDone = { store.endGesture() }))
            }
            Spacer(Modifier.width(10.dp))
        }
        AureaToggle(checked = v[3] > 0.5f, onCheckedChange = { store.setTextStyleValue(3, if (it) 1f else 0f) })
    }
    if (v[3] > 0.5f) {
        TextRuler(store, "Margem do fundo", { store.textStyle?.get(8) ?: 14f }, "${v[8].roundToInt()} px", 0.2f, 0f, 500f, "margem do fundo") {
            store.setTextStyleValue(8, it)
        }
        TextRuler(store, "Cantos arredondados", { store.textStyle?.get(9) ?: 10f }, "${v[9].roundToInt()} px", 0.2f, 0f, 500f, "raio do fundo") {
            store.setTextStyleValue(9, it)
        }
    }
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Sombra", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
        if (v[10] > 0.5f) {
            ColorWell(Color(v[11], v[12], v[13])) {
                store.beginGesture("cor da sombra do texto")
                env.openColor(ColorRequest(floatArrayOf(v[11], v[12], v[13], v[14]),
                    onChange = { r, g, b, a -> store.setTextStyleValues(mapOf(11 to r, 12 to g, 13 to b, 14 to a)) }, onDone = { store.endGesture() }))
            }
            Spacer(Modifier.width(10.dp))
        }
        AureaToggle(checked = v[10] > 0.5f, onCheckedChange = { store.setTextStyleValue(10, if (it) 1f else 0f) })
    }
    if (v[10] > 0.5f) {
        TextRuler(store, "Distância X", { store.textStyle?.get(15) ?: 4f }, "${v[15].roundToInt()} px", 0.2f, -500f, 500f, "sombra x") { store.setTextStyleValue(15, it) }
        TextRuler(store, "Distância Y", { store.textStyle?.get(16) ?: 6f }, "${v[16].roundToInt()} px", 0.2f, -500f, 500f, "sombra y") { store.setTextStyleValue(16, it) }
        TextRuler(store, "Desfoque", { store.textStyle?.get(17) ?: 6f }, "${v[17].roundToInt()} px", 0.1f, 0f, 200f, "desfoque da sombra") { store.setTextStyleValue(17, it) }
    }
}

@Composable
private fun TextRuler(
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
