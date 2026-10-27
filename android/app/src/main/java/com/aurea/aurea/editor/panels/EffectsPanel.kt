package com.aurea.aurea.editor.panels

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.util.lerp
import androidx.compose.ui.zIndex
import com.aurea.aurea.R
import com.aurea.aurea.editor.EffectPointStage
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectDetailSheet
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.HyperEffectGate
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.effects.toolGlyph
import com.aurea.aurea.effects.toolLabelRes
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.ParamControl
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.presets.readPreset
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.AureaDialogField
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.ds.FxParamMath
import com.aurea.aurea.ui.ds.FxRow
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.PropertyRow
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.ds.comUnidade
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Qual linha está escolhida: o efeito, o parâmetro e o componente (ponto = X/Y). */
@Immutable
internal data class ParamKey(val effectId: Int, val param: Int, val component: Int)

/**
 * A FORMA de um parâmetro (sem o valor): o que o corpo precisa para montar as
 * linhas. Muda só quando o efeito muda — o valor que anda com o cabeçote fica de
 * fora, e por isso o corpo não recompõe a cada quadro da reprodução.
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
    /** Seção do painel (1..N; 0 = solto), o interruptor dela (−1 = sem) e se está aberta. */
    val section: Int = 0,
    val sectionId: String = "",
    val sectionLabel: String = "",
    val sectionToggle: Int = -1,
    val sectionOpen: Boolean = true,
    /** Dica (ⓘ, só leitura) mostrada acima da linha; vazia = sem dica. */
    val tip: String = "",
    // Omega efeitos (P0-1): apresentação resolvida pelo motor.
    /** [ParamControl] (0 = sem controle numérico). */
    val control: Int = ParamControl.AUTO,
    /** Valor por risco da régua (unidade do motor; 0 = regra da UI). */
    val step: Float = 0f,
    /** Origem do preenchimento do slider (unidade do motor). */
    val origin: Float = 0f,
    /** [ParamControl.ENUM_SEGMENTED] / [ParamControl.ENUM_DROPDOWN]. */
    val enumStyle: Int = 0,
    /** Bits de `EffectParam.rowFlags` (habilitado, disco de matiz, passo declarado). */
    val rowFlags: Int = EffectParam.ROW_ENABLED,
    /** O padrão da declaração (encaixe do slider e da régua). */
    val defaults: List<Float> = emptyList(),
) {
    val hidden get() = (flags and ParamType.FLAG_HIDDEN) != 0
    /** Editável agora (habilitação cruzada e interruptor da seção, regra do motor). */
    val enabled get() = (rowFlags and EffectParam.ROW_ENABLED) != 0
    /** O efeito declarou o passo: as casas decimais vêm dele (formato da referência). */
    val stepDeclared get() = (rowFlags and EffectParam.ROW_STEP_DECLARED) != 0

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
            ParamSlot(p.index, p.type, p.flags, p.min, p.max, p.label, p.unit, p.enumLabels, p.hardMin, p.hardMax,
                p.section, p.sectionId, p.sectionLabel, p.sectionToggle, p.sectionOpen, p.tip,
                p.control, p.step, p.origin, p.enumStyle, p.rowFlags,
                p.defaultValue.take(max(1, ParamType.componentCount(p.type))))
    }
}

/** O tipo (`typeId`) do efeito [effectId] da camada principal (0 = sumiu). */
private fun EditorStore.typeOf(effectId: Int): Int = effects.firstOrNull { it.effectId == effectId }?.typeId ?: 0

/** Um toque longo no rótulo pede o menu do parâmetro. */
@Immutable
internal data class ParamMenuTarget(val effectId: Int, val param: Int, val label: String)

/**
 * O ARRASTE da lista: a ordem VISUAL enquanto o dedo arrasta e o quanto a linha
 * erguida andou desde o último lugar. Ao soltar, UM `reorderEffect` (um passo de
 * desfazer) só se a ordem mudou; a ordem local some porque o store já traz a nova.
 */
@Stable
private class ReorderState {
    var dragging by mutableStateOf<Int?>(null)
    var offset by mutableFloatStateOf(0f)
    var order by mutableStateOf<List<Int>?>(null)

    /** Troca com o vizinho quando o centro da linha erguida passa do centro dele. */
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

    fun begin(id: Int, visible: List<Int>) {
        order = visible
        dragging = id
        offset = 0f
    }

    /**
     * Solta: UM `reorderEffect` se a ordem mudou. A lista mostra só os efeitos
     * comuns, e as decorações (Borda e sombra) ficam sempre depois deles na
     * pilha: o índice na lista é o índice na pilha.
     */
    fun finish(store: EditorStore) {
        val id = dragging
        val ord = order
        dragging = null
        offset = 0f
        order = null
        if (id == null || ord == null) return
        val from = store.effects.indexOfFirst { it.effectId == id }
        val to = ord.indexOf(id)
        if (to >= 0 && from >= 0 && to != from) store.reorderEffect(id, to)
    }
}

/** O efeito aparece na LISTA de Efeitos? (Borda e sombra — decoração, `role` ≠ 0 — moram em Aparência.) */
internal fun fxShownInEffectList(e: LayerEffect): Boolean = !e.isDecoration

/** Nome do preset de efeito pelo id estável (o motor fala pt-BR). */
@androidx.annotation.StringRes
private fun effectPresetLabel(id: String): Int? = when (id) {
    "impact" -> R.string.fxp_impact
    "handheld" -> R.string.fxp_handheld
    "glitch" -> R.string.fxp_glitch
    "saber_default" -> R.string.fxp_saber_default
    "saber_lightsaber" -> R.string.fxp_saber_lightsaber
    "saber_neon" -> R.string.fxp_saber_neon
    "saber_electric" -> R.string.fxp_saber_electric
    "saber_fire" -> R.string.fxp_saber_fire
    "saber_plasma" -> R.string.fxp_saber_plasma
    "saber_energy" -> R.string.fxp_saber_energy
    "saber_soft" -> R.string.fxp_saber_soft
    "saber_magic" -> R.string.fxp_saber_magic
    "classic" -> R.string.fxp_constellation_classic
    "sphere" -> R.string.fx030c_src_sphere
    "dust" -> R.string.fxp_constellation_dust
    "living_grid" -> R.string.fxp_constellation_living_grid
    "auto" -> R.string.fxp_enhance_auto
    "social" -> R.string.fxp_enhance_social
    "restore" -> R.string.fxp_enhance_restore
    "anime" -> R.string.fxp_enhance_anime
    "upscaled" -> R.string.fxp_enhance_upscaled
    "film" -> R.string.fxp_enhance_film
    else -> null
}

/**
 * O PAINEL "EFEITOS" — Omega 0.3.2 (área 07), no formato da referência
 * (`fragment_effects` / `WHy.lG`), dentro da moldura comum de 200 dp
 * ([PanelFrame]: ‹ · ◆± · Curva · = · ⋯).
 *
 * - A LISTA: linhas de 37 dp `▸ · Nome · selo · 👁 · ≡` (as ferramentas-efeito
 *   antes, no mesmo formato) e "+ Adicionar efeito" por último. Entrar = lista
 *   FECHADA; camada vazia mostra só o "+". Toque longo na linha (ou arrastar o ≡)
 *   reordena; deslizar para a esquerda apaga (com "Desfazer").
 * - Os CONTROLES de UM efeito por vez, SOBRE a lista: nascem da linha tocada
 *   (recorte + deslocamento em 200 ms, a curva padrão do Android) e fecham pelo
 *   cabeçalho. Cabeçalho: `▾ · Nome · ⋮ · 🗑`.
 * - O efeito novo abre sozinho SÓ quando a camada não tinha efeito (o guia
 *   "deslize para ajustar" fica mais longo se o 1º parâmetro for um ponto).
 * - ⋮ do efeito: Detalhes · Restaurar padrão · Duplicar · Copiar efeito · Colar
 *   configurações (mesmo tipo) e, em "Mais do AUREA", Expressão · Salvar preset
 *   do efeito · Turbulência dinâmica. ⋯ da lista (inativo com controles abertos):
 *   Copiar efeitos · Colar efeito(s) · Ligar/Desligar todos · Criar efeito ·
 *   Meus efeitos · Importar efeitos de projeto.
 *
 * [focusedType] (letras do texto 3D, Particular) mostra só aquele efeito, aberto.
 * Par do iOS: `EffectsView.swift` (mesmos ids e regras).
 */
