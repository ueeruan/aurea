package com.aurea.aurea.editor.panels

import androidx.annotation.StringRes
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch

// =============================================================================
// Omega aparência (área 05, P0-1 + P1-5): MESCLAGEM E OPACIDADE em página única,
// como a referência: a régua de opacidade no topo e, embaixo, a sanfona de
// categorias (uma aberta por vez; "Normal" aplica direto; abrir rola a categoria
// para o topo) com os modos numa fileira horizontal de itens 60×80.
// O texto de ajuda saiu da página e virou a entrada "Sobre a mesclagem" do ⋯.
// =============================================================================

/**
 * Um modo: rótulo, o `aurea::BlendMode` (Types.hpp) e o modo do Compose que a
 * miniatura usa (o mais próximo; a miniatura é ilustrativa — a conta de verdade
 * é a do motor, shaders/composite/blend.frag). [cut] 1 = Máscara, 2 = Excluir.
 */
private class BlendChoice(@StringRes val label: Int, val engine: Int, val preview: BlendMode, val cut: Int = 0)

/** Uma categoria da sanfona; [direct] = tocar aplica o modo sem abrir (Normal). */
private class BlendCategory(val key: String, @StringRes val name: Int, val modes: List<BlendChoice>, val direct: Int? = null)

/**
 * A ORDEM DA REFERÊNCIA (catálogo de 28 modos). Os números são `aurea::BlendMode`:
 * Normal 0, Add 1, Subtract 2, Multiply 3, Screen 4, Overlay 5, Darken 6, Lighten 7,
 * ColorDodge 8, ColorBurn 9, HardLight 10, SoftLight 11, Difference 12, Exclusion 13,
 * Hue 14, Saturation 15, Color 16, Luminosity 17, Divide 18, VividLight 19,
 * LinearDodge 20, LinearBurn 21, Mask 22, Exclude 23, DarkerColor 24, LighterColor 25,
 * LinearLight 26, PinLight 27, SoftOverlay 28. "Adicionar" (1) é extra do AUREA, no fim de Clarear.
 */
private val BlendCategories = listOf(
    BlendCategory("normal", R.string.panel_normal, listOf(BlendChoice(R.string.panel_normal, 0, BlendMode.SrcOver)), direct = 0),
    BlendCategory(
        "darken", R.string.pn_blend_darken,
        listOf(
            BlendChoice(R.string.pn_blend_multiply, 3, BlendMode.Multiply),
            BlendChoice(R.string.pn_blend_darken, 6, BlendMode.Darken),
            BlendChoice(R.string.om_ap_blend_darker_color, 24, BlendMode.Darken),
            BlendChoice(R.string.pn_blend_color_burn, 9, BlendMode.ColorBurn),
            BlendChoice(R.string.pn_blend_linear_burn, 21, BlendMode.Multiply),
        ),
    ),
    BlendCategory(
        "lighten", R.string.pn_blend_lighten,
        listOf(
            BlendChoice(R.string.pn_blend_screen, 4, BlendMode.Screen),
            BlendChoice(R.string.pn_blend_lighten, 7, BlendMode.Lighten),
            BlendChoice(R.string.om_ap_blend_lighter_color, 25, BlendMode.Lighten),
            BlendChoice(R.string.pn_blend_color_dodge, 8, BlendMode.ColorDodge),
            BlendChoice(R.string.pn_blend_linear_dodge, 20, BlendMode.Plus),
            BlendChoice(R.string.pn_blend_add, 1, BlendMode.Plus),
        ),
    ),
    BlendCategory(
        "contrast", R.string.pn_blend_cat_contrast,
        listOf(
            BlendChoice(R.string.pn_blend_overlay, 5, BlendMode.Overlay),
            BlendChoice(R.string.pn_blend_soft_light, 11, BlendMode.Softlight),
            BlendChoice(R.string.pn_blend_hard_light, 10, BlendMode.Hardlight),
            BlendChoice(R.string.om_ap_blend_soft_overlay, 28, BlendMode.Softlight),
            BlendChoice(R.string.pn_blend_vivid_light, 19, BlendMode.ColorDodge),
            BlendChoice(R.string.om_ap_blend_linear_light, 26, BlendMode.Hardlight),
            BlendChoice(R.string.om_ap_blend_pin_light, 27, BlendMode.Overlay),
        ),
    ),
    BlendCategory(
        "difference", R.string.pn_blend_difference,
        listOf(
            BlendChoice(R.string.pn_blend_difference, 12, BlendMode.Difference),
            BlendChoice(R.string.pn_blend_exclusion, 13, BlendMode.Exclusion),
            BlendChoice(R.string.pn_blend_subtract, 2, BlendMode.Difference),
            BlendChoice(R.string.pn_blend_divide, 18, BlendMode.ColorDodge),
        ),
    ),
    BlendCategory(
        "color", R.string.pn_blend_color,
        listOf(
            BlendChoice(R.string.pn_blend_hue, 14, BlendMode.Hue),
            BlendChoice(R.string.pn_blend_saturation, 15, BlendMode.Saturation),
            BlendChoice(R.string.pn_blend_color, 16, BlendMode.Color),
            BlendChoice(R.string.pn_blend_luminosity, 17, BlendMode.Luminosity),
        ),
    ),
    // Máscara do Alight Motion: a camada recorta as de baixo (22) ou fura (23).
    BlendCategory(
        "mask", R.string.pn_blend_cat_mask,
        listOf(
            BlendChoice(R.string.pn_blend_mask, 22, BlendMode.DstIn, cut = 1),
            BlendChoice(R.string.pn_blend_exclude, 23, BlendMode.DstOut, cut = 2),
        ),
    ),
)

