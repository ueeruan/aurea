package com.aurea.aurea.editor

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

// =============================================================================
// Botão do cromo (`_BotaoDoCromo`): alvo N×44, ícone 21, sem enfeite próprio
// =============================================================================

/**
 * Botão de ícone das barras A.01. Desabilitado = branco 25 % (o
 * `apagado.withValues(alpha: .25)` do Flutter SUBSTITUI o alfa).
 */
@Composable
internal fun ChromeButton(
    glyph: Char,
    description: String,
    onClick: (() -> Unit)?,
    modifier: Modifier = Modifier,
    size: Dp = 21.dp,
    width: Dp = 40.dp,
    height: Dp = 44.dp,
    tint: Color? = null,
    onLongClick: (() -> Unit)? = null,
) {
    Box(
        modifier
            .size(width, height)
            .semantics { contentDescription = description }
            .tocavel(enabled = onClick != null, haptic = true, onLongClick = onLongClick) { onClick?.invoke() },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, size, tint ?: if (onClick == null) AureaColors.Disabled else AureaColors.Text)
    }
}

/** Mesma casca para os poucos ícones Material que a A.01 usava (⋮, sair, "+"). */
@Composable
internal fun ChromeVectorButton(
    icon: ImageVector,
    description: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 19.dp,
    width: Dp = 40.dp,
    height: Dp = 44.dp,
    tint: Color = AureaColors.Text,
    mirror: Boolean = false,
) {
    Box(
        modifier
            .size(width, height)
            .semantics { contentDescription = description }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = tint,
            modifier = Modifier.size(size).then(if (mirror) Modifier.mirrorX() else Modifier),
        )
    }
}

/** Espelha na horizontal (o `Transform.flip` do ícone de sair). */
internal fun Modifier.mirrorX(): Modifier = this.graphicsLayer { scaleX = -1f }

// =============================================================================
// Folha de menu A.01 (`mostrarFolhaDeMenu` / `ItemDoMenu` / `SecaoDoMenu`)
// =============================================================================

/**
 * A folha de menu: fundo `#0F141A`, raio 18, puxador 36×4, lista rolável
 * que nunca passa de 80 % da tela.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ShellMenuSheet(
    onDismiss: () -> Unit,
    maxHeightFraction: Float = 0.8f,
    scrim: Color = ShellColors.MenuScrim,
    handle: Color = ShellColors.MenuHandle,
    content: @Composable ColumnScope.() -> Unit,
) {
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val screenH = LocalConfiguration.current.screenHeightDp
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = state,
        containerColor = AureaColors.EditorPanel,
        contentColor = AureaColors.Text,
        scrimColor = scrim,
        shape = RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp),
        dragHandle = {
            Box(
                Modifier
                    .padding(top = 6.dp)
                    .size(width = 36.dp, height = 4.dp)
                    .clip(RoundedCornerShape(2.dp))
                    .background(handle),
            )
        },
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .heightIn(max = (screenH * maxHeightFraction).dp)
                .navigationBarsPadding()
                .verticalScroll(rememberScrollState())
                .padding(bottom = 12.dp),
            content = content,
        )
    }
}

/** Título de seção da folha de menu. */
@Composable
internal fun MenuSection(title: String) {
    Text(
        title,
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, letterSpacing = 0.2.sp, color = AureaColors.Muted)),
        modifier = Modifier.padding(start = 20.dp, top = 14.dp, end = 20.dp, bottom = 4.dp),
    )
}

/**
 * Uma linha de menu: ícone 20, rótulo 15, detalhe 12 e, quando é escolha, o
 * visto à direita. Apagada continua à vista — diz que a ação existe.
 */
