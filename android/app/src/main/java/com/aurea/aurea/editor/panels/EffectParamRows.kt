package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.engine.ParamControl
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.ui.ds.AureaSlider
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.FxDropdownBox
import com.aurea.aurea.ui.ds.FxParamLabel
import com.aurea.aurea.ui.ds.FxParamMath
import com.aurea.aurea.ui.ds.FxRow
import com.aurea.aurea.ui.ds.FxRuler
import com.aurea.aurea.ui.ds.FxRulerState
import com.aurea.aurea.ui.ds.FxSegmented
import com.aurea.aurea.ui.ds.FxValueBox
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.comUnidade
import com.aurea.aurea.ui.ds.displayToEngine
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.rememberFxRulerState
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
//  Omega efeitos (0.3.2, área 07, P0-4 / P1-1 / P1-3): AS LINHAS DE PARÂMETRO do
//  corpo dos controles, nas medidas da referência. O motor diz o controle de cada
//  número (`EffectParam.control`: slider, régua, ponto, XYZ, orientação), o passo
//  da régua, a origem do slider, o estilo da escolha e se a linha está
//  habilitada agora (habilitação cruzada / interruptor da seção). Par do iOS:
//  `EffectsView.swift` (mesmos ids e medidas).
//
//    Slider 51 · Régua/Ponto/XYZ/Orientação 57 · Interruptor/Escolha/Cor 38.
//    Rótulo 75 × (alt − 6) com margem 3; caixa de valor 56.
// =============================================================================

/** Valor de UM componente do parâmetro, derivado: a linha só recompõe quando ELE muda. */
@Composable
internal fun rememberParamValue(env: PanelEnv, effectId: Int, index: Int, component: Int): Float {
    val store = env.store
    val v by remember(store, effectId, index, component) {
        derivedStateOf { store.paramOf(effectId, index)?.value?.getOrNull(component) ?: 0f }
    }
    return v
}

@Composable
internal fun rememberExpr(env: PanelEnv, effectId: Int, s: ParamSlot): ExpressionLook {
    val store = env.store
    val look by remember(store, effectId, s.index) { derivedStateOf { store.expressionLook(paramKeys(effectId, s)) } }
    return look
}

@Composable
internal fun rememberLook(env: PanelEnv, effectId: Int, index: Int, component: Int?): KeyframeLook {
    val store = env.store
    val look by remember(store, effectId, index, component) { derivedStateOf { effectLook(store, effectId, index, component) } }
    return look
}

/** A camada principal está travada? (derivado: o detalhe muda a cada quadro). */
@Composable
private fun rememberLocked(env: PanelEnv): Boolean {
    val store = env.store
    val locked by remember(store) { derivedStateOf { store.detail?.locked == true } }
    return locked
}

/**
 * O passo da régua na unidade MOSTRADA: o do motor × a escala de exibição. Linha
 * antiga (sem passo) usa a mesma regra do motor (`resolve_step`): (max − min)/200
 * arredondado em 1·2·5×10ⁿ; inteiro e ângulo, 1.
 */
internal fun fxDisplayStep(s: ParamSlot, d: ParamDisplay): Float {
    val engineStep = if (s.step > 0f && s.step.isFinite()) s.step else fxFallbackStep(s)
    return abs(engineStep * d.scale).takeIf { it > 0f && it.isFinite() } ?: 1f
}

private fun fxFallbackStep(s: ParamSlot): Float {
    if (s.type == ParamType.INT || s.type == ParamType.ANGLE) return 1f
    val range = (s.max - s.min).toDouble()
    if (!range.isFinite() || range <= 0.0) return 0.01f
    val x = range / 200.0
    val e = kotlin.math.floor(kotlin.math.log10(x))
    val base = Math.pow(10.0, e)
    val m = x / base
    val pick = when {
        m < 1.4142135623730951 -> 1.0
        m < 3.1622776601683795 -> 2.0
        m < 7.0710678118654755 -> 5.0
        else -> 10.0
    }
    return (pick * base).toFloat()
}

/** Texto do estado "desligado" da linha (a11y): "Desligado por …" quando se sabe quem desliga. */
@Composable
private fun disabledText(by: String?): String =
    if (by.isNullOrEmpty()) stringResource(R.string.om_fx_disabled) else stringResource(R.string.om_fx_disabled_by, by)

