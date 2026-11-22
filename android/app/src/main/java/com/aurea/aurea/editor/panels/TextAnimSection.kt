package com.aurea.aurea.editor.panels

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/** 0.5.0: presets no modelo do Motion (mesma ordem de `build_motion_text_preset`). */
private val TextPresets = listOf(
    R.string.tp_m_type_in to CupertinoGlyph.Pencil, R.string.tp_m_decode to CupertinoGlyph.LockOpen, R.string.tp_m_uncensor to CupertinoGlyph.Eye,
    R.string.tp_m_password to CupertinoGlyph.Lock, R.string.tp_m_title_spread to CupertinoGlyph.ArrowLeftRight, R.string.tp_m_fade_up to CupertinoGlyph.ArrowUp,
    R.string.tp_m_wave to CupertinoGlyph.Waveform, R.string.tp_m_spin to CupertinoGlyph.ArrowCounterclockwise,
    R.string.tp_m_scale_word to CupertinoGlyph.Fullscreen, R.string.tp_m_scale_out to CupertinoGlyph.FullscreenExit,
    R.string.app_text_preset_smooth_bounce to CupertinoGlyph.Waveform,
)

// Propriedades do motor (TextAnimProp) e chaves das trilhas (TextAnimParam).
private const val PROP_POSITION = 1 shl 0
private const val PROP_SCALE = 1 shl 1
private const val PROP_ROTATION = 1 shl 2
private const val PROP_OPACITY = 1 shl 3
private const val PROP_TRACKING = 1 shl 4
private const val PROP_BLUR = 1 shl 5
private const val PROP_SKEW = 1 shl 6
private const val PROP_STROKE_WIDTH = 1 shl 7
/** B5 (AE animation): Deslocamento de caractere (TextAnimProp kTextPropCharOffset). */
private const val PROP_CHAR_OFFSET = 1 shl 8
private const val PROP_FILL = 1 shl 9
private const val PROP_STROKE = 1 shl 10
private const val PROP_ANCHOR = 1 shl 11
private const val PROP_LINE_SPACING = 1 shl 12
private const val PROP_FILL_HSB = 1 shl 13
private const val PROP_FILL_OPACITY = 1 shl 14
// v70: o restante do animador do After Effects.
private const val PROP_STROKE_HSB = 1 shl 18
private const val PROP_STROKE_OPACITY = 1 shl 19
private const val PROP_CHAR_VALUE = 1 shl 20
private const val PROP_LINE_ANCHOR = 1 shl 21
private const val KEY_BLOCK = 64
private const val FIELD_PROPS = 0
private const val FIELD_RIG = 1
private const val FIELD_RIG_ANCHOR = 2
private const val FIELD_GROUPING = 3
private const val FIELD_CHAR_RANGE = 10
private const val FIELD_TRACKING_TYPE = 11
/** Alinhamento de caractere (Character Alignment do AE): 0 esquerda, 1 centro, 2 direita, 3 ajustar kerning. */
private const val FIELD_CHAR_ALIGN = 12
/** Posição da ordem preenchimento/contorno no estilo do texto (query_text_style). */
private const val STYLE_RENDER_ORDER = 20
/** Mistura entre caracteres (Inter-Character Blending do AE): o valor do BlendMode do motor. */
private const val STYLE_INTER_CHAR_BLEND = 21
private const val FIELD_SELECTOR_COUNT = 15
private const val SEL_TYPE = 1
private const val SEL_BASED_ON = 2
private const val SEL_UNITS = 3
private const val SEL_MODE = 4
private const val SEL_SHAPE = 5
private const val SEL_RANDOM = 6
private const val SEL_SEED = 7
private const val SEL_LOCK = 8
private const val TYPE_RANGE = 2
private const val TYPE_EXPRESSION = 3
/** A janela do Transformar texto do Motion (kTextSelectorSweep), própria do AUREA. */
private const val TYPE_SWEEP = 4
private const val TYPE_WIGGLY = 5
private fun selectorField(selector: Int, field: Int) = 16 * (selector + 1) + field

/** Um valor animável do animador. */
private class KeyParam(val key: Int, @StringRes val label: Int, val unit: String, val step: Float, val min: Float, val max: Float)

private fun rangeKeys(block: Int) = listOf(
    KeyParam(block, R.string.panel_inicio, "%", 0.5f, -1000f, 1000f),
    KeyParam(block + 1, R.string.panel_fim, "%", 0.5f, -1000f, 1000f),
    KeyParam(block + 2, R.string.fxl_offset, "%", 0.5f, -1000f, 1000f),
)

/** O ícone do tipo do animador: a primeira propriedade que ele mexe. */
private fun animatorGlyph(props: Int): Char = when {
    props and PROP_POSITION != 0 -> CupertinoGlyph.Move
    props and PROP_SCALE != 0 -> CupertinoGlyph.Fullscreen
    props and PROP_ROTATION != 0 -> CupertinoGlyph.ArrowCounterclockwise
    props and PROP_OPACITY != 0 -> CupertinoGlyph.CircleLefthalfFill
    props and PROP_BLUR != 0 -> CupertinoGlyph.Drop
    props and PROP_FILL != 0 -> CupertinoGlyph.Paintbrush
    else -> CupertinoGlyph.Sparkles
}

