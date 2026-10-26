package com.aurea.aurea.editor.panels

import androidx.annotation.StringRes
import androidx.compose.animation.animateContentSize
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.TextButton
import androidx.compose.ui.platform.testTag
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.ds.KeypadRequest
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
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
    R.string.edt_tp_pop, R.string.edt_tp_jump, R.string.pn_textpreset_slide, R.string.panel_escala, R.string.pn_textpreset_appear,
    R.string.panel_desfoque, R.string.pn_textpreset_word_highlight, R.string.pn_pop, R.string.pn_textpreset_typewriter,
    R.string.pn_textpreset_wave, R.string.pn_textpreset_elastic,
    // Os de `ExtraTextPresetNames`, na mesma ordem.
    R.string.edt_tp_x_bounce, R.string.edt_tp_x_soft_in, R.string.edt_tp_x_reveal, R.string.edt_tp_x_slide,
    R.string.edt_tp_x_quick_in, R.string.edt_tp_x_elastic_jump, R.string.edt_tp_x_word_jump, R.string.edt_tp_x_smooth,
) + com.aurea.aurea.presets.PackTextPresetLabels

/** Propriedade do animador: bit (TextAnimProp), rótulo e os parâmetros animáveis dela. */
private class AnimProp(val bit: Int, @StringRes val label: Int, val params: List<AnimParam>)
private class AnimParam(val id: Int, val slot: Int, @StringRes val label: Int, val unit: String, val step: Float, val min: Float, val max: Float)

private val AnimProps = listOf(
    AnimProp(1 shl 0, R.string.fx_posicao, listOf(
        AnimParam(10, 14, R.string.panel_posicao_x, "px", 1f, -5000f, 5000f),
        AnimParam(11, 15, R.string.panel_posicao_y, "px", 1f, -5000f, 5000f),
        AnimParam(12, 16, R.string.pn_depth, "px", 1f, -5000f, 5000f),
    )),
    AnimProp(1 shl 1, R.string.panel_escala, listOf(
        AnimParam(13, 17, R.string.fx_escala_x, "%", 1f, -2000f, 2000f),
        AnimParam(14, 18, R.string.fx_escala_y, "%", 1f, -2000f, 2000f),
    )),
    AnimProp(1 shl 2, R.string.panel_rotacao, listOf(
        AnimParam(15, 19, R.string.edt_rotation_x, "°", 1f, -3600f, 3600f),
        AnimParam(16, 20, R.string.edt_rotation_y, "°", 1f, -3600f, 3600f),
        AnimParam(17, 21, R.string.edt_rotation_z, "°", 1f, -3600f, 3600f),
    )),
    AnimProp(1 shl 3, R.string.panel_opacidade, listOf(AnimParam(18, 22, R.string.panel_opacidade, "%", 0.5f, 0f, 100f))),
    AnimProp(1 shl 4, R.string.edt_prop_spacing, listOf(AnimParam(19, 23, R.string.edt_prop_spacing, "px", 0.5f, -500f, 500f))),
    AnimProp(1 shl 5, R.string.panel_desfoque, listOf(AnimParam(20, 24, R.string.panel_desfoque, "px", 0.2f, 0f, 200f))),
    AnimProp(1 shl 6, R.string.edt_prop_skew, listOf(AnimParam(21, 25, R.string.edt_prop_skew, "°", 0.5f, -80f, 80f))),
    AnimProp(1 shl 7, R.string.panel_contorno, listOf(AnimParam(22, 26, R.string.panel_contorno, "px", 0.1f, -50f, 50f))),
    AnimProp(1 shl 8, R.string.edt_prop_scramble, listOf(AnimParam(23, 27, R.string.edt_prop_scramble, "", 0.1f, -1000f, 1000f))),
    AnimProp(1 shl 9, R.string.panel_cor, emptyList()),
    AnimProp(1 shl 10, R.string.panel_cor_contorno, emptyList()),
)

