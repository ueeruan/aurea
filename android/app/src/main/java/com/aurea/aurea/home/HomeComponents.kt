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
import androidx.compose.foundation.shape.RoundedCornerShape
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
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

/** `_BotaoRedondo`: alvo 44 com círculo 36 #1B2530 e ícone 17. */
@Composable
internal fun RoundIconButton(glyph: Char, description: String, onClick: () -> Unit) {
    Box(
        Modifier
            .size(HomeDims.RoundTarget)
            .semantics { contentDescription = description }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.size(HomeDims.RoundCircle).clip(CircleShape).background(AureaColors.SurfaceHigh),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 17.dp, AureaColors.Text)
        }
    }
}

/**
 * `_AvatarDaConta` sem conta: círculo 34 #A9D3EC com "?". A inicial era
 * branca (contraste 1,6:1, spec §8.9) — aqui sai em `OnAccent`.
 */
@Composable
internal fun AccountAvatar(onClick: () -> Unit) {
    Box(
        Modifier
            .size(HomeDims.RoundTarget)
            .semantics { contentDescription = "Perfil" }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.size(34.dp).clip(CircleShape).background(AureaColors.Keyframe),
            contentAlignment = Alignment.Center,
        ) {
            Text("?", style = HomeType.AvatarInitial)
        }
    }
}

/** `_TituloSecao`: 21 w700, padding (20, 28, 20, 12). */
@Composable
internal fun SectionTitle(text: String) {
    Text(
        text,
        style = HomeType.SectionTitle,
        modifier = Modifier.fillMaxWidth().padding(start = 20.dp, top = 28.dp, end = 20.dp, bottom = 12.dp),
    )
}

/** `_Linha`: ícone 19 · texto 14,5 · chevron 15 (altura 45,6). */
@Composable
internal fun HomeLinkRow(glyph: Char, text: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .tocavel(onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 13.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 19.dp, AureaColors.Accent)
        Spacer(Modifier.width(14.dp))
        Text(text, style = HomeType.LinkRow, modifier = Modifier.weight(1f))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 15.dp, AureaColors.Muted)
    }
}

/** Rótulo de seção em caixa-alta (12 w500 +0,6 muted). */
@Composable
internal fun CapsLabel(text: String, modifier: Modifier = Modifier) {
    Text(text.uppercase(), style = HomeType.SectionLabel, modifier = modifier)
}

/**
 * Realce do FilledButton do tema antigo: não encolhe, só acende 5 % enquanto
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
            if (pressed) drawRect(HomeColors.ButtonHighlight)
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
            .clip(RoundedCornerShape(9.dp))
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
                            HomeColors.SegmentSeparator.copy(alpha = HomeColors.SegmentSeparator.alpha * alpha),
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
                    style = HomeType.Segment,
                    color = if (chosen) AureaColors.Accent else AureaColors.Text,
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

/** `CupertinoSwitch` dos Ajustes: trilho 51×31 (#6FAED9 ligado), polegar 27 (onAccent ligado). */
@Composable
internal fun AureaSwitch(checked: Boolean, onChange: (Boolean) -> Unit) {
    val t by animateFloatAsState(if (checked) 1f else 0f, tween(200), label = "interruptor")
    Box(
        Modifier
            .size(width = 59.dp, height = 39.dp)
            .clickable(interactionSource = null, indication = null, role = Role.Switch) { onChange(!checked) }
            .padding(horizontal = 4.dp, vertical = 4.dp)
            .drawBehind {
                val track = if (checked) AureaColors.Accent else HomeColors.SwitchOffTrack
                drawRoundRect(track, cornerRadius = CornerRadius(size.height / 2))
                val r = size.height / 2 - 2.dp.toPx()
                val cx = size.height / 2 + (size.width - size.height) * t
                drawCircle(if (checked) AureaColors.OnAccent else HomeColors.White, radius = r, center = Offset(cx, size.height / 2))
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

// =============================================================================
// Listas agrupadas (Ajustes, Sobre)
// =============================================================================

/** `_GroupHeader`: caixa-alta, recuo 16, 8 embaixo. */
@Composable
internal fun GroupHeader(text: String) {
    CapsLabel(text, Modifier.padding(start = 16.dp, bottom = 8.dp))
}

/** `_Group`: caixa #151C24 raio 16. */
@Composable
internal fun Group(content: @Composable () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(HomeDims.GroupRadius))
            .background(AureaColors.Surface),
    ) { content() }
}

/** `_GroupDivider`: hairline 0,5 recuada 16 à esquerda. */
@Composable
internal fun GroupDivider() {
    Box(Modifier.padding(start = 16.dp).fillMaxWidth().height(0.5.dp).background(AureaColors.Hairline))
}

/** `_SegmentedRow`: rótulo bodyLarge + segmentado (fundo #0F141A, polegar #1B2530). */
@Composable
internal fun <T> SegmentedRow(label: String, values: List<T>, selected: T, labelOf: (T) -> String, onChange: (T) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(16.dp, 14.dp)) {
        Text(label, style = AureaType.BodyLarge)
        Spacer(Modifier.height(10.dp))
        AureaSegmented(values, selected, labelOf, onChange, AureaColors.Background, AureaColors.SurfaceHigh, 6.dp)
    }
}

/** `_SwitchRow`: título + subtítulo e o interruptor. */
@Composable
internal fun SwitchRow(title: String, subtitle: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(start = 16.dp, top = 10.dp, end = 12.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = AureaType.BodyLarge)
            if (subtitle != null) {
                Spacer(Modifier.height(1.dp))
                Text(subtitle, style = AureaType.BodySmall)
            }
        }
        AureaSwitch(checked, onChange)
    }
}

/** `_TapRow`: título + subtítulo e chevron 16. */
@Composable
internal fun TapRow(title: String, subtitle: String?, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .pressHighlight(onClick)
            .padding(16.dp, 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = AureaType.BodyLarge)
            if (subtitle != null) {
                Spacer(Modifier.height(1.dp))
                Text(subtitle, style = AureaType.BodySmall)
            }
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, AureaColors.Muted)
    }
}

/** Nota 12,5 muted embaixo de uma linha de grupo. */
@Composable
internal fun GroupNote(text: String) {
    Text(text, style = HomeType.Note, modifier = Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 14.dp))
}

/**
 * O `ListTile` do Material 3 como a A.01 usava em Ajustes/Sobre: ícone à
 * esquerda, título bodyLarge, subtítulo e, se tocável, a seta.
 */
@Composable
internal fun TileRow(
    leading: @Composable () -> Unit,
    title: String,
    subtitle: String? = null,
    subtitleStyle: TextStyle = HomeType.TileSubtitle,
    trailing: (@Composable () -> Unit)? = null,
    onClick: (() -> Unit)? = null,
) {
    val base = Modifier.fillMaxWidth().heightIn(min = if (subtitle == null) 56.dp else 72.dp)
    Row(
        (if (onClick != null) base.pressHighlight(onClick) else base).padding(start = 16.dp, end = 24.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.width(24.dp), contentAlignment = Alignment.CenterStart) { leading() }
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = AureaType.BodyLarge)
            if (subtitle != null) Text(subtitle, style = subtitleStyle)
        }
        if (trailing != null) {
            Spacer(Modifier.width(16.dp))
            trailing()
        }
    }
}

/** A seta Cupertino 16 das linhas tocáveis de grupo. */
@Composable
internal fun TileChevron() {
    CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, AureaColors.Muted)
}

/** Seta do Material (`Icons.chevron_right`) da linha de Idioma. */
@Composable
internal fun MaterialChevron() {
    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = AureaColors.Muted)
}