/**
 * O ANIMADOR DE TEXTO, único. A lista mostra o que está aplicado e "+ Adicionar"; abrir um
 * animador troca a lista pelo editor, na largura toda, com as seções recolhidas do Transformar
 * texto do Motion. A potência do After Effects (faixa, oscilação, expressão, combinações) fica
 * em "Seletores", sem atrapalhar quem só quer animar.
 */
/** Partes de [TextAnimSection] (Funções, F3: cada aba de Animação mostra UMA). */
internal const val TEXT_ANIM_LIST = 1
internal const val TEXT_ANIM_READY = 2
internal const val TEXT_ANIM_EXPLODE = 4
internal const val TEXT_ANIM_ALL = TEXT_ANIM_LIST or TEXT_ANIM_READY or TEXT_ANIM_EXPLODE

@Composable
internal fun TextAnimSection(env: PanelEnv, onEditing: (Boolean) -> Unit = {}, parts: Int = TEXT_ANIM_ALL) {
    val store = env.store
    val list by remember(store) { derivedStateOf { store.textAnimators } }
    var editing by rememberSaveable(store.primary) { mutableIntStateOf(-1) }
    val open = parts and TEXT_ANIM_LIST != 0 && editing in list.indices
    LaunchedEffect(open) { onEditing(open) }
    if (open) {
        AnimatorEditor(env, editing, list[editing]) { editing = -1 }
        return
    }
    if (parts and TEXT_ANIM_LIST != 0) PanelSection(stringResource(R.string.ui_text_animators), CupertinoGlyph.Sparkles, "text.sec.animators", count = list.size) {
        list.forEachIndexed { index, v -> AnimatorRow(env, index, v) { editing = index } }
        PanelAddRow(stringResource(R.string.la_add), "anim.add") {
            store.addTextAnimator(PROP_POSITION or PROP_SCALE or PROP_ROTATION or PROP_OPACITY)
            editing = store.textAnimators.size - 1
        }
    }
    if (parts and TEXT_ANIM_READY != 0) PanelSection(stringResource(R.string.ui_ready_anims), CupertinoGlyph.WandStars, "text.sec.presets") {
        PanelItemGrid(TextPresets.mapIndexed { i, (label, glyph) ->
            PanelItem(glyph, stringResource(label), "text.motion.preset.$i") { store.applyMotionTextPreset(i) }
        })
    }
    // 0.4.4 Explodir texto (só camada de texto 2D): cada pedaço vira uma camada.
    if (parts and TEXT_ANIM_EXPLODE != 0 && store.detail?.kind == LayerType.Text.kind) {
        PanelSection(stringResource(R.string.tx044_explode), CupertinoGlyph.Scissors, "text.sec.explode", startOpen = false) {
            KitHint(stringResource(R.string.tx044_explode_hint))
            Spacer(Modifier.height(6.dp))
            PanelItemGrid(listOf(
                PanelItem(CupertinoGlyph.Textformat, stringResource(R.string.tx044_explode_chars), "text.explode.0") { store.explodeText(0) },
                PanelItem(CupertinoGlyph.DocText, stringResource(R.string.tx044_explode_words), "text.explode.1") { store.explodeText(1) },
                PanelItem(CupertinoGlyph.LineHorizontal3, stringResource(R.string.tx044_explode_lines), "text.explode.2") { store.explodeText(2) },
            ), columns = 3)
        }
    }
}

/** A linha do animador na lista, 37 dp, como a lista de efeitos do Motion. */
@Composable
private fun AnimatorRow(env: PanelEnv, index: Int, v: FloatArray, onOpen: () -> Unit) {
    val store = env.store
    val on = v[0] > 0.5f
    val remove = stringResource(R.string.panel_remover)
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp).height(37.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.FxLowSurface).padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(Modifier.weight(1f).fillMaxHeight().testTag("anim.item.$index").tocavel(shrink = 1f, onClick = onOpen), verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(animatorGlyph(v[1].toInt()), 18.dp, if (on) AureaColors.Text else AureaColors.Muted, autoMirror = false)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.edt_animation_n, index + 1), maxLines = 1, overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Text else AureaColors.Muted)))
        }
        Box(Modifier.size(34.dp).testTag("anim.item.$index.eye").tocavel(shrink = 1f) { store.setTextAnimatorValues(index, mapOf(0 to if (on) 0f else 1f)) }, contentAlignment = Alignment.Center) {
            CupertinoIcon(if (on) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 18.dp, if (on) AureaColors.Text else AureaColors.Muted)
        }
        Box(Modifier.size(34.dp).testTag("anim.item.$index.delete").semantics { contentDescription = remove }.tocavel(shrink = 1f) { store.removeTextAnimator(index) }, contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
        }
    }
}

