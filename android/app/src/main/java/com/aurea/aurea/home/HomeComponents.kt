package com.aurea.aurea.home

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaMotion
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
//  O KIT DA HOME (Fase 7.3 §8). Tudo aqui lê os tokens do design system —
//  nenhum número ou cor solta. O que era "HomeColors/HomeType/HomeDims" da
//  A.01 virou `AureaColors`/`AureaType`/`AureaDims`.
// =============================================================================

/** `_BotaoRedondo`: alvo 44 com círculo 36 e ícone 17. */
@Composable
internal fun RoundIconButton(glyph: Char, description: String, onClick: () -> Unit) {
    Box(
        Modifier
            .size(AureaDims.RoundTarget)
            .semantics { contentDescription = description }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.size(AureaDims.RoundCircle).clip(AureaShape.Circle).background(AureaColors.SurfaceHigh),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 17.dp, AureaColors.Text)
        }
    }
}

/**
 * Cabeçalho de seção: título 21 w700 com uma ação opcional à direita
 * ("Ver todos"). Sem ação, é só o título.
 */
@Composable
internal fun SectionHeader(text: String, actionLabel: String? = null, onAction: (() -> Unit)? = null) {
    Row(
        Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = 26.dp, end = AureaDims.Gutter, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, style = AureaType.ScreenTitle, modifier = Modifier.weight(1f))
        if (actionLabel != null && onAction != null) {
            Text(
                actionLabel,
                style = AureaType.of(13.5f, androidx.compose.ui.text.font.FontWeight.W600, color = AureaColors.Accent),
                modifier = Modifier.tocavel(onClick = onAction).padding(4.dp),
            )
        }
    }
}

/** Rótulo em caixa-alta. */
@Composable
internal fun CapsLabel(text: String, modifier: Modifier = Modifier) {
    Text(text.uppercase(), style = AureaType.Caps, modifier = modifier)
}

/**
 * Realce de um botão cheio: não encolhe, só acende branco 5 % enquanto
 * pressionado (o ripple era desligado; sobrava o `highlightColor`).
 */
@Composable
internal fun Modifier.pressHighlight(onClick: () -> Unit): Modifier {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    return this
        .clickable(interactionSource = interaction, indication = null, role = Role.Button, onClick = onClick)
        .drawWithContent {
            drawContent()
            if (pressed) drawRect(AureaColors.PressHighlight)
        }
}

/**
 * `CupertinoSlidingSegmentedControl` do SDK: raio 9, polegar raio 7,
 * padding 2/3, separadores 1 px que somem ao lado do escolhido. O polegar
 * desliza (o SDK usa mola; aqui criticamente amortecida, sem quique).
 */