/**
 * UMA linha de parâmetro, escolhida pelo tipo e pelo controle do motor. [onSelect]
 * recebe a chave e se foi um toque EXPLÍCITO no rótulo/eixo (ativa o ponto no
 * preview); [disabledBy] é o rótulo do interruptor da seção (para a acessibilidade).
 */
@Composable
internal fun FxParamRow(
    env: PanelEnv,
    effectId: Int,
    typeId: Int,
    s: ParamSlot,
    selected: ParamKey?,
    onSelect: (ParamKey, Boolean) -> Unit,
    onParamMenu: (ParamMenuTarget) -> Unit,
    disabledBy: String?,
) {
    val d = remember(typeId, s) { paramDisplay(typeId, s) }
    val label = d.label()
    val menu = { onParamMenu(ParamMenuTarget(effectId, s.index, label)) }
    // Dica (ⓘ) do motor, só leitura, logo acima da linha.
    if (s.tip.isNotEmpty()) FxTipRow(effectId, s.index, s.tip)
    val store = env.store
    when (s.type) {
        ParamType.FLOAT, ParamType.INT, ParamType.ANGLE ->
            key(store.projectGeneration, store.composition?.id, store.primary, effectId, s.index) {
                FxNumberRow(env, effectId, s, d, label, selected == ParamKey(effectId, s.index, 0), onSelect, menu, disabledBy)
            }
        ParamType.POINT2D, ParamType.POINT3D ->
            key(store.projectGeneration, store.composition?.id, store.primary, effectId, s.index) {
                FxVectorRow(env, effectId, s, d, label, selected, onSelect, menu, disabledBy)
            }
        ParamType.BOOL -> FxBoolRow(env, effectId, s, label, selected?.let { it.effectId == effectId && it.param == s.index } == true, onSelect, menu, disabledBy)
        ParamType.ENUM -> FxChoiceRow(env, effectId, s, label, selected?.let { it.effectId == effectId && it.param == s.index } == true, onSelect, menu, disabledBy)
        ParamType.COLOR -> FxColorRow(env, effectId, s, label, selected?.let { it.effectId == effectId && it.param == s.index } == true, onSelect, menu, disabledBy)
        // Outra camada ("Camada de áudio"): escolhe na lista das camadas.
        ParamType.LAYER_REFERENCE -> EffectLayerRow(env, effectId, s, label, menu)
        ParamType.TEXTURE_REFERENCE ->
            if (typeId == effectTypeId("aurea.3d.part_texture")) PartTextureImageRow(env, effectId, label)
            else CubeLutImportRow(env, effectId)
        ParamType.CURVE -> ColorCurveEditor(env, effectId, s.index)
        // Degradê: o motor tem, o app ainda não edita — sem botão falso.
        else -> Row(Modifier.fillMaxWidth().height(FxRow.Small), verticalAlignment = Alignment.CenterVertically) {
            FxParamLabel(label, FxRow.Small - 6.dp, Modifier.padding(FxRow.Margin))
            Text(
                stringResource(R.string.panel_ainda_nao_editavel_app),
                modifier = Modifier.padding(horizontal = 8.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.FxTipText)),
            )
        }
    }
}

/** Escrita guardada: só na mesma camada/projeto/composição, destravada e finita. */
private class FxWriteGuard(env: PanelEnv) {
    private val store = env.store
    private val owner = store.primary
    private val generation = store.projectGeneration
    private val composition = store.composition?.id
    fun ok(): Boolean = owner != null && store.primary == owner && store.projectGeneration == generation &&
        store.composition?.id == composition && store.detail?.locked != true
}

/**
 * NÚMERO (Float / Int / Ângulo): slider (51) ou régua (57), pelo controle do
 * motor. Tudo na UNIDADE HUMANA (motor × escala); a escrita desfaz a escala e
 * prende na faixa DIGITADA. Tocar no valor: teclado.
 */