@Composable
internal fun EffectsPanel(env: PanelEnv, focusedType: Int? = null) {
    val store = env.store
    val context = androidx.compose.ui.platform.LocalContext.current
    val layerId = store.primary
    val tabbed = focusedType == null
    val effects = store.effects.filter { if (tabbed) fxShownInEffectList(it) else it.typeId == focusedType }
    // `detail` muda a cada quadro da reprodução; o painel só quer o TIPO.
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val locked by remember(store) { derivedStateOf { store.detail?.locked == true } }

    // Veio de um losango de efeito da timeline (ou volta da curva dele)? Abre ali.
    val entry = remember(layerId) {
        store.selectedKeyframe?.takeIf { it.first == layerId && (it.second.property == TrackProperty.EFFECT_PARAM || it.second.property == TrackProperty.MESH_WARP) }?.second
            ?.takeIf { k -> effects.any { it.effectId == k.effectIndex } }
    }
    // As ferramentas-efeito que a camada usa (linhas no topo da lista).
    val maskCount by remember(store) { derivedStateOf { store.masks?.takeIf { it.layer == store.primary }?.masks?.size ?: 0 } }
    val hasMatte by remember(store) { derivedStateOf { store.trackMatte?.let { (it.getOrNull(0) ?: 0L) != 0L && (it.getOrNull(1) ?: 0L) != 0L } == true } }
    val maskPinned by remember(store) { derivedStateOf { store.primary?.let { it in store.maskToolLayers } == true } }
    val maskTool = maskCount > 0 || hasMatte || maskPinned
    var captionCount by remember(layerId) {
        mutableIntStateOf(if (tabbed && layerId != null) store.engineForStress.captionCount(layerId) else 0)
    }
    LaunchedEffect(layerId, store.layers) {
        captionCount = if (tabbed && layerId != null) store.engineForStress.captionCount(layerId) else 0
    }
    LaunchedEffect(layerId, kind) {
        if (tabbed && kind == LayerType.Video.kind) {
            store.refreshCameraFeatures(true)
            store.refreshCameraFeatures(false)
        }
    }
    val hasCameraTrack by remember(store) {
        derivedStateOf { store.detail?.kind == LayerType.Video.kind && store.cameraTrack?.state == 2 }
    }
    val tools = buildList {
        if (!tabbed) return@buildList
        if (maskTool) add(EffectTool.Mask)
        if (hasCameraTrack) add(EffectTool.CameraTrack)
        if (captionCount > 0) add(EffectTool.Captions)
    }
    var adding by remember(layerId) { mutableStateOf(false) }
    var toolToRemove by remember { mutableStateOf<EffectTool?>(null) }
    val hasAudio by remember(store) { derivedStateOf { store.detail?.hasAudio == true } }
    var aboutEntry by remember { mutableStateOf<EffectCatalogEntry?>(null) }
    var unknownMenu by remember { mutableStateOf<LayerEffect?>(null) }
    var saveDialog by remember { mutableStateOf<Int?>(null) }        // -1 = Criar efeito (todos); id = só este efeito
    var paramMenu by remember { mutableStateOf<ParamMenuTarget?>(null) }

    // ENTRAR = lista fechada (salvo pelo losango da timeline; ou o efeito em foco).
    var openId by remember(layerId, focusedType) {
        mutableStateOf(entry?.effectIndex ?: if (!tabbed) effects.firstOrNull()?.effectId else null)
    }
    var known by remember(layerId) { mutableStateOf(effects.map { it.effectId }.toSet()) }
    var selected by remember(layerId) {
        mutableStateOf(entry?.takeIf { it.property == TrackProperty.EFFECT_PARAM }?.let { ParamKey(it.effectIndex, it.paramIndex / 4, it.paramIndex % 4) })
    }
    var selectedMesh by remember(layerId) { mutableStateOf(entry?.takeIf { it.property == TrackProperty.MESH_WARP }?.effectIndex) }
    var advancedOpen by remember(layerId) {
        mutableStateOf(
            entry?.let { k ->
                val visible = store.effectParams[k.effectIndex].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
                if (effectLayout(store.typeOf(k.effectIndex), visible).advanced.any { it.index == k.paramIndex / 4 }) setOf(k.effectIndex) else emptySet()
            } ?: emptySet(),
        )
    }
    // Guia longo no 1º parâmetro de ponto do efeito que abriu sozinho.
    var longGuideFor by remember(layerId) { mutableStateOf<Int?>(null) }

    // --- Abrir / fechar os controles (recorte + deslocamento a partir da linha) ---
    val scope = rememberCoroutineScope()
    val density = LocalDensity.current
    val gapPx = with(density) { FxRow.Gap.toPx() }
    val rowPx = with(density) { FxRow.List.toPx() }
    val rowTops = remember(layerId) { mutableStateMapOf<Int, Float>() }
    var containerTop by remember { mutableFloatStateOf(0f) }
    val progress = remember(layerId, focusedType) { Animatable(if (openId != null) 1f else 0f) }
    var fromY by remember { mutableFloatStateOf(0f) }
    var closing by remember { mutableStateOf(false) }
    val listState = rememberLazyListState()
    val bodyState = rememberLazyListState()
    fun openEffect(id: Int) {
        fromY = (rowTops[id] ?: gapPx) - gapPx
        closing = false
        openId = id
        scope.launch {
            progress.snapTo(0f)
            progress.animateTo(1f, tween(FxParamMath.OPEN_MS, easing = FxAccelDecel))
        }
    }
    fun closeEffect(animated: Boolean) {
        val id = openId ?: return
        if (!tabbed) return
        EffectPointStage.hide()
        if (!animated) {
            openId = null
            closing = false
            scope.launch { progress.snapTo(0f) }
            return
        }
        fromY = (rowTops[id] ?: (fromY + gapPx)) - gapPx
        closing = true
        scope.launch {
            progress.animateTo(0f, tween(FxParamMath.OPEN_MS, easing = FxAccelDecel))
            if (closing) {
                openId = null
                closing = false
            }
        }
    }

    // Efeito novo: abre sozinho SÓ se a camada não tinha efeito (`autoSelectEffect`
    // da referência); o aberto que sumiu fecha. Fora da composição (bug B-09).
    LaunchedEffect(effects, store.pendingEffectFocus) {
        val ids = effects.map { it.effectId }.toSet()
        val added = ids - known
        val previous = known.size
        known = ids
        val request = store.pendingEffectFocus?.takeIf { it.layer == layerId }
        val requested = effects.indexOfLast { request != null && it.typeId == request.type && it.effectId !in request.previous }
        if (requested >= 0) store.consumeEffectFocus()
        if (!tabbed) {
            if (openId == null || openId !in ids) openId = effects.firstOrNull()?.effectId
            return@LaunchedEffect
        }
        val target = when {
            // Pedido EXPLÍCITO de outro painel (Texto: "+ animador"; Fantoche no
            // palco): é navegação, abre o efeito pedido sempre.
            requested >= 0 -> effects[requested].effectId
            // Adicionado pela lista/navegador: só o 1º efeito da camada abre sozinho.
            FxParamMath.opensAfterAdd(previous, added.size) -> effects.lastOrNull { it.effectId in added }?.effectId
            else -> null
        }
        if (target != null) {
            if (target in added && previous == 0) longGuideFor = target
            androidx.compose.runtime.withFrameNanos { }
            if (openId != target) openEffect(target)
        } else if (openId != null && openId !in ids) {
            closeEffect(false)
        }
    }
    // Os controles abertos escolhem a sua primeira linha PRINCIPAL (◆± e Curva miram nela).
    LaunchedEffect(openId, effects, openId?.let { store.effectParams[it] }) {
        val id = openId
        if (id == null) {
            selected = null
            selectedMesh = null
            return@LaunchedEffect
        }
        if (effects.any { it.effectId == id && it.typeId == effectTypeId("aurea.distort.mesh_warp") } && selected?.effectId != id) {
            selected = null; selectedMesh = id
            return@LaunchedEffect
        }
        if (selected?.effectId == id) return@LaunchedEffect
        selectedMesh = null
        val visible = store.effectParams[id].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
        val layout = effectLayout(store.typeOf(id), visible)
        val first = layout.all.firstOrNull { it.animatable && it.enabled } ?: layout.all.firstOrNull { it.components > 0 }
        selected = first?.let { ParamKey(id, it.index, 0) }
        // 1º efeito da camada aberto sozinho: um ponto já entra ATIVO com o guia longo.
        if (longGuideFor == id && first != null && layerId != null) {
            longGuideFor = null
            val p = store.paramOf(id, first.index)
            if (p != null && com.aurea.aurea.editor.effectParamDraggable(p.type, p.flags)) {
                EffectPointStage.show(layerId, id, first.index, 0, FxParamMath.controlFor(p.control, p.type))
                EffectPointStage.showGuide(paramDisplay(store.typeOf(id), first).let { d -> d.labelRes?.let { context.getString(it) } ?: d.engineLabel }, FxParamMath.Guide.Long)
            }
        }
    }
    // Entrou pelo losango de um parâmetro que mora numa seção recolhida: a seção abre.
    LaunchedEffect(entry) {
        val k = entry?.takeIf { it.property == TrackProperty.EFFECT_PARAM } ?: return@LaunchedEffect
        val visible = store.effectParams[k.effectIndex].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
        val closed = closedSectionOf(effectLayout(store.typeOf(k.effectIndex), visible), k.paramIndex / 4)
        if (closed > 0) store.setEffectSectionOpen(k.effectIndex, closed, true)
    }
    // Parâmetro ativo no preview: some quando o efeito fecha ou a linha escolhida muda de efeito.
    LaunchedEffect(openId, selected?.effectId) {
        val s = EffectPointStage
        if (s.param >= 0 && (openId == null || s.effect != openId || s.layer != layerId)) s.hide()
    }
    DisposableEffect(layerId) { onDispose { EffectPointStage.hide() } }

    // --- Mesh / fantoche / foco da timeline (como antes) -------------------------
    val meshOpen = openId != null && effects.any { it.effectId == openId && it.typeId == effectTypeId("aurea.distort.mesh_warp") }
    val meshSelected = meshOpen && selectedMesh == openId
    val puppet = com.aurea.aurea.editor.PuppetStage
    val puppetOpen = layerId != null && puppet.layer == layerId && puppet.effect == openId &&
        effects.any { it.effectId == openId && it.typeId == puppet.TYPE }
    val activePins = remember(store, layerId, openId, puppetOpen, store.rigRevision, store.curveRevision) {
        if (puppetOpen && layerId != null && openId != null) store.puppetPins(layerId, openId!!) else FloatArray(0)
    }
    val focus = if (meshSelected) listOf(TrackKey(TrackProperty.MESH_WARP, openId!!, 0))
    else if (puppetOpen && (puppet.editing || selected == null)) puppetPanelTimelineFocus(puppet.effect, puppet.selected, activePins)
    else selected?.let { listOf(TrackKey(31, it.effectId, it.param * 4 + it.component)) } ?: emptyList()
    val puppetKey = if (!meshSelected && puppetOpen && (puppet.editing || selected == null))
        focus.firstOrNull()?.let { ParamKey(it.effectIndex, it.paramIndex / 4, it.paramIndex % 4) } else null
    val railKey = puppetKey ?: selected
    DisposableEffect(store, layerId, focus) {
        store.timelineFocus = focus
        onDispose { store.timelineFocus = null }
    }

    // O trilho só precisa do ESTADO do losango — derivado, não do valor que anda.
    val railLook by remember(store, meshSelected, openId, puppetKey) {
        derivedStateOf {
            if (meshSelected) {
                val keys = store.primaryKeys().filter { it.property == TrackProperty.MESH_WARP && it.effectIndex == openId }
                val local = store.detail?.localFrame(store.playhead)
                if (keys.any { it.time == local }) KeyframeLook.KeyHere else if (keys.isEmpty()) KeyframeLook.None else KeyframeLook.Animated
            } else (puppetKey ?: selected)?.let { effectLook(store, it.effectId, it.param) } ?: KeyframeLook.None
        }
    }
    val railAnimatable by remember(store, meshSelected, puppetKey) {
        derivedStateOf {
            openId != null && (meshSelected || ((puppetKey ?: selected)?.let { k -> store.paramOf(k.effectId, k.param)?.let { ParamSlot.of(it).animatable } } ?: false))
        }
    }
    val curveTrack: () -> List<com.aurea.aurea.engine.KeyframeRow> = {
        val mesh = openId
        if (meshSelected && mesh != null) store.primaryKeys().filter { it.property == TrackProperty.MESH_WARP && it.effectIndex == mesh }.sortedBy { it.time }
        else railKey?.let { k -> store.primaryKeys().effectTrack(k.effectId, k.param, k.component) } ?: emptyList()
    }
    val latestCurveTrack by rememberUpdatedState(curveTrack)
    val curveTrackReady by remember(store, puppetKey, meshSelected, openId) { derivedStateOf { latestCurveTrack().size >= 2 } }
    val curveKey: () -> com.aurea.aurea.engine.KeyframeRow? = {
        val track = curveTrack()
        store.curveStart(track) ?: store.detail?.localFrame(store.playhead)?.let { track.segmentStart(it) }
    }
    val exprSlot = railKey?.let { k -> store.paramOf(k.effectId, k.param)?.let { ParamSlot.of(it) } }?.takeIf { it.animatable && !meshSelected }
    val exprLook by remember(store, railKey) {
        derivedStateOf { railKey?.let { k -> store.paramOf(k.effectId, k.param)?.let { store.expressionLook(paramKeys(k.effectId, ParamSlot.of(it))) } } ?: com.aurea.aurea.engine.ExpressionLook.None }
    }

    // Projeto de efeitos: qualquer arquivo (o .amproj/.xml não tem MIME padrão).
    val amPicker = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            val target = store.primary
            scope.launch {
                val bytes = withContext(Dispatchers.IO) {
                    runCatching { context.contentResolver.openInputStream(uri)?.use { it.readPreset() } }.getOrNull()
                }
                if (bytes != null && store.primary == target) store.importAlightMotion(bytes)
                else store.showToast(context.getString(R.string.am_import_failed, context.getString(R.string.am_import_read_failed)))
            }
        }
    }
    val reorder = remember(layerId) { ReorderState() }
    val byId = effects.associateBy { it.effectId }
    val order = (reorder.order ?: effects.map { it.effectId }).mapNotNull { byId[it] }
    val openAdd: () -> Unit = { if (tabbed) adding = true else env.onOpenEffectsBrowser() }
    // A área de transferência (copiar/colar): o tipo do efeito único é lido do motor
    // só quando ela muda.
    val clipboardType = remember(store.clipboard, store.effects) { store.clipboardEffectType() }

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

    // --- ⋯ da lista (coluna esquerda): inativo com os controles abertos ---------
    val anyOn = effects.any { it.enabled }
    val listMenu: List<MenuEntry> = buildList {
        add(MenuEntry.Item(stringResource(R.string.panel_copiar_efeitos), enabled = effects.isNotEmpty(), tag = "effects.list.copy") { store.copyEffects() })
        add(
            MenuEntry.Item(
                stringResource(if (clipboardType != 0) R.string.om_fx_list_paste_one else R.string.panel_colar_efeitos),
                enabled = store.clipboard and 4 != 0 && !locked, tag = "effects.list.paste",
            ) { store.pasteEffects() },
        )
        add(MenuEntry.Divider)
        add(
            MenuEntry.Item(stringResource(if (anyOn) R.string.panel_desligar_todos else R.string.panel_ligar_todos), enabled = effects.isNotEmpty(), tag = "effects.list.toggle_all") {
                store.beginGesture(if (anyOn) "desligar efeitos" else "ligar efeitos")
                try {
                    effects.forEach { if (it.enabled == anyOn) store.setEffectEnabled(it.effectId, !anyOn) }
                } finally {
                    store.endGesture()
                }
            },
        )
        add(MenuEntry.Item(stringResource(R.string.fx_custom_create) + "…", enabled = effects.isNotEmpty(), tag = "effects.list.create") { saveDialog = -1 })
        add(MenuEntry.Item(stringResource(R.string.fx_custom_library), tag = "effects.list.library") {
            store.presetsOpenKind = com.aurea.aurea.presets.PresetKind.Effects
            env.onOpenPanel(EditorPanel.Presets)
        })
        add(MenuEntry.Item(stringResource(R.string.am_import_action), tag = "effects.list.import") { amPicker.launch(arrayOf("*/*")) })
    }

    PanelFrame(
        rail = PanelRail(
            onBack = env.onClose,
            keyframeLook = if (openId != null) railLook else KeyframeLook.None,
            onKeyframe = if (railAnimatable) {
                {
                    // Lido NO TOQUE: o parâmetro e o cabeçote de agora.
                    if (meshSelected) store.primary?.let { layer -> openId?.let { store.meshWarpToggleKey(layer, it) } }
                    else railKey?.let { k ->
                        store.paramOf(k.effectId, k.param)?.let { store.toggleEffectKeyframe(k.effectId, it) }
                        // P1-5: o corpo rola até a linha do parâmetro ativo.
                        val rowIndex = fxBodyIndexOf(store, k, k.effectId in advancedOpen)
                        if (rowIndex >= 0) scope.launch { bodyState.animateScrollToItem(rowIndex) }
                    }
                }
            } else {
                null
            },
            onDeleteAllKeyframes = if (railAnimatable && !meshSelected) {
                { railKey?.let { k -> deleteParamKeyframes(store, k) } }
            } else {
                null
            },
            onCurve = if (railAnimatable) {
                {
                    val layer = store.primary
                    if (layer != null) curveKey()?.let { key ->
                        store.selectKeyframe(layer, key)
                        env.onOpenPanel(EditorPanel.Curve)
                    }
                }
            } else {
                null
            },
            curveReady = curveTrackReady && curveKey() != null,
            expression = exprLook,
            onExpression = exprSlot?.let { s ->
                {
                    val k = railKey
                    if (k != null) openParamExpression(store, k.effectId, s, paramDisplay(store.typeOf(k.effectId), s).let { d -> d.labelRes?.let { context.getString(it) } ?: d.engineLabel })
                }
            },
            moreEntries = if (tabbed) listMenu else null,
            moreEnabled = openId == null,
        ),
        scrollContent = false,
    ) {
        Box(
            Modifier
                .fillMaxSize()
                .background(AureaColors.FxPanel)
                .onGloballyPositioned { containerTop = it.positionInRoot().y },
        ) {
            if (tabbed) {
                // A LISTA: some no INÍCIO da abertura (referência: invisível antes da animação).
                LazyColumn(
                    Modifier
                        .fillMaxSize()
                        .alpha(if (openId == null) 1f else 0f)
                        .testTag("aurea.effects.stack"),
                    state = listState,
                    userScrollEnabled = openId == null,
                    contentPadding = PaddingValues(start = FxRow.Gap, end = FxRow.Gap, bottom = FxRow.Gap),
                ) {
                    if (kind == LayerType.Adjustment.kind) item(key = "ajuste") { AdjustmentIntensity(env) }
                    items(tools, key = { "tool." + it.key }) { tool ->
                        FxToolRow(
                            tool = tool,
                            count = when (tool) {
                                EffectTool.Mask -> maskCount
                                EffectTool.Captions -> captionCount
                                EffectTool.CameraTrack -> 0
                            },
                            onOpen = { openEffectTool(env, tool) },
                            onRemove = if (tool == EffectTool.CameraTrack) null else ({ toolToRemove = tool }),
                            modifier = Modifier.animateItem(),
                        )
                    }
                    items(order, key = { it.effectId }) { e ->
                        val dragged = reorder.dragging == e.effectId
                        val name = effectDisplayName(e.typeId, e.name)
                        FxSwipeToDelete(
                            enabled = openId == null && !dragged && !locked,
                            removeLabel = stringResource(R.string.fxui_a11y_remove, name),
                            onDelete = { store.removeEffectWithUndo(e.effectId) },
                            modifier = (if (dragged) Modifier.zIndex(1f).graphicsLayer { translationY = reorder.offset } else Modifier.animateItem())
                                .padding(top = FxRow.Gap)
                                .onGloballyPositioned { rowTops[e.effectId] = it.positionInRoot().y - containerTop },
                        ) {
                            FxListRow(
                                store = store,
                                effect = e,
                                name = name,
                                lifted = dragged,
                                onOpen = {
                                    if (!e.known) unknownMenu = e else if (openId == null) openEffect(e.effectId)
                                },
                                onToggleEnabled = { store.setEffectEnabled(e.effectId, !e.enabled) },
                                onMove = { delta ->
                                    val i = effects.indexOfFirst { it.effectId == e.effectId }
                                    val j = i + delta
                                    if (i >= 0 && j in effects.indices) store.reorderEffect(e.effectId, j)
                                },
                                onRemove = { store.removeEffectWithUndo(e.effectId) },
                                canMoveUp = effects.firstOrNull()?.effectId != e.effectId,
                                canMoveDown = effects.lastOrNull()?.effectId != e.effectId,
                                dragEnabled = openId == null && !locked,
                                reorder = reorder,
                                listState = listState,
                                visibleIds = effects.map { it.effectId },
                            )
                        }
                    }
                    item(key = "rodape") { FxAddRow(onAdd = openAdd) }
                }
            } else if (effects.isEmpty() && focusedType != null) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Box(
                        Modifier.fillMaxWidth().padding(FxRow.Gap).height(FxRow.List).clip(RoundedCornerShape(5.dp))
                            .background(AureaColors.FxLowSurface).tocavel(shrink = 1f) { store.addEffect(focusedType) },
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(stringResource(R.string.t3d_enable_letters), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
                    }
                }
            }

            // OS CONTROLES, sobre a lista, nascendo da linha tocada.
            val shownId = openId
            val shown = shownId?.let { id -> effects.firstOrNull { it.effectId == id } }
            if (shown != null) {
                FxSettingsOverlay(
                    env = env,
                    effect = shown,
                    focused = !tabbed,
                    locked = locked,
                    selected = selected,
                    meshSelected = meshSelected,
                    advancedOpen = shown.effectId in advancedOpen,
                    bodyState = bodyState,
                    clipboardType = clipboardType,
                    onClose = { closeEffect(true) },
                    onRemove = {
                        closeEffect(false)
                        store.removeEffectWithUndo(shown.effectId)
                    },
                    onDuplicate = {
                        store.duplicateEffect(shown.effectId)
                        closeEffect(true)
                    },
                    onDetails = { store.catalog.firstOrNull { it.typeId == shown.typeId }?.let { aboutEntry = it } },
                    onSavePreset = { saveDialog = shown.effectId },
                    onToggleAdvanced = {
                        advancedOpen = if (shown.effectId in advancedOpen) advancedOpen - shown.effectId else advancedOpen + shown.effectId
                    },
                    onSelect = { k, explicit ->
                        selectedMesh = null
                        selected = k
                        val prm = store.paramOf(k.effectId, k.param)
                        if (explicit && layerId != null && prm != null) {
                            if (com.aurea.aurea.editor.effectParamDraggable(prm.type, prm.flags) && prm.enabled) {
                                // P1-2: ativa no preview e mostra o guia "deslize para ajustar".
                                EffectPointStage.show(layerId, k.effectId, k.param, k.component, FxParamMath.controlFor(prm.control, prm.type))
                                EffectPointStage.showGuide(paramDisplay(shown.typeId, ParamSlot.of(prm)).let { d -> d.labelRes?.let { context.getString(it) } ?: d.engineLabel }, FxParamMath.Guide.Short)
                            } else {
                                EffectPointStage.hide()
                            }
                        } else if (EffectPointStage.param >= 0 && EffectPointStage.isActive(layerId, k.effectId, k.param)) {
                            EffectPointStage.component = k.component
                        }
                    },
                    onSelectMesh = { selected = null; selectedMesh = shown.effectId },
                    onParamMenu = { paramMenu = it },
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(FxRow.Gap)
                        // Lido no desenho: a animação não recompõe o corpo a cada quadro.
                        .graphicsLayer { translationY = lerp(fromY, 0f, progress.value) }
                        .drawWithContent {
                            val h = lerp(rowPx, size.height, progress.value)
                            clipRect(0f, 0f, size.width, h) { this@drawWithContent.drawContent() }
                        },
                )
            }
        }
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

    // Efeito que saiu do catálogo: só entender e tirar (A.01).
    unknownMenu?.let { e ->
        AureaActionSheet(
            title = stringResource(R.string.panel_efeito_removido),
            message = stringResource(R.string.panel_este_efeito_saiu_aurea_nao_desenha),
            actions = listOf(SheetAction(stringResource(R.string.panel_remover_efeito), destructive = true) { store.removeEffect(e.effectId) }),
            cancelLabel = stringResource(R.string.panel_manter),
            onDismiss = { unknownMenu = null },
        )
    }

    aboutEntry?.let { entry ->
        // "Detalhes do efeito" (⋮): a ficha, SEM variações (o efeito já está na camada).
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

    saveDialog?.let { target ->
        var name by remember(target) { mutableStateOf(TextFieldValue("")) }
        val save = {
            val n = name.text.trim()
            if (n.isNotEmpty()) {
                val ok = if (target >= 0) store.saveEffectPreset(target, n)
                else store.savePreset(com.aurea.aurea.presets.PresetKind.Effects, n, store.capturePreset(com.aurea.aurea.presets.PresetKind.Effects, n))
                if (ok) saveDialog = null
            }
        }
        AureaDialog(
            title = stringResource(if (target >= 0) R.string.om_fx_menu_save_preset else R.string.fx_custom_create),
            actions = listOf(
                DialogAction(stringResource(R.string.fx_custom_cancel), DialogRole.Negative),
                DialogAction(stringResource(R.string.fx_custom_save), DialogRole.Positive, enabled = name.text.isNotBlank(), dismiss = false, tag = "effects.save.confirm") { save() },
            ),
            onDismiss = { saveDialog = null },
            tag = "effects.save",
        ) {
            AureaDialogField(
                value = name,
                onValueChange = { name = it.copy(text = it.text.take(60)) },
                placeholder = stringResource(R.string.fx_custom_name),
                tag = "dialog.field",
            )
        }
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
                    add(SheetAction(stringResource(R.string.panel_expressao_3c65)) { openParamExpression(store, t.effectId, slot, t.label) })
                }
            },
            onDismiss = { paramMenu = null },
        )
    }
}

