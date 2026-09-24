package com.aurea.aurea.home

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.plural
import com.aurea.aurea.ui.theme.tocavel
import java.time.LocalTime

/** Placeholder de miniatura: degradê #1B2530 → #151C24 (topLeft → bottomRight). */
private val PlaceholderBrush = Brush.linearGradient(listOf(AureaColors.SurfaceHigh, AureaColors.Surface))

/** Scrim do hero: transparente até 45 %, preto 70 % no fim. */
private val HeroScrimBrush = Brush.verticalGradient(0.45f to Color.Transparent, 1f to AureaColors.ImageScrim)

/**
 * A saudação pela hora do aparelho (05–11 dia, 12–17 tarde, resto noite).
 *
 * @Composable porque o texto vem do catálogo — a hora continua sendo a do
 * aparelho, o rótulo é que muda de idioma.
 */
@Composable
private fun greeting(): String {
    val h = LocalTime.now().hour
    return when {
        h in 5..11 -> stringResource(R.string.greeting_morning)
        h in 12..17 -> stringResource(R.string.greeting_afternoon)
        else -> stringResource(R.string.greeting_evening)
    }
}

/**
 * O cabeçalho da Início: logo 38, "Aurea" 30 w800, a saudação e os dois
 * acessos rápidos (buscar, ajustes). O avatar de perfil saiu com a aba (§2).
 */
@Composable
internal fun HomeHeader(onSearch: () -> Unit, onSettings: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = 14.dp, end = AureaDims.S3, bottom = AureaDims.S3),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AureaLogo(38.dp)
        Spacer(Modifier.width(AureaDims.S3))
        Column(Modifier.weight(1f)) {
            Text("Aurea", style = AureaType.Display)
            Spacer(Modifier.height(2.dp))
            Text(greeting(), style = AureaType.Greeting, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        RoundIconButton(CupertinoGlyph.Search, stringResource(R.string.home_search_projects), onSearch)
        Spacer(Modifier.width(2.dp))
        RoundIconButton(CupertinoGlyph.SliderHorizontal3, stringResource(R.string.home_title_settings), onSettings)
    }
}

/**
 * O cartão "Continuar editando": o projeto mais recente em 16:9, miniatura de
 * verdade, scrim, nome e a pílula. Toque abre; toque longo ou ⋯ abre o menu.
 */
@Composable
internal fun ContinueEditingCard(entry: ProjectEntry, thumbs: HomeThumbnails, onOpen: () -> Unit, onMenu: () -> Unit) {
    val spec = remember(entry) { projectSpec(entry) }
    val image = rememberProjectThumbnail(thumbs, entry, HERO_DECODE_PX)
    Box(
        Modifier
            .padding(start = AureaDims.Gutter, top = AureaDims.S4, end = AureaDims.Gutter)
            .fillMaxWidth()
            .tocavel(onLongClick = onMenu, onClick = onOpen)
            .clip(AureaShape.Xl)
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
            Modifier.align(Alignment.BottomStart).fillMaxWidth().padding(start = AureaDims.S4, end = AureaDims.S4, bottom = 14.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.home_continue), style = AureaType.HeroKicker)
                Spacer(Modifier.height(3.dp))
                Text(entry.title, style = AureaType.HeroTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Spacer(Modifier.height(2.dp))
                Text(spec, style = AureaType.HeroSpec, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Spacer(Modifier.width(10.dp))
            Row(
                Modifier.clip(AureaShape.Pill).background(AureaColors.Accent).padding(horizontal = 14.dp, vertical = 9.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.PlayFill, AureaDims.IconXs, AureaColors.OnAccent)
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.home_continue_action), style = AureaType.HeroPill)
            }
        }
        val menuLabel = stringResource(R.string.home_menu_content)
        Box(
            Modifier
                .align(Alignment.TopEnd)
                .padding(top = 2.dp, end = 2.dp)
                .size(40.dp)
                .semantics { contentDescription = menuLabel }
                .tocavel(onClick = onMenu),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Ellipsis, 18.dp, AureaColors.OnImage70)
        }
    }
}