/** Seletor: início, fim, deslocamento, quantidade (parâmetros 0..3 → posições 7..10). */
private val SelectorParams = listOf(
    AnimParam(0, 7, R.string.panel_inicio, "%", 0.5f, 0f, 100f),
    AnimParam(1, 8, R.string.panel_fim, "%", 0.5f, 0f, 100f),
    AnimParam(2, 9, R.string.edt_selector_offset, "%", 0.5f, -1000f, 1000f),
    AnimParam(3, 10, R.string.panel_intensidade, "%", 0.5f, -100f, 100f),
)

/**
 * ANIMAÇÃO DO TEXTO: presets (dados, não código) e a pilha de animadores —
 * cada um com seletor (letra/palavra/linha, intervalo ou aleatório) e as
 * propriedades que ele mexe, com losango de keyframe por valor.
 */
@Composable
internal fun TextAnimSection(env: PanelEnv, showAnimatorEffect: Boolean = true) {
    val store = env.store
    val list by remember(store) { derivedStateOf { store.textAnimators } }
    Spacer(Modifier.height(10.dp))
    Text(stringResource(R.string.panel_animacao), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        TextPresets.forEachIndexed { i, name -> AnimChip(stringResource(name), false) { store.applyTextPreset(i) } }
    }
    // Omega conteúdo (área 06, P1-11): os animadores em LISTA (linha de 37 dp que
    // abre os ajustes a partir dela em 200 ms) e "+ Adicionar animador" no fim.
    AnimatorList(env, list)
    Spacer(Modifier.height(4.dp))
    if (showAnimatorEffect) TextButton(modifier = Modifier.testTag("text.animator.add"), onClick = {
            store.addEffectAndFocus(effectTypeId("aurea.text.animator")); env.onOpenPanel(EditorPanel.Effects)
    }) { Text(stringResource(R.string.text_animator_add)) }
    if (showAnimatorEffect) TextButton(modifier = Modifier.testTag("text.transform.add"), onClick = {
            store.addEffectAndFocus(effectTypeId("aurea.text.transform")); env.onOpenPanel(EditorPanel.Effects)
    }) { Text(stringResource(R.string.text_transform_add)) }
}

/**
 * Omega conteúdo (área 06, P1-11): a LISTA de animadores. Cada um é uma linha de
 * 37 dp (▸, ícone, nome e o que ele mexe); tocar abre os ajustes a partir da
 * linha (200 ms) com cabeçalho ▾ · ícone · nome · ⋯ (duplicar, subir, descer) ·
 * 🗑. O último item é "+ Adicionar animador", que abre a lista dos tipos.
 */
@Composable
private fun AnimatorList(env: PanelEnv, list: List<FloatArray>) {
    val store = env.store
    var open by remember(store.primary) { mutableStateOf(-1) }
    var adding by remember { mutableStateOf(false) }
    val listLabel = stringResource(R.string.om_anim_list_cd)
    Column(Modifier.fillMaxWidth().testTag("anim.list").semantics { contentDescription = listLabel }) {
        list.forEachIndexed { index, v ->
            Column(
                Modifier
                    .fillMaxWidth()
                    .padding(vertical = 6.5.dp / 2)
                    .clip(RoundedCornerShape(5.dp))
                    .background(AureaColors.FxListSurface)
                    .animateContentSize(androidx.compose.animation.core.tween(200)),
            ) {
                if (open == index) {
                    AnimatorHeader(env, index, v, list.size, onCollapse = { open = -1 }, onMoved = { open = it })
                    AnimatorCard(env, index, v)
                } else {
                    AnimatorRow(index, v) { open = index }
                }
            }
        }
        Box {
            Row(
                Modifier
                    .fillMaxWidth()
                    .height(37.dp)
                    .padding(vertical = 2.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(AureaColors.FxLowSurface)
                    .testTag("anim.add")
                    .tocavel(shrink = 1f) { adding = true }
                    .padding(horizontal = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Plus, 16.dp, AureaColors.Text)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.la_add), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            }
            com.aurea.aurea.ui.ds.AnchoredMenu(
                expanded = adding,
                onDismiss = { adding = false },
                width = 300.dp,
                tag = "anim.add.menu",
                entries = addAnimatorEntries(env) { open = it },
            )
        }
    }
}

