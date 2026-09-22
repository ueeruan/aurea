package com.aurea.aurea.ui.ds

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
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

/**
 * O CARTÃO DO EFEITO [A] (`_CartaoDoEfeito`, t2): fundo `#151C24`, raio 14,
 * margem inferior 10, padding L10 T2 R6 B8; cabeçalho 48 =
 * `▼ 13 · 12 · nome 17 sp w600 · [olho cortado se desligado] · ⋯ 44 · 🗑 44`.
 *
 * Recolher é instantâneo (sem animar altura) e o corpo recolhido NEM É
 * COMPOSTO — um cartão fechado não escuta o cabeçote. Desligado, nome e corpo
 * ficam a 45 % (continua visível, só esmaecido).
 */
@Composable
fun EffectCard(
    name: String,
    enabled: Boolean,
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    onMenu: () -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier
            .fillMaxWidth()
            .padding(bottom = 10.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(AureaColors.Surface)
            .padding(start = 10.dp, top = 2.dp, end = 6.dp, bottom = 8.dp),
    ) {
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(
                Modifier
                    .weight(1f)
                    .height(48.dp)
                    .tocavel(shrink = 1f, onClick = onToggleExpanded),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(
                    if (expanded) CupertinoGlyph.ArrowtriangleDownFill else CupertinoGlyph.ArrowtriangleRightFill,
                    13.dp,
                    AureaColors.Text,
                )
                Spacer(Modifier.width(12.dp))
                Text(
                    name,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false).alpha(if (enabled) 1f else 0.45f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)),
                )
                if (!enabled) {
                    Spacer(Modifier.width(8.dp))
                    CupertinoIcon(CupertinoGlyph.EyeSlash, 16.dp, AureaColors.Muted)
                }
            }
            HeaderButton(CupertinoGlyph.Ellipsis, onMenu)
            HeaderButton(CupertinoGlyph.Trash, onRemove)
        }
        if (expanded) {
            Column(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.45f), content = content)
        }
    }
}

@Composable
private fun HeaderButton(glyph: Char, onClick: () -> Unit) {
    Box(Modifier.size(44.dp).tocavel(onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 22.dp, AureaColors.Text)
    }
}

/**
 * A CARTELA GENÉRICA DO CATÁLOGO [A] (tela96b): degradê vertical
 * `#232B3A → #606F98`, disco `#FF4D2D` Ø 42 % em (36 %, 55 %) e traço branco
 * 60 % × 1,2 % em (50 %, 20 %). Sem as tiras de prévia da A.01 (não há motor de
 * miniatura offscreen ainda), todo efeito usa esta cartela.
 */
@Composable
fun EffectPreviewArt(modifier: Modifier = Modifier) {
    Canvas(modifier) {
        drawRect(Brush.verticalGradient(listOf(AureaColors.EffectPreviewTop, AureaColors.EffectPreviewBottom)))
        val w = size.width
        val h = size.height
        drawCircle(AureaColors.EffectPreviewDisc, radius = w * 0.21f, center = Offset(w * 0.36f, h * 0.55f))
        drawLine(
            Color.White,
            start = Offset(w * 0.20f, h * 0.20f),
            end = Offset(w * 0.80f, h * 0.20f),
            strokeWidth = (h * 0.012f).coerceAtLeast(1f),
            cap = StrokeCap.Butt,
        )
    }
}

/**
 * UM EFEITO NA GRADE DO CATÁLOGO [A] (`_EffectTile`): prévia quadrada raio 10,
 * ☆ 32 × 32 no canto superior direito (favorito = `destaque`, senão branco
 * 70 %), selo "custo N" (só custo > 1: preto 55 %, raio 6, 9 sp), 5, nome 12 sp
 * w600 e categoria 10,5 sp muted.
 */
@Composable
fun EffectTile(
    name: String,
    category: String,
    cost: Int,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.tocavel(shrink = 1f, onClick = onClick)) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(RoundedCornerShape(10.dp)),
        ) {
            EffectPreviewArt(Modifier.fillMaxSize())
            if (cost > 1) {
                Box(
                    Modifier
                        .align(Alignment.BottomStart)
                        .padding(start = 6.dp, bottom = 6.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(Color.Black.copy(alpha = 0.55f))
                        .padding(horizontal = 5.dp, vertical = 2.dp),
                ) {
                    Text("custo $cost", style = AureaType.Base.merge(TextStyle(fontSize = 9.sp, color = Color.White)))
                }
            }
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 2.dp, end = 2.dp)
                    .size(32.dp)
                    .tocavel(onClick = onFavorite),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(
                    if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star,
                    16.dp,
                    if (favorite) AureaColors.Accent else Color.White.copy(alpha = 0.7f),
                )
            }
        }
        Spacer(Modifier.height(5.dp))
        Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)))
        Text(category, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 10.5.sp, color = AureaColors.Muted)))
    }
}
