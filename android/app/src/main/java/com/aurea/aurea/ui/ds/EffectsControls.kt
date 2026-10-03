package com.aurea.aurea.ui.ds

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
 * O CARTÃO DA PILHA DE EFEITOS (redesenho 2026-09-29, Efeitos.dc.html): raio 10,
 * fundo #252F43. Aberto: cabeçalho de 50 `▾ Nome · ••• · 🗑` e o corpo — as
 * linhas de 40 com vão de 4 ([ParamRowDims]). Recolhido: `▸ Nome · 👁 · ≡` (o
 * olho liga/desliga, o ≡ é a alça de arrastar — [dragHandle] recebe o gesto).
 * O corpo recolhido NEM É COMPOSTO (não escuta o cabeçote). Desligado, nome e
 * corpo a 45 %. [lifted] = sendo arrastado (borda acesa, fundo mais alto).
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
    val shape = RoundedCornerShape(10.dp)
    val toggleDescription = stringResource(if (expanded) R.string.app_a11y_close else R.string.app_a11y_open, name)
    val menuDescription = stringResource(R.string.app_a11y_more_options, name)
    val removeDescription = stringResource(R.string.app_a11y_remove, name)
    val enableDescription = stringResource(if (enabled) R.string.app_a11y_turn_off else R.string.app_a11y_turn_on, name)
    val dragDescription = stringResource(R.string.ds_arrastar_reordenar)
    Column(
        modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp)
            .clip(shape)
            .background(if (lifted) AureaColors.SurfaceHigh else ParamRowColors.Card)
            .then(if (lifted) Modifier.border(1.dp, AureaColors.Accent, shape) else Modifier)
            .padding(bottom = if (expanded) 6.dp else 0.dp),
    ) {
        Row(Modifier.fillMaxWidth().height(50.dp).padding(end = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(
                Modifier
                    .weight(1f)
                    .height(50.dp)
                    .semantics { contentDescription = toggleDescription }
                    .tocavel(shrink = 1f, onClick = onToggleExpanded)
                    .padding(start = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(
                    if (expanded) CupertinoGlyph.ArrowtriangleDownFill else CupertinoGlyph.ArrowtriangleRightFill,
                    13.dp,
                    AureaColors.Text,
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    name,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false).alpha(if (enabled) 1f else 0.45f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W600)),
                )
            }
            if (expanded) {
                CardButton(CupertinoGlyph.Ellipsis, menuDescription, AureaColors.Text, onMenu)
                CardButton(CupertinoGlyph.Trash, removeDescription, AureaColors.Text, onRemove)
            } else {
                CardButton(
                    if (enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash,
                    enableDescription,
                    if (enabled) AureaColors.Text else AureaColors.Muted,
                    onToggleEnabled,
                )
                Box(
                    dragHandle
                        .size(40.dp)
                        .semantics { contentDescription = dragDescription },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.LineHorizontal3, 20.dp, if (lifted) AureaColors.Accent else AureaColors.Muted)
                }
            }
        }
        if (expanded) {
            Column(
                Modifier.fillMaxWidth().padding(horizontal = 6.dp).alpha(if (enabled) 1f else 0.45f),
                verticalArrangement = Arrangement.spacedBy(4.dp),
                content = content,
            )
        }
    }
}

@Composable
private fun CardButton(glyph: Char, label: String, tint: Color, onClick: () -> Unit) {
    Box(
        Modifier
            .size(40.dp)
            .semantics { contentDescription = label }
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 20.dp, tint)
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
            stringResource(R.string.ds_avancado),
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 13.dp, AureaColors.Accent)
        Spacer(Modifier.weight(1f))
        if (!open) {
            Text(
                if (count == 1) stringResource(R.string.ds_1_ajuste) else stringResource(R.string.ds_adjustment_count, count),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
    }
}
