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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/** Presets nativos do motor (mesma ordem de `text_preset_name`). */
private val TextPresets = listOf(
    "Pop", "Pulo", "Deslizar", "Escala", "Surgir", "Desfoque", "Destaque palavra", "Karaokê", "Máquina de escrever", "Onda", "Elástico",
) + com.aurea.aurea.presets.JuanTextPresetNames

/** Propriedade do animador: bit (TextAnimProp), rótulo e os parâmetros animáveis dela. */
private class AnimProp(val bit: Int, val label: String, val params: List<AnimParam>)
private class AnimParam(val id: Int, val slot: Int, val label: String, val unit: String, val step: Float, val min: Float, val max: Float)

private val AnimProps = listOf(
    AnimProp(1 shl 0, "Posição", listOf(
        AnimParam(10, 14, "Posição X", "px", 1f, -5000f, 5000f),
        AnimParam(11, 15, "Posição Y", "px", 1f, -5000f, 5000f),
        AnimParam(12, 16, "Profundidade", "px", 1f, -5000f, 5000f),
    )),
    AnimProp(1 shl 1, "Escala", listOf(
        AnimParam(13, 17, "Escala X", "%", 1f, -2000f, 2000f),
        AnimParam(14, 18, "Escala Y", "%", 1f, -2000f, 2000f),
    )),
    AnimProp(1 shl 2, "Rotação", listOf(
        AnimParam(15, 19, "Rotação X", "°", 1f, -3600f, 3600f),
        AnimParam(16, 20, "Rotação Y", "°", 1f, -3600f, 3600f),
        AnimParam(17, 21, "Rotação Z", "°", 1f, -3600f, 3600f),
    )),
    AnimProp(1 shl 3, "Opacidade", listOf(AnimParam(18, 22, "Opacidade", "%", 0.5f, 0f, 100f))),
    AnimProp(1 shl 4, "Espaçamento", listOf(AnimParam(19, 23, "Espaçamento", "px", 0.5f, -500f, 500f))),
    AnimProp(1 shl 5, "Desfoque", listOf(AnimParam(20, 24, "Desfoque", "px", 0.2f, 0f, 200f))),
    AnimProp(1 shl 6, "Inclinação", listOf(AnimParam(21, 25, "Inclinação", "°", 0.5f, -80f, 80f))),
    AnimProp(1 shl 7, "Contorno", listOf(AnimParam(22, 26, "Contorno", "px", 0.1f, -50f, 50f))),
    AnimProp(1 shl 8, "Embaralhar letra", listOf(AnimParam(23, 27, "Embaralhar letra", "", 0.1f, -1000f, 1000f))),
    AnimProp(1 shl 9, "Cor", emptyList()),
    AnimProp(1 shl 10, "Cor do contorno", emptyList()),
)

/** Seletor: início, fim, deslocamento, quantidade (parâmetros 0..3 → posições 7..10). */
private val SelectorParams = listOf(
    AnimParam(0, 7, "Início", "%", 0.5f, 0f, 100f),
    AnimParam(1, 8, "Fim", "%", 0.5f, 0f, 100f),
    AnimParam(2, 9, "Atraso entre elas", "%", 0.5f, -1000f, 1000f),
    AnimParam(3, 10, "Intensidade", "%", 0.5f, -100f, 100f),
)

/**
 * ANIMAÇÃO DO TEXTO: presets (dados, não código) e a pilha de animadores —
 * cada um com seletor (letra/palavra/linha, intervalo ou aleatório) e as
 * propriedades que ele mexe, com losango de keyframe por valor.
 */
@Composable
internal fun TextAnimSection(env: PanelEnv) {
    val store = env.store
    val list by remember(store) { derivedStateOf { store.textAnimators } }
    Spacer(Modifier.height(10.dp))
    Text(stringResource(R.string.panel_animacao), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        TextPresets.forEachIndexed { i, name -> AnimChip(name, false) { store.applyTextPreset(i) } }
    }
    list.forEachIndexed { index, v -> AnimatorCard(env, index, v) }
    Spacer(Modifier.height(4.dp))
    AnimChip(stringResource(R.string.panel_adicionar_animacao), false) { store.addTextAnimator(1 shl 3) }
}

