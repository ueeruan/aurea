package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.ExpressionLook
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
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
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ChevronLeft
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AdvancedToggle
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.EffectStackCard
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.PropertyRow
import com.aurea.aurea.ui.ds.SheetAction
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

    /**
     * Sensibilidade [A], na unidade do MOTOR: `(max − min) / 500` por dp
     * (atravessar a régua ≈ a faixa inteira em poucos arrastos). Ângulo = 0,5 °/dp;
     * faixas enormes presas em 2 unidades/dp; sem faixa, 0,5/dp.
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

/** O tipo (`typeId`) do efeito [effectId] da camada principal (0 = sumiu). */
private fun EditorStore.typeOf(effectId: Int): Int = effects.firstOrNull { it.effectId == effectId }?.typeId ?: 0

/** Rótulo humano da linha (com eixo quando é ponto). */
private fun rowLabel(label: String, s: ParamSlot, component: Int): String =
    if (s.type == ParamType.POINT2D || s.type == ParamType.POINT3D) axisLabel(label, component) else label

/** Um toque longo no rótulo pede o menu do parâmetro. */
@Immutable
private data class ParamMenuTarget(val effectId: Int, val param: Int, val label: String)

/**
 * O ARRASTE da pilha (≡): a ordem VISUAL enquanto o dedo arrasta e o quanto o
 * cartão erguido andou desde o último lugar. Ao soltar, UM `reorderEffect` (um
 * passo de desfazer); a ordem local some porque o store já traz a nova.
 */
@Stable
private class ReorderState {
    var dragging by mutableStateOf<Int?>(null)
    var offset by mutableFloatStateOf(0f)
    var order by mutableStateOf<List<Int>?>(null)

    /** Troca com o vizinho quando o centro do cartão erguido passa do centro dele. */
    fun swapIfNeeded(list: LazyListState) {
        val id = dragging ?: return
        val ord = order ?: return
        val visible = list.layoutInfo.visibleItemsInfo
        val me = visible.firstOrNull { it.key == id } ?: return
        val i = ord.indexOf(id)
        if (i < 0) return
        val center = me.offset + offset + me.size / 2f
        if (offset > 0f && i < ord.lastIndex) {
            val next = visible.firstOrNull { it.key == ord[i + 1] } ?: return
            if (center > next.offset + next.size / 2f) {
                order = ord.toMutableList().also { it[i] = ord[i + 1]; it[i + 1] = id }
                offset -= next.size
            }
        } else if (offset < 0f && i > 0) {
            val prev = visible.firstOrNull { it.key == ord[i - 1] } ?: return
            if (center < prev.offset + prev.size / 2f) {
                order = ord.toMutableList().also { it[i] = ord[i - 1]; it[i - 1] = id }
                offset += prev.size
            }
        }
    }
}

/**
 * O PAINEL "EFEITOS" (ref16/ref17, Fase 7.2).
 *
 * - LISTA (nenhum aberto): trilho estreito `‹` em cima e `⋯` embaixo; cada efeito
 *   um cartão `▶ Nome · 👁 · ≡` (≡ arrasta para reordenar) e "+ Adicionar efeito".
 * - EFEITO ABERTO: cartão `▼ Nome · ⋯ · 🗑`; os PRINCIPAIS primeiro e
 *   "Avançado ▾" com o resto; trilho `‹ · ◇ · curva · =` mirando a linha escolhida.
 * - Acordeão: UM cartão aberto; efeito recém-adicionado abre sozinho; entrar pelo
 *   losango de um parâmetro de efeito na timeline abre aquele efeito naquela linha.
 * - Toque longo no rótulo: Redefinir (e Expressão). Tocar no valor: teclado.
 */