/**
 * O cartão da grade: miniatura (raio 14) ou a moldura do formato, o nome, a
 * ficha e as reticências. Escolhendo vários, o toque marca em vez de abrir.
 *
 * A miniatura é decodificada no tamanho do CARTÃO ([CARD_DECODE_PX]), não no
 * tamanho do hero — numa grade de dezenas de projetos isso é a diferença entre
 * alguns MB e algumas dezenas de MB (§74).
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
            .aspectRatio(HOME_CARD_ASPECT)
            .tocavel(onLongClick = if (selecting) onMark else onMenu, onClick = if (selecting) onMark else onOpen),
    ) {
        Box(Modifier.weight(1f).fillMaxWidth().clip(AureaShape.Md)) {
            if (image != null) {
                Image(image, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
            } else {
                FormatPlaceholder(projectRatio(entry))
            }
            if (selecting) {
                Box(Modifier.fillMaxSize().background(AureaColors.Background.copy(alpha = if (marked) 0.35f else 0.15f)))
                Box(Modifier.align(Alignment.TopEnd).padding(6.dp)) {
                    CupertinoIcon(
                        if (marked) CupertinoGlyph.CheckmarkCircleFill else CupertinoGlyph.Circle,
                        AureaDims.IconLg,
                        if (marked) AureaColors.Accent else AureaColors.OnImage70,
                    )
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(entry.title, style = AureaType.CardTitle, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Spacer(Modifier.height(1.dp))
                Text(spec, style = AureaType.CardSpec, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            val menuLabel = stringResource(R.string.home_menu_content)
            Box(
                Modifier
                    .size(40.dp)
                    .semantics { contentDescription = menuLabel }
                    .tocavel(onClick = if (selecting) onMark else onMenu),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 18.dp, AureaColors.Muted)
            }
        }
    }
}

/** Antes da 1ª miniatura: a moldura do formato do projeto. */
@Composable
private fun FormatPlaceholder(ratio: Float) {
    Box(Modifier.fillMaxSize().background(PlaceholderBrush), contentAlignment = Alignment.Center) {
        val frame = if (ratio >= 1f) Modifier.fillMaxWidth(0.52f).aspectRatio(ratio)
        else Modifier.fillMaxHeight(0.62f).aspectRatio(ratio, matchHeightConstraintsFirst = true)
        Box(
            frame.clip(RoundedCornerShape(AureaDims.S1)).background(AureaColors.Background.copy(alpha = 0.55f)),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Film, 18.dp, AureaColors.Muted)
        }
    }
}

/** `_SemProjetos`: film + a frase. Também serve para busca sem resultado. */
@Composable
internal fun ProjectsEmptyState(message: String? = null) {
    val text = message ?: stringResource(R.string.home_empty_hint)
    Row(
        Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = AureaDims.S1, end = AureaDims.Gutter),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Film, 22.dp, AureaColors.Muted)
        Spacer(Modifier.width(AureaDims.S3))
        Text(text, style = AureaType.Empty, modifier = Modifier.weight(1f))
    }
}

/**
 * A barra da lista: "{N} projetos" + buscar/ordenar/selecionar; buscando, o
 * campo toma o lugar do título; selecionando, "{n} escolhidos" + marcar todos
 * + sair.
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
    onSelectAll: () -> Unit,
    onExitSelection: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(start = AureaDims.Gutter, top = 18.dp, end = AureaDims.S3, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (selecting) {
            Text(stringResource(R.string.home_selected_chosen, selectedCount), style = AureaType.ListCount, modifier = Modifier.weight(1f))
            BarButton(CupertinoGlyph.CheckmarkCircle, stringResource(R.string.home_mark_all), onSelectAll)
            BarButton(CupertinoGlyph.Xmark, stringResource(R.string.home_exit_selection), onExitSelection)
            return@Row
        }
        if (!searching) {
            Text(plural(R.plurals.home_project_count, count), style = AureaType.ListCount, modifier = Modifier.weight(1f))
            BarButton(CupertinoGlyph.Search, stringResource(R.string.common_search), onOpenSearch)
        } else {
            SearchField(query, onQuery, onClearSearch, Modifier.weight(1f))
        }
        BarButton(CupertinoGlyph.ArrowUpArrowDown, stringResource(R.string.home_sort_title), onSort)
        if (!searching) BarButton(CupertinoGlyph.CheckmarkCircle, stringResource(R.string.home_select)) { onSelectAll() }
    }
}

/** `_BotaoDaBarra`: padding 8 + ícone 19 muted. */
@Composable
private fun BarButton(glyph: Char, description: String, onClick: () -> Unit) {
    Box(
        Modifier
            .semantics { contentDescription = description }
            .tocavel(onClick = onClick)
            .padding(AureaDims.S2),
    ) {
        CupertinoIcon(glyph, 19.dp, AureaColors.Muted)
    }
}

