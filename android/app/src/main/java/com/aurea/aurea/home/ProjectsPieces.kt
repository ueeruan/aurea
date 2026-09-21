package com.aurea.aurea.home

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.time.LocalTime

// Largura de decodificação das imagens dos modelos (o `cacheWidth` da A.01).
private const val MODEL_DECODE_PX = 464

/** Placeholder de miniatura: degradê #1B2530 → #151C24 (topLeft → bottomRight). */
private val PlaceholderBrush = Brush.linearGradient(listOf(AureaColors.SurfaceHigh, AureaColors.Surface))

/** Scrim do hero: transparente até 45 %, preto 70 % no fim. */
private val HeroScrimBrush = Brush.verticalGradient(0.45f to Color.Transparent, 1f to HomeColors.HeroScrim)

private val PillShape = RoundedCornerShape(999.dp)

/** A saudação pela hora do aparelho (05–11 dia, 12–17 tarde, resto noite). */
private fun greeting(): String {
    val h = LocalTime.now().hour
    return when {
        h in 5..11 -> "Bom dia"
        h in 12..17 -> "Boa tarde"
        else -> "Boa noite"
    }
}

/** `_Cabecalho`: logo 38, "Aurea" 30 w800, saudação, template e avatar. */
@Composable
internal fun HomeHeader(onTemplate: () -> Unit, onProfile: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(start = 20.dp, top = 14.dp, end = 12.dp, bottom = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AureaLogo(38.dp)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text("Aurea", style = HomeType.HeaderTitle)
            Spacer(Modifier.height(2.dp))
            Text(greeting(), style = HomeType.Greeting, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        RoundIconButton(CupertinoGlyph.DocOnDoc, "Abrir template", onTemplate)
        Spacer(Modifier.width(6.dp))
        AccountAvatar(onProfile)
    }
}

/** Botão "Novo projeto": 54 de altura, raio 16, #6FAED9, "+" 20. */
@Composable
internal fun NewProjectButton(onClick: () -> Unit) {
    Box(
        Modifier
            .padding(start = 20.dp, top = 6.dp, end = 20.dp)
            .fillMaxWidth()
            .height(HomeDims.NewButtonHeight)
            .clip(RoundedCornerShape(HomeDims.NewButtonRadius))
            .background(AureaColors.Accent)
            .pressHighlight(onClick),
        contentAlignment = Alignment.Center,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Plus, 20.dp, AureaColors.OnAccent)
            Spacer(Modifier.width(8.dp))
            Text("Novo projeto", style = HomeType.NewButton)
        }
    }
}

/** Os três atalhos redondos (padding 12/18/12). */
@Composable
internal fun HomeShortcuts(onMedia: () -> Unit, onTemplate: () -> Unit, onScene: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(start = 12.dp, top = 18.dp, end = 12.dp)) {
        Shortcut(CupertinoGlyph.PhotoOnRectangle, "Mídia", onMedia)
        Shortcut(CupertinoGlyph.DocOnDoc, "Template", onTemplate)
        Shortcut(CupertinoGlyph.Cube, "Cena 3D", onScene)
    }
}