@Composable
internal fun EffectsPanel(env: PanelEnv) {
    val store = env.store
    val layerId = store.primary
    val effects = store.effects
    // `detail` muda a cada quadro da reprodução; o painel só quer o TIPO.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }

    // Veio de um losango de efeito da timeline (ou volta da curva dele)? Abre ali.
    val entry = remember(layerId) {
        store.selectedKeyframe?.takeIf { it.first == layerId && it.second.property == TrackProperty.EFFECT_PARAM }?.second
            ?.takeIf { k -> effects.any { it.effectId == k.effectIndex } }
    }
    var openId by remember(layerId) { mutableStateOf(entry?.effectIndex) }
    var known by remember(layerId) { mutableStateOf(effects.map { it.effectId }.toSet()) }
    var selected by remember(layerId) {
        mutableStateOf(entry?.let { ParamKey(it.effectIndex, it.paramIndex / 4, it.paramIndex % 4) })
    }
    var advancedOpen by remember(layerId) {
        mutableStateOf(
            entry?.let { k ->
                val type = store.typeOf(k.effectIndex)
                val visible = store.effectParams[k.effectIndex].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
                if (splitPrincipal(type, visible).second.any { it.index == k.paramIndex / 4 }) setOf(k.effectIndex) else emptySet()
            } ?: emptySet(),
        )
    }
    var menuFor by remember { mutableStateOf<LayerEffect?>(null) }
    var paramMenu by remember { mutableStateOf<ParamMenuTarget?>(null) }
    var railMenu by remember { mutableStateOf(false) }
    val reorder = remember(layerId) { ReorderState() }
    val listState = rememberLazyListState()

    // Efeito novo abre sozinho; o aberto que sumiu fecha. Fora da composição
    // (bug B-09: estado mutado durante o build).
    LaunchedEffect(effects) {
        val ids = effects.map { it.effectId }.toSet()
        val added = ids - known
        if (added.isNotEmpty()) openId = effects.last { it.effectId in added }.effectId
        else if (openId != null && openId !in ids) openId = null
        known = ids
    }
    // O cartão aberto escolhe a sua primeira linha PRINCIPAL (se a escolhida não é dele).
    LaunchedEffect(openId, effects) {
        val id = openId
        if (id == null) {
            selected = null
            return@LaunchedEffect
        }
        if (selected?.effectId == id) return@LaunchedEffect
        val visible = store.effectParams[id].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
        val first = splitPrincipal(store.typeOf(id), visible).first.firstOrNull { it.components > 0 }
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
    // O rótulo do parâmetro escolhido, no idioma do app: a folha de expressão
    // abre com ele no título.
    val railParamLabel: String = selected?.let { k ->
        store.paramOf(k.effectId, k.param)?.let { p -> paramDisplay(store.typeOf(k.effectId), ParamSlot.of(p)).label() }
    } ?: ""

    val railExpr by remember(store) {
        derivedStateOf { selected?.let { k -> store.paramOf(k.effectId, k.param)?.let { store.expressionLook(paramKeys(k.effectId, ParamSlot.of(it))) } } ?: ExpressionLook.None }
    }
    val curveTrackReady by remember(store) {
        derivedStateOf { selected?.let { k -> store.primaryKeys().effectTrack(k.effectId, k.param, k.component).size >= 2 } ?: false }
    }

    val byId = effects.associateBy { it.effectId }
    val order = (reorder.order ?: effects.map { it.effectId }).mapNotNull { byId[it] }

    Row(Modifier.fillMaxSize()) {
        if (openId == null) {
            ListRail(onBack = env.onClose, onMore = { railMenu = true })
        } else {
            LeftRail(
                // Com um efeito aberto, o ‹ volta para a LISTA (ref17 → ref16).
                onBack = { openId = null },
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
                expression = railExpr,
                onExpression = if (railAnimatable) {
                    { selected?.let { k -> store.paramOf(k.effectId, k.param)?.let { p -> openParamExpression(store, k.effectId, ParamSlot.of(p), railParamLabel) } } }
                } else {
                    null
                },
            )
        }
        LazyColumn(
            Modifier.weight(1f).fillMaxHeight(),
            state = listState,
            contentPadding = PaddingValues(start = 2.dp, top = 8.dp, end = 12.dp, bottom = 16.dp),
        ) {
            items(order, key = { it.effectId }) { e ->
                val dragged = reorder.dragging == e.effectId
                EffectCardItem(
                    env = env,
                    effect = e,
                    expanded = openId == e.effectId,
                    selected = selected?.takeIf { it.effectId == e.effectId },
                    advanced = e.effectId in advancedOpen,
                    lifted = dragged,
                    onToggle = { openId = if (openId == e.effectId) null else e.effectId },
                    onToggleAdvanced = {
                        advancedOpen = if (e.effectId in advancedOpen) advancedOpen - e.effectId else advancedOpen + e.effectId
                    },
                    onSelect = { selected = it },
                    onParamMenu = { paramMenu = it },
                    onMenu = { menuFor = e },
                    onRemove = { store.removeEffect(e.effectId) },
                    dragHandle = Modifier.reorderHandle(e.effectId, reorder, listState, store),
                    modifier = if (dragged) {
                        Modifier.zIndex(1f).graphicsLayer { translationY = reorder.offset }
                    } else {
                        Modifier.animateItem()
                    },
                )
            }
            item(key = "rodape") { EffectsFooter(env, kind) }
        }
    }

    if (railMenu) {
        val anyOn = effects.any { it.enabled }
        AureaActionSheet(
            title = stringResource(R.string.panel_efeitos_camada),
            actions = buildList {
                add(SheetAction(stringResource(R.string.panel_adicionar_efeito)) { env.onOpenEffectsBrowser() })
                if (effects.isNotEmpty()) add(SheetAction(stringResource(R.string.panel_copiar_efeitos)) { store.copyEffects() })
                if (store.clipboard and 4 != 0) add(SheetAction(stringResource(R.string.panel_colar_efeitos)) { store.pasteEffects() })
                if (effects.isNotEmpty()) {
                    add(
                        SheetAction(if (anyOn) stringResource(R.string.panel_desligar_todos) else stringResource(R.string.panel_ligar_todos)) {
                            store.beginGesture(if (anyOn) "desligar efeitos" else "ligar efeitos")
                            try {
                                effects.forEach { if (it.enabled == anyOn) store.setEffectEnabled(it.effectId, !anyOn) }
                            } finally {
                                store.endGesture()
                            }
                        },
                    )
                }
            },
            onDismiss = { railMenu = false },
        )
    }

    menuFor?.let { e ->
        val index = effects.indexOfFirst { it.effectId == e.effectId }
        val actions = if (!e.known) {
            // Efeito que saiu do catálogo: só entender e tirar (A.01).
            listOf(SheetAction(stringResource(R.string.panel_remover_efeito), destructive = true) { store.removeEffect(e.effectId) })
        } else {
            buildList {
                add(SheetAction(if (e.enabled) stringResource(R.string.panel_desligar_efeito) else stringResource(R.string.panel_ligar_efeito)) { store.setEffectEnabled(e.effectId, !e.enabled) })
                add(SheetAction(stringResource(R.string.panel_redefinir_efeito)) { resetEffect(env, e.effectId) })
                if (index > 0) add(SheetAction(stringResource(R.string.panel_mover_cima)) { store.reorderEffect(e.effectId, index - 1) })
                if (index in 0 until effects.lastIndex) add(SheetAction(stringResource(R.string.panel_mover_baixo)) { store.reorderEffect(e.effectId, index + 1) })
                add(SheetAction(stringResource(R.string.panel_remover_efeito), destructive = true) { store.removeEffect(e.effectId) })
            }
        }
        AureaActionSheet(
            title = if (e.known) effectDisplayName(e.typeId, e.name) else stringResource(R.string.panel_efeito_removido),
            message = if (e.known) null else stringResource(R.string.panel_este_efeito_saiu_aurea_nao_desenha),
            actions = actions,
            cancelLabel = if (e.known) stringResource(R.string.panel_cancelar) else stringResource(R.string.panel_manter),
            onDismiss = { menuFor = null },
        )
    }

    paramMenu?.let { t ->
        val p = store.paramOf(t.effectId, t.param)
        val slot = p?.let { ParamSlot.of(it) }
        val d = slot?.let { paramDisplay(store.typeOf(t.effectId), it) }
        AureaActionSheet(
            title = t.label,
            message = if (p != null && d != null) "Padrão: ${defaultText(p, slot, d)}" else null,
            actions = buildList {
                add(SheetAction(stringResource(R.string.panel_redefinir), enabled = p != null) { resetParam(store, t.effectId, t.param) })
                if (slot?.animatable == true) {
                    val slotLabel = paramDisplay(store.typeOf(t.effectId), slot).label()
                    add(SheetAction(stringResource(R.string.panel_expressao_3c65)) { openParamExpression(store, t.effectId, slot, slotLabel) })
                }
            },
            onDismiss = { paramMenu = null },
        )
    }
}

/** O valor padrão como a linha mostraria ("100%", "0°", "Ligado", a opção). */
private fun defaultText(p: EffectParam, s: ParamSlot, d: ParamDisplay): String {
    val v = p.defaultValue
    return when (s.type) {
        ParamType.BOOL -> if (v[0] >= 0.5f) "ligado" else "desligado"
        ParamType.ENUM -> s.enumLabels.getOrNull(v[0].roundToInt()) ?: "${v[0].roundToInt() + 1}"
        ParamType.COLOR -> rgbaColor(engineToDisplay(v)).let { "RGB ${(it.red * 255).roundToInt()} ${(it.green * 255).roundToInt()} ${(it.blue * 255).roundToInt()}" }
        ParamType.POINT2D, ParamType.POINT3D -> (0 until s.components).joinToString(" × ") {
            comUnidade(numeroPtBr(d.toDisplay(v[it]), d.decimals), d.suffix)
        }
        else -> comUnidade(numeroPtBr(d.toDisplay(v[0]), d.decimals), d.suffix)
    }
}

/** REDEFINIR UM parâmetro: o padrão no cabeçote, num passo de desfazer. */
private fun resetParam(store: EditorStore, effectId: Int, index: Int) {
    val p = store.paramOf(effectId, index) ?: return
    if (ParamType.componentCount(p.type) == 0) return
    store.beginGesture("redefinir ${p.label}")
    try {
        store.writeParamVector(effectId, p, p.defaultValue)
    } finally {
        store.endGesture()
    }
}

/**
 * A alça ≡: arrasto vertical imediato (a alça é só dela, não briga com a rolagem
 * da lista — o filho consome primeiro). Soltar grava a ordem com UM comando.
 */
private fun Modifier.reorderHandle(id: Int, state: ReorderState, list: LazyListState, store: EditorStore): Modifier =
    this.then(
        Modifier.pointerInput(id) {
            fun finish() {
                val ord = state.order
                val from = store.effects.indexOfFirst { it.effectId == id }
                val to = ord?.indexOf(id) ?: -1
                state.dragging = null
                state.offset = 0f
                if (to >= 0 && from >= 0 && to != from) store.reorderEffect(id, to)
                state.order = null
            }
            detectDragGestures(
                onDragStart = {
                    state.order = store.effects.map { it.effectId }
                    state.dragging = id
                    state.offset = 0f
                },
                onDrag = { change, amount ->
                    change.consume()
                    state.offset += amount.y
                    state.swapIfNeeded(list)
                },
                onDragEnd = { finish() },
                onDragCancel = { finish() },
            )
        },
    )

/**
 * O TRILHO DA LISTA (ref16): estreito, `‹` em cima (volta às ferramentas da
 * camada) e `⋯` embaixo (copiar/colar/ligar todos).
 */
@Composable
private fun ListRail(onBack: () -> Unit, onMore: () -> Unit) {
    Column(Modifier.width(46.dp).fillMaxHeight()) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(56.dp)
                .semantics { contentDescription = "Voltar às ferramentas" }
                .tocavel(shrink = 1f, onClick = onBack),
            contentAlignment = Alignment.Center,
        ) {
            Icon(Icons.Rounded.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(24.dp))
        }
        Spacer(Modifier.weight(1f))
        Box(
            Modifier
                .fillMaxWidth()
                .height(56.dp)
                .semantics { contentDescription = "Mais opções dos efeitos" }
                .tocavel(shrink = 1f, onClick = onMore),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, AureaColors.Text)
        }
    }
}