/** O editor do animador, na largura toda: uma linha por valor e as seções recolhidas. */
@Composable
private fun AnimatorEditor(env: PanelEnv, index: Int, v: FloatArray, onClose: () -> Unit) {
    val store = env.store
    val fields = store.textAnimatorFields(index) ?: return
    val props = fields[FIELD_PROPS].toInt()
    val rig = fields[FIELD_RIG] > 0.5f
    val sweep = !rig && fields[selectorField(0, SEL_TYPE)].toInt() == TYPE_SWEEP
    val on = v[0] > 0.5f
    val setField = { field: Int, value: Float -> store.setTextAnimatorField(index, field, value) }
    val toggleProp = { bit: Int -> setField(FIELD_PROPS, (props xor bit).toFloat()) }
    val tag = "anim.$index"
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.weight(1f).fillMaxHeight().testTag("$tag.back").tocavel(shrink = 1f, onClick = onClose), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(CupertinoGlyph.ChevronLeft, 18.dp, AureaColors.Text)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.edt_animation_n, index + 1), maxLines = 1,
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W700)))
            }
            Box(Modifier.size(36.dp).testTag("$tag.eye").tocavel(shrink = 1f) { store.setTextAnimatorValues(index, mapOf(0 to if (on) 0f else 1f)) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(if (on) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 18.dp, if (on) AureaColors.Text else AureaColors.Muted)
            }
            Box(Modifier.size(36.dp).testTag("$tag.duplicate").tocavel(shrink = 1f) { store.duplicateTextAnimator(index) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.PlusSquareOnSquare, 18.dp, AureaColors.FxListIcon)
            }
            Box(Modifier.size(36.dp).testTag("$tag.delete").tocavel(shrink = 1f) { store.removeTextAnimator(index); onClose() }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
            }
        }
        PanelSection(stringResource(if (sweep) R.string.ae_anim_sweep else R.string.fxs_range), CupertinoGlyph.ArrowLeftRight, "$tag.sec.range", startOpen = sweep, stacked = false) {
            if (sweep) SweepSelector(env, index, fields, 0, "$tag.range")
            else rangeKeys(0).forEach { KeyRow(env, index, it) }
        }
        PanelSection(stringResource(R.string.fxs_transform), CupertinoGlyph.Move, "$tag.sec.transform", startOpen = false, stacked = false) {
            ChipRow(stringResource(R.string.panel_anima_cada),
                listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha), stringResource(R.string.ui_char_no_spaces)),
                fields[selectorField(0, SEL_BASED_ON)].toInt()) { setField(selectorField(0, SEL_BASED_ON), it.toFloat()) }
            if (rig) {
                ChipRow(stringResource(R.string.fxl_anchor), listOf(stringResource(R.string.fxo_layer), stringResource(R.string.fxl_component)), fields[FIELD_RIG_ANCHOR].toInt()) { setField(FIELD_RIG_ANCHOR, it.toFloat()) }
                KeyRow(env, index, KeyParam(10, R.string.panel_posicao_x, "px", 1f, -20000f, 20000f))
                KeyRow(env, index, KeyParam(11, R.string.panel_posicao_y, "px", 1f, -20000f, 20000f))
                KeyRow(env, index, KeyParam(17, R.string.fxl_angle, "°", 1f, -1800f, 1800f))
                KeyRow(env, index, KeyParam(13, R.string.panel_escala, "%", 1f, -100f, 1000f))
                KeyRow(env, index, KeyParam(14, R.string.fxl_stretch, "%", 1f, -100f, 1000f))
                KeyRow(env, index, KeyParam(18, R.string.panel_opacidade, "%", 0.5f, -100f, 100f))
                KeyRow(env, index, KeyParam(20, R.string.panel_desfoque, "px", 0.2f, 0f, 200f))
            } else {
                KeyRow(env, index, KeyParam(10, R.string.panel_posicao_x, "px", 1f, -20000f, 20000f))
                KeyRow(env, index, KeyParam(11, R.string.panel_posicao_y, "px", 1f, -20000f, 20000f))
                KeyRow(env, index, KeyParam(12, R.string.fxl_position_z, "px", 1f, -20000f, 20000f))
                KeyRow(env, index, KeyParam(13, R.string.fx_escala_x, "%", 1f, -2000f, 2000f))
                KeyRow(env, index, KeyParam(14, R.string.fx_escala_y, "%", 1f, -2000f, 2000f))
                KeyRow(env, index, KeyParam(17, R.string.panel_rotacao, "°", 1f, -3600f, 3600f))
                KeyRow(env, index, KeyParam(15, R.string.edt_rotation_x, "°", 1f, -3600f, 3600f))
                KeyRow(env, index, KeyParam(16, R.string.edt_rotation_y, "°", 1f, -3600f, 3600f))
                KeyRow(env, index, KeyParam(18, R.string.panel_opacidade, "%", 0.5f, 0f, 100f))
                val extras = listOf(
                    Triple(PROP_BLUR, R.string.panel_desfoque, KeyParam(20, R.string.panel_desfoque, "px", 0.2f, 0f, 200f)),
                    Triple(PROP_TRACKING, R.string.edt_prop_spacing, KeyParam(19, R.string.edt_prop_spacing, "px", 0.5f, -500f, 500f)),
                    Triple(PROP_SKEW, R.string.edt_prop_skew, KeyParam(21, R.string.edt_prop_skew, "°", 0.5f, -80f, 80f)),
                    Triple(PROP_STROKE_WIDTH, R.string.panel_contorno, KeyParam(22, R.string.panel_contorno, "px", 0.1f, -50f, 50f)),
                    Triple(PROP_LINE_SPACING, R.string.fxl_line_spacing, KeyParam(45, R.string.fxl_line_spacing, "px", 1f, -20000f, 20000f)),
                    // B5: Character Offset do AE (troca cada letra pela N-ésima seguinte).
                    Triple(PROP_CHAR_OFFSET, R.string.ae_anim_char_offset, KeyParam(23, R.string.ae_anim_char_offset, "", 0.05f, -1000f, 1000f)),
                    // v70: Valor de caractere (o código Unicode que troca a letra) e a âncora da linha do tracking.
                    Triple(PROP_CHAR_VALUE, R.string.ae_anim_char_value, KeyParam(62, R.string.ae_anim_char_value, "", 1f, 0f, 1114111f)),
                    Triple(PROP_LINE_ANCHOR, R.string.ae_anim_line_anchor, KeyParam(63, R.string.ae_anim_line_anchor, "%", 0.5f, -1000f, 1000f)),
                )
                extras.forEach { (bit, _, param) -> if (props and bit != 0) KeyRow(env, index, param) }
                if (props and PROP_TRACKING != 0) {
                    ChipRow(stringResource(R.string.ae_anim_tracking_type),
                        listOf(stringResource(R.string.ae_anim_tracking_both), stringResource(R.string.ae_anim_tracking_before), stringResource(R.string.ae_anim_tracking_after)),
                        fields[FIELD_TRACKING_TYPE].toInt()) { setField(FIELD_TRACKING_TYPE, it.toFloat()) }
                }
                if (props and (PROP_CHAR_OFFSET or PROP_CHAR_VALUE) != 0) {
                    ChipRow(stringResource(R.string.ae_anim_char_range),
                        listOf(stringResource(R.string.ae_anim_char_range_case), stringResource(R.string.ae_anim_char_range_unicode)),
                        fields[FIELD_CHAR_RANGE].toInt()) { setField(FIELD_CHAR_RANGE, it.toFloat()) }
                    // Character Alignment do AE: onde a letra nova fica na vaga da antiga (ou o kerning refeito).
                    ChipRow(stringResource(R.string.ae_anim_char_align),
                        listOf(stringResource(R.string.ae_anim_char_align_left), stringResource(R.string.ae_anim_char_align_center),
                            stringResource(R.string.ae_anim_char_align_right), stringResource(R.string.ae_anim_char_align_kerning)),
                        fields[FIELD_CHAR_ALIGN].toInt()) { setField(FIELD_CHAR_ALIGN, it.toFloat()) }
                }
                // B5: o eixo da inclinação (Skew Axis do AE) junto da inclinação.
                if (props and PROP_SKEW != 0) KeyRow(env, index, KeyParam(26, R.string.ae_anim_skew_axis, "°", 1f, -3600f, 3600f))
                if (props and PROP_ANCHOR != 0) {
                    KeyRow(env, index, KeyParam(42, R.string.fxl_anchor, "px", 1f, -20000f, 20000f))
                    KeyRow(env, index, KeyParam(43, R.string.fxl_anchor, "px", 1f, -20000f, 20000f))
                }
                ChipRow(stringResource(R.string.panel_propriedades),
                    extras.map { stringResource(it.second) } + stringResource(R.string.fxl_anchor), -1,
                    on = { i -> props and (if (i < extras.size) extras[i].first else PROP_ANCHOR) != 0 }) { i -> toggleProp(if (i < extras.size) extras[i].first else PROP_ANCHOR) }
            }
        }
        if (!rig) PanelSection(stringResource(R.string.fxl_anchor_point_grouping), CupertinoGlyph.Move, "$tag.sec.grouping", startOpen = false, stacked = false) {
            ChipRow(stringResource(R.string.fxl_anchor_point_grouping),
                listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha), stringResource(R.string.edt_cmd_all)),
                fields[FIELD_GROUPING].toInt()) { setField(FIELD_GROUPING, it.toFloat()) }
            KeyRow(env, index, KeyParam(56, R.string.fxl_group_align_x, "%", 0.5f, -100f, 100f))
            KeyRow(env, index, KeyParam(57, R.string.fxl_group_align_y, "%", 0.5f, -100f, 100f))
        }
        PanelSection(stringResource(R.string.fxs_visual_attrs), CupertinoGlyph.Paintbrush, "$tag.sec.visual", startOpen = false, stacked = false) {
            ToggleLine(stringResource(R.string.fxl_override_fill_color), props and PROP_FILL != 0) { toggleProp(PROP_FILL) }
            if (props and PROP_FILL != 0) AnimatorColor(env, index, stringResource(R.string.panel_cor), 6, v)
            if (!rig) {
                ToggleLine(stringResource(R.string.panel_cor_contorno), props and PROP_STROKE != 0) { toggleProp(PROP_STROKE) }
                if (props and PROP_STROKE != 0) AnimatorColor(env, index, stringResource(R.string.panel_cor_contorno), 29, v)
                ToggleLine(stringResource(R.string.ui_fill_hsb), props and PROP_FILL_HSB != 0) { toggleProp(PROP_FILL_HSB) }
                if (props and PROP_FILL_HSB != 0) {
                    KeyRow(env, index, KeyParam(46, R.string.fx_matiz, "°", 1f, -3600f, 3600f))
                    KeyRow(env, index, KeyParam(47, R.string.fx_saturacao, "%", 0.5f, -100f, 100f))
                    KeyRow(env, index, KeyParam(48, R.string.fx_brilho, "%", 0.5f, -100f, 100f))
                }
                ToggleLine(stringResource(R.string.ae_anim_fill_opacity), props and PROP_FILL_OPACITY != 0) { toggleProp(PROP_FILL_OPACITY) }
                if (props and PROP_FILL_OPACITY != 0) KeyRow(env, index, KeyParam(49, R.string.ae_anim_fill_opacity, "%", 0.5f, 0f, 100f))
                // v70: matiz, saturação, brilho e opacidade do contorno, como no After Effects.
                ToggleLine(stringResource(R.string.ae_anim_stroke_hsb), props and PROP_STROKE_HSB != 0) { toggleProp(PROP_STROKE_HSB) }
                if (props and PROP_STROKE_HSB != 0) {
                    KeyRow(env, index, KeyParam(58, R.string.fx_matiz, "°", 1f, -3600f, 3600f))
                    KeyRow(env, index, KeyParam(59, R.string.fx_saturacao, "%", 0.5f, -100f, 100f))
                    KeyRow(env, index, KeyParam(60, R.string.fx_brilho, "%", 0.5f, -100f, 100f))
                }
                ToggleLine(stringResource(R.string.ae_anim_stroke_opacity), props and PROP_STROKE_OPACITY != 0) { toggleProp(PROP_STROKE_OPACITY) }
                if (props and PROP_STROKE_OPACITY != 0) KeyRow(env, index, KeyParam(61, R.string.ae_anim_stroke_opacity, "%", 0.5f, 0f, 100f))
            }
        }
        // v70: "Mais opções" do After Effects. A ordem preenchimento/contorno é do texto inteiro.
        val style = store.textStyle
        if (!rig && style != null && style.size > STYLE_RENDER_ORDER) {
            PanelSection(stringResource(R.string.ae_anim_more_options), CupertinoGlyph.SliderHorizontal3, "$tag.sec.more", startOpen = false, stacked = false) {
                // Ordem do motor: 0 preenchimentos por cima, 1 por caractere, 2 contornos por cima.
                ChipRow(stringResource(R.string.ae_anim_fill_stroke),
                    listOf(stringResource(R.string.ae_anim_fill_stroke_char), stringResource(R.string.ae_anim_fill_stroke_fills), stringResource(R.string.ae_anim_fill_stroke_strokes)),
                    when (style[STYLE_RENDER_ORDER].toInt()) { 1 -> 0; 2 -> 2; else -> 1 }) { i ->
                    store.setTextStyleValue(STYLE_RENDER_ORDER, when (i) { 0 -> 1f; 2 -> 2f; else -> 0f })
                }
                // Inter-Character Blending do AE: como as letras do texto se misturam onde se sobrepõem.
                if (style.size > STYLE_INTER_CHAR_BLEND) {
                    val current = InterCharBlends.indexOfFirst { it.second == style[STYLE_INTER_CHAR_BLEND].toInt() }.coerceAtLeast(0)
                    ChipRow(stringResource(R.string.ae_anim_inter_char_blend), InterCharBlends.map { stringResource(it.first) }, current) { i ->
                        store.setTextStyleValue(STYLE_INTER_CHAR_BLEND, InterCharBlends[i].second.toFloat())
                    }
                }
            }
        }
        if (!sweep) PanelSection(stringResource(R.string.fxs_interp_ease), CupertinoGlyph.ArrowUpArrowDown, "$tag.sec.ease", startOpen = false, stacked = false) {
            val shapes = if (rig) listOf(R.string.fxo_square_2, R.string.fxo_smooth, R.string.fxo_triangle)
                else listOf(R.string.fxo_square_2, R.string.panel_sobe, R.string.panel_desce, R.string.fxo_triangle, R.string.panel_redondo, R.string.fxo_smooth)
            ChipRow(stringResource(R.string.fxl_shape), shapes.map { stringResource(it) }, fields[selectorField(0, SEL_SHAPE)].toInt()) { setField(selectorField(0, SEL_SHAPE), it.toFloat()) }
            if (!rig && fields[selectorField(0, SEL_SHAPE)].toInt() == 0) {
                KeyRow(env, index, KeyParam(32, R.string.fx_suavidade, "%", 0.5f, 0f, 100f))
                KeyRow(env, index, KeyParam(41, R.string.fxl_overlap, "%", 0.5f, 0f, 1000f))
            }
            KeyRow(env, index, KeyParam(4, if (rig) R.string.fxl_ease_in else R.string.fxl_ease_high, "%", 0.5f, -100f, 100f))
            KeyRow(env, index, KeyParam(5, if (rig) R.string.fxl_ease_out else R.string.fxl_ease_low, "%", 0.5f, -100f, 100f))
            if (rig) KeyRow(env, index, KeyParam(41, R.string.fxl_overlap, "%", 0.5f, 0f, 1000f))
        }
        PanelSection(stringResource(R.string.fxl_random_order), CupertinoGlyph.Arrow2Squarepath, "$tag.sec.random", startOpen = false, stacked = false) {
            ToggleLine(stringResource(R.string.fxl_random_order), fields[selectorField(0, SEL_RANDOM)] > 0.5f) { setField(selectorField(0, SEL_RANDOM), if (it) 1f else 0f) }
            Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.fxl_seed), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                ValueBox("${fields[selectorField(0, SEL_SEED)].toLong()}", onTap = null)
                Spacer(Modifier.width(8.dp))
                AnimChip(stringResource(R.string.la_reroll), false) { setField(selectorField(0, SEL_SEED), (1..999_999).random().toFloat()) }
            }
        }
        if (!rig) SelectorsSection(env, index, fields, tag)
    }
}

