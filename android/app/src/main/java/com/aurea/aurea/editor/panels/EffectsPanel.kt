package com.aurea.aurea.editor.panels

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.presets.readPreset
import androidx.compose.runtime.rememberCoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.aurea.aurea.engine.ExpressionLook
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
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
import androidx.compose.foundation.Image
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.graphics.Brush
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectDetailSheet
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.GenericPlate
import com.aurea.aurea.effects.categoryGlyph
import com.aurea.aurea.effects.effectGroupLabelRes
import com.aurea.aurea.effects.effectGroupOf
import com.aurea.aurea.effects.rememberEffectPreview
import com.aurea.aurea.effects.toolGlyph
import com.aurea.aurea.effects.toolLabelRes
import com.aurea.aurea.engine.EffectCatalogEntry
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
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.ParamRowColors
import com.aurea.aurea.ui.ds.ParamRowDims
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
    /** Faixa DIGITADA (teclado), na unidade do motor; [min]..[max] é só a da régua. */
    val hardMin: Float = min,
    val hardMax: Float = max,
) {
    val hidden get() = (flags and ParamType.FLAG_HIDDEN) != 0

    /**
     * Limites do teclado na unidade do motor: a faixa digitada, nunca mais
     * estreita que a da régua. Sem faixa finita, ±∞ (o motor ainda prende).
     */
    val typedLo: Float
        get() {
            val lo = if (min.isFinite()) min else Float.NEGATIVE_INFINITY
            return if (hardMin.isFinite()) min(hardMin, lo) else lo
        }
    val typedHi: Float
        get() {
            val hi = if (max.isFinite()) max else Float.POSITIVE_INFINITY
            return if (hardMax.isFinite()) max(hardMax, hi) else hi
        }
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
        fun of(p: EffectParam) =
            ParamSlot(p.index, p.type, p.flags, p.min, p.max, p.label, p.unit, p.enumLabels, p.hardMin, p.hardMax)
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
 * O PAINEL "EFEITOS" (ref16/ref17, Fase 7.2; catálogo em tela cheia em 2026-10-01).
 *
 * - A PILHA da camada, limpa: no topo as FERRAMENTAS-EFEITO que a camada usa
 *   (máscaras, rastreio de câmera, legendas geradas — tocar reabre, 🗑 tira);
 *   depois um cartão compacto por efeito `≡ · prévia · Nome · 👁 · ⌄` (≡ arrasta
 *   para reordenar) e "+ Adicionar efeito".
 * - ADICIONAR: a tela cheia "Adicionar efeito" ([EffectAddSheet]): destaques,
 *   recentes, ladrilhos de categoria e busca. UM toque adiciona, fecha e abre os
 *   controles do efeito novo. Camada sem nada abre direto nela.
 * - EFEITO ABERTO: cartão `≡ · prévia · Nome · ⋯ · 🗑 · ⌃`; os PRINCIPAIS primeiro e
 *   "Avançado ▾" com o resto; trilho `‹ · ◇ · curva · =` mirando a linha escolhida.
 * - Acordeão: UM cartão aberto; efeito recém-adicionado abre sozinho; entrar pelo
 *   losango de um parâmetro de efeito na timeline abre aquele efeito naquela linha.
 * - Toque longo no rótulo: Redefinir (e Expressão). Tocar no valor: teclado.
 *
 * [focusedType] (as letras do texto 3D, dentro de Transformar) mostra só aquele
 * efeito, sem abas: é um editor embutido, não o navegador.
 */
@Composable
internal fun EffectsPanel(env: PanelEnv, focusedType: Int? = null) {
    val store = env.store
    val layerId = store.primary
    val effects = store.effects.filter { focusedType == null || it.typeId == focusedType }
    val tabbed = focusedType == null
    var creatorMode by remember(layerId) { mutableStateOf(-1) }
    var saveCustom by remember(layerId) { mutableStateOf(false) }
    var customName by remember(layerId) { mutableStateOf("") }
    // `detail` muda a cada quadro da reprodução; o painel só quer o TIPO.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }

    // Veio de um losango de efeito da timeline (ou volta da curva dele)? Abre ali.
    val entry = remember(layerId) {
        store.selectedKeyframe?.takeIf { it.first == layerId && it.second.property == TrackProperty.EFFECT_PARAM }?.second
            ?.takeIf { k -> effects.any { it.effectId == k.effectIndex } }
    }
    // As ferramentas-efeito que a camada usa (cartões no topo da pilha).
    val maskCount by remember(store) { derivedStateOf { store.masks?.takeIf { it.layer == store.primary }?.masks?.size ?: 0 } }
    var captionCount by remember(layerId) {
        mutableIntStateOf(if (tabbed && layerId != null) store.engineForStress.captionCount(layerId) else 0)
    }
    LaunchedEffect(layerId, store.layers) {
        // As legendas geradas viram camadas: a lista de camadas muda junto.
        captionCount = if (tabbed && layerId != null) store.engineForStress.captionCount(layerId) else 0
    }
    LaunchedEffect(layerId, kind, store.layers) {
        // Restaura o rastreio guardado NA CAMADA pelo caminho do store (que sabe de
        // qual camada é a análise aberta) e apaga os pontos do palco em seguida.
        if (tabbed && kind == LayerType.Video.kind) {
            store.refreshCameraFeatures(true)
            store.refreshCameraFeatures(false)
        }
    }
    val hasCameraTrack by remember(store) {
        derivedStateOf { store.detail?.kind == LayerType.Video.kind && (store.cameraTrack?.state == 2 || store.cameraTrack?.state == 3) }
    }
    val tools = buildList {
        if (!tabbed) return@buildList
        if (maskCount > 0) add(EffectTool.Mask)
        if (hasCameraTrack) add(EffectTool.CameraTrack)
        if (captionCount > 0) add(EffectTool.Captions)
    }
    // Camada sem nada abre direto na tela "Adicionar efeito"; entrar pelo losango
    // de um parâmetro é editar: abre na pilha.
    var adding by remember(layerId) {
        mutableStateOf(tabbed && layerId != null && entry == null && effects.isEmpty() && maskCount == 0 && captionCount == 0)
    }
    var toolToRemove by remember { mutableStateOf<EffectTool?>(null) }
    val hasAudio by remember(store) { derivedStateOf { store.detail?.hasAudio == true } }
    var aboutEntry by remember { mutableStateOf<EffectCatalogEntry?>(null) }
    // A pilha já abre com o primeiro cartão à vista (um toque a menos); os outros recolhidos.
    var openId by remember(layerId, focusedType) {
        mutableStateOf(entry?.effectIndex ?: effects.firstOrNull { it.typeId == focusedType }?.effectId ?: effects.firstOrNull()?.effectId?.takeIf { tabbed })
    }
    var known by remember(layerId) { mutableStateOf(effects.map { it.effectId }.toSet()) }
    var selected by remember(layerId) {
        mutableStateOf(entry?.let { ParamKey(it.effectIndex, it.paramIndex / 4, it.paramIndex % 4) })
    }
    androidx.compose.runtime.DisposableEffect(store, selected) {
        store.timelineFocus = selected?.let { listOf(TrackKey(31, it.effectId, it.param * 4 + it.component)) } ?: emptyList()
        onDispose { store.timelineFocus = null }
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
    val context = androidx.compose.ui.platform.LocalContext.current
    val importScope = rememberCoroutineScope()
    // Alight Motion: qualquer arquivo (o .amproj/.xml não tem MIME padrão); o
    // motor reconhece XML ou zip pelo conteúdo.
    val amPicker = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            val target = store.primary
            importScope.launch {
                val bytes = withContext(Dispatchers.IO) {
                    runCatching { context.contentResolver.openInputStream(uri)?.use { it.readPreset() } }.getOrNull()
                }
                if (bytes != null && store.primary == target) store.importAlightMotion(bytes)
                else store.showToast(context.getString(R.string.am_import_failed, context.getString(R.string.am_import_read_failed)))
            }
        }
    }
    val reorder = remember(layerId) { ReorderState() }
    val listState = rememberLazyListState()

    // Efeito novo abre sozinho; o aberto que sumiu fecha. Fora da composição
    // (bug B-09: estado mutado durante o build).
    LaunchedEffect(effects, store.pendingEffectFocus) {
        val ids = effects.map { it.effectId }.toSet()
        val added = ids - known
        known = ids
        val request = store.pendingEffectFocus?.takeIf { it.layer == layerId }
        val requested = effects.indexOfLast { request != null && it.typeId == request.type && it.effectId !in request.previous }
        if (requested >= 0 || added.isNotEmpty()) {
            val index = if (requested >= 0) requested else effects.indexOfLast { it.effectId in added }
            openId = effects[index].effectId
            // Adicionou (pelo catálogo, pela busca geral, colando): a tela de
            // adicionar fecha e os controles do efeito novo aparecem na pilha
            // (os cartões de ferramenta vêm antes dos efeitos na lista).
            adding = false
            androidx.compose.runtime.withFrameNanos { }
            listState.animateScrollToItem(index + tools.size)
            if (requested >= 0) store.consumeEffectFocus()
        }
        else if (openId != null && openId !in ids) openId = null
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
    val curveTrackReady by remember(store) {
        derivedStateOf { selected?.let { k -> store.primaryKeys().effectTrack(k.effectId, k.param, k.component).size >= 2 } ?: false }
    }

    val byId = effects.associateBy { it.effectId }
    val order = (reorder.order ?: effects.map { it.effectId }).mapNotNull { byId[it] }
    val openAdd: () -> Unit = { if (tabbed) adding = true else env.onOpenEffectsBrowser() }

    if (adding) {
        EffectAddSheet(
            store = store,
            layerHasAudio = hasAudio,
            onPick = { e ->
                adding = false
                store.addEffect(e.typeId)
            },
            onTool = { tool ->
                adding = false
                openEffectTool(env, tool)
            },
            onDismiss = { adding = false },
        )
    }

    Column(Modifier.fillMaxSize().background(ParamRowColors.Panel)) {
        Row(Modifier.fillMaxWidth().weight(1f)) {
            // O TRILHO (redesenho 2026-09-29): ‹ volta às seções da camada; ◇+ e a
            // curva miram o parâmetro escolhido; ⋯ no pé = ações da pilha inteira.
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
                        val t = store.detail?.localFrame(store.playhead)
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
                more = { RailMoreButton(active = false) { railMenu = true } },
            )
            LazyColumn(
                Modifier.weight(1f).fillMaxHeight().testTag("aurea.effects.stack"),
                state = listState,
                contentPadding = PaddingValues(start = 8.dp, top = 8.dp, end = 6.dp, bottom = 16.dp),
            ) {
                if (tabbed) item(key = "custom-effects") {
                    Column {
                        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())) {
                            androidx.compose.material3.TextButton(onClick = { creatorMode = if (creatorMode < 0) 0 else -1 }) {
                                Text(stringResource(R.string.fx_custom_create))
                            }
                            androidx.compose.material3.TextButton(onClick = {
                                store.presetsOpenKind = com.aurea.aurea.presets.PresetKind.Effects
                                env.onOpenPanel(EditorPanel.Presets)
                            }) { Text(stringResource(R.string.fx_custom_library)) }
                        }
                        if (creatorMode >= 0) {
                            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())) {
                                listOf(R.string.fx_custom_normal, R.string.fx_custom_advanced).forEachIndexed { index, label ->
                                    androidx.compose.material3.FilterChip(selected = creatorMode == index, onClick = { creatorMode = index },
                                        label = { Text(stringResource(label)) }, modifier = Modifier.padding(end = 8.dp))
                                }
                                androidx.compose.material3.TextButton(onClick = { saveCustom = true }, enabled = effects.isNotEmpty() && (creatorMode == 0 || openId != null)) {
                                    Text(stringResource(R.string.fx_custom_save))
                                }
                            }
                            Text(stringResource(if (creatorMode == 0) R.string.fx_custom_normal_help else R.string.fx_custom_advanced_help),
                                modifier = Modifier.padding(8.dp), color = AureaColors.Muted, fontSize = 13.sp)
                        }
                    }
                }
                if (tabbed && effects.isEmpty() && tools.isEmpty()) {
                    item(key = "vazio") { PanelNotice(stringResource(R.string.effects_applied_empty)) }
                }
                items(tools, key = { "tool." + it.key }) { tool ->
                    ToolStackCard(
                        tool = tool,
                        subtitle = when (tool) {
                            EffectTool.Mask -> stringResource(R.string.fxui_tool_masks_n, maskCount)
                            EffectTool.Captions -> stringResource(R.string.fxui_tool_captions_n, captionCount)
                            EffectTool.CameraTrack -> stringResource(R.string.fxui_tool_camera_ready)
                        },
                        onOpen = { openEffectTool(env, tool) },
                        onRemove = if (tool == EffectTool.CameraTrack) null else ({ toolToRemove = tool }),
                        modifier = Modifier.animateItem(),
                    )
                }
                items(order, key = { it.effectId }) { e ->
                    val dragged = reorder.dragging == e.effectId
                    EffectCardItem(
                        env = env,
                        effect = e,
                        expanded = openId == e.effectId,
                        selected = selected?.takeIf { it.effectId == e.effectId },
                        advanced = creatorMode == 1 || e.effectId in advancedOpen,
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
                item(key = "rodape") {
                    if (focusedType == null) EffectsFooter(env, kind, onAdd = openAdd)
                    else if (effects.isEmpty()) androidx.compose.material3.TextButton(onClick = { store.addEffect(focusedType) }) {
                        Text(stringResource(R.string.t3d_enable_letters))
                    }
                }
            }
        }
    }

    if (saveCustom) {
        androidx.compose.material3.AlertDialog(onDismissRequest = { saveCustom = false },
            title = { Text(stringResource(R.string.fx_custom_save)) },
            text = { androidx.compose.material3.OutlinedTextField(value = customName, onValueChange = { customName = it.take(60) },
                label = { Text(stringResource(R.string.fx_custom_name)) }, singleLine = true) },
            confirmButton = { androidx.compose.material3.TextButton(enabled = customName.isNotBlank(), onClick = {
                val name = customName.trim()
                val ok = if (creatorMode == 1 && openId != null) store.saveEffectPreset(openId!!, name)
                    else store.savePreset(com.aurea.aurea.presets.PresetKind.Effects, name,
                        store.capturePreset(com.aurea.aurea.presets.PresetKind.Effects, name))
                if (ok) saveCustom = false
            }) { Text(stringResource(R.string.fx_custom_save)) } },
            dismissButton = { androidx.compose.material3.TextButton(onClick = { saveCustom = false }) { Text(stringResource(R.string.fx_custom_cancel)) } })
    }

    if (railMenu) {
        val anyOn = effects.any { it.enabled }
        AureaActionSheet(
            title = stringResource(R.string.panel_efeitos_camada),
            actions = buildList {
                // "Meus presets" saiu (pedido de 2026-10-01: ninguém usa); o motor
                // continua lendo os presets dos projetos antigos.
                add(SheetAction(stringResource(R.string.panel_adicionar_efeito)) { openAdd() })
                add(SheetAction(stringResource(R.string.am_import_action)) { amPicker.launch(arrayOf("*/*")) })
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

    toolToRemove?.let { tool ->
        AureaActionSheet(
            title = stringResource(toolLabelRes(tool)),
            message = stringResource(if (tool == EffectTool.Mask) R.string.fxui_tool_remove_masks_q else R.string.fxui_tool_remove_captions_q),
            actions = listOf(
                SheetAction(stringResource(R.string.panel_remover), destructive = true) { removeEffectTool(store, tool) },
            ),
            onDismiss = { toolToRemove = null },
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
                // A expressão do parâmetro escolhido deste efeito (o "=" saiu do trilho).
                val exprTarget = selected?.takeIf { it.effectId == e.effectId }?.let { k -> store.paramOf(k.effectId, k.param)?.let { ParamSlot.of(it) } }
                if (exprTarget?.animatable == true) {
                    val exprLabel = paramDisplay(e.typeId, exprTarget).label()
                    add(SheetAction(stringResource(R.string.fx_param_expression, exprLabel)) { openParamExpression(store, e.effectId, exprTarget, exprLabel) })
                }
                store.catalog.firstOrNull { it.typeId == e.typeId }?.let { entry ->
                    add(SheetAction(stringResource(R.string.effects_about)) { aboutEntry = entry })
                }
                add(SheetAction(stringResource(R.string.fx_copy_this_effect)) { store.copyEffects(e.effectId) })
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

    aboutEntry?.let { entry ->
        // "Sobre o efeito" (⋯ do cartão): a ficha — o que ele faz, onde funciona,
        // custo e parâmetros. Sem botão de aplicar: ele já está na camada.
        EffectDetailSheet(
            store = store,
            entry = entry,
            previews = store.effectPreviews,
            favorite = store.effectPrefs.isFavorite(entry.typeId),
            onFavorite = { store.effectPrefs.toggleFavorite(entry.typeId) },
            onApply = null,
            onDismiss = { aboutEntry = null },
        )
    }

    paramMenu?.let { t ->
        val p = store.paramOf(t.effectId, t.param)
        val slot = p?.let { ParamSlot.of(it) }
        val d = slot?.let { paramDisplay(store.typeOf(t.effectId), it) }
        AureaActionSheet(
            title = t.label,
            message = if (p != null && d != null) {
                stringResource(R.string.fx_default_value, defaultText(p, slot, d, stringResource(R.string.common_on), stringResource(R.string.common_off)))
            } else {
                null
            },
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
private fun defaultText(p: EffectParam, s: ParamSlot, d: ParamDisplay, on: String, off: String): String {
    val v = p.defaultValue
    return when (s.type) {
        ParamType.BOOL -> if (v[0] >= 0.5f) on else off
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
 * TIRA uma ferramenta-efeito da camada, num passo de desfazer: todas as
 * máscaras, ou as legendas geradas (pelo fluxo das legendas). O rastreio de
 * câmera não tem "tirar": o motor guarda a análise no vídeo e a câmera criada é
 * uma camada comum.
 */
private fun removeEffectTool(store: EditorStore, tool: EffectTool) {
    val layer = store.primary ?: return
    when (tool) {
        EffectTool.Mask -> {
            val ids = store.masks?.takeIf { it.layer == layer }?.masks?.map { it.id }.orEmpty()
            if (ids.isEmpty()) return
            store.beginGesture("remover máscaras")
            try {
                ids.forEach { store.deleteMask(it) }
            } finally {
                store.endGesture()
            }
        }
        EffectTool.Captions -> {
            store.captions.open(layer)
            store.captions.removeAll()
        }
        EffectTool.CameraTrack -> Unit
    }
}

/** Miniatura do cartão da pilha (40 dp): a prévia do efeito, ou a cartela. */
@Composable
private fun StackThumb(typeId: Int, category: String, store: EditorStore) {
    Box(Modifier.size(40.dp).clip(RoundedCornerShape(8.dp))) {
        val preview = rememberEffectPreview(store.effectPreviews, typeId, 320, 200)
        if (preview != null) {
            Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
        } else {
            GenericPlate(categoryGlyph(category))
        }
    }
}

/**
 * O CARTÃO DE UMA FERRAMENTA-EFEITO na pilha: `glifo · Nome / quantos · 🗑 · ›`.
 * Tocar reabre a ferramenta; a lixeira (quando existe) pede confirmação.
 * testTag `effects.tool.<chave>`.
 */
@Composable
private fun ToolStackCard(tool: EffectTool, subtitle: String, onOpen: () -> Unit, onRemove: (() -> Unit)?, modifier: Modifier = Modifier) {
    val name = stringResource(toolLabelRes(tool))
    val openLabel = stringResource(R.string.fxui_a11y_open, name)
    val removeLabel = stringResource(R.string.fxui_a11y_remove, name)
    Row(
        modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(ParamRowColors.Card)
            .height(56.dp)
            .testTag("effects.tool." + tool.key.substringAfterLast('.'))
            .semantics { contentDescription = openLabel; role = Role.Button }
            .tocavel(shrink = 1f, onClick = onOpen)
            .padding(start = 10.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(40.dp).clip(RoundedCornerShape(8.dp))
                .background(Brush.verticalGradient(listOf(AureaColors.ActionDim, AureaColors.SurfaceHigh))),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(toolGlyph(tool), 20.dp, AureaColors.Accent)
        }
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)))
            Text(subtitle, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        if (onRemove != null) StackButton(CupertinoGlyph.Trash, removeLabel, AureaColors.Text, Modifier, onRemove)
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Muted, Modifier.padding(horizontal = 12.dp))
    }
}

/** Botão de 40 dp do cabeçalho do cartão, com o rótulo de acessibilidade traduzido. */
@Composable
private fun StackButton(glyph: Char, label: String, tint: androidx.compose.ui.graphics.Color, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier.size(40.dp).semantics { contentDescription = label; role = Role.Button }.tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 19.dp, tint)
    }
}

/**
 * O CARTÃO COMPACTO de um efeito aplicado (redesenho 2026-10-01).
 * Recolhido: `≡ · prévia · Nome / grupo · 👁 · ⌄`. Aberto: `≡ · prévia · Nome ·
 * ⋯ · 🗑 · ⌃` e o corpo. O ≡ arrasta para reordenar; tocar no nome abre/fecha.
 * Recolhido não compõe o corpo. Desligado, nome e corpo ficam a 45 %.
 * testTags `effects.expand|eye|more|remove.<id>` (iguais no iOS).
 */
@Composable
private fun FxStackCard(
    store: EditorStore,
    effect: LayerEffect,
    name: String,
    expanded: Boolean,
    lifted: Boolean,
    onToggleExpanded: () -> Unit,
    onToggleEnabled: () -> Unit,
    onMenu: () -> Unit,
    onRemove: () -> Unit,
    dragHandle: Modifier,
    modifier: Modifier = Modifier,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit,
) {
    val shape = RoundedCornerShape(10.dp)
    val entry = remember(store.catalog, effect.typeId) { store.catalog.firstOrNull { it.typeId == effect.typeId } }
    val id = com.aurea.aurea.effects.effectCardId(effect.typeId)
    val subtitle = when {
        !effect.enabled -> stringResource(R.string.fxui_effect_off)
        entry != null -> stringResource(effectGroupLabelRes(effectGroupOf(entry)))
        else -> ""
    }
    val toggleLabel = stringResource(if (expanded) R.string.fxui_a11y_collapse else R.string.fxui_a11y_expand, name)
    Column(
        modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp)
            .clip(shape)
            .background(if (lifted) AureaColors.SurfaceHigh else ParamRowColors.Card)
            .then(if (lifted) Modifier.border(1.dp, AureaColors.Accent, shape) else Modifier)
            .padding(bottom = if (expanded) 6.dp else 0.dp),
    ) {
        Row(Modifier.fillMaxWidth().height(56.dp).padding(end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            val dragLabel = stringResource(R.string.fxui_a11y_drag, name)
            Box(
                dragHandle.size(32.dp, 56.dp).semantics { contentDescription = dragLabel },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.LineHorizontal3, 16.dp, if (lifted) AureaColors.Accent else AureaColors.Muted)
            }
            Row(
                Modifier
                    .weight(1f)
                    .height(56.dp)
                    .testTag("effects.expand.$id")
                    .semantics { contentDescription = toggleLabel; role = Role.Button }
                    .tocavel(shrink = 1f, onClick = onToggleExpanded),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.alpha(if (effect.enabled) 1f else 0.45f)) {
                    StackThumb(effect.typeId, entry?.category.orEmpty(), store)
                }
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        name,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.alpha(if (effect.enabled) 1f else 0.45f),
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)),
                    )
                    if (subtitle.isNotEmpty()) {
                        Text(subtitle, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
                    }
                }
            }
            if (expanded) {
                StackButton(CupertinoGlyph.Ellipsis, stringResource(R.string.app_a11y_more_options, name), AureaColors.Text, Modifier.testTag("effects.more.$id"), onMenu)
                StackButton(CupertinoGlyph.Trash, stringResource(R.string.fxui_a11y_remove, name), AureaColors.Text, Modifier.testTag("effects.remove.$id"), onRemove)
            } else {
                StackButton(
                    if (effect.enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
                    stringResource(if (effect.enabled) R.string.fxui_a11y_disable else R.string.fxui_a11y_enable, name),
                    if (effect.enabled) AureaColors.Text else AureaColors.Muted,
                    Modifier.testTag("effects.eye.$id"),
                    onToggleEnabled,
                )
            }
            StackButton(if (expanded) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, toggleLabel, AureaColors.Muted, Modifier, onToggleExpanded)
        }
        if (expanded) {
            Column(
                Modifier.fillMaxWidth().padding(horizontal = 6.dp).alpha(if (effect.enabled) 1f else 0.45f),
                verticalArrangement = Arrangement.spacedBy(4.dp),
                content = content,
            )
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
    FxStackCard(
        store = store,
        effect = effect,
        name = effectDisplayName(effect.typeId, effect.name),
        expanded = expanded,
        lifted = lifted,
        onToggleExpanded = onToggle,
        onToggleEnabled = { store.setEffectEnabled(effect.effectId, !effect.enabled) },
        onMenu = onMenu,
        onRemove = onRemove,
        dragHandle = dragHandle,
        modifier = modifier,
    ) {
        val id = effect.effectId
        val slots by remember(store, id) {
            derivedStateOf { store.effectParams[id]?.map { ParamSlot.of(it) } ?: emptyList() }
        }
        if (!effect.known) {
            PanelNotice(stringResource(R.string.panel_este_efeito_saiu_catalogo_ele_nao))
            return@FxStackCard
        }
        if (effect.typeId == effectTypeId("aurea.key.rotobrush")) {
            PanelNotice(stringResource(R.string.roto_note))
        }
        val localAiBit = when (effect.typeId) {
            effectTypeId("aurea.ai.depth_map") -> 1
            effectTypeId("aurea.key.rotobrush") -> 2
            else -> 0
        }
        if (effect.enabled && localAiBit != 0) {
            if (store.localAiActivity and localAiBit != 0) {
                PanelNotice(stringResource(R.string.local_ai_processing), Modifier.testTag("effects.local_ai.processing"))
            } else if (store.localAiActivity and (localAiBit shl 2) != 0) {
                PanelNotice(stringResource(R.string.local_ai_failed), Modifier.testTag("effects.local_ai.failed"))
            }
        }
        if (effect.typeId == effectTypeId("aurea.time.remap")) {
            TimeRemapEffectEditor(env, id)
            return@FxStackCard
        }
        val visible = slots.filter { !it.hidden }
        if (visible.isEmpty()) {
            PanelNotice(stringResource(R.string.panel_este_efeito_nao_tem_ajustes))
            return@FxStackCard
        }
        // Presets do efeito (o do app antigo: Impacto/Na mão/Glitch do Tremor):
        // uma fileira de fichas no topo do cartão; tocar = um passo de desfazer.
        val presets = remember(effect.typeId) { store.effectPresets(effect.typeId) }
        if (presets.isNotEmpty()) EffectPresetRow(presets) { store.applyEffectPreset(id, it) }
        val (main, rest) = remember(visible, effect.typeId) { splitPrincipal(effect.typeId, visible) }
        // EQ paramétrico: o gráfico da resposta em cima das bandas.
        if (effect.typeId == effectTypeId("aurea.audio.parametric_eq")) EqResponseGraph(env, id)
        main.forEach { s -> ParamRows(env, id, effect.typeId, s, selected, onSelect, onParamMenu) }
        if (rest.isNotEmpty()) {
            AdvancedToggle(open = advanced, count = rest.size, onToggle = onToggleAdvanced)
            if (advanced) rest.forEach { s -> ParamRows(env, id, effect.typeId, s, selected, onSelect, onParamMenu) }
        }
    }
}

/** Nome do preset de efeito pelo id estável (o motor fala pt-BR). */
@androidx.annotation.StringRes
private fun effectPresetLabel(id: String): Int? = when (id) {
    "impact" -> R.string.fxp_impact
    "handheld" -> R.string.fxp_handheld
    "glitch" -> R.string.fxp_glitch
    else -> null
}

/** Fichas dos presets do efeito, como a fileira de presets do Particular. */
@Composable
private fun EffectPresetRow(presets: List<Pair<String, String>>, onApply: (Int) -> Unit) {
    Column(Modifier.padding(bottom = 6.dp)) {
        Text(stringResource(R.string.fx_presets), style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)))
        Spacer(Modifier.height(5.dp))
        Row(Modifier.horizontalScroll(androidx.compose.foundation.rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            presets.forEachIndexed { i, (pid, name) ->
                val res = effectPresetLabel(pid)
                Box(Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).testTag("fx.preset.$pid")
                    .tocavel(onClick = { onApply(i) }).padding(horizontal = 12.dp, vertical = 6.dp)) {
                    Text(if (res != null) stringResource(res) else name, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
                }
            }
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
        // Outra camada ("Camada de áudio"): escolhe na lista das camadas.
        ParamType.LAYER_REFERENCE -> EffectLayerRow(env, id, s, label, menu)
        ParamType.TEXTURE_REFERENCE -> CubeLutImportRow(env, id)
        // Curva/degradê/textura: o motor tem, o app ainda não edita — sem botão falso.
        else -> PropertyCustomRow(label = label, selected = false, onSelect = {}) {
            Text(
                stringResource(R.string.panel_ainda_nao_editavel_app),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
        }
    }
}

@Composable
private fun CubeLutImportRow(env: PanelEnv, effect: Int) {
    val store = env.store
    val layer = store.primary ?: return
    var target by remember { mutableStateOf<Pair<Long, Int>?>(null) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        target?.let { (id, fx) -> if (uri != null) store.importColorLut(uri, id, fx) }
        target = null
    }
    val name = store.colorLutName(layer, effect)
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        if (name.isNotBlank()) Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        Box(Modifier.fillMaxWidth().height(48.dp).testTag("effect.lut.import")
            .clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip)
            .tocavel(onClick = { target = layer to effect; picker.launch(arrayOf("*/*")) }),
            contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.lut_import), color = AureaColors.Text)
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
    // Duas faixas: a RÉGUA anda só na do slider ([s.min]..[s.max]); o TECLADO
    // aceita a digitada ([s.typedLo]..[s.typedHi]), que o motor impõe. A
    // escrita prende só na digitada — um valor digitado além da régua fica.
    val lo = if (s.min.isFinite()) s.min else Float.NEGATIVE_INFINITY
    val hi = if (s.max.isFinite()) s.max else Float.POSITIVE_INFINITY
    val typedLo = s.typedLo
    val typedHi = s.typedHi
    val shownLo = if (lo.isFinite()) d.toDisplay(lo) else lo
    val shownHi = if (hi.isFinite()) d.toDisplay(hi) else hi
    val typedShownLo = if (typedLo.isFinite()) d.toDisplay(typedLo) else typedLo
    val typedShownHi = if (typedHi.isFinite()) d.toDisplay(typedHi) else typedHi
    fun write(display: Float) {
        val p = store.paramOf(effectId, s.index) ?: return
        val clamped = d.toEngine(display).coerceIn(typedLo, typedHi)
        store.setEffectParam(effectId, p, if (s.type == ParamType.INT) clamped.roundToInt().toFloat() else clamped, component)
    }
    val shown = d.toDisplay(value)
    PropertyRow(
        label = label,
        value = shown,
        modifier = Modifier.testTag("effects.param.$effectId.${s.index}.$component"),
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
            // "50%" no teclado continua sendo 50 % do fim da RÉGUA (não do teto digitado).
            env.openKeypad(
                KeypadRequest(
                    label, shown, d.suffix, min(typedShownLo, typedShownHi), max(typedShownLo, typedShownHi), d.decimals,
                    percentBase = max(shownLo, shownHi),
                ) { write(it) },
            )
        },
    )
}

