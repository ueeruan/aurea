package com.aurea.aurea.ui.ds

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  Componentes da frente EFEITOS (Fase 7.2). Novos: não mudam EffectCard/EffectTile.
// =============================================================================

/**
 * O CARTÃO DA PILHA DE EFEITOS (ref16/ref17). Recolhido: `▶ Nome · 👁 · ≡`
 * (o olho liga/desliga, o ≡ é a alça de arrastar — [dragHandle] recebe o gesto).
 * Aberto: `▼ Nome · ⋯ · 🗑` e o corpo. O corpo recolhido NEM É COMPOSTO (não
 * escuta o cabeçote). Desligado, nome e corpo a 45 %. [lifted] = sendo arrastado
 * (borda acesa, fundo mais alto).
 */
@Composable
fun EffectStackCard(
    name: String,
    enabled: Boolean,
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    onToggleEnabled: () -> Unit,
    onMenu: () -> Unit,
    onRemove: () -> Unit,
    dragHandle: Modifier,
    modifier: Modifier = Modifier,
    lifted: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = RoundedCornerShape(12.dp)
    Column(
        modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp)
            .clip(shape)
            .background(if (lifted) AureaColors.SurfaceHigh else AureaColors.Surface)
            .then(if (lifted) Modifier.border(1.dp, AureaColors.Accent, shape) else Modifier)
            .padding(start = 10.dp, end = 4.dp, bottom = if (expanded) 8.dp else 0.dp),
    ) {
        Row(Modifier.fillMaxWidth().height(52.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(
                Modifier
                    .weight(1f)
                    .height(52.dp)
                    .semantics { contentDescription = if (expanded) "Fechar $name" else "Abrir $name" }
                    .tocavel(shrink = 1f, onClick = onToggleExpanded),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(
                    if (expanded) CupertinoGlyph.ArrowtriangleDownFill else CupertinoGlyph.ArrowtriangleRightFill,
                    13.dp,
                    AureaColors.Text,
                )
                Spacer(Modifier.width(14.dp))
                Text(
                    name,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false).alpha(if (enabled) 1f else 0.45f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)),
                )
            }
            if (expanded) {
                CardButton(CupertinoGlyph.Ellipsis, "Mais opções de $name", AureaColors.Text, onMenu)
                CardButton(CupertinoGlyph.Trash, "Remover $name", AureaColors.Text, onRemove)
            } else {
                CardButton(
                    if (enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
                    if (enabled) "Desligar $name" else "Ligar $name",
                    if (enabled) AureaColors.Text else AureaColors.Muted,
                    onToggleEnabled,
                )
                Box(
                    dragHandle
                        .size(48.dp)
                        .semantics { contentDescription = "Arrastar para reordenar" },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.LineHorizontal3, 22.dp, if (lifted) AureaColors.Accent else AureaColors.Muted)
                }
            }
        }
        if (expanded) {
            Column(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.45f), content = content)
        }
    }
}

@Composable
private fun CardButton(glyph: Char, label: String, tint: Color, onClick: () -> Unit) {
    Box(
        Modifier
            .size(48.dp)
            .semantics { contentDescription = label }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 22.dp, tint)
    }
}

/**
 * "Avançado ▾" — a porta dos parâmetros que não são principais. Mostra quantos
 * há atrás dela; aberta vira "Avançado ▴". Linha de 44 dp inteira tocável.
 */
@Composable
fun AdvancedToggle(open: Boolean, count: Int, onToggle: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .height(44.dp)
            .tocavel(shrink = 1f, onClick = onToggle)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "Avançado",
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 13.dp, AureaColors.Accent)
        Spacer(Modifier.weight(1f))
        if (!open) {
            Text(
                if (count == 1) "1 ajuste" else "$count ajustes",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
    }
}

/**
 * UM EFEITO NO NAVEGADOR, GRANDE: prévia 16:10 (a cartela do catálogo — o motor
 * ainda não renderiza miniatura por efeito) com o ícone da CATEGORIA no canto
 * (rótulo visual, não finge ser prévia), ☆ com área de 44, nome 14 sp w600 e a
 * categoria embaixo.
 */
@Composable
fun EffectBrowserTile(
    name: String,
    category: String,
    categoryGlyph: Char,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .semantics { contentDescription = "Adicionar $name" }
            .tocavel(shrink = 0.97f, haptic = true, onClick = onClick),
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1.6f)
                .clip(RoundedCornerShape(12.dp)),
        ) {
            EffectPreviewArt(Modifier.fillMaxSize())
            Box(
                Modifier
                    .align(Alignment.BottomStart)
                    .padding(8.dp)
                    .size(30.dp)
                    .background(Color.Black.copy(alpha = 0.45f), CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(categoryGlyph, 16.dp, Color.White)
            }
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .size(44.dp)
                    .semantics { contentDescription = if (favorite) "Tirar dos favoritos" else "Pôr nos favoritos" }
                    .tocavel(onClick = onFavorite),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    Modifier.size(28.dp).background(Color.Black.copy(alpha = 0.35f), CircleShape),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(
                        if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star,
                        16.dp,
                        if (favorite) AureaColors.Accent else Color.White,
                    )
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(
            name,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)),
        )
        Text(
            category,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
        )
    }
}