@Composable
private fun FxNumberRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    d: ParamDisplay,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey, Boolean) -> Unit,
    onMenu: () -> Unit,
    disabledBy: String?,
) {
    val store = env.store
    val guard = remember(store.primary, store.projectGeneration, store.composition?.id) { FxWriteGuard(env) }
    val value = rememberParamValue(env, effectId, s.index, 0)
    val look = rememberLook(env, effectId, s.index, 0)
    val expr = rememberExpr(env, effectId, s)
    val locked = rememberLocked(env)
    val enabled = s.enabled && !locked
    val control = FxParamMath.controlFor(s.control, s.type)
    val slider = control == ParamControl.SLIDER
    val lo = if (s.min.isFinite()) d.toDisplay(s.min) else Float.NEGATIVE_INFINITY
    val hi = if (s.max.isFinite()) d.toDisplay(s.max) else Float.POSITIVE_INFINITY
    val shownLo = min(lo, hi)
    val shownHi = max(lo, hi)
    val typedLo = if (s.typedLo.isFinite()) d.toDisplay(s.typedLo) else s.typedLo
    val typedHi = if (s.typedHi.isFinite()) d.toDisplay(s.typedHi) else s.typedHi
    val step = fxDisplayStep(s, d)
    val def = d.toDisplay(s.defaults.getOrElse(0) { 0f })
    val key = ParamKey(effectId, s.index, 0)
    fun write(display: Float) {
        if (!guard.ok() || !display.isFinite()) return
        val p = store.paramOf(effectId, s.index) ?: return
        val clamped = d.toEngine(display).coerceIn(s.typedLo, s.typedHi)
        store.setEffectParam(effectId, p, if (s.type == ParamType.INT) clamped.roundToInt().toFloat() else clamped, 0)
    }
    val format = { v: Float -> comUnidade(numeroPtBr(v, d.decimals), d.suffix) }
    val shown = d.toDisplay(value)
    val keypad = {
        onSelect(key, false)
        // "50%" no teclado continua sendo 50 % do fim da RÉGUA (não do teto digitado).
        env.openKeypad(
            KeypadRequest(label, shown, d.suffix, min(typedLo, typedHi), max(typedLo, typedHi), d.decimals, percentBase = shownHi) { write(it) },
        )
    }
    val rowH = if (slider) FxRow.Slider else FxRow.Ruler
    val boxH = rowH - FxRow.Margin * 2
    val off = disabledText(disabledBy)
    Row(
        Modifier
            .fillMaxWidth()
            .height(rowH)
            .testTag("effects.param.$effectId.${s.index}.0")
            .semantics { if (!enabled) stateDescription = off },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Slider: rótulo 75 × (51 − 6); régua: 75 × 45 (`effect_setting_spinner`).
        FxParamLabel(
            label, if (slider) boxH else 45.dp, Modifier.padding(FxRow.Margin),
            active = selected, enabled = enabled, keyframe = look, expression = expr,
            onClick = { onSelect(key, true) }, onLongClick = onMenu,
        )
        if (slider && shownLo.isFinite() && shownHi.isFinite() && shownHi > shownLo) {
            AureaSlider(
                value = shown,
                min = shownLo,
                max = shownHi,
                step = if (s.type == ParamType.INT) max(1f, step) else step,
                origin = d.toDisplay(s.origin),
                snap = def,
                onStart = { onSelect(key, false); store.beginGesture("ajustar $label") },
                onValue = ::write,
                onEnd = { store.endGesture() },
                modifier = Modifier.weight(1f).fillMaxHeight().padding(horizontal = 8.dp)
                    .testTag("effects.slider.$effectId.${s.index}"),
                enabled = enabled,
                a11yLabel = stringResource(R.string.om_fx_slider, label),
                a11yValue = format(shown),
            )
            FxValueBox(format(shown), boxH, Modifier.padding(FxRow.Margin), enabled = enabled, onClick = keypad)
        } else {
            val ruler = rememberFxRulerState()
            val now = ruler.live ?: shown
            val (turns, rest) = if (s.type == ParamType.ANGLE) FxParamMath.angleTurns(now) else 0 to now
            val rulerLabel = stringResource(R.string.om_fx_ruler, label)
            FxRuler(
                value = shown,
                step = step,
                min = shownLo,
                max = shownHi,
                onStart = { onSelect(key, false); store.beginGesture("ajustar $label") },
                onValue = ::write,
                onEnd = { store.endGesture() },
                modifier = Modifier.weight(1f).height(FxRow.Ruler - 12.dp)  // 0.3.4 painéis: a régua cresce com a linha
                    .testTag("effects.ruler.$effectId.${s.index}")
                    .semantics { contentDescription = rulerLabel; stateDescription = format(now) },
                state = ruler,
                enabled = enabled,
                snaps = listOf(def, 0f).filter { it.isFinite() && it >= shownLo && it <= shownHi }.distinct(),
            )
            FxValueBox(
                format(rest), 45.dp, Modifier.padding(FxRow.Margin), enabled = enabled,
                aux = if (turns != 0) "$turns× +" else null, onClick = keypad,
            )
        }
    }
}

