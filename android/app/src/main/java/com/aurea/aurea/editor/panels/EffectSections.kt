package com.aurea.aurea.editor.panels

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ParamRowColors
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch
import kotlin.math.max

// =============================================================================
//  SEÇÕES, DICAS E DESLIZAR PARA APAGAR no cartão de efeito (P1-4 e P2-9 da
//  especificação de 2026-10-14). O motor declara as seções (`ParamSection`):
//  título recolhível, interruptor opcional (o Bool que liga a seção) e a dica
//  (ⓘ) acima de um parâmetro. A ORDEM da tela é regra compartilhada com o iOS
//  (`fxEffectLayout`, EffectsHuman.swift): linhas soltas primeiro, depois as
//  seções pelo ordinal, cada uma com os parâmetros na ordem dos índices.
// =============================================================================

/** Uma seção do cartão: título, interruptor (o Bool que liga a seção), estado e linhas. */
@Immutable
internal data class FxSection(
    val ordinal: Int,
    val id: String,
    val label: String,
    val toggle: ParamSlot?,
    val open: Boolean,
    val params: List<ParamSlot>,
)

/** O corpo do cartão: linhas soltas (principais + "Avançado") e as seções. */
@Immutable
internal data class FxLayout(val main: List<ParamSlot>, val advanced: List<ParamSlot>, val sections: List<FxSection>) {
    /** Todas as linhas, na ordem da tela (o interruptor conta antes da sua seção). */
    val all: List<ParamSlot> get() = main + advanced + sections.flatMap { listOfNotNull(it.toggle) + it.params }
    val isEmpty: Boolean get() = main.isEmpty() && advanced.isEmpty() && sections.isEmpty()
}

/**
 * Monta o corpo do cartão a partir dos parâmetros VISÍVEIS. Efeito sem seção:
 * o de sempre (principais e "Avançado", [splitPrincipal]). Com seção: as linhas
 * soltas na ordem dos índices e cada seção (com pelo menos uma linha além do
 * interruptor) na ordem do ordinal — as seções substituem o "Avançado".
 */
internal fun effectLayout(typeId: Int, visible: List<ParamSlot>): FxLayout {
    if (visible.none { it.section > 0 }) {
        val (main, rest) = splitPrincipal(typeId, visible)
        return FxLayout(main, rest, emptyList())
    }
    val loose = visible.filter { it.section <= 0 }.sortedBy { it.index }
    val sections = visible.filter { it.section > 0 }.groupBy { it.section }.toSortedMap().mapNotNull { (ordinal, members) ->
        val head = members.first()
        val toggle = members.firstOrNull { it.index == head.sectionToggle && it.type == ParamType.BOOL }
        val rows = members.filter { it.index != head.sectionToggle }.sortedBy { it.index }
        if (rows.isEmpty()) null else FxSection(ordinal, head.sectionId, head.sectionLabel, toggle, head.sectionOpen, rows)
    }
    return FxLayout(loose, emptyList(), sections)
}

/** A seção [ordinal] que contém o parâmetro [param] e está recolhida (0 = nenhuma). */
internal fun closedSectionOf(layout: FxLayout, param: Int): Int =
    layout.sections.firstOrNull { s -> !s.open && (s.params.any { it.index == param } || s.toggle?.index == param) }?.ordinal ?: 0

/**
 * Ponto que ganha ALÇA no palco quando é o parâmetro ativo: Point2D (ou os X/Y
 * de um Point3D) medido em FRAÇÃO da camada (`kParamRelative`) e visível.
 * Deslocamentos e escalas XY (pixels, %) não são posições: sem alça.
 */
internal fun stagePointEligible(type: Int, flags: Int): Boolean =
    (type == ParamType.POINT2D || type == ParamType.POINT3D) &&
        (flags and ParamType.FLAG_RELATIVE) != 0 && (flags and ParamType.FLAG_HIDDEN) == 0

/**
 * Deslizar para a ESQUERDA apaga o efeito quando o dedo passa de 35 % da
 * largura do cartão (nunca menos que [minPx]); para a direita não faz nada.
 */
internal fun fxSwipeCommits(dx: Float, width: Float, minPx: Float): Boolean =
    dx < 0f && -dx >= max(minPx, width * 0.35f)

/**
 * Cabeçalho de SEÇÃO (43 dp de faixa): seta que gira 0→90° em 300 ms, título,
 * contagem quando recolhida e o interruptor que liga a seção. Tocar na faixa
 * abre/recolhe; o interruptor escreve o Bool (um passo de desfazer).
 * testTags `effects.section.<efeito>.<ordinal>` e `effects.section.toggle.<efeito>.<ordinal>` (iguais no iOS).
 */