/** Liga/desliga: valor ≥ 0,5 = ligado; escreve 1/0 num passo. O interruptor mora no lugar da caixa de valor. */
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
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
            AureaToggle(
                checked = value >= 0.5f,
                modifier = Modifier.testTag("effects.toggle.$effectId.${s.index}").semantics {
                    contentDescription = label
                    role = Role.Switch
                    toggleableState = if (value >= 0.5f) ToggleableState.On else ToggleableState.Off
                },
                onCheckedChange = { on ->
                    onSelect(ParamKey(effectId, s.index, 0))
                    store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, if (on) 1f else 0f) }
                },
            )
            Spacer(Modifier.width(6.dp))
        }
    }
}

/**
 * Escolha: a caixa de valor (a opção + ▾) abre a lista das opções — a mesma folha
 * de escolha das outras linhas (camada de referência). Índice arredondado e preso.
 */
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
    val current = value.roundToInt().coerceIn(0, max(0, options.lastIndex))
    var picking by remember { mutableStateOf(false) }
    PropertyCustomRow(
        label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look,
        expression = rememberExpr(env, effectId, s), onExpression = onMenu,
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(ParamRowDims.LabelH)
                .clip(RoundedCornerShape(ParamRowDims.Radius))
                .background(ParamRowColors.ValueBox)
                .testTag("effects.choice.$effectId.${s.index}")
                .tocavel(shrink = 1f) {
                    onSelect(ParamKey(effectId, s.index, 0))
                    picking = true
                }
                .padding(start = 10.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                options.getOrElse(current) { "" },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
            )
            CupertinoIcon(CupertinoGlyph.ChevronDown, 12.dp, AureaColors.Muted)
        }
    }
    if (picking) {
        AureaActionSheet(
            title = label,
            actions = options.mapIndexed { i, o ->
                SheetAction(if (i == current) "✓ $o" else o) {
                    store.paramOf(effectId, s.index)?.let { store.setEffectParam(effectId, it, i.toFloat()) }
                }
            },
            onDismiss = { picking = false },
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
    val pick = {
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
    PropertyCustomRow(
        label, selected, onSelect = { onSelect(ParamKey(effectId, s.index, 0)) }, keyframe = look,
        expression = rememberExpr(env, effectId, s), onExpression = onMenu,
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
            Text(
                "${(color.red * 255).roundToInt()} ${(color.green * 255).roundToInt()} ${(color.blue * 255).roundToInt()}",
                maxLines = 1,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = ParamRowColors.RgbText, fontFeatureSettings = "tnum")),
            )
            Spacer(Modifier.width(8.dp))
            Box(
                Modifier
                    .size(34.dp, 30.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(color)
                    .border(1.dp, ParamRowColors.SwatchBorder, RoundedCornerShape(5.dp))
                    .semantics { contentDescription = label }
                    .testTag("effects.color.$effectId.${s.index}")
                    .tocavel(onClick = pick),
            )
            Spacer(Modifier.width(8.dp))
        }
    }
}