/** Cor do eixo (X «eixo X», Y texto, Z «eixo Z»). */
internal fun fxAxisColor(component: Int): Color = when (component) {
    0 -> AureaColors.FxAxisX
    2 -> AureaColors.FxAxisZ
    else -> AureaColors.Text
}

/**
 * PONTO / XYZ / ORIENTAÇÃO (57): rótulo · régua do eixo escolhido (agulha na cor
 * do eixo) · caixas por eixo. 1º toque numa caixa escolhe o eixo; 2º toque no
 * eixo já escolhido abre o teclado; TOQUE LONGO zera aquele componente (Point
 * também — decisão do coordenador). Orientação: α/β/γ e rótulo de 42.
 */
@Composable
private fun FxVectorRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    d: ParamDisplay,
    label: String,
    selected: ParamKey?,
    onSelect: (ParamKey, Boolean) -> Unit,
    onMenu: () -> Unit,
    disabledBy: String?,
) {
    val store = env.store
    val guard = remember(store.primary, store.projectGeneration, store.composition?.id) { FxWriteGuard(env) }
    val control = FxParamMath.controlFor(s.control, s.type)
    val orientation = control == ParamControl.ORIENTATION
    val comps = s.components.coerceIn(2, 3)
    val own = selected?.takeIf { it.effectId == effectId && it.param == s.index }
    val axis = own?.component?.coerceIn(0, comps - 1) ?: 0
    val values = (0 until comps).map { d.toDisplay(rememberParamValue(env, effectId, s.index, it)) }
    val looks = (0 until comps).map { rememberLook(env, effectId, s.index, it) }
    val expr = rememberExpr(env, effectId, s)
    val locked = rememberLocked(env)
    val enabled = s.enabled && !locked
    val lo = d.toDisplay(s.min)
    val hi = d.toDisplay(s.max)
    val typedLo = d.toDisplay(s.typedLo)
    val typedHi = d.toDisplay(s.typedHi)
    val step = fxDisplayStep(s, d)
    val names = if (orientation) listOf("α", "β", "γ") else listOf("X", "Y", "Z")
    val a11yNames = if (orientation) {
        listOf(stringResource(R.string.om_fx_axis_pitch), stringResource(R.string.om_fx_axis_yaw), stringResource(R.string.om_fx_axis_roll))
    } else {
        names
    }
    val format = { v: Float -> comUnidade(numeroPtBr(v, d.decimals), d.suffix) }
    val compact = { v: Float -> com.aurea.aurea.ui.ds.compactPropertyNumber(v, d.decimals) }
    fun write(c: Int, display: Float) {
        if (!guard.ok() || !display.isFinite()) return
        val p = store.paramOf(effectId, s.index) ?: return
        store.setEffectParam(effectId, p, d.toEngine(display).coerceIn(s.typedLo, s.typedHi), c)
    }
    fun choose(c: Int, explicit: Boolean) = onSelect(ParamKey(effectId, s.index, c), explicit)
    fun keypad(c: Int) {
        choose(c, false)
        // P2-2: um teclado com TODOS os eixos (uma aba por eixo), foco no tocado.
        val defaults = (0 until comps).map { d.toDisplay(s.defaults.getOrElse(it) { 0f }) }
        val fields = (0 until comps).map {
            com.aurea.aurea.ui.ds.KeypadField(names[it], values[it], defaults[it], min(typedLo, typedHi), max(typedLo, typedHi))
        }
        env.openKeypad(
            KeypadRequest(
                label, values[c], d.suffix, min(typedLo, typedHi), max(typedLo, typedHi), d.decimals,
                percentBase = max(lo, hi),
                fields = fields,
                focus = c,
                onConfirmFields = { edits ->
                    val p = store.paramOf(effectId, s.index)
                    if (guard.ok() && p != null && edits.any { it != null }) {
                        // O vetor inteiro de uma vez (writeParamVector segue a cópia
                        // entre os componentes): dois `setEffectParam` seguidos
                        // releriam o vetor velho.
                        val next = p.value.copyOf()
                        edits.forEachIndexed { i, v ->
                            if (v != null && v.isFinite() && i < next.size) next[i] = d.toEngine(v).coerceIn(s.typedLo, s.typedHi)
                        }
                        store.beginGesture("ajustar $label")
                        try {
                            store.writeParamVector(effectId, p, next)
                        } finally {
                            store.endGesture()
                        }
                    }
                },
            ) { write(c, it) },
        )
    }
    fun zero(c: Int) {
        if (!guard.ok()) return
        choose(c, false)
        store.beginGesture("zerar ${names[c]}")
        try {
            write(c, 0f)
        } finally {
            store.endGesture()
        }
    }
    // Uma régua por eixo: trocar de eixo encerra o gesto (e o arremesso) do anterior.
    val ruler = remember(axis) { FxRulerState() }
    val off = disabledText(disabledBy)
    val tag = "effects.vector.$effectId.${s.index}"
    Row(
        Modifier
            .fillMaxWidth()
            .height(FxRow.Ruler)
            .testTag(tag)
            .semantics { if (!enabled) stateDescription = off },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        FxParamLabel(
            label, if (orientation) 42.dp else 45.dp, Modifier.padding(FxRow.Margin),
            active = own != null, enabled = enabled, keyframe = looks.getOrElse(axis) { KeyframeLook.None }, expression = expr,
            onClick = { choose(axis, true) }, onLongClick = onMenu,
        )
        val now = ruler.live ?: values[axis]
        FxRuler(
            value = values[axis],
            step = step,
            min = min(lo, hi),
            max = max(lo, hi),
            onStart = { choose(axis, false); store.beginGesture("ajustar $label") },
            onValue = { write(axis, it) },
            onEnd = { store.endGesture() },
            modifier = Modifier.weight(1f).height(FxRow.Ruler - 12.dp).testTag("$tag.ruler")
                .semantics { contentDescription = "$label · ${a11yNames[axis]}"; stateDescription = format(now) },
            state = ruler,
            enabled = enabled,
            needle = fxAxisColor(axis),
            snaps = listOf(0f).filter { it >= min(lo, hi) && it <= max(lo, hi) },
        )
        // Números e letras dos eixos sempre da esquerda para a direita.
        CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
            if (comps == 2 && control == ParamControl.POINT) {
                Column(Modifier.width(FxRow.AxisColumn + 18.dp).fillMaxHeight().padding(end = FxRow.Margin)) {
                    (0 until 2).forEach { c ->
                        val v = if (c == axis && ruler.live != null) now else values[c]
                        Row(
                            Modifier.weight(1f).fillMaxWidth()
                                .padding(top = if (c == 0) 7.dp else 1.dp, bottom = if (c == 0) 1.dp else 7.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                "${names[c]}:",
                                modifier = Modifier.width(18.dp),
                                style = AureaType.Base.merge(TextStyle(fontSize = FxRow.SmallText, color = AureaColors.FxAxisLabel)),
                            )
                            FxAxisBox(
                                text = compact(v),
                                color = fxAxisColor(c),
                                active = own != null && c == axis,
                                enabled = enabled,
                                letter = null,
                                modifier = Modifier.weight(1f).fillMaxHeight()
                                    .testTag("effects.axis.$effectId.${s.index}.$c"),
                                a11yLabel = "$label · ${a11yNames[c]}",
                                a11yValue = format(v),
                                resetLabel = stringResource(R.string.om_fx_axis_reset, a11yNames[c]),
                                onClick = { if (own != null && c == axis) keypad(c) else choose(c, true) },
                                onLongClick = { zero(c) },
                            )
                        }
                    }
                }
            } else {
                Row(
                    Modifier.padding(end = FxRow.Margin).height(FxRow.Ruler - 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    (0 until comps).forEach { c ->
                        val v = if (c == axis && ruler.live != null) now else values[c]
                        FxAxisBox(
                            text = compact(v),
                            color = fxAxisColor(c),
                            active = own != null && c == axis,
                            enabled = enabled,
                            letter = names[c],
                            modifier = Modifier.width(FxRow.AxisBox).fillMaxHeight()
                                .testTag("effects.axis.$effectId.${s.index}.$c"),
                            a11yLabel = "$label · ${a11yNames[c]}",
                            a11yValue = format(v),
                            resetLabel = stringResource(R.string.om_fx_axis_reset, a11yNames[c]),
                            onClick = { if (own != null && c == axis) keypad(c) else choose(c, true) },
                            onLongClick = { zero(c) },
                        )
                    }
                }
            }
        }
    }
}

