package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.EffectCard
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.PropertyRow
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.ds.casasAutomaticas
import com.aurea.aurea.ui.ds.comUnidade
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.displayToEngine
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Qual linha está escolhida: o efeito, o parâmetro e o componente (ponto = X/Y). */
@Immutable
internal data class ParamKey(val effectId: Int, val param: Int, val component: Int)

/**
 * A FORMA de um parâmetro (sem o valor): o que o cartão precisa para montar as
 * linhas. Muda só quando o efeito muda — o valor que anda com o cabeçote fica de
 * fora, e por isso o cartão não recompõe a cada quadro da reprodução.
 */
@Immutable
internal data class ParamSlot(
    val index: Int,
    val type: Int,
    val flags: Int,
    val min: Float,
    val max: Float,
    val label: String,
    val unit: String,
    val enumLabels: List<String>,
) {
    val hidden get() = (flags and ParamType.FLAG_HIDDEN) != 0
    val animatable get() = (flags and ParamType.FLAG_ANIMATABLE) != 0 && ParamType.componentCount(type) > 0
    val components get() = ParamType.componentCount(type)

    /** Casas [A]: inteiro 0, ângulo 1, senão pela faixa (`_casasAutomaticas`). */
    val decimals: Int
        get() = when (type) {
            ParamType.INT -> 0
            ParamType.ANGLE -> 1
            else -> casasAutomaticas(min, max)
        }

    /**
     * Sensibilidade [A]: `(max − min) / 500` por dp (atravessar a régua ≈ a faixa
     * inteira em poucos arrastos). O motor não publica o `dragStep` da A.01, então:
     * ângulo = 0,5 °/dp (o `dragStep` da Fase do Motion Tile) e faixas enormes são
     * presas em 2 unidades/dp; sem faixa, 0,5/dp.
     */
    val unitsPerDp: Float
        get() {
            if (type == ParamType.ANGLE) return 0.5f
            val r = max - min
            return if (r.isFinite() && r > 0f) min(r / 500f, 2f) else 0.5f
        }

    companion object {
        fun of(p: EffectParam) = ParamSlot(p.index, p.type, p.flags, p.min, p.max, p.label, p.unit, p.enumLabels)
    }
}

/**
 * Rótulo de cada eixo de um ponto. "Centro do mosaico" vira "Centro X" / "Centro Y"
 * (como a ficha da A.01 nomeava os eixos); sem "do/da", o rótulo inteiro + eixo.
 */
private fun axisLabel(label: String, component: Int): String {
    val axis = "XYZ".getOrElse(component) { 'X' }
    val cut = listOf(" do ", " da ", " dos ", " das ").map { label.indexOf(it) }.filter { it > 0 }.minOrNull()
    return "${if (cut != null) label.substring(0, cut) else label} $axis"
}

/**
 * O PAINEL "EFEITOS" [A] (t2): trilho esquerdo 46 (‹ · ◇ · curva) + pilha de
 * cartões (lista com padding 2/8/12/16) + "Adicionar efeito".
 *
 * - Acordeão: UM cartão aberto; ao entrar, o primeiro; efeito recém-adicionado
 *   abre sozinho.
 * - A linha ESCOLHIDA (chip aceso) é o alvo do trilho: o ◇ crava/tira a marca
 *   daquele parâmetro no cabeçote (keyframe por linha) e a curva abre o trecho
 *   dele. Abrir um cartão escolhe a primeira linha, para o ◇ nunca ficar sem alvo.
 */