/** A linha fechada: ▸ (alfa 0,3), ícone do tipo, o nome e o que ele mexe. */
@Composable
private fun AnimatorRow(index: Int, v: FloatArray, onOpen: () -> Unit) {
    val props = v[1].toInt()
    val name = stringResource(R.string.edt_animation_n, index + 1)
    val what = AnimProps.filter { props and it.bit != 0 }.map { stringResource(it.label) }.joinToString(", ")
    val label = stringResource(R.string.om_anim_item_cd, index + 1, what)
    Row(
        Modifier
            .fillMaxWidth()
            .height(37.dp)
            .testTag("anim.item.$index")
            .semantics { contentDescription = label }
            .tocavel(shrink = 1f, onClick = onOpen)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.ArrowtriangleRightFill, 18.dp, AureaColors.Text.copy(alpha = 0.3f))
        Spacer(Modifier.width(4.dp))
        com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat, 18.dp,
            if (v[0] > 0.5f) AureaColors.Text else AureaColors.Muted)
        Spacer(Modifier.width(8.dp))
        Text(name, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)))
        Spacer(Modifier.width(8.dp))
        Text(what, modifier = Modifier.weight(1f), maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
    }
}

/** Cabeçalho do animador aberto: ▾ · ícone · nome · ⋯ · 🗑 (36 dp). */
@Composable
private fun AnimatorHeader(env: PanelEnv, index: Int, v: FloatArray, count: Int, onCollapse: () -> Unit, onMoved: (Int) -> Unit) {
    val store = env.store
    var more by remember { mutableStateOf(false) }
    val collapse = stringResource(R.string.om_anim_collapse_cd)
    val remove = stringResource(R.string.panel_remover)
    Row(Modifier.fillMaxWidth().height(40.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(36.dp).semantics { contentDescription = collapse }.tocavel(shrink = 1f, onClick = onCollapse), contentAlignment = Alignment.Center) {
            com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.ArrowtriangleDownFill, 16.dp, AureaColors.Text)
        }
        com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat, 18.dp, AureaColors.Text)
        Spacer(Modifier.width(8.dp))
        Text(stringResource(R.string.edt_animation_n, index + 1), modifier = Modifier.weight(1f),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
        AureaToggle(checked = v[0] > 0.5f, onCheckedChange = { store.setTextAnimatorValues(index, mapOf(0 to if (it) 1f else 0f)) })
        Box {
            Box(Modifier.size(36.dp).testTag("anim.item.$index.more").semantics { contentDescription = "⋯" }.tocavel(shrink = 1f) { more = true },
                contentAlignment = Alignment.Center) {
                com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Ellipsis, 18.dp, AureaColors.FxListIcon)
            }
            com.aurea.aurea.ui.ds.AnchoredMenu(
                expanded = more, onDismiss = { more = false }, alignEnd = true, tag = "anim.item.$index.menu",
                entries = listOf(
                    com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.om_anim_duplicate), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.PlusSquareOnSquare,
                        tag = "anim.item.$index.duplicate") { store.duplicateTextAnimator(index); onMoved(index + 1) },
                    com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.panel_mover_cima), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.ArrowUp,
                        enabled = index > 0) { store.moveTextAnimator(index, index - 1); onMoved(index - 1) },
                    com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.panel_mover_baixo), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.ArrowDown,
                        enabled = index < count - 1) { store.moveTextAnimator(index, index + 1); onMoved(index + 1) },
                ),
            )
        }
        Box(Modifier.size(36.dp).testTag("anim.item.$index.delete").semantics { contentDescription = remove }
            .tocavel(shrink = 1f) { store.removeTextAnimator(index); onCollapse() }, contentAlignment = Alignment.Center) {
            com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
        }
    }
}