@Composable
internal fun MenuItemRow(
    glyph: Char,
    label: String,
    onClick: (() -> Unit)?,
    detail: String? = null,
    checked: Boolean? = null,
    radio: Boolean = false,
    danger: Boolean = false,
    icon: ImageVector? = null,
) {
    val active = onClick != null
    val color = when {
        !active -> ShellColors.DisabledMuted
        danger -> AureaColors.Danger
        else -> AureaColors.Text
    }
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .tocavel(enabled = active, haptic = true) { onClick?.invoke() }
            .padding(horizontal = 20.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) Icon(icon, null, tint = color, modifier = Modifier.size(20.dp))
        else CupertinoIcon(glyph, 20.dp, color)
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = color)))
            if (detail != null) {
                Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
        }
        if (checked != null) {
            val g = when {
                checked && radio -> '\uF6D3'           // largecircle_fill_circle
                checked -> CupertinoGlyph.CheckmarkAlt
                radio -> CupertinoGlyph.Circle
                else -> null
            }
            // Marcado em destaque (a A.01 pintava em `acao`, 2,6:1 sobre o fundo).
            if (g != null) CupertinoIcon(g, 18.dp, if (checked) AureaColors.Accent else AureaColors.Muted)
        }
    }
}

// =============================================================================
// Menu flutuante (AureaMenu): 250 × itens de 40, fundo `elevado`, raio 8
// =============================================================================

/** Uma opção do [ShellPopupMenu]. */
internal data class PopupItem(val label: String, val checked: Boolean, val onClick: () -> Unit)

/**
 * O menu flutuante do DS (§5.12), no lugar do `PopupMenuButton` Material que
 * a A.01 usava no chip de resolução (bug 14): abaixo da âncora se couber,
 * alinhado pela direita, preso a 8 dp das bordas.
 */
@Composable
internal fun ShellPopupMenu(items: List<PopupItem>, onDismiss: () -> Unit, width: Dp = 250.dp) {
    val density = LocalDensity.current
    val provider = remember(density) {
        with(density) { MenuPositionProvider(8.dp.roundToPx(), 4.dp.roundToPx()) }
    }
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        Column(
            Modifier
                .width(width)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.Pill)
                .padding(vertical = 4.dp),
        ) {
            items.forEach { item ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .height(40.dp)
                        .tocavel(shrink = 1f, haptic = true) {
                            onDismiss()
                            item.onClick()
                        },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(
                        Modifier
                            .size(4.dp, 24.dp)
                            .background(if (item.checked) AureaColors.Accent else Color.Transparent),
                    )
                    Spacer(Modifier.width(10.dp))
                    Text(
                        item.label,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = if (item.checked) AureaColors.Accent else AureaColors.Text)),
                        modifier = Modifier.weight(1f),
                    )
                    Spacer(Modifier.width(10.dp))
                }
            }
        }
    }
}

private class MenuPositionProvider(private val margin: Int, private val gap: Int) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        val x = (anchorBounds.right - popupContentSize.width)
            .coerceIn(margin, (windowSize.width - popupContentSize.width - margin).coerceAtLeast(margin))
        var y = anchorBounds.bottom + gap
        if (y + popupContentSize.height > windowSize.height - margin) y = anchorBounds.top - gap - popupContentSize.height
        return IntOffset(x, y.coerceAtLeast(margin))
    }
}

// =============================================================================
// Tempo
// =============================================================================

internal object ShellTime {
    private fun millis(frame: Int, fps: Float): Long =
        (frame / (if (fps > 0f) fps else 30f) * 1000.0).toLong()

    /** "m:ss.cc" — o relógio da barra do projeto (`_tempoCurto`). */
    fun short(frame: Int, fps: Float): String {
        val ms = millis(frame, fps)
        val cs = (ms % 1000) / 10
        val s = (ms / 1000) % 60
        val m = ms / 60000
        return "$m:${pad2(s)}.${pad2(cs)}"
    }

    /** "m:ss.d" — a barra de tempo da tela cheia (`formatTime`). */
    fun tenths(frame: Int, fps: Float): String {
        val ms = millis(frame, fps)
        val m = ms / 60000
        val s = (ms % 60000) / 1000
        val d = (ms % 1000) / 100
        return "$m:${pad2(s)}.$d"
    }