@Composable
internal fun EffectsPanel(env: PanelEnv) {
    val store = env.store
    val layerId = store.primary
    val effects = store.effects
    // `detail` muda a cada quadro da reprodução; o painel só quer o TIPO.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }

    var openId by remember(layerId) { mutableStateOf(effects.firstOrNull()?.effectId) }
    var known by remember(layerId) { mutableStateOf(effects.map { it.effectId }.toSet()) }
    var selected by remember(layerId) { mutableStateOf<ParamKey?>(null) }
    var menuFor by remember { mutableStateOf<LayerEffect?>(null) }

    // Efeito novo abre sozinho; o aberto que sumiu fecha. Fora da composição
    // (bug B-09: estado mutado durante o build).
    LaunchedEffect(effects) {
        val ids = effects.map { it.effectId }.toSet()
        val added = ids - known
        if (added.isNotEmpty()) openId = effects.last { it.effectId in added }.effectId
        else if (openId != null && openId !in ids) openId = null
        known = ids
    }
    // O cartão aberto escolhe a sua primeira linha (se a escolhida não é dele).
    LaunchedEffect(openId, effects) {
        val id = openId
        if (id == null) {
            selected = null
            return@LaunchedEffect
        }
        if (selected?.effectId == id) return@LaunchedEffect
        val first = store.effectParams[id]?.firstOrNull { !it.hidden && ParamType.componentCount(it.type) > 0 }
        selected = first?.let { ParamKey(id, it.index, 0) }
    }

    // O trilho só precisa do ESTADO do losango — derivado, não do valor que anda.
    val railLook by remember(store) {
        derivedStateOf { selected?.let { effectLook(store, it.effectId, it.param) } ?: KeyframeLook.None }
    }
    val railAnimatable by remember(store) {
        derivedStateOf {
            selected?.let { k -> store.paramOf(k.effectId, k.param)?.let { ParamSlot.of(it).animatable } } ?: false
        }
    }
    val curveTrackReady by remember(store) {
        derivedStateOf { selected?.let { k -> store.primaryKeys().effectTrack(k.effectId, k.param, k.component).size >= 2 } ?: false }
    }

    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = railLook,
            onKeyframe = if (railAnimatable) {
                {
                    // Lido NO TOQUE: o parâmetro e o cabeçote de agora.
                    selected?.let { k -> store.paramOf(k.effectId, k.param)?.let { store.toggleEffectKeyframe(k.effectId, it) } }
                }
            } else {
                null
            },
            curveAnimated = railLook != KeyframeLook.None,
            onCurve = if (curveTrackReady) {
                {
                    val k = selected
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (k != null && layer != null && t != null) {
                        store.primaryKeys().effectTrack(k.effectId, k.param, k.component).segmentStart(t)?.let { key ->
                            store.selectKeyframe(layer, key)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
        )
        LazyColumn(
            Modifier.weight(1f).fillMaxHeight(),
            contentPadding = PaddingValues(start = 2.dp, top = 8.dp, end = 12.dp, bottom = 16.dp),
        ) {
            items(effects, key = { it.effectId }) { e ->
                EffectCardItem(
                    env = env,
                    effect = e,
                    expanded = openId == e.effectId,
                    selected = selected?.takeIf { it.effectId == e.effectId },
                    onToggle = { openId = if (openId == e.effectId) null else e.effectId },
                    onSelect = { selected = it },
                    onMenu = { menuFor = e },
                    onRemove = { store.removeEffect(e.effectId) },
                )
            }
            item(key = "rodape") { EffectsFooter(env, kind) }
        }
    }

    menuFor?.let { e ->
        val index = effects.indexOfFirst { it.effectId == e.effectId }
        val actions = if (!e.known) {
            // Efeito que saiu do catálogo: só entender e tirar (A.01).
            listOf(SheetAction("Remover efeito", destructive = true) { store.removeEffect(e.effectId) })
        } else {
            buildList {
                add(SheetAction(if (e.enabled) "Desativar efeito" else "Ativar efeito") { store.setEffectEnabled(e.effectId, !e.enabled) })
                add(SheetAction("Duplicar") { store.comingSoon("Duplicar efeito") })
                if (index > 0) add(SheetAction("Mover para cima") { store.reorderEffect(e.effectId, index - 1) })
                if (index in 0 until effects.lastIndex) add(SheetAction("Mover para baixo") { store.reorderEffect(e.effectId, index + 1) })
                add(SheetAction("Resetar") { resetEffect(env, e.effectId) })
                add(SheetAction("Salvar como preset") { store.comingSoon("Presets de efeito") })
                add(SheetAction("Meus presets") { store.comingSoon("Presets de efeito") })
                add(SheetAction("Como usar este efeito") { store.comingSoon("Guia dos efeitos") })
            }
        }
        AureaActionSheet(
            title = if (e.known) e.name else "Efeito removido",
            message = if (e.known) null else "Este efeito saiu do Aurea e não desenha mais nada. Ele ficou guardado aqui para você decidir — o resto da camada está intacto.",
            actions = actions,
            cancelLabel = if (e.known) "Cancelar" else "Manter",
            onDismiss = { menuFor = null },
        )
    }
}

/**
 * RESETAR: todos os parâmetros ao padrão NO CABEÇOTE, num passo de desfazer só
 * (parâmetro animado ganha a marca com o padrão, como qualquer edição).
 */
private fun resetEffect(env: PanelEnv, effectId: Int) {
    val store = env.store
    val params = store.effectParams[effectId] ?: return
    store.beginGesture("resetar efeito")
    try {
        params.filter { !it.hidden && ParamType.componentCount(it.type) > 0 }.forEach { p ->
            store.writeParamVector(effectId, p, p.defaultValue)
        }
    } finally {
        store.endGesture()
    }
}

/** Um cartão da pilha. Recolhido não compõe o corpo (nem escuta o cabeçote). */
@Composable
private fun EffectCardItem(
    env: PanelEnv,
    effect: LayerEffect,
    expanded: Boolean,
    selected: ParamKey?,
    onToggle: () -> Unit,
    onSelect: (ParamKey) -> Unit,
    onMenu: () -> Unit,
    onRemove: () -> Unit,
) {
    val store = env.store
    EffectCard(
        name = effect.name,
        enabled = effect.enabled,
        expanded = expanded,
        onToggleExpanded = onToggle,
        onMenu = onMenu,
        onRemove = onRemove,
    ) {
        val id = effect.effectId
        val slots by remember(store, id) {
            derivedStateOf { store.effectParams[id]?.map { ParamSlot.of(it) } ?: emptyList() }
        }
        if (!effect.known) {
            PanelNotice("Este efeito saiu do catálogo. Ele não desenha mais; apague pelo ⋯.")
            return@EffectCard
        }
        val visible = slots.filter { !it.hidden }
        if (visible.isEmpty()) {
            PanelNotice("Este efeito não tem ajustes.")
            return@EffectCard
        }
        visible.forEach { s ->
            when (s.type) {
                ParamType.FLOAT, ParamType.INT, ParamType.ANGLE ->
                    EffectNumberRow(env, id, s, 0, s.label, selected == ParamKey(id, s.index, 0), onSelect)
                ParamType.POINT2D, ParamType.POINT3D -> repeat(s.components) { c ->
                    EffectNumberRow(env, id, s, c, axisLabel(s.label, c), selected == ParamKey(id, s.index, c), onSelect)
                }
                ParamType.BOOL -> EffectToggleRow(env, id, s, selected?.param == s.index, onSelect)
                ParamType.ENUM -> EffectChoiceRow(env, id, s, selected?.param == s.index, onSelect)
                ParamType.COLOR -> EffectColorRow(env, id, s, selected?.param == s.index, onSelect)
                else -> PropertyCustomRow(
                    label = s.label,
                    selected = false,
                    onSelect = { store.comingSoon(s.label) },
                ) {
                    Text(
                        "Em breve",
                        modifier = Modifier.tocavel(shrink = 1f) { store.comingSoon(s.label) },
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                    )
                }
            }
        }
    }
}

/** Valor de UM componente do parâmetro, derivado: a linha só recompõe quando ELE muda. */
@Composable
private fun rememberParamValue(env: PanelEnv, effectId: Int, index: Int, component: Int): Float {
    val store = env.store
    val v by remember(store, effectId, index, component) {
        derivedStateOf { store.paramOf(effectId, index)?.value?.getOrNull(component) ?: 0f }
    }
    return v
}

@Composable
private fun rememberLook(env: PanelEnv, effectId: Int, index: Int, component: Int?): KeyframeLook {
    val store = env.store
    val look by remember(store, effectId, index, component) { derivedStateOf { effectLook(store, effectId, index, component) } }
    return look
}

/** Número / ângulo / eixo de ponto: a linha [A] com régua e caixa. */
@Composable
private fun EffectNumberRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    component: Int,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey) -> Unit,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, component)
    val look = rememberLook(env, effectId, s.index, component)
    val lo = if (s.min.isFinite()) s.min else Float.NEGATIVE_INFINITY
    val hi = if (s.max.isFinite()) s.max else Float.POSITIVE_INFINITY
    fun write(v: Float) {
        val p = store.paramOf(effectId, s.index) ?: return
        val clamped = v.coerceIn(lo, hi)
        store.setEffectParam(effectId, p, if (s.type == ParamType.INT) clamped.roundToInt().toFloat() else clamped, component)
    }
    PropertyRow(
        label = label,
        value = value,
        unitsPerDp = s.unitsPerDp,
        min = lo,
        max = hi,
        format = { comUnidade(numeroPtBr(it, s.decimals), s.unit) },
        selected = selected,
        keyframe = look,
        onSelect = { onSelect(ParamKey(effectId, s.index, component)) },
        onGestureStart = { store.beginGesture("ajustar $label") },
        onValue = ::write,
        onGestureEnd = { store.endGesture() },
        onTapValue = {
            env.openKeypad(KeypadRequest(label, value, s.unit, lo, hi, s.decimals) { write(it) })
        },
    )
}