/**
 * Caixa de UM eixo: fundo «superfície baixa» (ativa/pressionada «afundado»),
 * raio 4, número na cor do eixo; [letter] (XYZ / α β γ) embaixo, 10 sp.
 */
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
private fun FxAxisBox(
    text: String,
    color: Color,
    active: Boolean,
    enabled: Boolean,
    letter: String?,
    modifier: Modifier,
    a11yLabel: String,
    a11yValue: String,
    resetLabel: String,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val hint = stringResource(R.string.om_fx_axis_hint)
    Box(
        modifier
            .clip(RoundedCornerShape(FxRow.Radius))
            .background(if ((active || pressed) && enabled) AureaColors.FxSunken else AureaColors.FxLowSurface)
            .semantics(mergeDescendants = true) {
                contentDescription = a11yLabel
                stateDescription = if (active) "$a11yValue · $hint" else a11yValue
                role = Role.Button
                selected = active
                if (!enabled) disabled()
                customActions = listOf(CustomAccessibilityAction(resetLabel) { onLongClick(); true })
            }
            .then(
                if (enabled) {
                    Modifier.combinedClickable(
                        interactionSource = interaction,
                        indication = null,
                        onLongClick = onLongClick,
                        onClick = onClick,
                    )
                } else {
                    Modifier
                },
            )
            .padding(horizontal = 3.dp),
        contentAlignment = Alignment.Center,
    ) {
        BasicText(
            text = text,
            maxLines = 1,
            autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = FxRow.ValueText, stepSize = 0.5.sp),
            modifier = Modifier.padding(bottom = if (letter != null) 10.dp else 0.dp),
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = FxRow.ValueText,
                    color = if (enabled) color else AureaColors.FxDisabledText,
                    textAlign = TextAlign.Center,
                    fontFeatureSettings = "tnum",
                    textDirection = TextDirection.Ltr,
                ),
            ),
        )
        if (letter != null) {
            Text(
                letter,
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 1.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = FxRow.SmallText, color = AureaColors.FxAxisLabel)),
            )
        }
    }
}