/** Faixa, oscilação e expressão combináveis: o nível do After Effects, recolhido por padrão. */
@Composable
private fun SelectorsSection(env: PanelEnv, index: Int, fields: FloatArray, tag: String) {
    val store = env.store
    val count = fields[FIELD_SELECTOR_COUNT].toInt()
    val setField = { field: Int, value: Float -> store.setTextAnimatorField(index, field, value) }
    val modes = listOf(R.string.panel_somar, R.string.panel_subtrair, R.string.panel_intersecao, R.string.fxo_min, R.string.fxo_maximum, R.string.panel_diferenca)
    PanelSection(stringResource(R.string.ui_selectors), CupertinoGlyph.SliderHorizontal3, "$tag.sec.selectors", startOpen = false, count = count, stacked = false) {
        ChipRow(stringResource(R.string.fxl_units), listOf(stringResource(R.string.fxo_percentage), stringResource(R.string.fxo_index)), fields[selectorField(0, SEL_UNITS)].toInt()) { setField(selectorField(0, SEL_UNITS), it.toFloat()) }
        ChipRow(stringResource(R.string.fxl_mode), modes.map { stringResource(it) }, fields[selectorField(0, SEL_MODE)].toInt()) { setField(selectorField(0, SEL_MODE), it.toFloat()) }
        KeyRow(env, index, KeyParam(3, R.string.panel_intensidade, "%", 0.5f, -100f, 100f))
        for (s in 1 until count) {
            val type = fields[selectorField(s, SEL_TYPE)].toInt()
            val block = KEY_BLOCK * s
            val title = stringResource(when (type) { TYPE_WIGGLY -> R.string.ui_wiggly; TYPE_EXPRESSION -> R.string.panel_expressao; TYPE_SWEEP -> R.string.ae_anim_sweep; else -> R.string.fxs_range })
            val glyph = when (type) { TYPE_WIGGLY -> CupertinoGlyph.Waveform; TYPE_EXPRESSION -> CupertinoGlyph.Textformat; TYPE_SWEEP -> CupertinoGlyph.Arrow2Squarepath; else -> CupertinoGlyph.ArrowLeftRight }
            PanelSection("$title ${s + 1}", glyph, "$tag.sel.$s.$type", startOpen = true, flat = true, stacked = false) {
                ChipRow(stringResource(R.string.fxl_mode), modes.map { stringResource(it) }, fields[selectorField(s, SEL_MODE)].toInt()) { setField(selectorField(s, SEL_MODE), it.toFloat()) }
                when (type) {
                    TYPE_WIGGLY -> {
                        // B7: a base (letra/palavra/linha) vale em cada seletor.
                        SelectorBasedOn(env, index, fields, s)
                        KeyRow(env, index, KeyParam(block + 34, R.string.fxl_max_amount, "%", 0.5f, -100f, 100f))
                        KeyRow(env, index, KeyParam(block + 33, R.string.fxl_min_amount, "%", 0.5f, -100f, 100f))
                        KeyRow(env, index, KeyParam(block + 25, R.string.fxl_wiggles_second, "/s", 0.05f, 0f, 60f))
                        KeyRow(env, index, KeyParam(block + 35, R.string.fxl_correlation, "%", 0.5f, 0f, 100f))
                        KeyRow(env, index, KeyParam(block + 36, R.string.fxl_temporal_phase, "°", 1f, -3600f, 3600f))
                        KeyRow(env, index, KeyParam(block + 37, R.string.fxl_spatial_phase, "°", 1f, -3600f, 3600f))
                        ToggleLine(stringResource(R.string.fxl_lock_dimensions), fields[selectorField(s, SEL_LOCK)] > 0.5f) { setField(selectorField(s, SEL_LOCK), if (it) 1f else 0f) }
                    }
                    TYPE_EXPRESSION -> {
                        KitHint(stringResource(R.string.ui_expr_selector_hint))
                        KeyRow(env, index, KeyParam(block + 38, R.string.panel_intensidade, "%", 1f, -1000f, 1000f))
                    }
                    TYPE_SWEEP -> SweepSelector(env, index, fields, s, "$tag.sel.$s")
                    else -> {
                        rangeKeys(block).forEach { KeyRow(env, index, it) }
                        KeyRow(env, index, KeyParam(block + 3, R.string.panel_intensidade, "%", 0.5f, -100f, 100f))
                        RangeSelectorShape(env, index, fields, s, "$tag.sel.$s")
                    }
                }
                PanelItemRow(CupertinoGlyph.Trash, stringResource(R.string.panel_remover), "$tag.sel.$s.delete", danger = true) { store.removeTextSelector(index, s) }
            }
        }
        Spacer(Modifier.height(4.dp))
        PanelItemGrid(listOf(
            PanelItem(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.fxs_range), "$tag.add.range") { store.addTextSelector(index, TYPE_RANGE) },
            PanelItem(CupertinoGlyph.Waveform, stringResource(R.string.ui_wiggly), "$tag.add.wiggly") { store.addTextSelector(index, TYPE_WIGGLY) },
            PanelItem(CupertinoGlyph.Textformat, stringResource(R.string.panel_expressao), "$tag.add.expression") { store.addTextSelector(index, TYPE_EXPRESSION) },
            PanelItem(CupertinoGlyph.Arrow2Squarepath, stringResource(R.string.ae_anim_sweep), "$tag.add.sweep") { store.addTextSelector(index, TYPE_SWEEP) },
        ), columns = 4)
    }
}