/** Liga/desliga: valor ≥ 0,5 = ligado; escreve 1/0 num passo. */
@Composable
private fun EffectToggleRow(env: PanelEnv, effectId: Int, s: ParamSlot, selected: Boolean, onSelect: (ParamKey) -> Unit) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val look = rememberLook(env, effectId, s.index, null)
    PropertyCustomRow(s.label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            AureaToggle(
                checked = value >= 0.5f,
                onCheckedChange = { on -> store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, if (on) 1f else 0f) } },
            )
            Spacer(Modifier.width(4.dp))
        }
    }
}

/** Escolha: chips à vista (a linha cresce se quebrar). Índice arredondado e preso. */
@Composable
private fun EffectChoiceRow(env: PanelEnv, effectId: Int, s: ParamSlot, selected: Boolean, onSelect: (ParamKey) -> Unit) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val look = rememberLook(env, effectId, s.index, null)
    val options = s.enumLabels.ifEmpty { List((s.max - s.min).roundToInt().coerceAtLeast(0) + 1) { "${it + 1}" } }
    PropertyCustomRow(s.label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look) {
        ChoiceChips(
            options = options,
            selected = value.roundToInt().coerceIn(0, max(0, options.lastIndex)),
            onSelect = { i -> store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, i.toFloat()) } },
        )
    }
}