/**
 * REDEFINIR O EFEITO: todos os parâmetros ao padrão NO CABEÇOTE, num passo de
 * desfazer só (parâmetro animado ganha a marca com o padrão, como qualquer edição).
 */
private fun resetEffect(env: PanelEnv, effectId: Int) {
    val store = env.store
    val params = store.effectParams[effectId] ?: return
    store.beginGesture("redefinir efeito")
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
    advanced: Boolean,
    lifted: Boolean,
    onToggle: () -> Unit,
    onToggleAdvanced: () -> Unit,
    onSelect: (ParamKey) -> Unit,
    onParamMenu: (ParamMenuTarget) -> Unit,
    onMenu: () -> Unit,
    onRemove: () -> Unit,
    dragHandle: Modifier,
    modifier: Modifier = Modifier,
) {
    val store = env.store
    EffectStackCard(
        name = effectDisplayName(effect.typeId, effect.name),
        enabled = effect.enabled,
        expanded = expanded,
        onToggleExpanded = onToggle,
        onToggleEnabled = { store.setEffectEnabled(effect.effectId, !effect.enabled) },
        onMenu = onMenu,
        onRemove = onRemove,
        dragHandle = dragHandle,
        lifted = lifted,
        modifier = modifier,
    ) {
        val id = effect.effectId
        val slots by remember(store, id) {
            derivedStateOf { store.effectParams[id]?.map { ParamSlot.of(it) } ?: emptyList() }
        }
        if (!effect.known) {
            PanelNotice(stringResource(R.string.panel_este_efeito_saiu_catalogo_ele_nao))
            return@EffectStackCard
        }
        if (effect.typeId == effectTypeId("aurea.time.remap")) {
            TimeRemapEffectEditor(store)
            return@EffectStackCard
        }
        val visible = slots.filter { !it.hidden }
        if (visible.isEmpty()) {
            PanelNotice(stringResource(R.string.panel_este_efeito_nao_tem_ajustes))
            return@EffectStackCard
        }
        val (main, rest) = remember(visible, effect.typeId) { splitPrincipal(effect.typeId, visible) }
        main.forEach { s -> ParamRows(env, id, effect.typeId, s, selected, onSelect, onParamMenu) }
        if (rest.isNotEmpty()) {
            AdvancedToggle(open = advanced, count = rest.size, onToggle = onToggleAdvanced)
            if (advanced) rest.forEach { s -> ParamRows(env, id, effect.typeId, s, selected, onSelect, onParamMenu) }
        }
    }
}