/** B7 (AE animation): a base do seletor `s` (letra, palavra, linha, letra sem espaço). */
@Composable
private fun SelectorBasedOn(env: PanelEnv, index: Int, fields: FloatArray, s: Int) {
    ChipRow(stringResource(R.string.panel_anima_cada),
        listOf(stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha), stringResource(R.string.ui_char_no_spaces)),
        fields[selectorField(s, SEL_BASED_ON)].toInt()) { env.store.setTextAnimatorField(index, selectorField(s, SEL_BASED_ON), it.toFloat()) }
}

/**
 * B7 (AE animation): forma, suavidade, ease alto/baixo, unidades, base e ordem
 * aleatória do seletor de faixa `s` — antes só o 1º seletor tinha estes controles.
 */
@Composable
private fun RangeSelectorShape(env: PanelEnv, index: Int, fields: FloatArray, s: Int, tag: String) {
    val store = env.store
    val block = KEY_BLOCK * s
    val setField = { field: Int, value: Float -> store.setTextAnimatorField(index, field, value) }
    SelectorBasedOn(env, index, fields, s)
    ChipRow(stringResource(R.string.fxl_units), listOf(stringResource(R.string.fxo_percentage), stringResource(R.string.fxo_index)),
        fields[selectorField(s, SEL_UNITS)].toInt()) { setField(selectorField(s, SEL_UNITS), it.toFloat()) }
    val shapes = listOf(R.string.fxo_square_2, R.string.panel_sobe, R.string.panel_desce, R.string.fxo_triangle, R.string.panel_redondo, R.string.fxo_smooth)
    ChipRow(stringResource(R.string.fxl_shape), shapes.map { stringResource(it) }, fields[selectorField(s, SEL_SHAPE)].toInt()) {
        setField(selectorField(s, SEL_SHAPE), it.toFloat())
    }
    if (fields[selectorField(s, SEL_SHAPE)].toInt() == 0) {
        KeyRow(env, index, KeyParam(block + 32, R.string.fx_suavidade, "%", 0.5f, 0f, 100f))
        KeyRow(env, index, KeyParam(block + 41, R.string.fxl_overlap, "%", 0.5f, 0f, 1000f))
    }
    KeyRow(env, index, KeyParam(block + 4, R.string.fxl_ease_high, "%", 0.5f, -100f, 100f))
    KeyRow(env, index, KeyParam(block + 5, R.string.fxl_ease_low, "%", 0.5f, -100f, 100f))
    ToggleLine(stringResource(R.string.fxl_random_order), fields[selectorField(s, SEL_RANDOM)] > 0.5f) {
        setField(selectorField(s, SEL_RANDOM), if (it) 1f else 0f)
    }
    if (fields[selectorField(s, SEL_RANDOM)] > 0.5f) {
        Row(Modifier.fillMaxWidth().height(44.dp).testTag("$tag.seed"), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.fxl_seed), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ValueBox("${fields[selectorField(s, SEL_SEED)].toLong()}", onTap = null)
            Spacer(Modifier.width(8.dp))
            AnimChip(stringResource(R.string.la_reroll), false) { setField(selectorField(s, SEL_SEED), (1..999_999).random().toFloat()) }
        }
    }
}