/** Cor: "R G B" + amostra → seletor; a folha inteira = UM passo de desfazer. */
@Composable
private fun EffectColorRow(env: PanelEnv, effectId: Int, s: ParamSlot, selected: Boolean, onSelect: (ParamKey) -> Unit) {
    val store = env.store
    // Derivado como `Color` (igualdade por valor): um FloatArray novo a cada
    // leitura faria a linha recompor a cada quadro da reprodução.
    val color by remember(store, effectId, s.index) {
        derivedStateOf { rgbaColor(engineToDisplay(store.paramOf(effectId, s.index)?.value ?: floatArrayOf(1f, 1f, 1f, 1f))) }
    }
    val look = rememberLook(env, effectId, s.index, null)
    PropertyCustomRow(s.label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
            Text(
                "${(color.red * 255).roundToInt()} ${(color.green * 255).roundToInt()} ${(color.blue * 255).roundToInt()}",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontFeatureSettings = "tnum")),
            )
            Spacer(Modifier.width(10.dp))
            ColorWell(color) {
                onSelect(ParamKey(effectId, s.index, 0))
                store.beginGesture("cor ${s.label}")
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
            Spacer(Modifier.width(4.dp))
        }
    }
}

/**
 * O RODAPÉ [A] (`_Rodape`): intensidade da camada de ajuste (é a opacidade),
 * "Efeitos de áudio" em vídeo, e o botão largo "+ Adicionar efeito".
 */