/** `_Atalho`: círculo 56 com ícone 23 no destaque e o rótulo; o terço inteiro é o alvo. */
@Composable
private fun RowScope.Shortcut(glyph: Char, label: String, onClick: () -> Unit) {
    Column(Modifier.weight(1f).tocavel(onClick = onClick), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            Modifier.size(HomeDims.ShortcutCircle).clip(CircleShape).background(AureaColors.SurfaceHigh),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 23.dp, AureaColors.Accent)
        }
        Spacer(Modifier.height(7.dp))
        Text(label, style = HomeType.ShortcutLabel, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/**
 * `_CartaoContinuar`: o projeto mais recente em 16:9, miniatura de verdade,
 * scrim, nome e a pílula "Continuar" (só visual: o cartão inteiro abre).
 * Toque longo ou reticências = menu do projeto.
 */
@Composable
internal fun ContinueEditingCard(entry: ProjectEntry, thumbs: HomeThumbnails, onOpen: () -> Unit, onMenu: () -> Unit) {
    val spec = remember(entry) { projectSpec(entry) }
    val image = rememberProjectThumbnail(thumbs, entry)
    Box(
        Modifier
            .padding(start = 20.dp, top = 18.dp, end = 20.dp)
            .fillMaxWidth()
            .tocavel(onLongClick = onMenu, onClick = onOpen)
            .clip(RoundedCornerShape(HomeDims.HeroRadius))
            .aspectRatio(16f / 9f),
    ) {
        if (image != null) {
            Image(image, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
        } else {
            Box(Modifier.fillMaxSize().background(PlaceholderBrush), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Film, 34.dp, AureaColors.Muted)
            }
        }
        Box(Modifier.fillMaxSize().background(HeroScrimBrush))
        Row(
            Modifier.align(Alignment.BottomStart).fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 14.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Column(Modifier.weight(1f)) {
                Text("Continuar editando", style = HomeType.HeroKicker)
                Spacer(Modifier.height(3.dp))
                Text(entry.title, style = HomeType.HeroTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Spacer(Modifier.height(2.dp))
                Text(spec, style = HomeType.HeroSpec, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Spacer(Modifier.width(10.dp))
            // A.01 pintava ícone e texto com #10130C (sobra do tema lima, §8.10): aqui é onAccent.
            Row(
                Modifier.clip(PillShape).background(AureaColors.Accent).padding(horizontal = 14.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.PlayFill, 13.dp, AureaColors.OnAccent)
                Spacer(Modifier.width(6.dp))
                Text("Continuar", style = HomeType.HeroPill)
            }
        }
        Box(
            Modifier
                .align(Alignment.TopEnd)
                .padding(top = 2.dp, end = 2.dp)
                .size(40.dp)
                .semantics { contentDescription = "Menu do projeto" }
                .tocavel(onClick = onMenu),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Ellipsis, 18.dp, HomeColors.White70)
        }
    }
}

/**
 * `_CartaoProjeto`: miniatura (raio 14) ou a moldura do formato, o nome, a
 * ficha e as reticências. Escolhendo vários, o toque marca em vez de abrir.
 */
@Composable
internal fun ProjectGridCard(
    entry: ProjectEntry,
    thumbs: HomeThumbnails,
    selecting: Boolean,
    marked: Boolean,
    onOpen: () -> Unit,
    onMenu: () -> Unit,
    onMark: () -> Unit,
    modifier: Modifier,
) {
    val spec = remember(entry) { projectSpec(entry) }
    val image = rememberProjectThumbnail(thumbs, entry)
    Column(
        modifier
            .aspectRatio(HomeDims.CARD_ASPECT)
            .tocavel(onLongClick = if (selecting) onMark else onMenu, onClick = if (selecting) onMark else onOpen),
    ) {
        Box(Modifier.weight(1f).fillMaxWidth().clip(RoundedCornerShape(HomeDims.CardRadius))) {
            if (image != null) {
                Image(image, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
            } else {
                FormatPlaceholder(projectRatio(entry))
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (selecting) {
                CupertinoIcon(
                    if (marked) CupertinoGlyph.CheckmarkCircleFill else CupertinoGlyph.Circle,
                    18.dp,
                    if (marked) AureaColors.Accent else AureaColors.Muted,
                )
                Spacer(Modifier.width(6.dp))
            }
            Column(Modifier.weight(1f)) {
                Text(entry.title, style = HomeType.CardTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Spacer(Modifier.height(1.dp))
                Text(spec, style = HomeType.CardSpec, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Box(
                Modifier
                    .size(40.dp)
                    .semantics { contentDescription = "Menu do projeto" }
                    .tocavel(onClick = if (selecting) onMark else onMenu),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 18.dp, AureaColors.Muted)
            }
        }
    }
}

/** Antes da 1ª miniatura: a moldura do formato do projeto (52 % da largura ou 62 % da altura). */
@Composable
private fun FormatPlaceholder(ratio: Float) {
    Box(Modifier.fillMaxSize().background(PlaceholderBrush), contentAlignment = Alignment.Center) {
        val frame = if (ratio >= 1f) Modifier.fillMaxWidth(0.52f).aspectRatio(ratio)
        else Modifier.fillMaxHeight(0.62f).aspectRatio(ratio, matchHeightConstraintsFirst = true)
        Box(
            frame.clip(RoundedCornerShape(4.dp)).background(AureaColors.Background.copy(alpha = 0.55f)),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Film, 18.dp, AureaColors.Muted)
        }
    }
}

/** `_SemProjetos`: film 22 + a frase (também para busca sem resultado, como na A.01). */
@Composable
internal fun ProjectsEmptyState() {
    Row(
        Modifier.fillMaxWidth().padding(start = 20.dp, top = 4.dp, end = 20.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Film, 22.dp, AureaColors.Muted)
        Spacer(Modifier.width(12.dp))
        Text("Seus projetos aparecem aqui, com a miniatura do que voce fez.", style = HomeType.Empty, modifier = Modifier.weight(1f))
    }
}

/**
 * `_BarraDaLista`: "{N} projetos" + buscar/ordenar/selecionar; buscando, o
 * campo toma o lugar do título; selecionando, "{n} escolhidos" + marcar
 * todos + sair. Os botões são 35×35 como na A.01 — o hit-test do Compose já
 * estende o toque até o mínimo de 48 dp sem mexer no desenho (§8.8).
 */
@Composable
internal fun ProjectListBar(
    count: Int,
    selecting: Boolean,
    selectedCount: Int,
    searching: Boolean,
    query: String,
    onQuery: (String) -> Unit,
    onOpenSearch: () -> Unit,
    onClearSearch: () -> Unit,
    onSort: () -> Unit,
    onSelect: () -> Unit,
    onMarkAll: () -> Unit,
    onExitSelection: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(start = 20.dp, top = 18.dp, end = 12.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (selecting) {
            Text("$selectedCount escolhidos", style = HomeType.ListCount, modifier = Modifier.weight(1f))
            BarButton(CupertinoGlyph.CheckmarkCircle, "Marcar todos", onMarkAll)
            BarButton(CupertinoGlyph.Xmark, "Sair da seleção", onExitSelection)
            return@Row
        }
        if (!searching) {
            // A.01 dizia "1 projetos" (§8.6).
            Text(if (count == 1) "1 projeto" else "$count projetos", style = HomeType.ListCount, modifier = Modifier.weight(1f))
            BarButton(CupertinoGlyph.Search, "Buscar", onOpenSearch)
        } else {
            SearchField(query, onQuery, onClearSearch, Modifier.weight(1f))
        }
        BarButton(CupertinoGlyph.ArrowUpArrowDown, "Ordenar", onSort)
        BarButton(CupertinoGlyph.CheckmarkCircle, "Selecionar", onSelect)
    }
}

/** `_BotaoDaBarra`: padding 8 + ícone 19 muted. */
@Composable
private fun BarButton(glyph: Char, description: String, onClick: () -> Unit) {
    Box(
        Modifier
            .semantics { contentDescription = description }
            .tocavel(onClick = onClick)
            .padding(8.dp),
    ) {
        CupertinoIcon(glyph, 19.dp, AureaColors.Muted)
    }
}

/**
 * O `CupertinoTextField` padrão no escuro (altura 34, raio 5, hairline),
 * com foco automático e o "x" que limpa e fecha a busca.
 */
@Composable
private fun SearchField(query: String, onQuery: (String) -> Unit, onClear: () -> Unit, modifier: Modifier) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    BasicTextField(
        value = query,
        onValueChange = onQuery,
        singleLine = true,
        textStyle = HomeType.SearchText,
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Search),
        modifier = modifier.height(34.dp).focusRequester(focus),
        decorationBox = { inner ->
            Row(
                Modifier
                    .fillMaxSize()
                    .clip(RoundedCornerShape(5.dp))
                    .background(HomeColors.SearchField)
                    .border(Dp.Hairline, HomeColors.SearchFieldBorder, RoundedCornerShape(5.dp)),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.weight(1f).padding(horizontal = 7.dp), contentAlignment = Alignment.CenterStart) {
                    if (query.isEmpty()) Text("Procurar pelo nome", style = HomeType.SearchPlaceholder, maxLines = 1)
                    inner()
                }
                Box(
                    Modifier
                        .fillMaxHeight()
                        .semantics { contentDescription = "Limpar busca" }
                        .tocavel(onClick = onClear)
                        .padding(horizontal = 8.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.Text)
                }
            }
        },
    )
}

/** Um modelo pronto do carrossel (visual da A.01; abrir ainda não existe no motor novo). */
internal class TemplateModel(val image: Int, val title: String, val detail: String)

internal val TemplateModels = listOf(
    TemplateModel(R.drawable.modelo_vhf, "VHF · Neon Orbit", "12 cenas · vetores e gradientes animados"),
    TemplateModel(R.drawable.modelo_dnyx, "Aurea App · RMK Dnyx", "Texto, fotos, cursores e audio editaveis"),
    TemplateModel(R.drawable.modelo_reference, "Nova recriacao · Codex", "5 cenas · 280 quadros · camadas editaveis"),
    TemplateModel(R.drawable.modelo_notes, "Notes", "Icone, botao, listas, whip e glow"),
    // A A.01 usava a miniatura do Notes no Pindown (§8.12): não há outra imagem.
    TemplateModel(R.drawable.modelo_notes, "Pindown", "Casa, faisca medida e coroas 3D"),
)

/** `_CartaoModelo`: 232 de largura, imagem 232×146 raio 16, título e detalhe. */
@Composable
internal fun TemplateCard(model: TemplateModel, thumbs: HomeThumbnails, onClick: () -> Unit) {
    val image = rememberResourceThumbnail(thumbs, model.image, MODEL_DECODE_PX)
    Column(Modifier.padding(end = 12.dp).width(232.dp).tocavel(onClick = onClick)) {
        Box(
            Modifier.size(232.dp, 146.dp).clip(RoundedCornerShape(HomeDims.ModelRadius)).background(AureaColors.SurfaceHigh),
            contentAlignment = Alignment.Center,
        ) {
            if (image != null) {
                Image(image, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
            } else {
                CupertinoIcon(CupertinoGlyph.Film, 24.dp, AureaColors.Muted)
            }
        }
        Spacer(Modifier.height(8.dp))
        Text(model.title, style = HomeType.CardTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Spacer(Modifier.height(2.dp))
        Text(model.detail, style = HomeType.ModelDetail, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** `_LinhaGrande`: círculo 48 com o ícone, duas linhas e a seta. */
@Composable
internal fun HomeFeatureRow(glyph: Char, title: String, subtitle: String, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().tocavel(onClick = onClick).padding(horizontal = 20.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(48.dp).clip(CircleShape).background(AureaColors.Accent.copy(alpha = 0.14f)),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 22.dp, AureaColors.Accent)
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = HomeType.FeatureTitle)
            Spacer(Modifier.height(2.dp))
            Text(subtitle, style = HomeType.FeatureSubtitle)
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, 15.dp, AureaColors.Muted)
    }
}

/**
 * O vidro é opaco ao toque, como o Container colorido do Flutter: um toque
 * na área vazia da barra não pode abrir o cartão escondido embaixo dela.
 */
internal fun Modifier.blockTouches(): Modifier = pointerInput(Unit) {}

/** `_BarraAoRolar`: barra compacta de vidro (52) com a logo 22, "Aurea" e os dois botões. */
@Composable
internal fun CompactHomeBar(backdrop: Backdrop, onTemplate: () -> Unit, onProfile: () -> Unit, modifier: Modifier = Modifier) {
    Row(
        modifier
            .fillMaxWidth()
            .height(HomeDims.CompactBarHeight)
            .blockTouches()
            .glass(backdrop, HomeDims.CompactBarBlur, AureaColors.Background.copy(alpha = 0.62f), AureaColors.Background.copy(alpha = 0.97f))
            .padding(start = 20.dp, end = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AureaLogo(22.dp)
        Spacer(Modifier.width(8.dp))
        Text("Aurea", style = HomeType.CompactTitle)
        Spacer(Modifier.weight(1f))
        RoundIconButton(CupertinoGlyph.DocOnDoc, "Template", onTemplate)
        Spacer(Modifier.width(2.dp))
        RoundIconButton(CupertinoGlyph.PersonCropCircle, "Perfil", onProfile)
    }
}

/**
 * `_AcoesEmLote`: vidro #151C24 @ 0,88, "{n} escolhidos", Duplicar e Excluir.
 * Posicionada explicitamente ACIMA da barra de abas (a A.01 dependia de um
 * efeito colateral do `extendBody`, §8.14).
 */
@Composable
internal fun BatchActionsBar(count: Int, backdrop: Backdrop, onDuplicate: () -> Unit, onDelete: () -> Unit, modifier: Modifier) {
    Column(
        modifier
            .fillMaxWidth()
            .blockTouches()
            .glass(backdrop, HomeDims.BatchBarBlur, AureaColors.Surface.copy(alpha = 0.88f), AureaColors.Surface),
    ) {
        Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.Border))
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("$count escolhidos", style = HomeType.BatchCount, modifier = Modifier.weight(1f))
            Row(
                Modifier.tocavel(onClick = onDuplicate).padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.PlusSquareOnSquare, 18.dp, AureaColors.Text)
                Spacer(Modifier.width(6.dp))
                Text("Duplicar", style = HomeType.BatchAction)
            }
            Row(
                Modifier.tocavel(onClick = onDelete).padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.Danger)
                Spacer(Modifier.width(6.dp))
                Text("Excluir", style = HomeType.BatchDanger)
            }
        }
    }
}
