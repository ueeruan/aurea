package com.aurea.aurea.home

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * Omega home · P0-5: a linha do projeto (96 dp) da referência — cartão branco
 * de raio 12 com sombra suave, miniatura de 80 dp com o selo de duração, o
 * título (16 negrito) e a ficha 2 × 2 (proporção · resolução · tamanho · fps),
 * e o ⋯ que abre o menu rápido.
 *
 * Gestos (P1-1, A.01 cede à referência): toque abre (ou marca, selecionando);
 * toque longo entra na seleção marcando este projeto, com háptico; o ⋯ fica
 * desabilitado na seleção. Selecionando, a caixa redonda aparece no início e o
 * cartão anda 40 dp em 300 ms; os não marcados ganham um véu claro.
 *
 * [coords] recebe as coordenadas do cartão (o menu rápido desenha a cópia no
 * mesmo lugar).
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun ProjectRowCard(
    entry: ProjectEntry,
    thumbs: HomeThumbnails,
    selecting: Boolean,
    marked: Boolean,
    onOpen: () -> Unit,
    onLongPress: () -> Unit,
    onMenu: (LayoutCoordinates?) -> Unit,
    modifier: Modifier = Modifier,
    interactive: Boolean = true,
) {
    val haptics = LocalHapticFeedback.current
    val shift by animateDpAsState(if (selecting) 40.dp else 0.dp, tween(300), label = "linha-desliza")
    val holder = remember { arrayOfNulls<LayoutCoordinates>(1) }
    val shape = RoundedCornerShape(12.dp)
    Box(modifier.fillMaxWidth().height(OmegaHomeDims.Row).testTag("home.project.row")) {
        if (selecting || shift > 0.dp) {
            Box(
                Modifier.align(Alignment.CenterStart).padding(start = 2.dp).size(20.dp).testTag("home.project.check")
                    .clip(CircleShape)
                    .then(
                        if (marked) Modifier.background(AureaColors.LightAccentInk)
                        else Modifier.border(1.5.dp, AureaColors.LightTextMuted, CircleShape)
                    ),
                contentAlignment = Alignment.Center,
            ) {
                if (marked) CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 12.dp, AureaColors.LightSurface)
            }
        }
        Row(
            Modifier
                .padding(start = shift)
                .fillMaxSize()
                .homeShadow(12.dp, shape, AureaColors.HomeShadowSoft)
                .clip(shape)
                .background(AureaColors.LightSurface)
                .onGloballyPositioned { holder[0] = it }
                .semantics(mergeDescendants = true) {
                    if (selecting) selected = marked
                    role = Role.Button
                }
                .then(
                    if (interactive) Modifier.combinedClickable(
                        onClick = onOpen,
                        onLongClick = {
                            haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                            onLongPress()
                        },
                    ) else Modifier
                )
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ProjectThumb(entry, thumbs)
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    entry.title.ifBlank { stringResource(R.string.new_project_untitled) },
                    style = OmegaHomeType.RowTitle,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(Modifier.height(8.dp))
                ProjectMetaGrid(entry)
            }
            val menuLabel = stringResource(R.string.home_menu_content)
            Box(
                Modifier
                    .size(44.dp)
                    .testTag("home.project.more")
                    .semantics {
                        contentDescription = menuLabel
                        if (selecting) disabled()
                    }
                    .tocavel(enabled = !selecting && interactive) { onMenu(holder[0]) },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, AureaColors.LightTextMuted.copy(alpha = if (selecting) 0.4f else 1f))
            }
            Spacer(Modifier.width(4.dp))
        }
        // Os não marcados ficam sob um véu claro enquanto se escolhe.
        if (selecting && !marked) {
            Box(
                Modifier.padding(start = shift).fillMaxSize().clip(shape)
                    .background(AureaColors.LightSurface.copy(alpha = 0.5f)),
            )
        }
    }
}

/**
 * Miniatura de 80 dp (raio 8, fundo `HomeThumb`, imagem em "cover"); sem
 * imagem ainda, o brilho animado. O selo de duração fica no canto inferior
 * de início.
 */