/**
 * O `CupertinoTextField` padrão no escuro (altura 34, raio 5, hairline),
 * com foco automático e o "x" que limpa e fecha a busca.
 */
@Composable
internal fun SearchField(query: String, onQuery: (String) -> Unit, onClear: () -> Unit, modifier: Modifier) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    BasicTextField(
        value = query,
        onValueChange = onQuery,
        singleLine = true,
        textStyle = AureaType.SearchText,
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Search),
        modifier = modifier.height(AureaDims.ChipHeight).focusRequester(focus),
        decorationBox = { inner ->
            Row(
                Modifier
                    .fillMaxSize()
                    .clip(AureaShape.Xs)
                    .background(AureaColors.Field)
                    .border(Dp.Hairline, AureaColors.FieldBorder, AureaShape.Xs),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.weight(1f).padding(horizontal = 7.dp), contentAlignment = Alignment.CenterStart) {
                    if (query.isEmpty()) Text(stringResource(R.string.home_search_hint), style = AureaType.SearchPlaceholder, maxLines = 1)
                    inner()
                }
                val clearLabel = stringResource(R.string.home_clear_search)
                Box(
                    Modifier
                        .fillMaxHeight()
                        .semantics { contentDescription = clearLabel }
                        .tocavel(onClick = onClear)
                        .padding(horizontal = AureaDims.S2),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.XmarkCircleFill, AureaDims.IconSm, AureaColors.Text)
                }
            }
        },
    )
}

/**
 * `_AcoesEmLote`: vidro, "{n} escolhidos", Duplicar e Excluir. Posicionada
 * explicitamente ACIMA da barra de abas.
 */
@Composable
internal fun BatchActionsBar(count: Int, backdrop: Backdrop, onDuplicate: () -> Unit, onDelete: () -> Unit, modifier: Modifier) {
    Column(
        modifier
            .fillMaxWidth()
            .blockTouches()
            .glass(backdrop, com.aurea.aurea.ui.theme.AureaElevation.BatchBarBlur,
                com.aurea.aurea.ui.theme.AureaElevation.batchBarTint(),
                com.aurea.aurea.ui.theme.AureaElevation.batchBarFallback()),
    ) {
        Box(Modifier.fillMaxWidth().height(AureaDims.Hairline).background(AureaColors.Border))
        Row(Modifier.fillMaxWidth().padding(horizontal = AureaDims.S3, vertical = AureaDims.S2), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.home_selected_chosen, count), style = AureaType.BatchCount, modifier = Modifier.weight(1f))
            BatchButton(CupertinoGlyph.PlusSquareOnSquare, stringResource(R.string.common_duplicate), AureaColors.Text, AureaType.BatchAction, onDuplicate)
            BatchButton(CupertinoGlyph.Trash, stringResource(R.string.common_delete), AureaColors.Danger, AureaType.BatchDanger, onDelete)
        }
    }
}

@Composable
private fun BatchButton(glyph: Char, label: String, tint: Color, style: androidx.compose.ui.text.TextStyle, onClick: () -> Unit) {
    Row(
        Modifier.tocavel(onClick = onClick).padding(horizontal = 14.dp, vertical = AureaDims.S2),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 18.dp, tint)
        Spacer(Modifier.width(6.dp))
        Text(label, style = style)
    }
}

/** Proporção do cartão da grade: miniatura + duas linhas de texto. */
internal const val HOME_CARD_ASPECT = 1.08f