@Composable
internal fun FxSectionHeader(
    effectId: Int,
    section: FxSection,
    toggleLabel: String,
    toggleOn: Boolean,
    onOpen: (Boolean) -> Unit,
    onToggle: (Boolean) -> Unit,
) {
    val angle by animateFloatAsState(
        if (section.open) 90f else 0f, tween(300, easing = FastOutSlowInEasing), label = "secao",
    )
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val a11y = stringResource(
        if (section.open) R.string.fxui_a11y_section_collapse else R.string.fxui_a11y_section_expand, section.label,
    )
    val count = section.params.size
    val countText = if (count == 1) stringResource(R.string.ds_1_ajuste) else stringResource(R.string.ds_adjustment_count, count)
    Row(
        Modifier
            .fillMaxWidth()
            .padding(top = 4.dp)
            .height(43.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(ParamRowColors.ValueBox),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier
                .weight(1f)
                .height(43.dp)
                .testTag("effects.section.$effectId.${section.ordinal}")
                .semantics {
                    contentDescription = a11y
                    role = Role.Button
                    if (!section.open) stateDescription = countText
                }
                .tocavel(shrink = 1f) { onOpen(!section.open) }
                .padding(start = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(
                CupertinoGlyph.ChevronRight, 13.dp, AureaColors.Muted,
                Modifier.graphicsLayer { rotationZ = if (rtl) -angle else angle },
            )
            Spacer(Modifier.width(8.dp))
            Text(
                section.label,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f, fill = false),
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
            )
            Spacer(Modifier.width(8.dp))
            if (!section.open) {
                Text(countText, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
        }
        if (section.toggle != null) {
            AureaToggle(
                checked = toggleOn,
                modifier = Modifier.testTag("effects.section.toggle.$effectId.${section.ordinal}").semantics {
                    contentDescription = toggleLabel
                    role = Role.Switch
                    toggleableState = if (toggleOn) ToggleableState.On else ToggleableState.Off
                },
                onCheckedChange = onToggle,
            )
            Spacer(Modifier.width(8.dp))
        } else {
            Spacer(Modifier.width(6.dp))
        }
    }
}

/** DICA só de leitura: ⓘ + texto, acima da linha do parâmetro. testTag `effects.tip.<efeito>.<índice>`. */
@Composable
internal fun FxTipRow(effectId: Int, index: Int, text: String) {
    val a11y = stringResource(R.string.fxui_a11y_tip, text)
    Row(
        Modifier
            .fillMaxWidth()
            .padding(start = 6.dp, end = 6.dp, top = 4.dp, bottom = 2.dp)
            .testTag("effects.tip.$effectId.$index")
            .semantics(mergeDescendants = true) { contentDescription = a11y },
        verticalAlignment = Alignment.Top,
        horizontalArrangement = Arrangement.Start,
    ) {
        CupertinoIcon(CupertinoGlyph.InfoCircle, 14.dp, AureaColors.Muted, Modifier.padding(top = 1.dp))
        Spacer(Modifier.width(6.dp))
        Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}

/**
 * DESLIZAR PARA A ESQUERDA no cabeçalho do cartão apaga o efeito ([fxSwipeCommits]);
 * antes do limite, solta e volta. Atrás do cartão aparece a faixa vermelha com 🗑.
 * O leitor de tela ganha a mesma ação ("Remover …") sem o gesto.
 */
@Composable
internal fun FxSwipeToDelete(
    enabled: Boolean,
    removeLabel: String,
    onDelete: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val offset = remember { Animatable(0f) }
    var width by remember { mutableFloatStateOf(0f) }
    val minPx = with(LocalDensity.current) { 96.dp.toPx() }
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val hint = stringResource(R.string.fxui_a11y_swipe_delete)
    val state = rememberDraggableState { delta ->
        // Só para a esquerda (a direita é limitada a 0); em RTL a tela espelha o gesto.
        val d = if (rtl) -delta else delta
        scope.launch { offset.snapTo((offset.value + d).coerceIn(-width, 0f)) }
    }
    Box(
        modifier
            .onSizeChanged { width = it.width.toFloat() }
            .semantics {
                customActions = listOf(CustomAccessibilityAction(removeLabel) { onDelete(); true })
                stateDescription = hint
            },
    ) {
        if (offset.value < 0f) {
            Row(
                Modifier.matchParentSize().padding(bottom = 8.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Danger),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Trash, 20.dp, AureaColors.Text, Modifier.padding(end = 20.dp))
            }
        }
        Box(
            Modifier
                .graphicsLayer { translationX = if (rtl) -offset.value else offset.value }
                .draggable(
                    state = state,
                    orientation = Orientation.Horizontal,
                    enabled = enabled,
                    onDragStopped = {
                        if (fxSwipeCommits(offset.value, width, minPx)) {
                            offset.animateTo(-width, tween(160))
                            onDelete()
                            offset.snapTo(0f)
                        } else {
                            offset.animateTo(0f, tween(200))
                        }
                    },
                ),
        ) { content() }
    }
}