/** Os modos que o renderer desenha (todos os de `aurea::BlendMode`, até kLastBlendMode = 28). */
private val RenderedBlendModes = (0..28).toSet()

/**
 * MESCLAGEM E OPACIDADE: moldura de 200 dp (PanelFrame, sem abas). O ◆, a curva
 * e a expressão da coluna são da OPACIDADE (a referência grava a opacidade no ◆).
 */
@Composable
internal fun AppearancePanel(env: PanelEnv) {
    val store = env.store
    DisposableEffect(store, store.primary) {
        store.timelineFocus = OpacityKeys
        onDispose { store.timelineFocus = null }
    }
    val look by remember(store) { derivedStateOf { transformLook(store.detail, intArrayOf(TrackProperty.OPACITY)) } }
    val curveReady by remember(store) { derivedStateOf { store.curveStart(store.primaryKeys().transformTrack(TrackProperty.OPACITY)) != null } }
    val exprLook by remember(store) { derivedStateOf { store.expressionLook(OpacityKeys) } }
    val opacityTitle = stringResource(R.string.panel_opacidade)
    val aboutLabel = stringResource(R.string.om_ap_blend_about)
    var showAbout by remember { mutableStateOf(false) }

    PanelFrame(
        rail = PanelRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = { store.toggleTransformKeyframe(intArrayOf(TrackProperty.OPACITY)) },
            onDeleteAllKeyframes = { store.primary?.let { store.deleteAllKeyframes(it, OpacityKeys) } },
            onCurve = {
                val layer = store.primary
                if (layer != null) {
                    store.curveStart(store.primaryKeys().transformTrack(TrackProperty.OPACITY))?.let {
                        store.selectKeyframe(layer, it)
                        env.onOpenPanel(EditorPanel.Curve)
                    }
                }
            },
            curveReady = curveReady,
            expression = exprLook,
            onExpression = { store.openExpression(opacityTitle, OpacityKeys, 100f, "%") },
            // O texto de ajuda da mesclagem do AUREA virou dica no ⋯ (decisão do coordenador).
            moreEntries = listOf(MenuEntry.Item(aboutLabel, glyph = CupertinoGlyph.InfoCircle, tag = "blend.about") { showAbout = true }),
        ),
        scrollContent = false,
    ) {
        OpacityLine(env)
        BlendAccordion(env, Modifier.weight(1f).fillMaxWidth())
    }
    if (showAbout) {
        AureaAlert(
            title = aboutLabel,
            message = stringResource(R.string.pn_blend_help),
            showCancel = false,
            onConfirm = {},
            onDismiss = { showAbout = false },
        )
    }
}