@Composable
private fun AnimatorCard(env: PanelEnv, index: Int, v: FloatArray) {
    val store = env.store
    val props = v[1].toInt()
    Spacer(Modifier.height(8.dp))
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip.copy(alpha = 0.45f)).padding(8.dp)) {
        Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Animação ${index + 1}", modifier = Modifier.weight(1f),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
            AnimChip(stringResource(R.string.panel_remover), false) { store.removeTextAnimator(index) }
            Spacer(Modifier.width(8.dp))
            AureaToggle(checked = v[0] > 0.5f, onCheckedChange = { store.setTextAnimatorValues(index, mapOf(0 to if (it) 1f else 0f)) })
        }
        ChipRow(stringResource(R.string.panel_anima_cada), listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha)), v[2].toInt()) { store.setTextAnimatorValues(index, mapOf(2 to it.toFloat())) }
        ChipRow(stringResource(R.string.panel_escolhe), listOf(stringResource(R.string.panel_ordem), stringResource(R.string.panel_sorteado), "Intervalo AE"), v[3].toInt()) { store.setTextAnimatorValues(index, mapOf(3 to it.toFloat())) }
        if (v[3].toInt() != 1) {
            ChipRow(stringResource(R.string.panel_passagem), listOf(stringResource(R.string.panel_seco), stringResource(R.string.panel_sobe), stringResource(R.string.panel_desce), stringResource(R.string.panel_triangulo), stringResource(R.string.panel_redondo), stringResource(R.string.panel_suave)), v[4].toInt()) {
                store.setTextAnimatorValues(index, mapOf(4 to it.toFloat()))
            }
            Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_ordem_aleatoria), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
                AureaToggle(checked = v[5] > 0.5f, onCheckedChange = { store.setTextAnimatorValues(index, mapOf(5 to if (it) 1f else 0f)) })
            }
            SelectorParams.forEach { p -> AnimRuler(store, index, p, v) }
        } else {
            AnimRuler(store, index, AnimParam(25, 13, stringResource(R.string.panel_trocas_segundo), "", 0.05f, 0f, 60f), v)
            AnimRuler(store, index, SelectorParams[3], v)
        }
        AnimProps.forEach { p ->
            if (props and p.bit == 0) return@forEach
            if (p.params.isEmpty()) {
                val base = if (p.bit == (1 shl 9)) 28 else 32
                Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(p.label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
                    ColorWell(Color(v[base], v[base + 1], v[base + 2])) {
                        env.openColor(ColorRequest(floatArrayOf(v[base], v[base + 1], v[base + 2], 1f),
                            onChange = { r, g, b, _ -> store.setTextAnimatorValues(index, mapOf(base to r, base + 1 to g, base + 2 to b)) },
                            onDone = {}))
                    }
                }
            } else {
                p.params.forEach { AnimRuler(store, index, it, v) }
            }
        }
        // Liga/desliga propriedades do animador.
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            AnimProps.forEach { p ->
                val on = props and p.bit != 0
                AnimChip(p.label, on) { store.setTextAnimatorValues(index, mapOf(1 to (props xor p.bit).toFloat())) }
            }
        }
    }
}

@Composable
private fun ChipRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(40.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        options.forEachIndexed { i, o -> AnimChip(o, i == selected) { onPick(i) } }
    }
}

@Composable
private fun AnimChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/** Régua de um valor do animador, com o losango de keyframe (grava no cabeçote). */
@Composable
private fun AnimRuler(store: EditorStore, index: Int, p: AnimParam, v: FloatArray) {
    val bits = if (p.id < 10) v[36].toInt() else v[37].toInt()
    val keyBits = if (p.id < 10) v[38].toInt() else v[39].toInt()
    val bit = 1 shl (if (p.id < 10) p.id else p.id - 10)
    val look = when {
        keyBits and bit != 0 -> KeyframeLook.KeyHere
        bits and bit != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val value = { store.textAnimators.getOrNull(index)?.get(p.slot) ?: v[p.slot] }
    val exprKeys = listOf(com.aurea.aurea.engine.TrackKey(com.aurea.aurea.engine.TrackProperty.TEXT_ANIM_PARAM, index, p.id))
    val exprLook by androidx.compose.runtime.remember(store, index, p.id) {
        androidx.compose.runtime.derivedStateOf { store.expressionLook(exprKeys) }
    }
    PropertyCustomRow(
        p.label, selected = false, onSelect = {}, keyframe = look,
        expression = exprLook, onExpression = { store.openExpression(p.label, exprKeys, 1f, p.unit) },
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = p.step,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { p.step },
                        min = p.min,
                        max = p.max,
                        onStart = { store.beginGesture("animador de texto") },
                        onValue = { store.setTextAnimParam(index, p.id, it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            val shown = v[p.slot]
            ValueBox(if (p.step < 0.5f) "${com.aurea.aurea.ui.ds.numeroPtBr(shown, 1)}${p.unit}" else "${shown.roundToInt()}${p.unit}", onTap = null)
            Spacer(Modifier.width(4.dp))
            Box(Modifier.tocavel(onClick = { store.toggleTextAnimKey(index, p.id) }).padding(4.dp)) {
                KeyframeDiamondIcon(look, enabled = true)
            }
        }
    }
}
