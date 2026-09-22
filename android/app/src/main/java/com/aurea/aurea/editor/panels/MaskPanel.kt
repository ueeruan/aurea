package com.aurea.aurea.editor.panels

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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * MÁSCARA E RECORTE: máscaras da camada (roto) e track matte.
 *
 * Com este painel aberto o palco entra no modo de máscara: tocar põe pontos
 * (arrastar ao pôr curva o trecho), arrastar um ponto o move, as alças do
 * ponto escolhido mudam a curva. Modo, invertida, feather, expansão e
 * opacidade aqui; o caminho ganha keyframe no cabeçote e pode ser rastreado
 * no vídeo. Tudo é rasterizado pelo motor (GPU) antes dos efeitos.
 */
@Composable
internal fun MaskPanel(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.masks } }
    val editId by remember(store) { derivedStateOf { store.maskEdit } }
    val drawing by remember(store) { derivedStateOf { store.maskDrawing } }
    val state = st
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Title("Máscaras")
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            state?.masks?.forEachIndexed { i, m ->
                Chip("Máscara ${i + 1}" + if (!m.closed) " (aberta)" else "", editId == m.id) {
                    store.maskEdit = m.id
                    store.maskDrawing = !m.closed
                    store.maskPoint = -1
                }
            }
            Chip("+ Desenhar", false) { store.startMaskDrawing() }
            Chip("+ Retângulo", false) { store.addMaskPreset(0) }
            Chip("+ Elipse", false) { store.addMaskPreset(1) }
        }
        val m = state?.find(editId)
        if (m == null) {
            Spacer(Modifier.height(8.dp))
            Hint(
                if (state?.masks.isNullOrEmpty()) "Sem máscaras. Desenhe no palco ou comece por um retângulo/elipse."
                else "Escolha uma máscara para editar no palco.",
            )
        } else {
            MaskEditor(env, m, drawing)
        }
        Spacer(Modifier.height(18.dp))
        TrackMatteSection(store)
    }
}

@Composable
private fun MaskEditor(env: PanelEnv, m: EditorStore.MaskPath, drawing: Boolean) {
    val store = env.store
    Spacer(Modifier.height(10.dp))
    if (drawing) {
        Hint("Toque no palco para pôr pontos; arraste ao pôr para curvar. Toque no 1º ponto (ou em Fechar caminho) para fechar — a máscara só recorta fechada.")
        Spacer(Modifier.height(6.dp))
        Action("Fechar caminho", "${m.count} ponto(s)") { store.closeMaskPath() }
        Spacer(Modifier.height(10.dp))
    } else {
        Hint("Arraste os pontos no palco; toque num ponto para ver as alças de curva.")
        Spacer(Modifier.height(8.dp))
    }
    Title("Modo")
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to "Somar", 1 to "Subtrair", 2 to "Intersectar", 3 to "Diferença", 4 to "Nenhum").forEach { (op, label) ->
            Chip(label, m.op == op) { store.setMaskProps(m.id, op, m.inverted, m.feather, m.expansion, m.opacity) }
        }
        Chip("Invertida", m.inverted) { store.setMaskProps(m.id, m.op, !m.inverted, m.feather, m.expansion, m.opacity) }
    }
    Spacer(Modifier.height(6.dp))
    MaskRuler(store, "Feather (suavizar borda)", m.id, 0, m.feather, "${m.feather.roundToInt()} px", 0.5f, 0f, 500f)
    MaskRuler(store, "Expansão", m.id, 1, m.expansion, "${m.expansion.roundToInt()} px", 0.5f, -500f, 500f)
    MaskRuler(store, "Opacidade", m.id, 2, m.opacity * 100f, "${(m.opacity * 100f).roundToInt()} %", 0.5f, 0f, 100f)
    Spacer(Modifier.height(10.dp))
    Title("Animação do caminho")
    Row(verticalAlignment = Alignment.CenterVertically) {
        Chip(if (m.keyHere) "◆ Keyframe no cabeçote" else "◇ Pôr keyframe no cabeçote", m.keyHere) { store.toggleMaskKey(m.id) }
        Spacer(Modifier.width(10.dp))
        Text(
            if (m.keyCount == 0) "Caminho parado" else "${m.keyCount} keyframe(s) — editar no cabeçote grava ali",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        )
    }
    Spacer(Modifier.height(10.dp))
    val isVideo = store.layers.firstOrNull { it.id == store.primary }?.kind == LayerType.Video.kind
    if (isVideo) {
        Title("Rastrear máscara")
        if (store.maskTracking) {
            Hint("Rastreando…")
        } else {
            Action("Rastrear máscara (posição)", "Segue o detalhe sob a máscara do cabeçote até o fim do clipe.") { store.trackMask(m.id, 0) }
            Spacer(Modifier.height(6.dp))
            Action("Rastrear máscara (posição, escala e giro)", "Também acompanha o objeto se aproximando e girando.") { store.trackMask(m.id, 1) }
        }
        Spacer(Modifier.height(10.dp))
    }
    Action("Apagar máscara", "Tira esta máscara da camada.") { store.deleteMask(m.id) }
}