/** As linhas de UM parâmetro (ponto = uma linha por eixo). */
@Composable
private fun ParamRows(
    env: PanelEnv,
    id: Int,
    typeId: Int,
    s: ParamSlot,
    selected: ParamKey?,
    onSelect: (ParamKey) -> Unit,
    onParamMenu: (ParamMenuTarget) -> Unit,
) {
    val d = remember(typeId, s) { paramDisplay(typeId, s) }
    val label = d.label()
    val menu = { onParamMenu(ParamMenuTarget(id, s.index, label)) }
    when (s.type) {
        ParamType.FLOAT, ParamType.INT, ParamType.ANGLE ->
            EffectNumberRow(env, id, s, d, 0, label, selected == ParamKey(id, s.index, 0), onSelect, menu)
        ParamType.POINT2D, ParamType.POINT3D -> repeat(s.components) { c ->
            EffectNumberRow(env, id, s, d, c, rowLabel(label, s, c), selected == ParamKey(id, s.index, c), onSelect, menu)
        }
        ParamType.BOOL -> EffectToggleRow(env, id, s, label, selected?.param == s.index, onSelect, menu)
        ParamType.ENUM -> EffectChoiceRow(env, id, s, label, selected?.param == s.index, onSelect, menu)
        ParamType.COLOR -> EffectColorRow(env, id, s, label, selected?.param == s.index, onSelect, menu)
        // Curva/degradê/referência: o motor tem, o app ainda não edita — sem botão falso.
        else -> PropertyCustomRow(label = label, selected = false, onSelect = {}) {
            Text(
                stringResource(R.string.panel_ainda_nao_editavel_app),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
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

/** As trilhas de um parâmetro (um por componente): a chave dos keyframes. */
internal fun paramKeys(effectId: Int, s: ParamSlot): List<TrackKey> =
    List(ParamType.componentCount(s.type).coerceAtLeast(1)) { c -> TrackKey(TrackProperty.EFFECT_PARAM, effectId, s.index * 4 + c) }

/** Editor de expressão do parâmetro inteiro (ponto/cor: a expressão é vetorial), na unidade humana. */
internal fun openParamExpression(store: EditorStore, effectId: Int, s: ParamSlot, label: String) {
    if (!s.animatable) return
    val d = paramDisplay(store.typeOf(effectId), s)
    store.openExpression(label, paramKeys(effectId, s), d.scale, d.suffix)
}

@Composable
private fun rememberExpr(env: PanelEnv, effectId: Int, s: ParamSlot): ExpressionLook {
    val store = env.store
    val look by remember(store, effectId, s.index) { derivedStateOf { store.expressionLook(paramKeys(effectId, s)) } }
    return look
}

@Composable
private fun rememberLook(env: PanelEnv, effectId: Int, index: Int, component: Int?): KeyframeLook {
    val store = env.store
    val look by remember(store, effectId, index, component) { derivedStateOf { effectLook(store, effectId, index, component) } }
    return look
}

/**
 * Número / ângulo / eixo de ponto: rótulo + régua + caixa, NA UNIDADE HUMANA
 * (a régua, a caixa e o teclado falam `motor × scale`; a escrita desfaz a escala).
 */
@Composable
private fun EffectNumberRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    d: ParamDisplay,
    component: Int,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey) -> Unit,
    onMenu: () -> Unit,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, component)
    val look = rememberLook(env, effectId, s.index, component)
    val lo = if (s.min.isFinite()) s.min else Float.NEGATIVE_INFINITY
    val hi = if (s.max.isFinite()) s.max else Float.POSITIVE_INFINITY
    val shownLo = if (lo.isFinite()) d.toDisplay(lo) else lo
    val shownHi = if (hi.isFinite()) d.toDisplay(hi) else hi
    fun write(display: Float) {
        val p = store.paramOf(effectId, s.index) ?: return
        val clamped = d.toEngine(display).coerceIn(lo, hi)
        store.setEffectParam(effectId, p, if (s.type == ParamType.INT) clamped.roundToInt().toFloat() else clamped, component)
    }
    val shown = d.toDisplay(value)
    PropertyRow(
        label = label,
        value = shown,
        unitsPerDp = s.unitsPerDp * d.scale,
        min = min(shownLo, shownHi),
        max = max(shownLo, shownHi),
        format = { comUnidade(numeroPtBr(it, d.decimals), d.suffix) },
        selected = selected,
        keyframe = look,
        expression = rememberExpr(env, effectId, s),
        // O toque longo no rótulo abre o menu da linha (Redefinir · Expressão).
        onExpression = onMenu,
        onSelect = { onSelect(ParamKey(effectId, s.index, component)) },
        onGestureStart = { store.beginGesture("ajustar $label") },
        onValue = ::write,
        onGestureEnd = { store.endGesture() },
        onTapValue = {
            onSelect(ParamKey(effectId, s.index, component))
            env.openKeypad(KeypadRequest(label, shown, d.suffix, min(shownLo, shownHi), max(shownLo, shownHi), d.decimals) { write(it) })
        },
    )
}

/** Liga/desliga: valor ≥ 0,5 = ligado; escreve 1/0 num passo. */
@Composable
private fun EffectToggleRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey) -> Unit,
    onMenu: () -> Unit,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val look = rememberLook(env, effectId, s.index, null)
    PropertyCustomRow(
        label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look,
        expression = rememberExpr(env, effectId, s), onExpression = onMenu,
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            AureaToggle(
                checked = value >= 0.5f,
                onCheckedChange = { on ->
                    onSelect(ParamKey(effectId, s.index, 0))
                    store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, if (on) 1f else 0f) }
                },
            )
            Spacer(Modifier.width(4.dp))
        }
    }
}

