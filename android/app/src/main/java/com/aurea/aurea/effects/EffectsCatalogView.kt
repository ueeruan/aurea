package com.aurea.aurea.effects

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.editor.panels.effectDisplayName
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Largura de geração da prévia: o maior cartão da grade cabe nisto. */
private const val PREVIEW_W = 320
private const val PREVIEW_H = 200

/**
 * A cartela de recuo: degradê vertical, disco quente e a barra clara — a
 * "cena" do catálogo — com o ícone da categoria por cima. É o que aparece
 * antes da prévia real e para efeito que não se pré-visualiza.
 */
@Composable
fun GenericPlate(glyph: Char, modifier: Modifier = Modifier) {
    androidx.compose.foundation.Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(AureaColors.EffectPreviewTop, AureaColors.EffectPreviewBottom)))
        val w = size.width
        val h = size.height
        drawCircle(AureaColors.EffectPreviewDisc.copy(alpha = 0.85f), radius = w * 0.19f, center = Offset(w * 0.34f, h * 0.60f))
        drawRect(Color.White.copy(alpha = 0.6f), topLeft = Offset(w * 0.20f, h * 0.18f), size = androidx.compose.ui.geometry.Size(w * 0.60f, h * 0.012f))
        drawRect(Color.White.copy(alpha = 0.25f), topLeft = Offset(w * 0.20f, h * 0.26f), size = androidx.compose.ui.geometry.Size(w * 0.40f, h * 0.012f))
    }
    Box(Modifier.fillMaxSize().padding(AureaDims.S2), contentAlignment = Alignment.BottomEnd) {
        CupertinoIcon(glyph, AureaDims.IconSm, Color.White.copy(alpha = 0.85f))
    }
}

/**
 * A FICHA DE UM EFEITO (§20, §68): prévia grande, o que ele faz, onde
 * funciona, custo e a lista de parâmetros com tipo e faixa. Com [onApply],
 * termina em "Adicionar à seleção"; sem ele (o "Sobre o efeito" de um efeito
 * que já está na camada), a ficha é só leitura — a estrela continua valendo.
 */
@Composable
fun EffectDetailSheet(
    store: com.aurea.aurea.state.EditorStore,
    entry: EffectCatalogEntry,
    previews: EffectPreviewStore?,
    favorite: Boolean,
    onFavorite: () -> Unit,
    onApply: (() -> Unit)?,
    onDismiss: () -> Unit,
) {
    val name = effectDisplayName(entry.typeId, entry.name)
    val preview = rememberEffectPreview(previews, entry.typeId, PREVIEW_W, PREVIEW_H)
    val specs = remember(entry.typeId) { store.effectSpecs(entry.typeId) }
    val cost = effectCost(entry.effectClass)

    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        LazyColumn(
            Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(start = AureaDims.S5, end = AureaDims.S5, top = AureaDims.S2, bottom = AureaDims.S6),
        ) {
            item(key = "previa") {
                Box(Modifier.fillMaxWidth().aspectRatio(1.6f).clip(AureaShape.Card)) {
                    if (preview != null) {
                        Image(preview, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
                    } else {
                        GenericPlate(categoryGlyph(entry.category))
                    }
                }
                Spacer(Modifier.height(AureaDims.S4))
                val favoriteDesc = stringResource(if (favorite) R.string.effect_tirar_favoritos else R.string.effect_nos_favoritos)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(name, style = AureaType.of(22f, FontWeight.W700, -0.4f))
                        Spacer(Modifier.height(2.dp))
                        Text(effectCostLine(entry.category, cost), style = AureaType.CardSpec)
                    }
                    Box(
                        Modifier
                            .size(AureaDims.MinTap)
                            .semantics { contentDescription = favoriteDesc }
                            .tocavel(onClick = onFavorite),
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(
                            if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star,
                            AureaDims.IconLg,
                            if (favorite) AureaColors.Accent else AureaColors.Muted,
                        )
                    }
                }
                Spacer(Modifier.height(AureaDims.S3))
                Text(effectDescription(entry.typeId, entry.category), style = AureaType.of(14f, lineHeight = 1.45f))
                Spacer(Modifier.height(AureaDims.S3))
                Text(effectCompatibilityLine(entry.typeId), style = AureaType.CardSpec)
                Spacer(Modifier.height(AureaDims.S5))
            }
            if (specs.isNotEmpty()) {
                item(key = "titulo-params") {
                    Text(stringResource(R.string.effect_parameters), style = AureaType.Section)
                    Spacer(Modifier.height(AureaDims.S2))
                }
                items(specs.size) { i ->
                    val p = specs[i]
                    Row(Modifier.fillMaxWidth().padding(vertical = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(p.label, style = AureaType.of(14f), modifier = Modifier.weight(1f))
                        Text(paramSummary(p), style = AureaType.CardSpec, textAlign = TextAlign.End)
                    }
                }
            }
            if (onApply != null) {
                item(key = "acao") {
                    Spacer(Modifier.height(AureaDims.S5))
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(AureaDims.ButtonHeight)
                            .clip(AureaShape.Lg)
                            .background(AureaColors.Accent)
                            .tocavel(shrink = 1f, onClick = onApply),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(stringResource(R.string.effect_adicionar_selecao), style = AureaType.Button)
                    }
                }
            }
        }
    }
}

/**
 * O que o controle faz, em linguagem de edição: "Empurrar, Puxar, Torcer…",
 * "0 a 100 %", "Cor", "Liga/desliga", "Um ponto na tela".
 */
@Composable
private fun paramSummary(p: com.aurea.aurea.engine.EffectParam): String {
    if (p.enumLabels.isNotEmpty()) {
        val shown = p.enumLabels.take(3).joinToString(", ")
        return if (p.enumLabels.size > 3) "$shown…" else shown
    }
    return when (p.type) {
        com.aurea.aurea.engine.ParamType.COLOR -> stringResource(R.string.ds_cor)
        com.aurea.aurea.engine.ParamType.BOOL -> stringResource(R.string.effect_param_bool)
        com.aurea.aurea.engine.ParamType.POINT2D -> stringResource(R.string.effect_param_point)
        else -> {
            val unit = if (p.unit.isNotEmpty()) " ${p.unit}" else ""
            if (p.min.isFinite() && p.max.isFinite() && p.max > p.min) stringResource(R.string.fx_param_range, trimNumber(p.min), trimNumber(p.max)) + unit else unit.trim()
        }
    }
}

private fun trimNumber(v: Float): String =
    if (v == v.toInt().toFloat()) v.toInt().toString() else String.format(java.util.Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.')