/**
 * A linha de opacidade da referência (40 dp, margem de cima 5 dp): ícone · régua
 * · caixa de valor 56 dp que abre o teclado 0–100. Um arrasto = um desfazer.
 */
@Composable
private fun OpacityLine(env: PanelEnv) {
    val store = env.store
    val opacity by remember(store) { derivedStateOf { (store.detail?.opacity ?: 1f) * 100f } }
    val locked by remember(store) { derivedStateOf { store.detail?.locked == true } }
    val label = stringResource(R.string.panel_opacidade)
    Row(
        Modifier.fillMaxWidth().padding(top = 5.dp).height(40.dp).padding(start = 6.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.CircleLefthalfFill, 22.dp, AureaColors.Text)
        Spacer(Modifier.width(6.dp))
        TickRuler(
            value = { opacity },
            unitsPerDp = 0.35f,
            active = true,
            modifier = Modifier
                .weight(1f)
                .height(40.dp)
                .testTag("blend.opacity.ruler")
                .semantics { contentDescription = label }
                .valueDrag(
                    enabled = !locked,
                    start = { (store.detail?.opacity ?: 1f) * 100f },
                    unitsPerDp = { 0.35f },
                    min = 0f,
                    max = 100f,
                    onStart = { store.beginGesture("opacidade") },
                    onValue = { store.setTransform(TrackProperty.OPACITY, it.coerceIn(0f, 100f) / 100f) },
                    onEnd = { store.endGesture() },
                ),
        )
        Spacer(Modifier.width(4.dp))
        ValueBox(
            text = "${numeroPtBr(opacity, 0)}%",
            width = 56.dp,
            enabled = !locked,
            modifier = Modifier.testTag("blend.opacity.value"),
        ) {
            env.openKeypad(KeypadRequest(label, opacity, "%", 0f, 100f, 0) { store.setTransform(TrackProperty.OPACITY, it / 100f) })
        }
    }
}

/**
 * A SANFONA DE CATEGORIAS: só uma aberta; abrir rola a categoria para o topo;
 * Normal aplica direto. O cabeçalho da categoria do modo ligado mostra o nome do
 * modo em destaque e o ✓.
 */
@Composable
private fun BlendAccordion(env: PanelEnv, modifier: Modifier) {
    val store = env.store
    val mode by remember(store) { derivedStateOf { store.detail?.blendMode ?: 0 } }
    val locked by remember(store) { derivedStateOf { store.detail?.locked == true } }
    val initial = remember(store.primary) {
        BlendCategories.firstOrNull { c -> c.direct == null && c.modes.any { it.engine == mode } }?.key
    }
    var open by rememberSaveable(store.primary) { mutableStateOf(initial) }
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    LazyColumn(modifier, state = listState, contentPadding = PaddingValues(bottom = 6.5.dp)) {
        itemsIndexed(BlendCategories, key = { _, c -> c.key }) { index, cat ->
            val isOpen = open == cat.key
            val on = cat.modes.firstOrNull { it.engine == mode }
            BlendCategoryCard(
                cat = cat,
                isOpen = isOpen,
                current = on,
                currentMode = mode,
                enabled = !locked,
                onHeader = {
                    val direct = cat.direct
                    if (direct != null) {
                        store.setBlendMode(direct)
                        open = null
                    } else {
                        open = if (isOpen) null else cat.key
                        if (!isOpen) scope.launch { listState.animateScrollToItem(index) }
                    }
                },
                onPick = { m -> if (m.engine in RenderedBlendModes) store.setBlendMode(m.engine) },
            )
        }
    }
}

