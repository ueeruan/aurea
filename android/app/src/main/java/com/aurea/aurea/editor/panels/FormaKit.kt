package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.PropertyLabelChip
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.comUnidade
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs

// =============================================================================
// Frente D (Fase 7.2): peças comuns de Editar forma, Vetor e Máscara.
// =============================================================================

/**
 * A LINHA HUMANA: `[chip 94] régua [valor] [↺]`, 48 dp. O valor já vem na
 * unidade de gente (px, %, °, cópias) e sai formatado em pt-BR com casas
 * fixas; tocar no valor abre o teclado (um passo de desfazer); o ↺ só acende
 * quando o valor saiu do padrão e volta a ele num toque. Enquanto o dedo
 * arrasta a linha mostra o valor em voo (o motor recebe cada passo).
 *
 * [onStart]/[onEnd] abrem e fecham o passo de desfazer do arrasto; [onCommit]
 * grava um valor inteiro (teclado ou reset) como UM passo.
 */
@Composable
internal fun HumanRow(
    env: PanelEnv,
    label: String,
    value: Float,
    step: Float,
    min: Float,
    max: Float,
    unit: String,
    decimals: Int,
    default: Float?,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    onCommit: (Float) -> Unit,
    selected: Boolean = false,
    onSelect: (() -> Unit)? = null,
    keyframe: KeyframeLook = KeyframeLook.None,
) {
    var dragging by remember { mutableStateOf(false) }
    var live by remember { mutableFloatStateOf(value) }
    val current by rememberUpdatedState(value)
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val end by rememberUpdatedState(onEnd)
    val select by rememberUpdatedState(onSelect)
    val shown = if (dragging) live else value
    val text = comUnidade(numeroPtBr(shown, decimals), unit)
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        PropertyLabelChip(label, selected, keyframe = keyframe, onClick = onSelect)
        Spacer(Modifier.width(6.dp))
        TickRuler(
            value = { if (dragging) live else current },
            unitsPerDp = step,
            active = selected || onSelect == null,
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .padding(vertical = 4.dp)
                .valueDrag(
                    enabled = true,
                    start = { current },
                    unitsPerDp = { step },
                    min = min,
                    max = max,
                    onStart = {
                        live = current
                        dragging = true
                        select?.invoke()
                        begin()
                    },
                    onValue = { v ->
                        live = v
                        send(v)
                    },
                    onEnd = {
                        dragging = false
                        end()
                    },
                ),
        )
        Spacer(Modifier.width(6.dp))
        ValueBox(text, onTap = {
            env.openKeypad(KeypadRequest(label, current, unit, min, max, decimals) { onCommit(it.coerceIn(min, max)) })
        })
        ResetButton(visible = default != null && abs(shown - default) > 1e-3f * maxOf(1f, abs(default))) {
            if (default != null) onCommit(default)
        }
    }
}

/** O ↺ da linha: 34 dp de toque; reserva o lugar mesmo apagado (a régua não pula). */
@Composable
internal fun ResetButton(visible: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .size(34.dp, 44.dp)
            .alpha(if (visible) 1f else 0f)
            .semantics { contentDescription = "Voltar ao padrão" }
            .then(if (visible) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.ArrowCounterclockwise, 16.dp, AureaColors.Muted)
    }
}

/** Linha "rótulo · interruptor", 48 dp. */
@Composable
internal fun ToggleLine(label: String, on: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(48.dp).tocavel(shrink = 1f) { onChange(!on) },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        AureaToggle(checked = on, onCheckedChange = onChange)
    }
}

/** Linha "rótulo · amostra de cor" (tocar abre o seletor). */
@Composable
internal fun ColorLine(label: String, color: Color, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(48.dp).tocavel(shrink = 1f, onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp)))
        ColorWell(color, onClick = onClick)
    }
}

/** Subtítulo de seção (13 sp w700 muted). */
@Composable
internal fun KitTitle(text: String) {
    Spacer(Modifier.height(8.dp))
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(4.dp))
}

/** Dica curta em muted. */
@Composable
internal fun KitHint(text: String) {
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)))
}

/**
 * "Avançado ▾": o resto dos controles, fechado por padrão. Linha inteira de
 * 44 dp é o alvo.
 */
@Composable
internal fun AdvancedSection(open: Boolean, onToggle: () -> Unit, content: @Composable () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).tocavel(shrink = 1f, onClick = onToggle),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("Avançado", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)))
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 12.dp, AureaColors.Muted)
        Spacer(Modifier.weight(1f))
        Box(Modifier.weight(3f).height(1.dp).background(AureaColors.Border))
    }
    if (open) content()
}

/**
 * Abas em fileira que rola (quando são muitas para dividir a largura): chip
 * de 36 dp, aceso = `destaqueApagado` + texto destaque.
 */
@Composable
internal fun ScrollTabs(labels: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .height(48.dp)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 6.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        labels.forEachIndexed { i, l ->
            val on = i == selected
            Box(
                Modifier
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(9.dp))
                    .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(shrink = 1f) { onSelect(i) }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    l,
                    maxLines = 1,
                    style = AureaType.Base.merge(
                        TextStyle(
                            fontSize = 12.5.sp,
                            fontWeight = if (on) FontWeight.W700 else FontWeight.W500,
                            color = if (on) AureaColors.Accent else AureaColors.Text,
                        ),
                    ),
                )
            }
        }
    }
}

/** Chip de escolha solto (fileira que rola). */
@Composable
internal fun KitChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .height(36.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/** Fileira de chips que rola na horizontal. */
@Composable
internal fun ChipRow(content: @Composable RowScope.() -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        content = content,
    )
}

/**
 * A FERRAMENTA (ícone + nome), 60 dp de altura: acesa = borda destaque. Usada
 * na barra do Vetor e no fluxo da Máscara.
 */
@Composable
internal fun RowScope.ToolTile(
    label: String,
    on: Boolean,
    enabled: Boolean = true,
    icon: DrawScope.(Color) -> Unit,
    onClick: () -> Unit,
) {
    val tint = when {
        !enabled -> AureaColors.RailDisabled
        on -> AureaColors.Accent
        else -> AureaColors.Text
    }
    Column(
        Modifier
            .weight(1f)
            .height(60.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .then(if (on) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(10.dp)) else Modifier)
            .semantics { contentDescription = label }
            .then(if (enabled) Modifier.tocavel(shrink = 1f, haptic = true, onClick = onClick) else Modifier)
            .padding(horizontal = 2.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Canvas(Modifier.size(22.dp)) { icon(tint) }
        Spacer(Modifier.height(4.dp))
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, lineHeight = 11.sp, fontWeight = FontWeight.W600, color = tint)),
        )
    }
}

/** Botão largo de ação (título + detalhe). */
@Composable
internal fun ActionCard(title: String, detail: String?, danger: Boolean = false, onClick: () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .tocavel(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 11.dp),
    ) {
        Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = if (danger) AureaColors.Danger else AureaColors.Text)))
        if (detail != null) {
            Spacer(Modifier.height(2.dp))
            Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        }
    }
}