/** Escolha: chips à vista (a linha cresce se quebrar). Índice arredondado e preso. */
@Composable
private fun EffectChoiceRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey) -> Unit,
    onMenu: () -> Unit,
) {
    val store = env.store
    val value = rememberParamValue(env, effectId, s.index, 0)
    val look = rememberLook(env, effectId, s.index, null)
    val options = s.enumLabels.ifEmpty { List((s.max - s.min).roundToInt().coerceAtLeast(0) + 1) { "${it + 1}" } }
    PropertyCustomRow(
        label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look,
        expression = rememberExpr(env, effectId, s), onExpression = onMenu,
    ) {
        ChoiceChips(
            options = options,
            selected = value.roundToInt().coerceIn(0, max(0, options.lastIndex)),
            onSelect = { i -> store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, i.toFloat()) } },
        )
    }
}

/** Cor: "R G B" + amostra → seletor; a folha inteira = UM passo de desfazer. */
@Composable
private fun EffectColorRow(
    env: PanelEnv,
    effectId: Int,
    s: ParamSlot,
    label: String,
    selected: Boolean,
    onSelect: (ParamKey) -> Unit,
    onMenu: () -> Unit,
) {
    val store = env.store
    // Derivado como `Color` (igualdade por valor): um FloatArray novo a cada
    // leitura faria a linha recompor a cada quadro da reprodução.
    val color by remember(store, effectId, s.index) {
        derivedStateOf { rgbaColor(engineToDisplay(store.paramOf(effectId, s.index)?.value ?: floatArrayOf(1f, 1f, 1f, 1f))) }
    }
    val look = rememberLook(env, effectId, s.index, null)
    PropertyCustomRow(
        label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look,
        expression = rememberExpr(env, effectId, s), onExpression = onMenu,
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
            ColorWell(color) {
                onSelect(ParamKey(effectId, s.index, 0))
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
            Spacer(Modifier.width(4.dp))
        }
    }
}