/**
 * O RODAPÉ: intensidade da camada de ajuste (é a opacidade) e o botão largo
 * "+ Adicionar efeito" (ref16: texto sublinhado num cartão escuro), que leva à
 * aba "Adicionar".
 */
@Composable
private fun EffectsFooter(env: PanelEnv, kind: Int, onAdd: () -> Unit) {
    Column(Modifier.fillMaxWidth()) {
        if (kind == LayerType.Adjustment.kind) AdjustmentIntensity(env)
        Row(
            Modifier
                .fillMaxWidth()
                .height(48.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(ParamRowColors.Card)
                .testTag("aurea.effects.add")
                .tocavel(haptic = true, onClick = onAdd),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 16.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.panel_adicionar_efeito), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
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
            .clip(RoundedCornerShape(10.dp))
            .background(ParamRowColors.Card)
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
    val opacityLabel = stringResource(R.string.panel_opacidade)
    PropertyRow(
        expression = exprLook,
        onExpression = { store.openExpression(opacityLabel, OpacityKeys, 100f, "%") },
        label = opacityLabel,
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
            env.openKeypad(KeypadRequest(opacityLabel, opacity, "%", 0f, 100f, 0) { store.setTransform(TrackProperty.OPACITY, it / 100f) })
        },
    )
}
