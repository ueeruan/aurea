package com.aurea.aurea.home

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.absoluteOffset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Uma opção do menu rápido (texto 16 médio, ícone no fim). */
internal data class QuickMenuOption(
    val label: String,
    val glyph: Char,
    val tag: String,
    val destructive: Boolean = false,
    val onClick: () -> Unit,
)

/** O que o menu rápido precisa: as coordenadas do cartão (janela, px) e a área útil. */
internal data class QuickMenuAnchor(val row: Rect, val areaBottomPx: Float)

/**
 * Omega home · P1-2: o menu rápido do projeto (referência `ProjectItemSelected`).
 * Véu claro na janela inteira (toque e Voltar fecham), a cópia da linha no
 * mesmo lugar e, 8 dp abaixo, o cartão de 70 % alinhado ao fim, com opções de
 * 48 dp. Se não couber até o fim da área útil, o cartão abre ACIMA do item
 * ([quickMenuTop]). Entra com fade de 200 ms.
 *
 * [rowCopy] desenha a linha (a mesma da lista, sem toque).
 */
@Composable
internal fun ProjectQuickMenu(
    anchor: QuickMenuAnchor,
    options: List<QuickMenuOption>,
    onDismiss: () -> Unit,
    tag: String = "home.projectMenu",
    rowCopy: @Composable () -> Unit,
) {
    val density = LocalDensity.current
    val fade = remember { Animatable(0f) }
    LaunchedEffect(Unit) { fade.animateTo(1f, tween(200)) }
    Popup(
        popupPositionProvider = WindowOrigin,
        onDismissRequest = onDismiss,
        properties = PopupProperties(focusable = true, clippingEnabled = false),
    ) {
        Box(
            Modifier
                .fillMaxSize()
                .alpha(fade.value)
                .background(AureaColors.HomeMenuScrim)
                .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = onDismiss),
        ) {
            with(density) {
                val rowTop = anchor.row.top.toDp()
                val rowLeft = anchor.row.left.toDp()
                val rowWidth = anchor.row.width.toDp()
                val rowHeight = anchor.row.height.toDp()
                val ltr = LocalLayoutDirection.current == LayoutDirection.Ltr
                // A cópia da linha, no mesmo lugar (sem toque: o véu fecha).
                Box(Modifier.absoluteOffset(x = rowLeft, y = rowTop).width(rowWidth).height(rowHeight)) { rowCopy() }
                val menuWidth = rowWidth * 0.7f
                val top = quickMenuTop(rowTop.value, rowHeight.value, options.size, anchor.areaBottomPx.toDp().value).dp
                // Alinhado ao FIM da linha (direita em LTR, esquerda em árabe).
                val left = if (ltr) rowLeft + rowWidth - menuWidth else rowLeft
                val shape = RoundedCornerShape(12.dp)
                Column(
                    Modifier
                        .absoluteOffset(x = left, y = top)
                        .width(menuWidth)
                        .homeShadow(12.dp, shape, AureaColors.HomeShadowStrong)
                        .clip(shape)
                        .background(AureaColors.LightSurface)
                        .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
                        .testTag(tag),
                ) {
                    options.forEach { o ->
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .height(48.dp)
                                .testTag(o.tag)
                                .tocavel(shrink = 1f, role = Role.Button) {
                                    onDismiss()
                                    o.onClick()
                                }
                                .padding(horizontal = 16.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            val ink = if (o.destructive) AureaColors.LightDanger else AureaColors.LightText
                            Text(o.label, style = OmegaHomeType.Menu.copy(color = ink), maxLines = 1, modifier = Modifier.weight(1f))
                            Spacer(Modifier.width(24.dp))
                            CupertinoIcon(o.glyph, 20.dp, ink)
                        }
                    }
                }
            }
        }
    }
}

/** O popup ocupa a janela inteira, a partir da origem (as coordenadas são da janela). */
private object WindowOrigin : PopupPositionProvider {
    override fun calculatePosition(anchorBounds: IntRect, windowSize: IntSize, layoutDirection: LayoutDirection, popupContentSize: IntSize): IntOffset =
        IntOffset.Zero
}
