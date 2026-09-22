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
import androidx.compose.ui.text.TextStyle
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
    var draft by remember(store.primary) { mutableStateOf(td.content) }
    LaunchedEffect(td.content) { if (td.content != draft && !store.textEditing) draft = td.content }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Box(
            Modifier.fillMaxWidth().heightIn(min = 56.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        ) {
            BasicTextField(
                value = draft,
                onValueChange = {
                    draft = it
                    store.setTextContent(it)
                },
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth(),
            )
            if (draft.isEmpty()) Text("Digite o texto", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
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
                listOf(0 to "Esq.", 1 to "Centro", 2 to "Dir.").forEach { (a, label) ->
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