@Composable
internal fun <T> AureaSegmented(
    values: List<T>,
    selected: T,
    label: (T) -> String,
    onChange: (T) -> Unit,
    background: Color,
    thumb: Color,
    verticalPadding: Dp,
    modifier: Modifier = Modifier,
) {
    val index = values.indexOf(selected).coerceAtLeast(0)
    val pos by animateFloatAsState(index.toFloat(), spring(dampingRatio = 1f, stiffness = 500f), label = "segmentado")
    val count = values.size
    Row(
        modifier
            .fillMaxWidth()
            .heightIn(min = 28.dp)
            .clip(AureaShape.Chip)
            .background(background)
            .padding(horizontal = 3.dp, vertical = 2.dp)
            .drawBehind {
                val w = size.width / count
                val inset = 5.dp.toPx()
                for (i in 1 until count) {
                    // Some perto do polegar: distância até ele, presa em [0, 1].
                    val alpha = min(abs(i - pos), abs(i - 1 - pos)).coerceIn(0f, 1f)
                    if (alpha > 0f) {
                        drawRect(
                            AureaColors.SegmentSeparator.copy(alpha = AureaColors.SegmentSeparator.alpha * alpha),
                            topLeft = Offset(w * i - 0.5f, inset),
                            size = Size(1f, size.height - inset * 2),
                        )
                    }
                }
                drawRoundRect(
                    thumb,
                    topLeft = Offset(w * pos, 0f),
                    size = Size(w, size.height),
                    cornerRadius = CornerRadius(7.dp.toPx()),
                )
            },
    ) {
        values.forEach { v ->
            val chosen = v == selected
            Box(
                Modifier
                    .weight(1f)
                    .clickable(interactionSource = null, indication = null, role = Role.Tab) { if (!chosen) onChange(v) }
                    .padding(vertical = verticalPadding),
                contentAlignment = Alignment.Center,
            ) {
                // Como no SDK: rótulo longo ("Full HD 1080p" num aparelho
                // estreito) quebra em duas linhas em vez de virar reticências.
                Text(
                    label(v),
                    style = AureaType.Segment,
                    color = if (chosen) AureaColors.Accent else AureaColors.Text,
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

/** `CupertinoSwitch`: trilho 59×39 (#6FAED9 ligado), polegar 27. */
@Composable
internal fun AureaSwitch(checked: Boolean, onChange: (Boolean) -> Unit) {
    val t by animateFloatAsState(if (checked) 1f else 0f, tween(AureaMotion.NORMAL), label = "interruptor")
    Box(
        Modifier
            .size(AureaDims.SwitchWidth, AureaDims.SwitchHeight)
            .clickable(interactionSource = null, indication = null, role = Role.Switch) { onChange(!checked) }
            .padding(horizontal = 4.dp, vertical = 4.dp)
            .drawBehind {
                drawRoundRect(if (checked) AureaColors.Accent else AureaColors.SwitchOffTrack, cornerRadius = CornerRadius(size.height / 2))
                val r = size.height / 2 - 2.dp.toPx()
                val cx = size.height / 2 + (size.width - size.height) * t
                drawCircle(if (checked) AureaColors.OnAccent else AureaColors.OnImage, radius = r, center = Offset(cx, size.height / 2))
            },
    )
}

/**
 * O `FittedBox(scaleDown)` do Flutter: mede o conteúdo sem limite e, se não
 * couber, encolhe por inteiro (centralizado) em vez de quebrar ou cortar.
 */
@Composable
internal fun ScaleDownToFit(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Layout(content, modifier) { measurables, constraints ->
        val p = measurables.first().measure(Constraints())
        val maxW = if (constraints.hasBoundedWidth) constraints.maxWidth else p.width
        val maxH = if (constraints.hasBoundedHeight) constraints.maxHeight else p.height
        val scale = min(1f, min(maxW.toFloat() / p.width.coerceAtLeast(1), maxH.toFloat() / p.height.coerceAtLeast(1)))
        layout(maxW, maxH) {
            p.placeWithLayer(((maxW - p.width) / 2f).roundToInt(), ((maxH - p.height) / 2f).roundToInt()) {
                scaleX = scale
                scaleY = scale
            }
        }
    }
}

/**
 * O vidro é opaco ao toque, como o Container colorido do Flutter: um toque
 * na área vazia da barra não pode abrir o cartão escondido embaixo dela.
 */
internal fun Modifier.blockTouches(): Modifier = pointerInput(Unit) {}

// =============================================================================
// Listas agrupadas (Ajustes)
// =============================================================================

/** Cabeçalho de grupo em caixa-alta, recuo 16. */
@Composable
internal fun GroupHeader(text: String) {
    CapsLabel(text, Modifier.padding(start = AureaDims.S4, bottom = AureaDims.S2))
}

/** A caixa de um grupo de linhas. */
@Composable
internal fun Group(content: @Composable () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .clip(AureaShape.Lg)
            .background(AureaColors.Surface),
    ) { content() }
}

/** Hairline 0,5 recuada 16 à esquerda. */
@Composable
internal fun GroupDivider() {
    Box(Modifier.padding(start = AureaDims.S4).fillMaxWidth().height(AureaDims.Hairline).background(AureaColors.Hairline))
}

/** Rótulo + segmentado. */
@Composable
internal fun <T> SegmentedRow(label: String, values: List<T>, selected: T, labelOf: (T) -> String, onChange: (T) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(AureaDims.S4, 14.dp)) {
        Text(label, style = AureaType.BodyLarge)
        Spacer(Modifier.height(10.dp))
        AureaSegmented(values, selected, labelOf, onChange, AureaColors.SegmentTrack, AureaColors.SegmentThumb, 6.dp)
    }
}

/** Título + subtítulo e chevron 16. */
@Composable
internal fun TapRow(title: String, subtitle: String?, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .pressHighlight(onClick)
            .padding(AureaDims.S4, AureaDims.S3),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = AureaType.BodyLarge)
            if (subtitle != null) {
                Spacer(Modifier.height(1.dp))
                Text(subtitle, style = AureaType.BodySmall)
            }
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, AureaDims.IconSm, AureaColors.Muted)
    }
}

/** Nota muted embaixo de uma linha de grupo. */
@Composable
internal fun GroupNote(text: String) {
    Text(text, style = AureaType.Note, modifier = Modifier.fillMaxWidth().padding(start = AureaDims.S4, end = AureaDims.S4, bottom = 14.dp))
}

/** Linha de lista com ícone à esquerda, título, subtítulo e o que vier à direita. */
@Composable
internal fun TileRow(
    leading: @Composable () -> Unit,
    title: String,
    subtitle: String? = null,
    subtitleStyle: TextStyle = AureaType.BodySmall,
    trailing: (@Composable () -> Unit)? = null,
    onClick: (() -> Unit)? = null,
) {
    val base = Modifier.fillMaxWidth().heightIn(min = if (subtitle == null) 56.dp else 72.dp)
    Row(
        (if (onClick != null) base.pressHighlight(onClick) else base).padding(start = AureaDims.S4, end = AureaDims.S5, top = AureaDims.S2, bottom = AureaDims.S2),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.width(AureaDims.IconLg), contentAlignment = Alignment.CenterStart) { leading() }
        Spacer(Modifier.width(AureaDims.S4))
        Column(Modifier.weight(1f)) {
            Text(title, style = AureaType.BodyLarge)
            if (subtitle != null) Text(subtitle, style = subtitleStyle)
        }
        if (trailing != null) {
            Spacer(Modifier.width(AureaDims.S4))
            trailing()
        }
    }
}

/** Um botão cheio de largura inteira (criar projeto, ação principal). */
@Composable
internal fun FillButton(label: String, glyph: Char? = null, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(AureaDims.ButtonHeight)
            .clip(AureaShape.Lg)
            .background(AureaColors.Accent)
            .pressHighlight(onClick),
        contentAlignment = Alignment.Center,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (glyph != null) {
                CupertinoIcon(glyph, AureaDims.IconMd, AureaColors.OnAccent)
                Spacer(Modifier.width(AureaDims.S2))
            }
            Text(label, style = AureaType.Button)
        }
    }
}