/**
 * O índice, no corpo dos controles, da linha do parâmetro [k] (P1-5: depois do
 * ◆±, o corpo rola até ela). −1 = não está à vista (seção recolhida).
 */
private fun fxBodyIndexOf(store: EditorStore, k: ParamKey, advancedOpen: Boolean): Int {
    val typeId = store.typeOf(k.effectId)
    val visible = store.effectParams[k.effectId].orEmpty().map { ParamSlot.of(it) }.filter { !it.hidden }
    return fxBodyItems(store, k.effectId, typeId, visible, advancedOpen).indexOfFirst { it is FxBody.Param && it.slot.index == k.param }
}

/** Apaga TODAS as marcas do parâmetro (segurar ◆): um passo de desfazer. */
private fun deleteParamKeyframes(store: EditorStore, k: ParamKey) {
    val layer = store.primary ?: return
    val keys = store.primaryKeys().filter {
        it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == k.effectId && it.paramIndex / 4 == k.param
    }
    if (keys.isEmpty()) return
    store.beginGesture("apagar keyframes")
    try {
        keys.forEach { store.deleteKeyframe(layer, it) }
    } finally {
        store.endGesture()
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
 * RESTAURAR PADRÃO: todos os parâmetros ao padrão NO CABEÇOTE, num passo de
 * desfazer só (parâmetro animado ganha a marca com o padrão, como qualquer edição).
 */
private fun resetEffect(store: EditorStore, effectId: Int) {
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

/**
 * "Preset Turbulência dinâmica" (⋮ do efeito Turbulência; DECISOES: também no
 * Android, paridade com o iOS): quantidade e tamanho 15, complexidade 1, o resto
 * zerado e a evolução por expressão `time*6`. Um passo de desfazer.
 */
private fun applyTurbulencePreset(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    if (store.detail?.locked == true) return
    store.beginGesture("Preset Turbulência dinâmica")
    try {
        listOf(0 to 15f, 1 to 15f, 2 to 1f, 3 to 0f, 4 to 0f, 5 to 0f, 6 to 0f).forEach { (i, v) ->
            store.paramOf(effectId, i)?.let { store.setEffectParam(effectId, it, v) }
        }
    } finally {
        store.endGesture()
    }
    val label = store.paramOf(effectId, 24)?.label.orEmpty()
    store.applyExpression(
        EditorStore.ExpressionTarget(layer, label, listOf(TrackKey(TrackProperty.EFFECT_PARAM, effectId, 24 * 4))),
        "time*6",
    )?.takeIf { !it.ok }?.let { store.showToast(it.message) }
}

/**
 * Reordenar pelo ≡ (grip): arrasto vertical imediato, só dele (não briga com a
 * rolagem da lista). Soltar grava a ordem com UM comando.
 * 0.3.6 (sidebar): só VERTICAL — o arrasto em qualquer direção tomava também o
 * deslizar para a esquerda que começa no ≡ (canto direito da linha, onde o dedo
 * costuma começar), e o efeito não saía.
 */
private fun Modifier.reorderHandle(
    id: Int,
    state: ReorderState,
    list: LazyListState,
    store: EditorStore,
    visible: List<Int>,
    enabled: Boolean,
): Modifier = if (!enabled) this else this.then(
    Modifier.pointerInput(id, visible) {
        detectVerticalDragGestures(
            onDragStart = { state.begin(id, visible) },
            onVerticalDrag = { change, amount ->
                change.consume()
                state.offset += amount
                state.swapIfNeeded(list)
            },
            onDragEnd = { state.finish(store) },
            onDragCancel = { state.finish(store) },
        )
    },
)

/**
 * A LINHA INTEIRA: toque = abrir os controles; TOQUE LONGO = erguer e arrastar
 * para reordenar (`ItemTouchHelper` da referência; Inf forte). Andar antes do
 * toque longo solta o gesto para a rolagem (vertical) ou o deslizar (horizontal).
 */
private fun Modifier.reorderLongPress(
    id: Int,
    state: ReorderState,
    list: LazyListState,
    store: EditorStore,
    visible: List<Int>,
    enabled: Boolean,
    onTap: () -> Unit = {},
    onLift: () -> Unit = {},
): Modifier = this.then(
    Modifier.pointerInput(id, visible, enabled) {
        val slop = viewConfiguration.touchSlop
        val longPress = viewConfiguration.longPressTimeoutMillis
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = true)
            // 1 = toque, 2 = andou (não é nosso), null = toque longo.
            val outcome: Int? = withTimeoutOrNull(longPress) {
                var result = 0
                while (result == 0) {
                    val e = awaitPointerEvent()
                    val c = e.changes.firstOrNull { it.id == down.id }
                    result = when {
                        c == null || c.isConsumed -> 2
                        !c.pressed -> 1
                        (c.position - down.position).getDistance() > slop -> 2
                        else -> 0
                    }
                }
                result
            }
            when (outcome) {
                1 -> onTap()
                2 -> Unit
                else -> {
                    if (!enabled) return@awaitEachGesture
                    onLift()
                    state.begin(id, visible)
                    try {
                        while (true) {
                            val e = awaitPointerEvent()
                            val c = e.changes.firstOrNull { it.id == down.id } ?: break
                            if (!c.pressed) break
                            val dy = c.position.y - c.previousPosition.y
                            c.consume()
                            state.offset += dy
                            state.swapIfNeeded(list)
                        }
                    } finally {
                        state.finish(store)
                    }
                }
            }
        }
    },
)

/**
 * TIRA uma ferramenta-efeito da camada, num passo de desfazer: todas as
 * máscaras, ou as legendas geradas (pelo fluxo das legendas). O rastreio de
 * câmera não tem "tirar".
 */
private fun removeEffectTool(store: EditorStore, tool: EffectTool) {
    val layer = store.primary ?: return
    when (tool) {
        EffectTool.Mask -> {
            store.dropMaskTool(layer)
            val ids = store.masks?.takeIf { it.layer == layer }?.masks?.map { it.id }.orEmpty()
            val matte = (store.trackMatte?.getOrNull(0) ?: 0L) != 0L
            if (ids.isEmpty() && !matte) return
            store.beginGesture("remover máscaras")
            try {
                ids.forEach { store.deleteMask(it) }
                if (matte) store.setTrackMatte(0L, 0)
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

// =============================================================================
// A LISTA (P0-2): linhas de 37 dp
// =============================================================================

/**
 * A LINHA DE UM EFEITO (`effect_list_item`): 37 dp, raio 5, «superfície de
 * lista» sem realce de toque; `▸` 18 a 30 % · nome 14 · selo 35 (cadeado Hyper)
 * · 👁 (alvo 44×37) · ≡ a 15 % (alvo 32×37). Desligado: nome a 45 %.
 * testTags `effects.expand|eye|grip.<id>` (iguais no iOS).
 */
@Composable
private fun FxListRow(
    store: EditorStore,
    effect: LayerEffect,
    name: String,
    lifted: Boolean,
    onOpen: () -> Unit,
    onToggleEnabled: () -> Unit,
    onMove: (Int) -> Unit,
    onRemove: () -> Unit,
    canMoveUp: Boolean,
    canMoveDown: Boolean,
    dragEnabled: Boolean,
    reorder: ReorderState,
    listState: LazyListState,
    visibleIds: List<Int>,
) {
    val id = effectCardId(effect.typeId)
    val haptic = LocalHapticFeedback.current
    val openLabel = stringResource(R.string.fxui_a11y_expand, name)
    val up = stringResource(R.string.panel_mover_cima)
    val down = stringResource(R.string.panel_mover_baixo)
    val remove = stringResource(R.string.fxui_a11y_remove, name)
    val shownName = if (effect.known) name else "?$name"
    val hyperLocked = HyperEffectGate.locked(effect.typeId)
    // Selo (35): cadeado Hyper, "!" de efeito pesado ou a peça do plugin (paridade com o iOS).
    val heavy = !hyperLocked && store.catalog.firstOrNull { it.typeId == effect.typeId }
        ?.let { com.aurea.aurea.effects.effectCost(it.effectClass) >= 3 } == true
    val plugin = !hyperLocked && !heavy && com.aurea.aurea.effects.PluginTexts.isPlugin(effect.typeId)
    Row(
        Modifier
            .fillMaxWidth()
            .height(FxRow.List)
            .clip(RoundedCornerShape(5.dp))
            .background(if (lifted) AureaColors.FxSegmentOn else AureaColors.FxListSurface),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier
                .weight(1f)
                .height(FxRow.List)
                .testTag("effects.expand.$id")
                .semantics {
                    contentDescription = openLabel
                    role = Role.Button
                    onClick(openLabel) { onOpen(); true }
                    customActions = buildList {
                        if (canMoveUp) add(CustomAccessibilityAction(up) { onMove(-1); true })
                        if (canMoveDown) add(CustomAccessibilityAction(down) { onMove(1); true })
                        add(CustomAccessibilityAction(remove) { onRemove(); true })
                    }
                }
                .reorderLongPress(
                    effect.effectId, reorder, listState, store, visibleIds, dragEnabled,
                    onTap = onOpen,
                    onLift = { haptic.performHapticFeedback(HapticFeedbackType.LongPress) },
                )
                .padding(start = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.ChevronRight, 18.dp, AureaColors.Text.copy(alpha = 0.3f))
            Spacer(Modifier.width(6.dp))
            Text(
                shownName,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f).alpha(if (effect.enabled) 1f else 0.45f),
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
            )
            Box(Modifier.width(35.dp), contentAlignment = Alignment.Center) {
                when {
                    hyperLocked -> CupertinoIcon(CupertinoGlyph.LockFill, 14.dp, AureaColors.FxDisabledText)
                    heavy -> Text("!", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.Black, color = AureaColors.FxDisabledText)))
                    plugin -> CupertinoIcon(CupertinoGlyph.SquareStack3dUp, 14.dp, AureaColors.FxDisabledText)
                }
            }
        }
        FxIconTarget(
            44.dp, FxRow.List,
            stringResource(if (effect.enabled) R.string.fxui_a11y_disable else R.string.fxui_a11y_enable, name),
            Modifier.testTag("effects.eye.$id"),
            onClick = onToggleEnabled,
        ) {
            CupertinoIcon(
                if (effect.enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 18.dp,
                if (effect.enabled) AureaColors.Text else AureaColors.FxDisabledText,
            )
        }
        val dragLabel = stringResource(R.string.fxui_a11y_drag, name)
        Box(
            Modifier
                .reorderHandle(effect.effectId, reorder, listState, store, visibleIds, dragEnabled)
                .size(32.dp, FxRow.List)
                .testTag("effects.grip.$id")
                .semantics { contentDescription = dragLabel },
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.LineHorizontal3, 16.dp, AureaColors.Text.copy(alpha = if (lifted) 0.6f else 0.15f))
        }
    }
}

/**
 * A LINHA DE UMA FERRAMENTA-EFEITO (Máscara, Rastreio, Legendas) no formato da
 * lista: glifo no lugar da seta, contagem no lugar do selo, 🗑 no lugar do olho.
 * Tocar abre a ferramenta. testTag `effects.tool.<chave>`.
 */
@Composable
private fun FxToolRow(tool: EffectTool, count: Int, onOpen: () -> Unit, onRemove: (() -> Unit)?, modifier: Modifier = Modifier) {
    val name = stringResource(toolLabelRes(tool))
    val openLabel = stringResource(R.string.fxui_a11y_open, name)
    val removeLabel = stringResource(R.string.fxui_a11y_remove, name)
    Row(
        modifier
            .padding(top = FxRow.Gap)
            .fillMaxWidth()
            .height(FxRow.List)
            .clip(RoundedCornerShape(5.dp))
            .background(AureaColors.FxListSurface),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier
                .weight(1f)
                .height(FxRow.List)
                .testTag("effects.tool." + tool.key.substringAfterLast('.'))
                .semantics { contentDescription = openLabel; role = Role.Button }
                .tocavel(shrink = 1f, onClick = onOpen)
                .padding(start = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(toolGlyph(tool), 18.dp, AureaColors.Accent)
            Spacer(Modifier.width(6.dp))
            Text(
                name,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
            )
            Box(Modifier.width(35.dp), contentAlignment = Alignment.Center) {
                if (count > 0) Text("$count", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.FxTipText)))
            }
        }
        if (onRemove != null) {
            FxIconTarget(44.dp, FxRow.List, removeLabel, Modifier.testTag("effects.tool.remove." + tool.key.substringAfterLast('.')), onClick = onRemove) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
            }
        } else {
            Spacer(Modifier.width(44.dp))
        }
        Spacer(Modifier.width(32.dp))
    }
}