/**
 * INTERRUPTOR (38): padding 8 · rótulo em TEXTO SIMPLES (sem chip e sem ◆, como
 * a referência) · interruptor. Tocar no texto escolhe o parâmetro (◆± e Curva
 * miram nele); segurar abre o menu do parâmetro (extra do AUREA).
 */
@Composable
private fun FxBoolRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey, Boolean) -> Unit,
    onMenu: () -> Unit,
    disabledBy: String?,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val locked = rememberLocked(env)
    val enabled = s.enabled && !locked
    val off = disabledText(disabledBy)
    Row(
        Modifier
            .fillMaxWidth()
            .height(FxRow.Small)
            .padding(horizontal = 8.dp)
            .semantics { if (!enabled) stateDescription = off },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .weight(1f)
                .tocavel(enabled = enabled, shrink = 1f, onLongClick = onMenu) { onSelect(ParamKey(effectId, s.index, 0), true) },
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 14.sp,
                    color = when {
                        !enabled -> AureaColors.FxDisabledText
                        selected -> AureaColors.Accent
                        else -> AureaColors.Text
                    },
                ),
            ),
        )
        AureaToggle(
            checked = value >= 0.5f,
            enabled = enabled,
            modifier = Modifier.testTag("effects.toggle.$effectId.${s.index}").semantics {
                contentDescription = label
                role = Role.Switch
                toggleableState = if (value >= 0.5f) ToggleableState.On else ToggleableState.Off
            },
            onCheckedChange = { on ->
                onSelect(ParamKey(effectId, s.index, 0), false)
                store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, if (on) 1f else 0f) }
            },
        )
    }
}

/**
 * ESCOLHA (38): rótulo de 10 sp (até 75, margem 15) · botões segmentados de 27,5
 * (estilo do motor `ENUM_SEGMENTED`) ou caixa suspensa (`ENUM_DROPDOWN`) que abre
 * o menu ancorado. Índice arredondado e preso.
 */