/**
 * TRACK MATTE: a camada só aparece através do alfa ou da luminância de outra
 * (a matte some da tela enquanto é usada — como no AE).
 */
@Composable
private fun TrackMatteSection(store: EditorStore) {
    val tm by remember(store) { derivedStateOf { store.trackMatte } }
    val self = store.primary ?: return
    val matte = tm?.getOrNull(0) ?: 0L
    val mode = (tm?.getOrNull(1) ?: 0L).toInt()
    val rows = store.layers
    val me = rows.firstOrNull { it.id == self }
    val candidates = rows.filter { it.id != self && it.kind != LayerType.Audio.kind && it.kind != LayerType.Camera.kind && it.kind != LayerType.Light.kind }
    Title("Recortar por outra camada (track matte)")
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to "Nenhum", 1 to "Alfa", 2 to "Alfa invertido", 3 to "Luma", 4 to "Luma invertida").forEach { (value, label) ->
            Chip(label, mode == value) {
                if (value == 0) {
                    store.setTrackMatte(0L, 0)
                } else {
                    // Sem matte escolhida: a camada logo acima na pilha (AE).
                    val target = if (matte != 0L) matte
                    else candidates.filter { me != null && it.zIndex > me.zIndex }.minByOrNull { it.zIndex }?.id ?: 0L
                    if (target == 0L) store.showToast("Escolha a camada da matte abaixo")
                    else store.setTrackMatte(target, value)
                }
            }
        }
    }
    if (candidates.isNotEmpty()) {
        Spacer(Modifier.height(6.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            candidates.forEach { r ->
                Chip(r.name.ifEmpty { LayerType.of(r.kind).label }, matte == r.id) { store.setTrackMatte(r.id, if (mode == 0) 1 else mode) }
            }
        }
    }
    Spacer(Modifier.height(4.dp))
    Hint(if (matte != 0L) "A camada escolhida vira a matte e some da tela." else "Alfa: recorta pela forma da outra camada. Luma: pelo brilho dela.")
}

@Composable
private fun MaskRuler(store: EditorStore, label: String, mask: Int, prop: Int, value: Float, text: String, unitsPerDp: Float, min: Float, max: Float) {
    fun current(): Float {
        val m = store.masks?.find(mask) ?: return value
        return when (prop) { 0 -> m.feather; 1 -> m.expansion; else -> m.opacity * 100f }
    }
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { current() },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { current() },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("máscara") },
                        onValue = { v ->
                            val m = store.masks?.find(mask)
                            if (m != null) {
                                when (prop) {
                                    0 -> store.setMaskProps(mask, m.op, m.inverted, v, m.expansion, m.opacity)
                                    1 -> store.setMaskProps(mask, m.op, m.inverted, m.feather, v, m.opacity)
                                    else -> store.setMaskProps(mask, m.op, m.inverted, m.feather, m.expansion, v / 100f)
                                }
                            }
                        },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}

@Composable
private fun Title(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
}

@Composable
private fun Hint(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)))
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
private fun Action(title: String, detail: String, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        Spacer(Modifier.height(2.dp))
        Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}
