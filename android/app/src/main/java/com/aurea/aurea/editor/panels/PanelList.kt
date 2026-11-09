package com.aurea.aurea.editor.panels

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.ContentIcon
import com.aurea.aurea.editor.ContentIconKind
import com.aurea.aurea.ui.ds.LocalParamRowStacked
import com.aurea.aurea.ui.ds.LocalToggleOffTrack
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * 0.5.0: o kit de lista dos painéis, na organização do Motion (37 dp por item,
 * 5 dp de canto, seta + ícone + nome, seções que abrem e fecham). Par do iOS em
 * LayerPanels.swift. Tudo que é escolha fica à vista: nada de fileira cortada na borda.
 */
@Composable
internal fun PanelSection(
    title: String,
    glyph: Char,
    tag: String,
    startOpen: Boolean = true,
    count: Int = 0,
    flat: Boolean = false,
    stacked: Boolean = true,
    trailing: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    // Com o interruptor no título (como no Motion), ligar abre e desligar fecha.
    var open by rememberSaveable(tag, startOpen) { mutableStateOf(startOpen) }
    // O interruptor desligado usa a cor da seção: sobre ela, precisa escurecer para aparecer.
    val toggleTrack = if (flat) LocalToggleOffTrack.current else AureaColors.FxLowSurface
    CompositionLocalProvider(LocalToggleOffTrack provides toggleTrack) { Column(
        Modifier
            .fillMaxWidth()
            .padding(top = if (flat) 0.dp else 6.dp)
            .then(if (flat) Modifier else Modifier.clip(RoundedCornerShape(5.dp)).background(AureaColors.FxListSurface))
            .animateContentSize(tween(200)),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(40.dp)
                .testTag(tag)
                .then(if (trailing == null) Modifier.semantics(mergeDescendants = true) { selected = open } else Modifier)
                .tocavel(shrink = 1f, role = Role.Button) { open = !open }
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Text.copy(alpha = if (open) 0.9f else 0.3f))
            Spacer(Modifier.width(8.dp))
            CupertinoIcon(glyph, 18.dp, AureaColors.Text, autoMirror = false)
            Spacer(Modifier.width(8.dp))
            Text(
                title,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)),
            )
            if (count > 0) Text("$count", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            trailing?.invoke()
        }
        if (open) CompositionLocalProvider(LocalParamRowStacked provides stacked) {
            Column(Modifier.fillMaxWidth().padding(start = 8.dp, end = 8.dp, bottom = 8.dp), content = content)
        }
    } }
}

/** Um item que se aplica ou se escolhe: ícone, nome e o estado aceso. */
internal class PanelItem(val glyph: Char, val label: String, val tag: String, val on: Boolean = false, val contentIcon: ContentIconKind? = null, val onClick: () -> Unit)

/** Dentro de um cartão escuro os itens clareiam, para não sumirem no fundo. */
internal val LocalPanelItemSurface = compositionLocalOf<Color?> { null }

@Composable
internal fun PanelNested(content: @Composable () -> Unit) {
    CompositionLocalProvider(LocalPanelItemSurface provides AureaColors.FxListSurface, LocalToggleOffTrack provides null, content = content)
}

/** Grade de itens com nome e ícone. Até 2 colunas, em linha; 3 ou mais, ícone sobre o nome. */
@Composable
internal fun PanelItemGrid(items: List<PanelItem>, columns: Int = 2) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        items.chunked(columns).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                row.forEach { item -> Box(Modifier.weight(1f)) { PanelTile(item, stacked = columns >= 3) } }
                repeat(columns - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

@Composable
private fun PanelTile(item: PanelItem, stacked: Boolean) {
    val tint = if (item.on) AureaColors.Accent else AureaColors.Text
    val modifier = Modifier
        .fillMaxWidth()
        .height(if (stacked) 56.dp else 44.dp)
        .clip(RoundedCornerShape(5.dp))
        .background(if (item.on) AureaColors.AccentDim else LocalPanelItemSurface.current ?: AureaColors.FxLowSurface)
        .testTag(item.tag)
        .semantics(mergeDescendants = true) { selected = item.on }
        .tocavel(shrink = 1f, role = Role.Button, onClick = item.onClick)
        .padding(horizontal = if (stacked) 3.dp else 8.dp)
    val text: @Composable (Modifier, TextAlign) -> Unit = { m, align ->
        Text(item.label, modifier = m, maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = align,
            style = AureaType.Base.merge(TextStyle(fontSize = if (stacked) 11.sp else 12.sp, lineHeight = 13.sp, color = tint)))
    }
    if (stacked) {
        Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            PanelTileIcon(item, tint)
            Spacer(Modifier.height(3.dp))
            text(Modifier, TextAlign.Center)
        }
    } else {
        Row(modifier, verticalAlignment = Alignment.CenterVertically) {
            PanelTileIcon(item, tint)
            Spacer(Modifier.width(8.dp))
            text(Modifier.weight(1f), TextAlign.Start)
        }
    }
}

@Composable
private fun PanelTileIcon(item: PanelItem, tint: Color) {
    if (item.contentIcon != null) ContentIcon(item.contentIcon, 18.dp, tint)
    else CupertinoIcon(item.glyph, 18.dp, tint, autoMirror = false)
}

/** Linha inteira: ícone, nome e, à direita, o detalhe; com `chevron`, leva a outra tela. */
@Composable
internal fun PanelItemRow(
    glyph: Char,
    title: String,
    tag: String,
    detail: String? = null,
    on: Boolean = false,
    danger: Boolean = false,
    chevron: Boolean = false,
    onClick: () -> Unit,
) {
    val tint = when { danger -> AureaColors.Danger; on -> AureaColors.Accent; else -> AureaColors.Text }
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp)
            .heightIn(min = 37.dp)
            .clip(RoundedCornerShape(5.dp))
            .background(if (on) AureaColors.AccentDim else LocalPanelItemSurface.current ?: AureaColors.FxLowSurface)
            .testTag(tag)
            .semantics(mergeDescendants = true) { selected = on }
            .tocavel(shrink = 1f, role = Role.Button, onClick = onClick)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 18.dp, tint, autoMirror = false)
        Spacer(Modifier.width(8.dp))
        Text(title, modifier = Modifier.weight(1f).padding(vertical = 8.dp), maxLines = 2, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = tint)))
        if (detail != null) {
            Spacer(Modifier.width(8.dp))
            Text(detail, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        }
        if (chevron) {
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.ChevronRight, 12.dp, AureaColors.Muted)
        }
    }
}

/** "+ Adicionar": o último item da lista, centrado, como no Motion. */
@Composable
internal fun PanelAddRow(label: String, tag: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp)
            .height(37.dp)
            .clip(RoundedCornerShape(5.dp))
            .background(LocalPanelItemSurface.current ?: AureaColors.FxLowSurface)
            .testTag(tag)
            .semantics(mergeDescendants = true) {}
            .tocavel(shrink = 1f, role = Role.Button, onClick = onClick)
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.Plus, 16.dp, AureaColors.Text)
        Spacer(Modifier.width(6.dp))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
    }
}