/**
 * O RODAPÉ: intensidade da camada de ajuste (é a opacidade) e o botão largo
 * "+ Adicionar efeito" (ref16: texto sublinhado num cartão escuro).
 */
@Composable
private fun EffectsFooter(env: PanelEnv, kind: Int) {
    Column(Modifier.fillMaxWidth()) {
        if (kind == LayerType.Adjustment.kind) AdjustmentIntensity(env)
        Row(
            Modifier
                .fillMaxWidth()
                .height(52.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Surface)
                .tocavel(haptic = true, onClick = env.onOpenEffectsBrowser),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 17.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.panel_adicionar_efeito), style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
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
            Text(stringResource(R.string.panel_intensidade_camada_ajuste), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Box(
                Modifier.size(44.dp).tocavel { store.toggleTransformKeyframe(intArrayOf(TrackProperty.OPACITY)) },
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

/** A trilha da opacidade (expressão). */
internal val OpacityKeys = listOf(TrackKey(TrackProperty.OPACITY))

/** A linha de opacidade (0–100 %, casas 0) — Efeitos (ajuste) e Mesclagem. */
@Composable
internal fun OpacityRow(env: PanelEnv, opacity: Float, selected: Boolean, keyframe: KeyframeLook = KeyframeLook.None) {
    val store = env.store
    val exprLook by remember(store) { derivedStateOf { store.expressionLook(OpacityKeys) } }
    PropertyRow(
        expression = exprLook,
        onExpression = { store.openExpression("Opacidade", OpacityKeys, 100f, "%") },
        label = stringResource(R.string.panel_opacidade),
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