/**
 * Varredura (kTextSelectorSweep): a janela do Transformar texto do Motion, própria do AUREA.
 * Cada unidade ocupa uma janela que entra e sai com a sobreposição; forma quadrada, suave ou triângulo.
 */
@Composable
private fun SweepSelector(env: PanelEnv, index: Int, fields: FloatArray, s: Int, tag: String) {
    val store = env.store
    val block = KEY_BLOCK * s
    val setField = { field: Int, value: Float -> store.setTextAnimatorField(index, field, value) }
    val shape = fields[selectorField(s, SEL_SHAPE)].toInt().coerceIn(0, 3)
    if (shape == 3) {
        KeyRow(env, index, KeyParam(block, R.string.la_progress, "%", 0.5f, 0f, 100f))
        KeyRow(env, index, KeyParam(block + 2, R.string.fxl_offset, "%", 0.5f, -100f, 100f))
    } else {
        KitHint(stringResource(R.string.ae_anim_sweep_hint))
        rangeKeys(block).forEach { KeyRow(env, index, it) }
    }
    KeyRow(env, index, KeyParam(block + 41, R.string.fxl_overlap, "%", 0.5f, 0f, 1000f))
    KeyRow(env, index, KeyParam(block + 3, R.string.panel_intensidade, "%", 0.5f, -100f, 100f))
    SelectorBasedOn(env, index, fields, s)
    ChipRow(stringResource(R.string.fxl_shape),
        listOf(R.string.fxo_square_2, R.string.fxo_smooth, R.string.fxo_triangle, R.string.app_text_preset_smooth_bounce).map { stringResource(it) },
        shape) { setField(selectorField(s, SEL_SHAPE), it.toFloat()) }
    KeyRow(env, index, KeyParam(block + 4, if (shape == 3) R.string.fx_amplitude else R.string.fxl_ease_in, "%", 0.5f, if (shape == 3) 0f else -100f, 100f))
    KeyRow(env, index, KeyParam(block + 5, if (shape == 3) R.string.fx_frequencia else R.string.fxl_ease_out, "%", 0.5f, if (shape == 3) 0f else -100f, 100f))
    ToggleLine(stringResource(R.string.fxl_random_order), fields[selectorField(s, SEL_RANDOM)] > 0.5f) {
        setField(selectorField(s, SEL_RANDOM), if (it) 1f else 0f)
    }
    if (fields[selectorField(s, SEL_RANDOM)] > 0.5f) {
        Row(Modifier.fillMaxWidth().height(44.dp).testTag("$tag.seed"), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.fxl_seed), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ValueBox("${fields[selectorField(s, SEL_SEED)].toLong()}", onTap = null)
            Spacer(Modifier.width(8.dp))
            AnimChip(stringResource(R.string.la_reroll), false) { setField(selectorField(s, SEL_SEED), (1..999_999).random().toFloat()) }
        }
    }
}