@Composable
private fun BlendCategoryCard(
    cat: BlendCategory,
    isOpen: Boolean,
    current: BlendChoice?,
    currentMode: Int,
    enabled: Boolean,
    onHeader: () -> Unit,
    onPick: (BlendChoice) -> Unit,
) {
    val shape = RoundedCornerShape(5.dp)
    val name = stringResource(cat.name)
    val chevron by animateFloatAsState(if (isOpen) 90f else 0f, animationSpec = tween(150), label = "blend.chevron")
    Column(
        Modifier
            .fillMaxWidth()
            .padding(start = 6.5.dp, end = 6.5.dp, top = 6.5.dp)
            .clip(shape)
            .background(if (isOpen) AureaColors.Chip else AureaColors.SurfaceHigh),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(37.dp)
                .testTag("blend.category.${cat.key}")
                .semantics { contentDescription = name; selected = current != null }
                .tocavel(enabled = enabled, shrink = 1f, onClick = onHeader)
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (cat.direct == null) {
                Box(Modifier.size(18.dp).rotate(chevron), contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 13.dp, AureaColors.Muted)
                }
            } else {
                Spacer(Modifier.width(18.dp))
            }
            Spacer(Modifier.width(6.dp))
            Text(
                name,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f, fill = false),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
            )
            Spacer(Modifier.weight(1f))
            if (current != null) {
                Text(
                    stringResource(current.label),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)),
                )
                Spacer(Modifier.width(6.dp))
                CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 16.dp, AureaColors.Accent)
            }
        }
        if (isOpen) {
            LazyRow(
                Modifier.fillMaxWidth().height(80.dp),
                contentPadding = PaddingValues(horizontal = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                itemsIndexed(cat.modes, key = { _, m -> m.engine }) { _, m ->
                    BlendModeItem(m, lit = m.engine == currentMode, enabled = enabled) { onPick(m) }
                }
            }
        }
    }
}

/** Um modo na fileira: 60 × 80 — miniatura de 45 dp (raio = lado/6) e o nome 11 sp. */
@Composable
private fun BlendModeItem(m: BlendChoice, lit: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val label = stringResource(m.label)
    val thumbShape = RoundedCornerShape(7.5.dp)   // 45 / 6
    Column(
        Modifier
            .width(60.dp)
            .height(80.dp)
            .testTag("blend.mode.${m.engine}")
            .semantics { contentDescription = label; selected = lit }
            .tocavel(enabled = enabled, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Box(
            Modifier
                .size(45.dp)
                .clip(thumbShape)
                .background(AureaColors.Surface)
                .then(if (lit) Modifier.border(2.dp, AureaColors.Accent, thumbShape) else Modifier),
            contentAlignment = Alignment.Center,
        ) {
            if (m.cut != 0) {
                BlendCutThumb(exclude = m.cut == 2, modifier = Modifier.size(41.dp))
            } else {
                // Camada própria (offscreen): o disco de cima compõe só com o de baixo.
                Canvas(Modifier.size(41.dp).graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }) {
                    val r = size.minDimension * 0.30f
                    drawCircle(AureaColors.BlendThumbBottom, r, Offset(size.width * 0.38f, size.height / 2f))
                    drawCircle(AureaColors.BlendThumbTop, r, Offset(size.width * 0.62f, size.height / 2f), blendMode = m.preview)
                }
            }
        }
        Spacer(Modifier.height(4.dp))
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 2.dp),
            style = AureaType.Base.merge(
                TextStyle(fontSize = 11.sp, textAlign = TextAlign.Center, color = if (lit) AureaColors.Accent else AureaColors.Muted),
            ),
        )
    }
}
