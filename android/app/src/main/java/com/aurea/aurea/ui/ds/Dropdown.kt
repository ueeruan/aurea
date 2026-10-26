package com.aurea.aurea.ui.ds

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// Omega fundação · F3: CAIXA SUSPENSA (a da folha "Novo projeto" da referência)
//
// Caixa de 180 × 45 dp, raio 8, valor à esquerda e ⌄ à direita; o toque abre o
// [AnchoredMenu] com a LARGURA DA CAIXA, a opção vigente marcada com a barra de
// 4 dp. Par do `AureaDropdown` do iOS (DesignSystem.swift).
//
//   AureaDropdown(
//       value = "1080p",
//       entries = sizes.map { s -> MenuEntry.Item(s.label, selected = s == current) { pick(s) } },
//       tag = "newProject.resolution",
//       contentDescription = stringResource(R.string.editor_resolucao),
//   )
// =============================================================================

object AureaDropdownDefaults {
    val Width = 180.dp
    val Height = 45.dp
    val Radius = 8.dp
}

/**
 * Caixa suspensa.
 *
 * @param value o texto mostrado na caixa (a opção vigente).
 * @param entries as opções (em geral [MenuEntry.Item] com `selected` na vigente).
 * @param width largura da caixa (180 dp da referência); nulo = a largura que o [modifier] der (ex.: `fillMaxWidth`).
 * @param tone [AureaSurfaceTone.Light] (página clara) ou [AureaSurfaceTone.Editor] (painel escuro do editor).
 * @param swatch amostra de cor antes do valor (fundo do projeto); [checkerSwatch] = xadrez atrás (transparente).
 * @param enabled falso = alfa 0,25 e sem toque.
 * @param tag `testTag` da caixa; o menu recebe `"$tag.menu"`.
 * @param contentDescription rótulo de acessibilidade (o nome do campo); o valor vai como estado.
 */
@Composable
fun AureaDropdown(
    value: String,
    entries: List<MenuEntry>,
    modifier: Modifier = Modifier,
    width: Dp? = AureaDropdownDefaults.Width,
    tone: AureaSurfaceTone = AureaSurfaceTone.Light,
    swatch: Color? = null,
    checkerSwatch: Boolean = false,
    enabled: Boolean = true,
    tag: String? = null,
    contentDescription: String? = null,
) {
    var open by remember { mutableStateOf(false) }
    val light = tone == AureaSurfaceTone.Light
    val box = if (light) AureaColors.LightSurface else AureaColors.Chip
    val ink = if (light) AureaColors.LightText else AureaColors.Text
    val muted = if (light) AureaColors.LightTextMuted else AureaColors.Muted
    val line = if (light) AureaColors.LightDivider else AureaColors.Border
    val shape = RoundedCornerShape(AureaDropdownDefaults.Radius)
    Box(modifier.then(if (width != null) Modifier.width(width) else Modifier)) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(AureaDropdownDefaults.Height)
                .clip(shape)
                .background(box)
                .then(tag?.let { Modifier.testTag(it) } ?: Modifier)
                .semantics {
                    if (contentDescription != null) this.contentDescription = contentDescription
                    stateDescription = value
                }
                .tocavel(enabled = enabled, shrink = 1f, haptic = true) { open = true }
                .alpha(if (enabled) 1f else AnchoredMenuDefaults.DISABLED_ALPHA)
                .padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (swatch != null) {
                Box(
                    Modifier
                        .size(22.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .then(if (checkerSwatch) Modifier.drawBehind { drawChecker() } else Modifier)
                        .background(swatch)
                        .border(1.dp, line, RoundedCornerShape(5.dp)),
                )
                Spacer(Modifier.width(10.dp))
            }
            Text(
                value,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W500, color = ink)),
            )
            CupertinoIcon(CupertinoGlyph.ChevronDown, 14.dp, muted, Modifier.padding(start = 6.dp))
        }
        AnchoredMenu(
            expanded = open,
            onDismiss = { open = false },
            entries = entries,
            matchAnchorWidth = true,
            tone = tone,
            tag = tag?.let { "$it.menu" },
        )
    }
}
