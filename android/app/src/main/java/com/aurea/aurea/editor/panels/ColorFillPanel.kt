package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
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
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.drawChecker
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

// =============================================================================
// Omega aparência (área 05, P0-3 + P1-4 UI): COR E PREENCHIMENTO
//
//   ┌60─┬──────────────────────────────────────────┐ 200 dp, sem abas à direita
//   │ ‹ │  [ Nenhum · Cor · Degradê ]  cartão 29,5  │ (forma SDF)
//   │ ◆ │  ┌──────────── 127,5 ─────────────────┐  │ texto: [ Cor ]
//   │ ∿ │  │ seletor embutido / degradê / ícone │  │ vetor/grupo: [ Original ]
//   └───┴──┴────────────────────────────────────┴──┘
//
// ◆ e Curva são do grupo 0 (a cor) da forma, só no tipo Cor; segurar ◆ apaga as
// chaves da cor num passo. No Degradê o palco mostra as pontas (ColorStage.kt).
// Mídia (P2-1) não aparece: o motor ainda não desenha. O contorno da forma saiu
// daqui (vai para Borda e sombra › Traço). Par do `ColorFillPanel` do iOS.
// =============================================================================

/** O que o painel aberto pede ao palco (lido em [ColorStageOverlay]). */
internal object ColorFillStage {
    /** O painel está no Degradê de uma forma: o palco mostra as pontas. */
    var gradientHandles by mutableStateOf(false)
    /** A ponta arrastada agora (3 início, 4 fim; 0 nenhuma). */
    var dragging by mutableIntStateOf(0)
}

private enum class FillKind(val id: String) { None("none"), Color("color"), Gradient("gradient"), Media("media"), Original("original") }

/** Trilhas da cor do preenchimento (ShapeParam 32..35) e do degradê (36..47). */
private val FillColorKeys: List<TrackKey> = (32..35).map { TrackKey(TrackProperty.SHAPE_PARAM, 0, it) }
private val FillGradientKeys: List<TrackKey> = (36..47).map { TrackKey(TrackProperty.SHAPE_PARAM, 0, it) }

private val FillContentHeight = 127.5.dp