@Composable
private fun EffectsFooter(env: PanelEnv, kind: Int) {
    val store = env.store
    Column(Modifier.fillMaxWidth()) {
        if (kind == LayerType.Adjustment.kind) AdjustmentIntensity(env)
        if (kind == LayerType.Video.kind) {
            Row(
                Modifier
                    .padding(vertical = 4.dp)
                    .tocavel { store.comingSoon("Efeitos de áudio") }
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.MusicNote, 18.dp, AureaColors.Accent)
                Spacer(Modifier.width(8.dp))
                Text("Efeitos de áudio", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W500, color = AureaColors.Accent)))
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Chip)
                .tocavel(haptic = true, onClick = env.onOpenEffectsBrowser)
                .padding(vertical = 13.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 17.dp, AureaColors.Action)
            Spacer(Modifier.width(8.dp))
            Text("Adicionar efeito", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Action)))
        }
    }
}

/** "Intensidade da camada de ajuste": a opacidade da camada, 0–100 %, com o losango dela. */
@Composable
private fun AdjustmentIntensity(env: PanelEnv) {
    val store = env.store
    val opacity by remember(store) { derivedStateOf { (store.detail?.opacity ?: 1f) * 100f } }
    val look by remember(store) { derivedStateOf { transformLook(store.detail, intArrayOf(TrackProperty.OPACITY)) } }
    Column(
        Modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip.copy(alpha = 0.55f))
            .padding(start = 8.dp, top = 8.dp, end = 8.dp, bottom = 4.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Intensidade da camada de ajuste", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Box(
                Modifier.size(36.dp).tocavel { store.toggleTransformKeyframe(intArrayOf(TrackProperty.OPACITY)) },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(
                    if (look == KeyframeLook.KeyHere) CupertinoGlyph.RhombusFill else CupertinoGlyph.Rhombus,
                    16.dp,
                    if (look == KeyframeLook.None) AureaColors.Muted else AureaColors.Keyframe,
                )
            }
        }
        OpacityRow(env, opacity, selected = true)
    }
}

/** A linha de opacidade (0–100 %, casas 0) — Efeitos (ajuste) e Mesclagem. */
@Composable
internal fun OpacityRow(env: PanelEnv, opacity: Float, selected: Boolean, keyframe: KeyframeLook = KeyframeLook.None) {
    val store = env.store
    PropertyRow(
        label = "Opacidade",
        value = opacity,
        unitsPerDp = 0.35f,
        min = 0f,
        max = 100f,
        format = { "${numeroPtBr(it, 0)}%" },
        selected = selected,
        keyframe = keyframe,
        onSelect = {},
        onGestureStart = { store.beginGesture("opacidade") },
        onValue = { store.setTransform(TrackProperty.OPACITY, it.coerceIn(0f, 100f) / 100f) },
        onGestureEnd = { store.endGesture() },
        onTapValue = {
            env.openKeypad(KeypadRequest("Opacidade", opacity, "%", 0f, 100f, 0) { store.setTransform(TrackProperty.OPACITY, it / 100f) })
        },
    )
}