/**
 * "+ ADICIONAR EFEITO" (`effect_list_add`): 37 dp, margem 6,5, raio 5, «superfície
 * baixa» (pressionado «superfície de lista»), "+" 12 e o texto centralizados.
 * Contêiner `effects.add.row`, tocável `aurea.effects.add` (mantido).
 */
@Composable
private fun FxAddRow(onAdd: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Box(Modifier.fillMaxWidth().padding(top = FxRow.Gap).testTag("effects.add.row")) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(FxRow.List)
                .clip(RoundedCornerShape(5.dp))
                .background(if (pressed) AureaColors.FxListSurface else AureaColors.FxLowSurface)
                .testTag("aurea.effects.add")
                .semantics { role = Role.Button }
                .clickable(interactionSource = interaction, indication = null, onClick = onAdd),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Plus, 12.dp, AureaColors.Text)
            Spacer(Modifier.width(6.dp))
            Text(stringResource(R.string.panel_adicionar_efeito), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
        }
    }
}

/**
 * "Intensidade da camada de ajuste" (extra do AUREA, acima da lista): a opacidade
 * da camada, 0–100 %, como LINHA DE SLIDER da referência (51 dp; encaixe em 100).
 * ◆ no rótulo quando anima; tocar no valor abre o teclado. ids `effects.intensity` e `effects.intensity.slider` (iguais no iOS).
 */