@Composable
internal fun ColorFillPanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val fill = store.shapeFill?.takeIf { it.size >= 24 && it[21] >= 0.5f }
    val isText = detail.kind == LayerType.Text.kind
    val isGroup = detail.kind == LayerType.Group.kind
    val isVector = store.isVectorLayer
    val tabs: List<FillKind> = when {
        fill != null -> listOf(FillKind.None, FillKind.Color, FillKind.Gradient)
        isText -> listOf(FillKind.Color)
        else -> listOf(FillKind.Original)
    }
    val kind = when {
        fill != null -> when {
            fill[2] < 0.5f -> FillKind.None
            fill[0] >= 0.5f -> FillKind.Gradient
            else -> FillKind.Color
        }
        isText -> FillKind.Color
        else -> FillKind.Original
    }

    DisposableEffect(store, store.primary, kind, fill != null) {
        store.timelineFocus = when {
            fill == null -> null
            kind == FillKind.Color -> FillColorKeys
            kind == FillKind.Gradient -> FillGradientKeys
            else -> null
        }
        onDispose { store.timelineFocus = null }
    }
    val gradientOn = fill != null && kind == FillKind.Gradient
    DisposableEffect(gradientOn) {
        ColorFillStage.gradientHandles = gradientOn
        onDispose {
            ColorFillStage.gradientHandles = false
            ColorFillStage.dragging = 0
        }
    }

    // ◆ e Curva: o grupo 0 (a cor) da forma, só no tipo Cor.
    val colorKeyed = fill != null && kind == FillKind.Color
    val look = if (fill == null) {
        KeyframeLook.None
    } else {
        val animated = fill[19].roundToInt()
        val here = fill[20].roundToInt()
        when {
            here and 1 != 0 -> KeyframeLook.KeyHere
            animated and 1 != 0 -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
    }
    val curveKeys = curveTrack((32..35).map { store.primaryKeys().shapeTrack(it) })
    val curveReady = colorKeyed && store.curveStart(curveKeys) != null

    PanelFrame(
        rail = PanelRail(
            onBack = env.onClose,
            keyframeLook = if (colorKeyed) look else KeyframeLook.None,
            onKeyframe = if (colorKeyed) ({ store.toggleShapeFillKey(0) }) else null,
            onDeleteAllKeyframes = if (colorKeyed) ({ store.primary?.let { store.deleteAllKeyframes(it, FillColorKeys) } }) else null,
            onCurve = if (colorKeyed) ({
                val layer = store.primary
                val key = store.curveStart(curveTrack((32..35).map { store.primaryKeys().shapeTrack(it) }))
                if (layer != null && key != null) {
                    store.selectKeyframe(layer, key)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) else null,
            curveReady = curveReady,
        ),
    ) {
        Spacer(Modifier.height(5.75.dp))
        FillTypeBar(tabs, kind, enabled = store.eyedropper == null) { k ->
            when (k) {
                FillKind.None -> store.setShapeFillMode(0)
                FillKind.Color -> if (fill != null) store.setShapeFillMode(1) else Unit
                FillKind.Gradient -> store.setShapeFillMode(2)
                FillKind.Media, FillKind.Original -> Unit
            }
        }
        Spacer(Modifier.height(2.75.dp))
        Box(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 9.dp)
                .height(FillContentHeight)
                .testTag("fill.content"),
        ) {
            when (kind) {
                FillKind.None -> BigFillIcon(CupertinoGlyph.Circle, stringResource(R.string.om_ap_fill_none))
                FillKind.Original -> OriginalContent(env, isGroup = isGroup, isVector = isVector)
                FillKind.Color -> if (fill != null) {
                    StoreColorPicker(
                        store = store,
                        color = floatArrayOf(fill[3], fill[4], fill[5], fill[6]),
                        gestureLabel = "cor do preenchimento",
                        onColor = { store.setShapeFillValue(0, it) },
                    )
                } else {
                    // Texto: a cor do texto (sem ◆ aqui; a cor continua também no painel de Texto).
                    val td = store.textDetail
                    StoreColorPicker(
                        store = store,
                        color = td?.color?.copyOf() ?: floatArrayOf(1f, 1f, 1f, 1f),
                        gestureLabel = "cor do texto",
                        onColor = { store.setTextColor(it[0], it[1], it[2], it.getOrElse(3) { 1f }) },
                    )
                }
                FillKind.Gradient -> if (fill != null) GradientContent(env, fill) else Unit
                FillKind.Media -> Unit
            }
        }
        Spacer(Modifier.height(10.dp))
    }
}

private fun fillGlyph(k: FillKind): Char = when (k) {
    FillKind.None -> CupertinoGlyph.Circle
    FillKind.Color -> CupertinoGlyph.CircleFill
    FillKind.Gradient -> CupertinoGlyph.CircleLefthalfFill
    FillKind.Media -> CupertinoGlyph.Photo
    FillKind.Original -> CupertinoGlyph.Paintbrush
}

@Composable
private fun fillLabel(k: FillKind): String = stringResource(
    when (k) {
        FillKind.None -> R.string.om_ap_fill_none
        FillKind.Color -> R.string.om_ap_fill_color
        FillKind.Gradient -> R.string.om_ap_fill_gradient
        FillKind.Media -> R.string.om_ap_fill_media
        FillKind.Original -> R.string.om_ap_fill_original
    },
)

/**
 * A BARRA DE TIPO: cartão de 29,5 dp (raio 5, fundo «campo») com botões de
 * ícone de peso igual (texto; ativo em destaque). A fileira de toque tem 44 dp.
 */
@Composable
private fun FillTypeBar(tabs: List<FillKind>, current: FillKind, enabled: Boolean, onPick: (FillKind) -> Unit) {
    Box(Modifier.fillMaxWidth().padding(horizontal = 9.dp).height(44.dp)) {
        Box(
            Modifier
                .align(Alignment.Center)
                .fillMaxWidth()
                .height(29.5.dp)
                .clip(RoundedCornerShape(5.dp))
                .background(AureaColors.ChipHigh),
        )
        Row(Modifier.fillMaxSize()) {
            tabs.forEach { k ->
                val on = k == current
                val label = fillLabel(k)
                Box(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .testTag("fill.tab.${k.id}")
                        .semantics {
                            contentDescription = label
                            selected = on
                        }
                        .tocavel(enabled = enabled, shrink = 1f, haptic = true) { if (!on) onPick(k) },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(fillGlyph(k), 18.dp, if (on) AureaColors.Accent else AureaColors.Text)
                }
            }
        }
    }
}

/** Nenhum / Original: o ícone grande no cartão do conteúdo. */
@Composable
private fun BigFillIcon(glyph: Char, label: String) {
    Box(
        Modifier
            .fillMaxSize()
            .clip(RoundedCornerShape(5.dp))
            .background(AureaColors.PanelFrameDivider)
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 56.dp, AureaColors.Muted)
    }
}