/** Os tipos do "+ Adicionar animador": de texto (por propriedade), de camada e os efeitos de movimento. */
@Composable
private fun addAnimatorEntries(env: PanelEnv, onAdded: (Int) -> Unit): List<com.aurea.aurea.ui.ds.MenuEntry> {
    val store = env.store
    val entries = ArrayList<com.aurea.aurea.ui.ds.MenuEntry>()
    entries += com.aurea.aurea.ui.ds.MenuEntry.Header(stringResource(R.string.text_animator_add))
    AnimProps.forEach { p ->
        entries += com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(p.label), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat) {
            store.addTextAnimator(p.bit)
            onAdded(store.textAnimators.size - 1)
        }
    }
    entries += com.aurea.aurea.ui.ds.MenuEntry.Divider
    entries += com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(R.string.la_animators), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.Move) { env.onOpenAnimators() }
    listOf(
        "aurea.motion.oscillate.cycles" to R.string.fx_name_oscillate, "aurea.motion.swing" to R.string.fx_name_swing,
        "aurea.motion.wiggle" to R.string.fx_name_wiggle, "aurea.motion.twitch" to R.string.fx_name_twitch,
    ).forEach { (id, label) ->
        entries += com.aurea.aurea.ui.ds.MenuEntry.Item(stringResource(label), glyph = com.aurea.aurea.ui.theme.CupertinoGlyph.Sparkles) {
            store.addEffectAndFocus(effectTypeId(id)); env.onOpenPanel(EditorPanel.Effects)
        }
    }
    return entries
}

/** O corpo do animador aberto (os ajustes de sempre). */
@Composable
private fun AnimatorCard(env: PanelEnv, index: Int, v: FloatArray) {
    val store = env.store
    val props = v[1].toInt()
    Column(Modifier.fillMaxWidth().padding(start = 8.dp, end = 8.dp, bottom = 8.dp)) {
        ChipRow(stringResource(R.string.panel_anima_cada), listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha)), v[2].toInt()) { store.setTextAnimatorValues(index, mapOf(2 to it.toFloat())) }
        ChipRow(stringResource(R.string.panel_escolhe), listOf(stringResource(R.string.panel_ordem), stringResource(R.string.panel_sorteado), stringResource(R.string.ios_range_ae)), v[3].toInt()) { store.setTextAnimatorValues(index, mapOf(3 to it.toFloat())) }
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
            AnimRuler(store, index, AnimParam(25, 13, R.string.panel_trocas_segundo, "", 0.05f, 0f, 60f), v)
            AnimRuler(store, index, SelectorParams[3], v)
        }
        AnimProps.forEach { p ->
            if (props and p.bit == 0) return@forEach
            if (p.params.isEmpty()) {
                val base = if (p.bit == (1 shl 9)) 28 else 32
                Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(p.label), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
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
                AnimChip(stringResource(p.label), on) { store.setTextAnimatorValues(index, mapOf(1 to (props xor p.bit).toFloat())) }
            }
        }
    }
}

@Composable
internal fun ChipRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit) {
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
internal fun AnimChip(label: String, on: Boolean, onClick: () -> Unit) {
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
    val label = stringResource(p.label)
    PropertyCustomRow(
        label, selected = false, onSelect = {}, keyframe = look,
        expression = exprLook, onExpression = { store.openExpression(label, exprKeys, 1f, p.unit) },
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

/** Navigation follows the chosen property, including after trimming a layer. */
@Composable
internal fun AnimationTrackActions(env: PanelEnv, target: TrackKey, curveTag: String) {
        val store = env.store
        val layer = store.primary
        val navigation = layer?.let { store.keyframeNavigation(it, target) }
        val track = store.primaryKeys().filter { it.property == target.property && it.effectIndex == target.effectIndex && it.paramIndex == target.paramIndex }
        val start = navigation?.takeIf { it.hasSegment }?.let { nav -> track.firstOrNull { it.time == nav.segmentStart } }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).heightIn(min = 44.dp)) {
            TextButton(enabled = navigation?.hasPrevious == true, onClick = { navigation?.takeIf { it.hasPrevious }?.let { store.pause(); store.seek(it.previousFrame) } }) { Text(stringResource(R.string.panel_keyframe_anterior)) }
            TextButton(enabled = navigation?.hasNext == true, onClick = { navigation?.takeIf { it.hasNext }?.let { store.pause(); store.seek(it.nextFrame) } }) { Text(stringResource(R.string.panel_proximo_keyframe)) }
            TextButton(enabled = start != null, modifier = Modifier.testTag(curveTag), onClick = {
                if (layer != null && start != null) {
                    store.selectKeyframe(layer, start)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) { Text(stringResource(R.string.panel_curva)) }
        }
}
