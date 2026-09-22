package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.Text3DInfo
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
 * MATERIAL E AMBIENTE do objeto 3D. Material: só o que o motor muda de verdade
 * — a cor do texto 3D (a malha é gerada de novo); o modelo importado usa o
 * material do próprio arquivo (a cena do motor ainda não aceita troca de
 * material: `SceneSetMaterialParam` responde "não implementado"), então aqui
 * não há régua de Metálico/Rugosidade que não faria nada. Ambiente: a luz que
 * envolve a cena (HDRI ou estúdio neutro), intensidade e giro.
 */
@Composable
internal fun Element3DPanel(env: PanelEnv) {
    val store = env.store
    val e by remember(store) { derivedStateOf { store.environment } }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importHdri(uri)
    }
    val t3 by remember(store) { derivedStateOf { store.text3d } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        t3?.let { Text3DSection(env, it) }
        SectionTitle("Material")
        val info = t3
        if (info != null) {
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Cor", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                ColorWell(Color(info.color[0], info.color[1], info.color[2])) {
                    store.beginGesture("cor do texto 3D")
                    env.openColor(ColorRequest(info.color.copyOf(), onChange = { r, g, b, _ ->
                        store.text3d?.let { store.setText3D(it.copy(color = floatArrayOf(r, g, b, 1f)), lazy = true) }
                    }, onDone = { store.endGesture() }))
                }
            }
        } else {
            Text(
                "Cor, brilho metálico e rugosidade vêm do arquivo do modelo (glTF, FBX).",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
        }
        Spacer(Modifier.height(16.dp))
        SectionTitle("Luz do ambiente")
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip("Estúdio neutro", on = e[0] < 0.5f) { store.clearHdri() }
            Chip(if (e[0] >= 0.5f) "Imagem de ambiente ✓" else "Usar imagem de ambiente (.hdr)", on = e[0] >= 0.5f) {
                pick.launch(arrayOf("image/vnd.radiance", "application/octet-stream", "*/*"))
            }
        }
        Spacer(Modifier.height(10.dp))
        EnvRuler(store, "Intensidade", 0.01f, 0f, 20f, e[1], "${(e[1] * 100).roundToInt()}%") { store.setEnvironment(it, store.environment[2]) }
        EnvRuler(store, "Girar a luz", 1f, -360f, 360f, e[2], "${e[2].roundToInt()}°") { store.setEnvironment(store.environment[1], it) }
        Spacer(Modifier.height(8.dp))
        Text(
            "A luz do ambiente ilumina e reflete em todos os objetos 3D do projeto.",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
        )
    }
}

@Composable
private fun SectionTitle(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
}

/** Texto 3D: texto, profundidade e alinhamento (a cor mora em Material) (a malha é gerada de novo a cada mudança). */
@Composable
private fun Text3DSection(env: PanelEnv, info: Text3DInfo) {
    val store = env.store
    var draft by remember(store.primary) { mutableStateOf(info.content) }
    LaunchedEffect(info.content) { if (info.content != draft && !store.textEditing) draft = info.content }
    SectionTitle("Texto 3D")
    Box(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        BasicTextField(
            value = draft,
            onValueChange = {
                draft = it
                store.text3d?.let { cur -> store.setText3D(cur.copy(content = it), typing = true) }
            },
            textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
            cursorBrush = SolidColor(AureaColors.Accent),
            modifier = Modifier.fillMaxWidth(),
        )
        if (draft.isEmpty()) Text("Digite o texto", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
    }
    Spacer(Modifier.height(6.dp))
    PropertyCustomRow("Profundidade", selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.text3d?.depth ?: 0f },
                    unitsPerDp = 0.005f,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.text3d?.depth ?: 0f },
                        unitsPerDp = { 0.005f },
                        min = 0f,
                        max = 3f,
                        onStart = { store.beginGesture("profundidade do texto 3D") },
                        onValue = { v -> store.text3d?.let { store.setText3D(it.copy(depth = v), lazy = true) } },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox("${(info.depth * 100).roundToInt()}%", onTap = null)
        }
    }
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("Alinhamento", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to "Esquerda", 1 to "Centro", 2 to "Direita").forEach { (a, label) ->
                Chip(label, on = info.alignment == a) { store.text3d?.let { store.setText3D(it.copy(alignment = a)) } }
            }
        }
    }
    Spacer(Modifier.height(14.dp))
}

@Composable
private fun Chip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 12.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

@Composable
private fun EnvRuler(
    store: com.aurea.aurea.state.EditorStore,
    label: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    value: Float,
    text: String,
    onValue: (Float) -> Unit,
) {
    val index = if (label == "Intensidade") 1 else 2  // 2 = "Girar a luz"
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.environment[index] },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.environment[index] },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("ambiente") },
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