@Composable
internal fun ProjectThumb(entry: ProjectEntry, thumbs: HomeThumbnails) {
    // Omega 0.3.4: o brilho só enquanto carrega; sem capa fica o fundo `HomeThumb` parado.
    val load = rememberProjectThumbnailLoad(thumbs, entry, 256)
    val image = load.image
    Box(Modifier.size(OmegaHomeDims.Thumb).clip(RoundedCornerShape(8.dp)).background(AureaColors.HomeThumb)) {
        if (image != null) {
            Image(image, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, filterQuality = FilterQuality.Low)
        } else if (!load.finished) {
            HomeShimmer(Modifier.fillMaxSize())
        }
        val duration = remember(entry.durationFrames, entry.fps) { formatProjectDuration(entry.durationFrames, entry.fps) }
        if (duration != null) {
            Text(
                duration,
                style = OmegaHomeType.Badge,
                maxLines = 1,
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(4.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(AureaColors.HomeBadge)
                    .padding(horizontal = 4.dp, vertical = 1.dp)
                    .testTag("home.project.duration"),
            )
        }
    }
}

/**
 * A ficha 2 × 2 (168 dp): [proporção][resolução] / [tamanho][fps], cada
 * célula com ícone de 16 dp e texto 12. O leitor de tela lê os quatro juntos
 * (`home.project.meta`).
 */
@Composable
internal fun ProjectMetaGrid(entry: ProjectEntry) {
    val facts = remember(entry) { projectFacts(entry) }
    Column(
        Modifier.widthIn(max = 168.dp).testTag("home.project.meta").semantics(mergeDescendants = true) {},
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Row {
            MetaCell(CupertinoGlyph.Crop, facts.aspect, Modifier.weight(1f))
            MetaCell(CupertinoGlyph.Tv, facts.resolution, Modifier.weight(1f))
        }
        Row {
            MetaCell(CupertinoGlyph.DocText, facts.size, Modifier.weight(1f))
            MetaCell(CupertinoGlyph.Speedometer, facts.fps, Modifier.weight(1f))
        }
    }
}

@Composable
private fun MetaCell(glyph: Char, text: String, modifier: Modifier) {
    Row(modifier.height(16.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(glyph, 14.dp, AureaColors.LightTextMuted)
        Spacer(Modifier.width(6.dp))
        Text(text, style = OmegaHomeType.Meta, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/**
 * Omega home · P0-7: o estado vazio centralizado (padding 35): título 18
 * negrito, corpo com o "+" em negrito, 30 dp e a ilustração do AUREA (uma
 * moldura de filme com a marca, desenhada em código).
 */
@Composable
internal fun ProjectsEmptyState(modifier: Modifier = Modifier) {
    Column(
        modifier.fillMaxWidth().padding(horizontal = 35.dp).testTag("home.empty"),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            stringResource(R.string.home_no_projects),
            style = OmegaHomeType.ListTitle,
            textAlign = TextAlign.Center,
            modifier = Modifier.semantics { heading() },
        )
        Spacer(Modifier.height(8.dp))
        val body = stringResource(R.string.om_home_empty_body, PLUS_MARK)
        val bold = OmegaHomeType.Body.copy(fontWeight = FontWeight.W700, color = AureaColors.LightText).toSpanStyle()
        Text(
            buildAnnotatedString {
                val at = body.indexOf(PLUS_MARK)
                if (at < 0) append(body) else {
                    append(body.substring(0, at))
                    withStyle(SpanStyle(fontWeight = bold.fontWeight, color = bold.color)) { append(PLUS_MARK) }
                    append(body.substring(at + PLUS_MARK.length))
                }
            },
            style = OmegaHomeType.Body,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(30.dp))
        EmptyIllustration()
    }
}

private const val PLUS_MARK = "+"

/** Moldura de filme (perfurações em cima e embaixo) com a marca do AUREA no meio. */
@Composable
private fun EmptyIllustration() {
    val frame = AureaColors.LightSurface
    val line = AureaColors.LightDivider
    val holes = AureaColors.HomeThumb
    Box(
        Modifier
            .size(width = 132.dp, height = 96.dp)
            .homeShadow(12.dp, RoundedCornerShape(14.dp), AureaColors.HomeShadowSoft)
            .clip(RoundedCornerShape(14.dp))
            .background(frame)
            .border(1.dp, line, RoundedCornerShape(14.dp))
            .drawBehind {
                val hole = Size(10.dp.toPx(), 7.dp.toPx())
                val gap = (size.width - hole.width * 6) / 7
                for (i in 0 until 6) {
                    val x = gap + i * (hole.width + gap)
                    drawRoundRect(holes, Offset(x, 6.dp.toPx()), hole, CornerRadius(2.dp.toPx()))
                    drawRoundRect(holes, Offset(x, size.height - 6.dp.toPx() - hole.height), hole, CornerRadius(2.dp.toPx()))
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        AureaLogo(40.dp)
    }
}

/** Busca sem resultado (a lista existe, o filtro não achou nada). */
@Composable
internal fun ProjectsNoResults() {
    Text(
        stringResource(R.string.home_no_results),
        style = OmegaHomeType.Body,
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().padding(horizontal = 35.dp, vertical = 32.dp),
    )
}

/**
 * O campo de busca claro (40 dp, raio 8), com foco automático e o ✕ que limpa
 * e fecha a busca (extra do AUREA, D8).
 */
@Composable
internal fun HomeSearchField(query: String, onQuery: (String) -> Unit, onClear: () -> Unit, modifier: Modifier = Modifier) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    BasicTextField(
        value = query,
        onValueChange = onQuery,
        singleLine = true,
        textStyle = OmegaHomeType.Menu.copy(fontWeight = FontWeight.Normal),
        cursorBrush = SolidColor(AureaColors.LightAccentInk),
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Search),
        modifier = modifier.height(40.dp).focusRequester(focus).testTag("home.projects.searchField"),
        decorationBox = { inner ->
            Row(
                Modifier
                    .fillMaxSize()
                    .clip(RoundedCornerShape(8.dp))
                    .background(AureaColors.LightSurface)
                    .border(0.5.dp, AureaColors.LightDivider, RoundedCornerShape(8.dp)),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, 16.dp, AureaColors.LightTextMuted, Modifier.padding(start = 10.dp))
                Box(Modifier.weight(1f).padding(horizontal = 8.dp), contentAlignment = Alignment.CenterStart) {
                    if (query.isEmpty()) Text(stringResource(R.string.home_search_hint),
                        style = OmegaHomeType.Menu.copy(fontWeight = FontWeight.Normal, color = AureaColors.LightTextFaint), maxLines = 1)
                    inner()
                }
                val clearLabel = stringResource(R.string.home_clear_search)
                Box(
                    Modifier
                        .fillMaxHeight()
                        .width(44.dp)
                        .semantics { contentDescription = clearLabel }
                        .tocavel(onClick = onClear),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 18.dp, AureaColors.LightTextMuted)
                }
            }
        },
    )
}