@Composable
private fun FxChoiceRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey, Boolean) -> Unit,
    onMenu: () -> Unit,
    disabledBy: String?,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val locked = rememberLocked(env)
    val enabled = s.enabled && !locked
    val options = s.enumLabels.ifEmpty { List((s.max - s.min).roundToInt().coerceAtLeast(0) + 1) { "${it + 1}" } }
    val current = value.roundToInt().coerceIn(0, max(0, options.lastIndex))
    val pick: (Int) -> Unit = { i: Int ->
        onSelect(ParamKey(effectId, s.index, 0), false)
        store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, i.toFloat()) }
    }
    FxChoiceLayout(
        label = label,
        options = options,
        selected = current,
        segmented = s.enumStyle == ParamControl.ENUM_SEGMENTED || (s.enumStyle == 0 && options.size <= 4 && options.all { it.length <= 10 }),
        enabled = enabled,
        labelActive = selected,
        tag = "effects.choice.$effectId.${s.index}",
        disabledState = if (enabled) null else disabledText(disabledBy),
        onLabel = { onSelect(ParamKey(effectId, s.index, 0), true) },
        onLabelLong = onMenu,
        onPick = pick,
    )
}

/**
 * A montagem da linha de escolha (também usada pela "Variação" do efeito, que não
 * tem opção vigente: [selected] < 0).
 */
@Composable
internal fun FxChoiceLayout(
    label: String,
    options: List<String>,
    selected: Int,
    segmented: Boolean,
    enabled: Boolean,
    labelActive: Boolean,
    tag: String,
    disabledState: String? = null,
    onLabel: (() -> Unit)? = null,
    onLabelLong: (() -> Unit)? = null,
    onPick: (Int) -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(FxRow.Small)
            .padding(start = 8.dp, end = FxRow.Margin)
            .semantics { if (disabledState != null) stateDescription = disabledState },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .widthIn(max = FxRow.Label)
                .then(
                    if (onLabel != null) Modifier.tocavel(enabled = enabled, shrink = 1f, onLongClick = onLabelLong, onClick = onLabel)
                    else Modifier,
                ),
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = FxRow.LabelText,  // 0.3.4 painéis: antes 10
                    color = when {
                        !enabled -> AureaColors.FxDisabledText
                        labelActive -> AureaColors.Accent
                        else -> AureaColors.Text
                    },
                ),
            ),
        )
        Spacer(Modifier.width(15.dp))
        if (segmented) {
            FxSegmented(options, selected, onPick, Modifier.weight(1f), enabled = enabled, tag = tag)
        } else {
            FxDropdownBox(
                text = options.getOrElse(selected) { "—" },
                entries = options.mapIndexed { i, o ->
                    MenuEntry.Item(o, selected = i == selected, tag = "$tag.$i") { onPick(i) }
                },
                modifier = Modifier.weight(1f),
                enabled = enabled,
                tag = tag,
                contentDescription = label,
            )
        }
    }
}

/** COR (38): rótulo 75 · "R G B" 10 sp em «texto de cor» · amostra 24,5 (alvo 44) → seletor. */
@Composable
private fun FxColorRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey, Boolean) -> Unit,
    onMenu: () -> Unit,
    disabledBy: String?,
) {
    val store = env.store
    // Derivado como `Color` (igualdade por valor): um FloatArray novo a cada
    // leitura faria a linha recompor a cada quadro da reprodução.
    val color by remember(store, effectId, s.index) {
        derivedStateOf { rgbaColor(engineToDisplay(store.paramOf(effectId, s.index)?.value ?: floatArrayOf(1f, 1f, 1f, 1f))) }
    }
    val look = rememberLook(env, effectId, s.index, null)
    val expr = rememberExpr(env, effectId, s)
    val locked = rememberLocked(env)
    val enabled = s.enabled && !locked
    val pick = {
        onSelect(ParamKey(effectId, s.index, 0), false)
        store.beginGesture("cor $label")
        env.openColor(
            ColorRequest(
                initial = floatArrayOf(color.red, color.green, color.blue, color.alpha),
                onChange = { r, g, b, a ->
                    store.paramOf(effectId, s.index)?.let { store.writeParamVector(effectId, it, displayToEngine(r, g, b, a)) }
                },
                onDone = { store.endGesture() },
            ),
        )
    }
    val off = disabledText(disabledBy)
    Row(
        Modifier
            .fillMaxWidth()
            .height(FxRow.Small)
            .semantics { if (!enabled) stateDescription = off },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        FxParamLabel(
            label, FxRow.Small - FxRow.Margin * 2, Modifier.padding(FxRow.Margin),
            active = selected, enabled = enabled, keyframe = look, expression = expr,
            onClick = { onSelect(ParamKey(effectId, s.index, 0), true) }, onLongClick = onMenu,
        )
        Text(
            "${(color.red * 255).roundToInt()} ${(color.green * 255).roundToInt()} ${(color.blue * 255).roundToInt()}",
            maxLines = 1,
            modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
            style = AureaType.Base.merge(
                TextStyle(fontSize = FxRow.SmallText, color = AureaColors.FxColorText, fontFeatureSettings = "tnum", textDirection = TextDirection.Ltr),
            ),
        )
        Box(
            Modifier
                .size(44.dp, FxRow.Small)
                .semantics { contentDescription = label; role = Role.Button; if (!enabled) disabled() }
                .testTag("effects.color.$effectId.${s.index}")
                .tocavel(enabled = enabled, shrink = 1f, onClick = pick),
            contentAlignment = Alignment.CenterEnd,
        ) {
            Box(
                Modifier
                    .padding(end = 6.dp)
                    .size(30.dp)  // 0.3.4 painéis: antes 24,5
                    .clip(RoundedCornerShape(3.dp))
                    .background(color)
                    .border(0.5.dp, AureaColors.FxLowSurface, RoundedCornerShape(3.dp)),
            )
        }
    }
}