    private fun pad2(v: Long) = if (v < 10) "0$v" else v.toString()

    /**
     * Lê "12.5", "1:02.5", "1:02:03.5" e "00:01:02:15" (o último campo em
     * quadros quando há três separadores) — `parseTimecodeInput` da A.01.
     * Devolve o frame da composição, ou null se não entendeu.
     */
    fun parseToFrame(text: String, fps: Float): Int? {
        val f = if (fps > 0f) fps else 30f
        val s = text.trim().replace(',', '.')
        if (s.isEmpty()) return null
        val parts = s.split(':')
        val seconds: Double = try {
            when (parts.size) {
                1 -> parts[0].toDouble()
                2 -> parts[0].toInt() * 60 + parts[1].toDouble()
                3 -> parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toDouble()
                4 -> parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt() + parts[3].toInt() / f.toDouble()
                else -> return null
            }
        } catch (_: NumberFormatException) {
            return null
        }
        return (seconds * f).roundToInt()
    }
}

/**
 * "Ir para o tempo": o relógio da barra do projeto. Aceita segundos ou
 * mm:ss.ms; preso a [0, duração] pelo próprio `seek` do store.
 */
@Composable
internal fun GoToTimeDialog(initialSeconds: String, fps: Float, onSeek: (Int) -> Unit, onDismiss: () -> Unit) {
    var text by remember { mutableStateOf(initialSeconds) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    val confirm = { ShellTime.parseToFrame(text, fps)?.let(onSeek); Unit }
    AureaAlert(
        title = stringResource(R.string.editor_ir_tempo),
        confirmLabel = stringResource(R.string.editor_ir),
        onConfirm = confirm,
        onDismiss = onDismiss,
        extra = {
            Box(
                Modifier
                    .padding(top = 12.dp)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(7.dp))
                    .background(AureaColors.Stage)
                    .padding(horizontal = 8.dp, vertical = 7.dp),
            ) {
                if (text.isEmpty()) {
                    Text(stringResource(R.string.editor_segundos_ou_mm_ss_ms), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
                }
                BasicTextField(
                    value = text,
                    onValueChange = { text = it },
                    singleLine = true,
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp)).merge(AureaType.Tabular),
                    cursorBrush = SolidColor(AureaColors.Accent),
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
                    keyboardActions = KeyboardActions(onGo = {
                        onDismiss()
                        confirm()
                    }),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
            }
        },
    )
}

// =============================================================================
// Miudezas
// =============================================================================

/** O indicador de atividade Cupertino (12 raios girando), sem Material. */
@Composable
internal fun ActivityIndicator(size: Dp = 22.dp, color: Color = AureaColors.Text) {
    val t = rememberInfiniteTransition(label = "atividade")
    val step by t.animateFloat(
        initialValue = 0f,
        targetValue = 12f,
        animationSpec = infiniteRepeatable(tween(1000, easing = LinearEasing), RepeatMode.Restart),
        label = "atividade-passo",
    )
    Canvas(Modifier.size(size).rotate((step.toInt() * 30).toFloat())) {
        val r = this.size.minDimension / 2
        val w = r * 0.18f
        for (i in 0 until 12) {
            val a = Math.toRadians(i * 30.0 - 90.0)
            val c = kotlin.math.cos(a).toFloat()
            val s = kotlin.math.sin(a).toFloat()
            drawLine(
                color = color.copy(alpha = 0.25f + 0.75f * (i / 11f)),
                start = Offset(center.x + c * r * 0.5f, center.y + s * r * 0.5f),
                end = Offset(center.x + c * (r - w / 2), center.y + s * (r - w / 2)),
                strokeWidth = w,
                cap = StrokeCap.Round,
            )
        }
    }
}