@Composable
private fun AdjustmentIntensity(env: PanelEnv) {
    val store = env.store
    val opacity by remember(store) { derivedStateOf { (store.detail?.opacity ?: 1f) * 100f } }
    val look by remember(store) { derivedStateOf { transformLook(store.detail, intArrayOf(TrackProperty.OPACITY)) } }
    val label = stringResource(R.string.panel_intensidade_camada_ajuste)
    val text = "${numeroPtBr(opacity, 0)}%"
    Row(
        Modifier
            .padding(top = FxRow.Gap)
            .fillMaxWidth()
            .height(FxRow.Slider)
            .clip(RoundedCornerShape(5.dp))
            .background(AureaColors.FxListSurface)
            .testTag("effects.intensity"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        com.aurea.aurea.ui.ds.FxParamLabel(
            label, FxRow.Slider - FxRow.Margin * 2, Modifier.padding(FxRow.Margin),
            // O ◆ mostra a animação; marcar keyframe de opacidade é da Aparência (Mesclagem e opacidade).
            keyframe = look,
        )
        com.aurea.aurea.ui.ds.AureaSlider(
            value = opacity, min = 0f, max = 100f, step = 1f, origin = 0f, snap = 100f,
            onStart = { store.beginGesture("opacidade") },
            onValue = { store.setTransform(TrackProperty.OPACITY, it.coerceIn(0f, 100f) / 100f) },
            onEnd = { store.endGesture() },
            modifier = Modifier.weight(1f).height(FxRow.Slider).padding(horizontal = 8.dp).testTag("effects.intensity.slider"),
            a11yLabel = label,
            a11yValue = text,
        )
        com.aurea.aurea.ui.ds.FxValueBox(
            text, FxRow.Slider - FxRow.Margin * 2, Modifier.padding(FxRow.Margin),
            onClick = {
                env.openKeypad(KeypadRequest(label, opacity, "%", 0f, 100f, 0) { store.setTransform(TrackProperty.OPACITY, it / 100f) })
            },
        )
    }
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

// =============================================================================
// OS CONTROLES SOBREPOSTOS (P0-3)
// =============================================================================

/** Um item do corpo dos controles, na ordem da tela. */
@Immutable
private sealed interface FxBody {
    val key: String

    data class Special(val what: Int) : FxBody { override val key get() = "special.$what" }
    data class Notice(@androidx.annotation.StringRes val text: Int) : FxBody { override val key get() = "notice.$text" }
    data class Variation(val presets: List<Pair<String, String>>) : FxBody { override val key get() = "variation" }
    data class Param(val slot: ParamSlot, val disabledBy: String?) : FxBody { override val key get() = "param.${slot.index}" }
    data class Section(val section: FxSection, val advanced: Boolean) : FxBody {
        override val key get() = if (advanced) "section.advanced" else "section.${section.ordinal}"
    }
}

private const val SPECIAL_ROTO = 1
private const val SPECIAL_LOCAL_AI = 2
private const val SPECIAL_TIME_REMAP = 3
private const val SPECIAL_MESH = 4
private const val SPECIAL_PUPPET = 5
private const val SPECIAL_SABER = 6
private const val SPECIAL_EQ = 7
// Omega grid (0.3.5): "Controlado por" (nulo de controle) no topo do efeito da grade.
private const val SPECIAL_GRID = 8

/**
 * O CORPO, na ordem do P0-3: 1) ferramentas do efeito especial; 2) "Variação"
 * (≥ 2 presets); 3) linhas soltas; 4) "Avançado" desenhado como seção (recolhida);
 * 5) seções do motor. Seção recolhida não lista os membros.
 */
private fun fxBodyItems(store: EditorStore, effectId: Int, typeId: Int, visible: List<ParamSlot>, advancedOpen: Boolean): List<FxBody> = buildList {
    if (typeId == effectTypeId("aurea.key.rotobrush")) add(FxBody.Special(SPECIAL_ROTO))
    if (typeId == effectTypeId("aurea.ai.depth_map") || typeId == effectTypeId("aurea.key.rotobrush")) add(FxBody.Special(SPECIAL_LOCAL_AI))
    if (typeId == effectTypeId("aurea.time.remap")) {
        add(FxBody.Special(SPECIAL_TIME_REMAP))
        return@buildList
    }
    if (typeId == effectTypeId("aurea.distort.mesh_warp")) add(FxBody.Special(SPECIAL_MESH))
    if (typeId == effectTypeId("aurea.distort.puppet")) add(FxBody.Special(SPECIAL_PUPPET))
    if (typeId == effectTypeId("aurea.light.saber")) add(FxBody.Special(SPECIAL_SABER))
    if (typeId == effectTypeId("aurea.layout.grid_builder")) add(FxBody.Special(SPECIAL_GRID))   // Omega grid
    if (visible.isEmpty()) {
        add(FxBody.Notice(R.string.panel_este_efeito_nao_tem_ajustes))
        return@buildList
    }
    val presets = store.effectPresets(typeId)
    if (presets.size >= 2) add(FxBody.Variation(presets))
    if (typeId == effectTypeId("aurea.audio.parametric_eq")) add(FxBody.Special(SPECIAL_EQ))
    val layout = effectLayout(typeId, visible)
    layout.main.forEach { add(FxBody.Param(it, null)) }
    if (layout.advanced.isNotEmpty()) {
        add(FxBody.Section(FxSection(0, "advanced", "", null, advancedOpen, layout.advanced), advanced = true))
        if (advancedOpen) layout.advanced.forEach { add(FxBody.Param(it, null)) }
    }
    layout.sections.forEach { sec ->
        add(FxBody.Section(sec, advanced = false))
        if (sec.open) sec.params.forEach { add(FxBody.Param(it, sec.toggle?.label ?: sec.label)) }
    }
}

/**
 * A SOBREPOSIÇÃO dos controles de UM efeito: cabeçalho de 37 (cantos de cima 5,
 * «superfície de lista»): `▾ · Nome · ⋮ · 🗑` e o corpo rolável (cantos de baixo 5).
 * Tocar no cabeçalho fecha. ⋮ abre o menu do efeito (ancorado, 315 dp).
 * Ids `effects.settings.<id>`, `effects.settings.header`, `effects.more.<id>`, `effects.remove.<id>`.
 */
@Composable
private fun FxSettingsOverlay(
    env: PanelEnv,
    effect: LayerEffect,
    focused: Boolean,
    locked: Boolean,
    selected: ParamKey?,
    meshSelected: Boolean,
    advancedOpen: Boolean,
    bodyState: LazyListState,
    clipboardType: Int,
    onClose: () -> Unit,
    onRemove: () -> Unit,
    onDuplicate: () -> Unit,
    onDetails: () -> Unit,
    onSavePreset: () -> Unit,
    onToggleAdvanced: () -> Unit,
    onSelect: (ParamKey, Boolean) -> Unit,
    onSelectMesh: () -> Unit,
    onParamMenu: (ParamMenuTarget) -> Unit,
    modifier: Modifier = Modifier,
) {
    val store = env.store
    val id = effect.effectId
    val cardId = effectCardId(effect.typeId)
    val name = effectDisplayName(effect.typeId, effect.name)
    var menuOpen by remember { mutableStateOf(false) }
    val closeLabel = stringResource(R.string.fxui_a11y_collapse, name)
    Column(modifier.testTag("effects.settings.$cardId")) {
        // Cabeçalho.
        Row(
            Modifier
                .fillMaxWidth()
                .height(FxRow.List)
                .clip(RoundedCornerShape(topStart = 5.dp, topEnd = 5.dp))
                .background(AureaColors.FxListSurface),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Row(
                Modifier
                    .weight(1f)
                    .height(FxRow.List)
                    .testTag("effects.settings.header")
                    .semantics { contentDescription = closeLabel; role = Role.Button; heading() }
                    .tocavel(enabled = !focused, shrink = 1f, onClick = onClose)
                    .padding(start = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.ChevronDown, 18.dp, AureaColors.Text.copy(alpha = 0.9f))
                Text(
                    name,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f).padding(start = 6.dp, end = 4.dp).alpha(if (effect.enabled) 1f else 0.45f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
                )
            }
            Box {
                FxIconTarget(44.dp, FxRow.List, stringResource(R.string.app_a11y_more_options, name), Modifier.testTag("effects.more.$cardId"),
                    onClick = { menuOpen = true }) {
                    CupertinoIcon(CupertinoGlyph.Ellipsis, 20.dp, AureaColors.FxListIcon, Modifier.graphicsLayer { rotationZ = 90f })
                }
                if (menuOpen) {
                    AnchoredMenu(
                        expanded = true,
                        onDismiss = { menuOpen = false },
                        entries = fxEffectMenu(env, effect, locked, selected, clipboardType, onDetails, onDuplicate, onSavePreset),
                        width = 315.dp,
                        alignEnd = true,
                        tag = "effects.menu",
                    )
                }
            }
            FxIconTarget(44.dp, FxRow.List, stringResource(R.string.fxui_a11y_remove, name), Modifier.testTag("effects.remove.$cardId"),
                enabled = !locked, onClick = onRemove) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
            }
        }
        // Corpo.
        val slots by remember(store, id) { derivedStateOf { store.effectParams[id]?.map { ParamSlot.of(it) } ?: emptyList() } }
        val visible = slots.filter { !it.hidden }
        val bodyItems = if (effect.known) remember(visible, effect.typeId, advancedOpen) {
            // Bugs 0.3.4 (bug 3): um parâmetro listado em dois lugares do layout não
            // pode repetir a chave da LazyColumn (o app fechava).
            fxBodyItems(store, id, effect.typeId, visible, advancedOpen).distinctBy { it.key }
        } else {
            listOf(FxBody.Notice(R.string.panel_este_efeito_saiu_catalogo_ele_nao))
        }
        LazyColumn(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .clip(RoundedCornerShape(bottomStart = 5.dp, bottomEnd = 5.dp))
                .background(AureaColors.FxListSurface)
                .alpha(if (effect.enabled) 1f else 0.45f)
                .testTag("effects.settings.body"),
            state = bodyState,
            contentPadding = PaddingValues(bottom = 6.dp),
        ) {
            items(bodyItems, key = { it.key }) { item ->
                when (item) {
                    is FxBody.Special -> FxSpecialTools(env, effect, item.what, meshSelected, onSelectMesh)
                    is FxBody.Notice -> FxBodyNotice(stringResource(item.text))
                    is FxBody.Variation -> {
                        val labels = item.presets.map { (pid, n) -> effectPresetLabel(pid)?.let { stringResource(it) } ?: n }
                        FxChoiceLayout(
                            label = stringResource(R.string.om_fx_variation),
                            options = labels,
                            selected = -1,
                            segmented = labels.size <= 4 && labels.all { it.length <= 10 },
                            enabled = !locked,
                            labelActive = false,
                            tag = "effects.variation.$id",
                            onPick = { store.applyEffectPreset(id, it) },
                        )
                    }
                    is FxBody.Section -> {
                        val sec = item.section
                        if (item.advanced) {
                            FxSectionHeader(
                                effectId = id,
                                section = sec.copy(label = stringResource(R.string.om_fx_advanced)),
                                toggleLabel = "",
                                toggleOn = true,
                                onOpen = { onToggleAdvanced() },
                                onToggle = {},
                                tag = "effects.section.$id.advanced",
                            )
                        } else {
                            val toggle = sec.toggle
                            val on = if (toggle != null) rememberParamValue(env, id, toggle.index, 0) >= 0.5f else true
                            val toggleLabel = toggle?.let { paramDisplay(effect.typeId, it).label() } ?: sec.label
                            FxSectionHeader(
                                effectId = id,
                                section = sec,
                                toggleLabel = toggleLabel,
                                toggleOn = on,
                                toggleEnabled = toggle?.enabled != false && !locked,
                                onOpen = { open -> store.setEffectSectionOpen(id, sec.ordinal, open) },
                                onToggle = { value ->
                                    if (toggle != null) {
                                        onSelect(ParamKey(id, toggle.index, 0), false)
                                        store.paramOf(id, toggle.index)?.let { store.setEffectParam(id, it, if (value) 1f else 0f) }
                                    }
                                },
                            )
                        }
                    }
                    is FxBody.Param -> FxParamRow(env, id, effect.typeId, item.slot, selected, onSelect, onParamMenu, item.disabledBy)
                }
            }
        }
    }
}

/** As ferramentas dos efeitos especiais (como antes, agora no topo do corpo). */
@Composable
private fun FxSpecialTools(env: PanelEnv, effect: LayerEffect, what: Int, meshSelected: Boolean, onSelectMesh: () -> Unit) {
    val store = env.store
    val id = effect.effectId
    Column(Modifier.fillMaxWidth().padding(horizontal = 6.dp, vertical = 4.dp)) {
        when (what) {
            SPECIAL_ROTO -> {
                FxBodyNotice(stringResource(R.string.roto_note))
                val previousView = store.effectParams[id]?.firstOrNull { it.index == 10 }?.value?.firstOrNull()?.toInt() ?: 0
                com.aurea.aurea.editor.RotoPaintControls(store, id, previousView.coerceIn(0, 2))
            }
            SPECIAL_LOCAL_AI -> {
                val bit = if (effect.typeId == effectTypeId("aurea.ai.depth_map")) 1 else 2
                if (effect.enabled) {
                    if (store.localAiActivity and bit != 0) {
                        FxBodyNotice(stringResource(R.string.local_ai_processing), Modifier.testTag("effects.local_ai.processing"))
                    } else if (store.localAiActivity and (bit shl 2) != 0) {
                        FxBodyNotice(stringResource(R.string.local_ai_failed), Modifier.testTag("effects.local_ai.failed"))
                    }
                }
            }
            SPECIAL_TIME_REMAP -> TimeRemapEffectEditor(env, id)
            SPECIAL_MESH -> com.aurea.aurea.editor.MeshWarpCardTools(store, id, meshSelected, onSelectMesh)
            SPECIAL_PUPPET -> com.aurea.aurea.editor.PuppetCardTools(store, id)
            SPECIAL_SABER -> com.aurea.aurea.editor.SaberCardTools(store, id)
            SPECIAL_EQ -> EqResponseGraph(env, id)
            SPECIAL_GRID -> GridControlRow(store)   // Omega grid
        }
    }
}

/**
 * O MENU ⋮ do efeito aberto (ordem da referência, `lG.hda`): Detalhes do efeito ·
 * Restaurar padrão · Duplicar · Copiar efeito · Colar configurações do efeito (só
 * com UM efeito do mesmo tipo copiado). Depois, "Mais do AUREA": Expressão do
 * parâmetro escolhido · Salvar como preset do efeito · Preset Turbulência dinâmica.
 */
@Composable
private fun fxEffectMenu(
    env: PanelEnv,
    effect: LayerEffect,
    locked: Boolean,
    selected: ParamKey?,
    clipboardType: Int,
    onDetails: () -> Unit,
    onDuplicate: () -> Unit,
    onSavePreset: () -> Unit,
): List<MenuEntry> {
    val store = env.store
    val id = effect.effectId
    val exprTarget = selected?.takeIf { it.effectId == id }?.let { k -> store.paramOf(k.effectId, k.param)?.let { ParamSlot.of(it) } }
        ?.takeIf { it.animatable }
    val exprLabel = exprTarget?.let { paramDisplay(effect.typeId, it).label() }
    val details = stringResource(R.string.om_fx_menu_details)
    val reset = stringResource(R.string.om_fx_menu_reset_defaults)
    val duplicate = stringResource(R.string.common_duplicate)
    val copy = stringResource(R.string.om_fx_menu_copy_effect)
    val paste = stringResource(R.string.om_fx_menu_paste_settings)
    val more = stringResource(R.string.om_fx_more_from_aurea)
    val expression = exprLabel?.let { stringResource(R.string.fx_param_expression, it) }
    val save = stringResource(R.string.om_fx_menu_save_preset)
    val turbulence = stringResource(R.string.app_preset_fx_turbulence)
    return buildList {
        add(MenuEntry.Item(details, enabled = store.catalog.any { it.typeId == effect.typeId }, tag = "effects.menu.details") { onDetails() })
        add(MenuEntry.Item(reset, enabled = !locked, tag = "effects.menu.reset") { resetEffect(store, id) })
        add(MenuEntry.Item(duplicate, enabled = !locked, tag = "effects.menu.duplicate") { onDuplicate() })
        add(MenuEntry.Item(copy, tag = "effects.menu.copy") { store.copyEffects(id) })
        if (clipboardType != 0 && clipboardType == effect.typeId) {
            add(MenuEntry.Item(paste, enabled = !locked, tag = "effects.menu.paste_settings") { store.pasteEffectSettings(id) })
        }
        add(MenuEntry.Divider)
        add(MenuEntry.Header(more))
        if (exprTarget != null && expression != null) {
            add(MenuEntry.Item(expression, tag = "effects.menu.expression") { openParamExpression(store, id, exprTarget, exprLabel) })
        }
        add(MenuEntry.Item(save, tag = "effects.menu.save_preset") { onSavePreset() })
        if (effect.typeId == effectTypeId("aurea.distort.turbulence")) {
            add(MenuEntry.Item(turbulence, enabled = !locked, tag = "effects.menu.turbulence") { applyTurbulencePreset(store, id) })
        }
    }
}