/** "Trocar textura 3D": a imagem da galeria que cobre a parte (nome + botão grande). */
@Composable
internal fun PartTextureImageRow(env: PanelEnv, effect: Int, label: String) {
    val store = env.store
    store.primary ?: return
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) store.setEffectImage(uri, effect)
    }
    val name = remember(store.detail, store.curveRevision, store.effects) { store.effectImageName(effect) }
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp, horizontal = FxRow.Margin)) {
        Text(
            "$label · " + name.ifBlank { stringResource(R.string.fx3d_image_none) }, maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        )
        Box(
            Modifier.fillMaxWidth().height(44.dp).testTag("effect.part_texture.pick")
                .clip(RoundedCornerShape(5.dp)).background(AureaColors.FxSunken)
                .tocavel(onClick = { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }),
            contentAlignment = Alignment.Center,
        ) {
            Text(stringResource(if (name.isBlank()) R.string.fx3d_image_pick else R.string.shape3d_image_change), color = AureaColors.Text)
        }
    }
}

@Composable
internal fun CubeLutImportRow(env: PanelEnv, effect: Int) {
    val store = env.store
    val layer = store.primary ?: return
    var target by remember { mutableStateOf<Pair<Long, Int>?>(null) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        target?.let { (id, fx) -> if (uri != null) store.importColorLut(uri, id, fx) }
        target = null
    }
    val name = store.colorLutName(layer, effect)
    // O mesmo importador serve o LUT (.cube) e a paleta do Palettize.
    val palette = store.effects.firstOrNull { it.effectId == effect }?.typeId == effectTypeId("aurea.color.palettize")
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp, horizontal = FxRow.Margin)) {
        if (name.isNotBlank()) {
            Text(
                name, maxLines = 1, overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
        Box(
            Modifier.fillMaxWidth().height(44.dp).testTag("effect.lut.import")
                .clip(RoundedCornerShape(5.dp)).background(AureaColors.FxSunken)
                .tocavel(onClick = { target = layer to effect; picker.launch(arrayOf("*/*")) }),
            contentAlignment = Alignment.Center,
        ) {
            Text(stringResource(if (palette) R.string.palette_import else R.string.lut_import), color = AureaColors.Text)
        }
    }
}

/** Uma caixa de aviso dentro do corpo (efeito sem ajustes, IA local…). */
@Composable
internal fun FxBodyNotice(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        modifier = modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 8.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.FxTipText)),
    )
}

/** Área tocável mínima de 44 em volta de um desenho menor (alvo de toque). */
@Composable
internal fun FxIconTarget(
    width: Dp,
    height: Dp,
    label: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
    content: @Composable () -> Unit,
) {
    Box(
        modifier
            .size(width, height)
            .semantics { contentDescription = label; role = Role.Button; if (!enabled) disabled() }
            .tocavel(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { content() }
}

@Suppress("unused")
private fun Modifier.fillIf(full: Boolean): Modifier = if (full) this.fillMaxSize() else this