/** Original (vetor, grupo): as cores moram no próprio conteúdo; o botão leva até lá. */
@Composable
private fun OriginalContent(env: PanelEnv, isGroup: Boolean, isVector: Boolean) {
    val store = env.store
    Column(
        Modifier
            .fillMaxSize()
            .clip(RoundedCornerShape(5.dp))
            .background(AureaColors.PanelFrameDivider)
            .padding(horizontal = 12.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(if (isGroup) CupertinoGlyph.FolderFill else CupertinoGlyph.Paintbrush, 40.dp, AureaColors.Muted)
        if (isVector) {
            Spacer(Modifier.height(4.dp))
            Text(
                stringResource(R.string.om_ap_fill_original_hint),
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
        }
        if (isVector || isGroup) {
            Spacer(Modifier.height(4.dp))
            val action = stringResource(if (isGroup) R.string.editor_entrar_grupo else R.string.panel_vetor)
            Box(
                Modifier
                    .heightIn(min = 44.dp)
                    .testTag("fill.original.open")
                    .semantics { contentDescription = action }
                    .tocavel(haptic = true) {
                        val id = store.primary
                        when {
                            isGroup && id != null -> store.openPrecomp(id)
                            !isGroup -> env.onOpenPanel(EditorPanel.Vector)
                            else -> Unit
                        }
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    action,
                    maxLines = 1,
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
                    modifier = Modifier
                        .clip(RoundedCornerShape(15.dp))
                        .background(AureaColors.ChipHigh)
                        .padding(horizontal = 16.dp, vertical = 7.dp),
                )
            }
        }
    }
}

/**
 * Degradê: Linear · Radial · Angular (60 × 27,5, a 15 dp do topo) e a barra de
 * 12 dp entre as duas amostras de 24,5 (início à esquerda, fim à direita), que
 * abrem o balão do seletor (grupos 1 e 2). As pontas se arrastam no palco.
 */
@Composable
private fun GradientContent(env: PanelEnv, fill: FloatArray) {
    val store = env.store
    val type = fill[1].roundToInt()
    val startColor = floatArrayOf(fill[7], fill[8], fill[9], fill[10])
    val endColor = floatArrayOf(fill[11], fill[12], fill[13], fill[14])
    Column(Modifier.fillMaxSize()) {
        Spacer(Modifier.height(6.75.dp))
        Box(Modifier.align(Alignment.CenterHorizontally).width(180.dp).height(44.dp)) {
            Box(
                Modifier
                    .align(Alignment.Center)
                    .fillMaxWidth()
                    .height(27.5.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(AureaColors.ChipHigh),
            )
            Row(Modifier.fillMaxSize()) {
                GradientTypeCell(stringResource(R.string.om_ap_fill_grad_linear), type == 0, "fill.gradient.linear") { store.setShapeGradientType(0) }
                GradientTypeCell(stringResource(R.string.om_ap_fill_grad_radial), type == 1, "fill.gradient.radial") { store.setShapeGradientType(1) }
                GradientTypeCell(stringResource(R.string.om_ap_fill_grad_sweep), type == 2, "fill.gradient.sweep") { store.setShapeGradientType(2) }
            }
        }
        Spacer(Modifier.height(6.dp))
        // O degradê é espacial (caixa da camada): início à esquerda também em RTL.
        KeepLtr {
            Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                GradientWell(startColor, "fill.gradient.start", stringResource(R.string.om_ap_fill_grad_start)) { rect ->
                    val c = store.shapeFill ?: fill
                    store.beginGesture("cor do degradê")
                    env.openColor(
                        ColorRequest(
                            initial = floatArrayOf(c[7], c[8], c[9], c[10]),
                            onChange = { r, g, b, a -> store.setShapeFillValue(1, floatArrayOf(r, g, b, a)) },
                            onDone = { store.endGesture() },
                            anchor = rect,
                            withAlpha = true,
                        ),
                    )
                }
                Box(
                    Modifier
                        .weight(1f)
                        .padding(horizontal = 4.dp)
                        .height(12.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .drawBehind {
                            drawChecker()
                            drawRect(Brush.horizontalGradient(listOf(rgbaColor(startColor), rgbaColor(endColor))))
                        },
                )
                GradientWell(endColor, "fill.gradient.end", stringResource(R.string.om_ap_fill_grad_end)) { rect ->
                    val c = store.shapeFill ?: fill
                    store.beginGesture("cor do degradê")
                    env.openColor(
                        ColorRequest(
                            initial = floatArrayOf(c[11], c[12], c[13], c[14]),
                            onChange = { r, g, b, a -> store.setShapeFillValue(2, floatArrayOf(r, g, b, a)) },
                            onDone = { store.endGesture() },
                            anchor = rect,
                            withAlpha = true,
                        ),
                    )
                }
            }
        }
        Text(
            stringResource(R.string.om_ap_fill_grad_handles),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)),
        )
    }
}

@Composable
private fun RowScope.GradientTypeCell(label: String, on: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .testTag(tag)
            .semantics { selected = on }
            .tocavel(shrink = 1f, haptic = true) { if (!on) onClick() },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(fontSize = 12.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Text),
            ),
        )
    }
}

/** Amostra de 24,5 dp (toque de 44): abre o balão preso a ela ([onOpen] recebe o retângulo na janela). */
@Composable
private fun GradientWell(color: FloatArray, tag: String, label: String, onOpen: (Rect) -> Unit) {
    val rect = remember { arrayOfNulls<Rect>(1) }
    Box(
        Modifier
            .size(44.dp)
            .testTag(tag)
            .semantics { contentDescription = label }
            .tocavel(shrink = 0.9f) { rect[0]?.let(onOpen) },
        contentAlignment = Alignment.Center,
    ) {
        val shape = RoundedCornerShape(5.dp)
        Box(
            Modifier
                .size(24.5.dp)
                .onGloballyPositioned { rect[0] = it.boundsInWindow() }
                .clip(shape)
                .drawBehind {
                    drawChecker()
                    drawRect(rgbaColor(color))
                }
                .border(1.dp, AureaColors.Border, shape),
        )
    }
}