/**
 * Inter-Character Blending do AE, na ordem do menu dele: o rótulo e o valor do
 * BlendMode do motor (core/Types.hpp) que mistura cada letra com as anteriores.
 */
private val InterCharBlends = listOf(
    R.string.panel_normal to 0, R.string.ae_blend_dissolve to 31,
    R.string.pn_blend_darken to 6, R.string.pn_blend_multiply to 3, R.string.pn_blend_linear_burn to 21, R.string.pn_blend_color_burn to 9,
    R.string.pn_blend_add to 1, R.string.pn_blend_lighten to 7, R.string.pn_blend_screen to 4, R.string.pn_blend_linear_dodge to 20, R.string.pn_blend_color_dodge to 8,
    R.string.pn_blend_overlay to 5, R.string.pn_blend_soft_light to 11, R.string.pn_blend_hard_light to 10, R.string.om_ap_blend_linear_light to 26,
    R.string.pn_blend_vivid_light to 19, R.string.om_ap_blend_pin_light to 27, R.string.ae_blend_hard_mix to 33,
    R.string.pn_blend_difference to 12, R.string.pn_blend_exclusion to 13,
    R.string.pn_blend_hue to 14, R.string.pn_blend_saturation to 15, R.string.pn_blend_color to 16, R.string.pn_blend_luminosity to 17,
)

/** A cor do animador: `base` é a chave do vermelho; verde e azul vêm em seguida. */
@Composable
private fun AnimatorColor(env: PanelEnv, index: Int, label: String, base: Int, v: FloatArray) {
    val store = env.store
    val slot = if (base == 6) 28 else 32
    ColorLine(label, Color(v[slot], v[slot + 1], v[slot + 2])) {
        env.openColor(ColorRequest(floatArrayOf(v[slot], v[slot + 1], v[slot + 2], 1f),
            onChange = { r, g, b, _ -> store.setTextAnimParam(index, base, r); store.setTextAnimParam(index, base + 1, g); store.setTextAnimParam(index, base + 2, b) },
            onDone = {}))
    }
}

/** Uma linha de valor: rótulo com losango, régua e o número. */
@Composable
private fun KeyRow(env: PanelEnv, index: Int, p: KeyParam) {
    val store = env.store
    val state = store.textAnimValue(index, p.key)
    val flags = state[1].toInt()
    val look = when {
        flags and 2 != 0 -> KeyframeLook.KeyHere
        flags and 1 != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val value = { store.textAnimValue(index, p.key)[0] }
    val track = TrackKey(TrackProperty.TEXT_ANIM_PARAM, index, p.key)
    val exprKeys = listOf(track)
    val exprLook by remember(store, index, p.key) { derivedStateOf { store.expressionLook(exprKeys) } }
    val label = stringResource(p.label)
    val decimals = if (p.step < 0.5f) 1 else 0
    PropertyCustomRow(
        label, selected = store.timelineFocus?.singleOrNull() == track, onSelect = { store.timelineFocus = exprKeys }, keyframe = look,
        modifier = Modifier.testTag("anim.$index.key.${p.key}"),
        expression = exprLook, onExpression = { store.openExpression(label, exprKeys, 1f, p.unit) },
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            val keyLabel = stringResource(if (look == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui)
            Box(
                Modifier.size(40.dp).testTag("anim.$index.key.${p.key}.toggle").semantics { contentDescription = keyLabel }
                    .tocavel { store.timelineFocus = exprKeys; store.toggleTextAnimKey(index, p.key) },
                contentAlignment = Alignment.Center,
            ) {
                KeyframeDiamondIcon(look, enabled = true)
            }
            Box(Modifier.weight(1f).height(44.dp)) {
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
                        onStart = { store.timelineFocus = exprKeys; store.beginGesture("animador de texto") },
                        onValue = { store.setTextAnimParam(index, p.key, it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(if (decimals > 0) "${com.aurea.aurea.ui.ds.numeroPtBr(state[0], 1)}${p.unit}" else "${state[0].roundToInt()}${p.unit}", onTap = {
                env.openKeypad(KeypadRequest(label, value(), p.unit, p.min, p.max, decimals) {
                    store.beginGesture("animador de texto"); store.setTextAnimParam(index, p.key, it.coerceIn(p.min, p.max)); store.endGesture()
                })
            })
        }
    }
}

/** Rótulo em cima e as opções embaixo, quebrando linha: nenhuma fica cortada na borda. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun ChipRow(label: String, options: List<String>, selected: Int, on: (Int) -> Boolean = { it == selected }, onPick: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        Spacer(Modifier.height(4.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            options.forEachIndexed { i, o -> AnimChip(o, on(i)) { onPick(i) } }
        }
    }
}

@Composable
internal fun AnimChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else LocalPanelItemSurface.current ?: AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
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
