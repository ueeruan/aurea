package com.aurea.aurea.home

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.core.view.WindowCompat
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  Omega home (área 01): o kit da Home clara.
//
//  A referência é clara fora do editor (decisão D6): tudo aqui pinta só com os
//  papéis claros da fundação (`AureaColors.Light*`) e os da Home
//  (`AureaColors.Home*`) — claros no Omega, escuros nos outros temas. A fonte é
//  a do AUREA com os tamanhos e pesos da referência (D7). Par de `OmegaHome*`
//  no HomeView.swift (mesmas medidas e ids).
// =============================================================================

/** Tipografia da Home Omega (tamanho e peso da referência, fonte do AUREA). */
internal object OmegaHomeType {
    /** Rótulo da aba do dock (12 médio). */
    val TabLabel: TextStyle get() = AureaType.of(12f, FontWeight.W500, 0f, AureaColors.LightText)
    /** Texto da pílula de subaba (16 médio). */
    val Pill: TextStyle get() = AureaType.of(16f, FontWeight.W500, 0f, AureaColors.LightText)
    /** "Meus projetos" e a contagem da seleção (18 negrito). */
    val ListTitle: TextStyle get() = AureaType.of(18f, FontWeight.W700, -0.2f, AureaColors.LightText)
    /** "Ordem: Nome", cabeçalho de data (14 regular). */
    val Small: TextStyle get() = AureaType.of(14f, FontWeight.Normal, 0f, AureaColors.LightText)
    /** Título da linha do projeto (16 negrito). */
    val RowTitle: TextStyle get() = AureaType.of(16f, FontWeight.W700, -0.1f, AureaColors.LightText)
    /** Ficha da linha (12 regular). */
    val Meta: TextStyle get() = AureaType.of(12f, FontWeight.Normal, 0f, AureaColors.LightTextMuted)
    /** Selo de duração (10 médio). */
    val Badge: TextStyle get() = AureaType.of(10f, FontWeight.W500, 0f, AureaColors.HomeOnBadge)
    /** Opção de menu (16 médio). */
    val Menu: TextStyle get() = AureaType.of(16f, FontWeight.W500, 0f, AureaColors.LightText)
    /** "Cancelar" do cabeçalho de seleção (18 regular). */
    val Action: TextStyle get() = AureaType.of(18f, FontWeight.Normal, 0f, AureaColors.LightText)
    /** Corpo (16 regular). */
    val Body: TextStyle get() = AureaType.of(16f, FontWeight.Normal, 0f, AureaColors.LightTextMuted, lineHeight = 1.4f)
    /** Título da gaveta (17 negrito). */
    val DrawerTitle: TextStyle get() = AureaType.of(17f, FontWeight.W700, 0f, AureaColors.LightText)
    /** Rótulo das linhas da gaveta (12 negrito). */
    val DrawerLabel: TextStyle get() = AureaType.of(12f, FontWeight.W700, 0f, AureaColors.LightText)
    /** Nota e subtítulo das linhas da gaveta (11 regular). */
    val DrawerNote: TextStyle get() = AureaType.of(11f, FontWeight.Normal, 0f, AureaColors.LightTextMuted, lineHeight = 1.3f)
}

/** Medidas da Home Omega (as da referência). */
internal object OmegaHomeDims {
    val TopBar = 57.dp
    val Dock = 72.dp
    val SelectionBar = 72.dp
    val Gutter = 16.dp
    val Row = 96.dp
    val Thumb = 80.dp
    val PillHeight = 40.dp
    val Drawer = 280.dp
    val DrawerRow = 44.dp
    val ScrollShadow = 25.dp
}

/** Sombra colorida (a referência usa azul-marinho translúcido, não preto). */
internal fun Modifier.homeShadow(elevation: Dp, shape: Shape, color: Color): Modifier =
    shadow(elevation, shape, clip = false, ambientColor = color, spotColor = color)

/**
 * Pílula de subaba (40 dp, raio 12, padding 16/2, texto 16 médio). Cores da
 * referência confirmadas pelo REA: escolhida = texto claro sobre azul-marinho;
 * não escolhida = texto cinza sobre branco, que fica azul-marinho sobre lilás
 * enquanto pressionada.
 */
@Composable
internal fun HomePill(label: String, selected: Boolean, tag: String, onClick: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val shape = RoundedCornerShape(12.dp)
    val fill by animateColorAsState(
        when {
            selected -> AureaColors.HomePillSelected
            pressed -> AureaColors.HomePillPressed
            else -> AureaColors.HomePillUnselected
        }, tween(150), label = "pilula-fundo",
    )
    val ink = when {
        selected -> AureaColors.HomeOnPillSelected
        pressed -> AureaColors.HomeOnPillPressed
        else -> AureaColors.HomeOnPillUnselected
    }
    Box(
        Modifier
            .heightIn(min = OmegaHomeDims.PillHeight)
            .homeShadow(12.dp, shape, if (selected) AureaColors.HomeShadowStrong else AureaColors.HomeShadowSoft)
            .clip(shape)
            .background(fill)
            .testTag(tag)
            .semantics { role = Role.Tab; this.selected = selected }
            .clickable(interactionSource = interaction, indication = null, onClick = onClick)
            .padding(start = 16.dp, end = 16.dp, bottom = 2.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = OmegaHomeType.Pill.copy(color = ink), maxLines = 1)
    }
}

/** Fileira de pílulas com rolagem horizontal (16 antes, 12 entre, 16 depois). */
@Composable
internal fun HomeSubtabs(options: List<Pair<String, String>>, selected: Int, onSelect: (Int) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            // Folga para a sombra de 12 dp não ser cortada pela rolagem.
            .padding(vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Spacer(Modifier.width(4.dp))
        options.forEachIndexed { i, (label, tag) ->
            HomePill(label, i == selected, tag) { onSelect(i) }
        }
        Spacer(Modifier.width(4.dp))
    }
}

/**
 * Linha "Meus projetos" (24 dp de desenho, alvo de 44): título 18 negrito à esquerda; à direita o
 * extra de busca do AUREA (D8) e "Ordem: …" com o ícone de ordenação.
 */
@Composable
internal fun HomeListHeader(
    title: String,
    sortLabel: String,
    onSort: () -> Unit,
    tag: String,
    sortTag: String,
    searchLabel: String? = null,
    searchTag: String? = null,
    onSearch: (() -> Unit)? = null,
    sortAnchor: @Composable () -> Unit = {},
) {
    Row(
        // 24 dp de desenho dentro de um alvo de 44 dp (os espaços em volta compensam).
        Modifier.fillMaxWidth().heightIn(min = 44.dp).padding(horizontal = OmegaHomeDims.Gutter).testTag(tag),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title,
            style = OmegaHomeType.ListTitle,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).semantics { heading() },
        )
        if (onSearch != null && searchLabel != null) {
            Box(
                Modifier
                    .size(44.dp)
                    .then(if (searchTag != null) Modifier.testTag(searchTag) else Modifier)
                    .semantics { contentDescription = searchLabel }
                    .tocavel(onClick = onSearch),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, 20.dp, AureaColors.LightText)
            }
        }
        Box {
            Row(
                Modifier
                    .heightIn(min = 44.dp)
                    .testTag(sortTag)
                    .tocavel(shrink = 1f, onClick = onSort),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(sortLabel, style = OmegaHomeType.Small, maxLines = 1)
                Spacer(Modifier.width(4.dp))
                CupertinoIcon(CupertinoGlyph.ArrowUpArrowDown, 16.dp, AureaColors.LightText)
            }
            sortAnchor()
        }
    }
}

/**
 * A sombra de 25 dp no topo da lista, só quando ela saiu do topo (REA:
 * `P.B8K.Rw`, degradê `#261f2238` por cima dos itens).
 */
@Composable
internal fun HomeScrollShadow(visible: Boolean, modifier: Modifier = Modifier) {
    if (!visible) return
    val color = AureaColors.LightShadow
    Box(
        modifier
            .fillMaxWidth()
            .height(OmegaHomeDims.ScrollShadow)
            .drawBehind { drawRect(Brush.verticalGradient(listOf(color, Color.Transparent))) },
    )
}

/**
 * O brilho animado do quadrado sem imagem (carregando ou sem miniatura): uma
 * faixa clara que atravessa o fundo `HomeThumb` em 1,2 s.
 */
@Composable
internal fun HomeShimmer(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "brilho")
    val t by transition.animateFloat(
        0f, 1f, infiniteRepeatable(tween(1200, easing = LinearEasing), RepeatMode.Restart), label = "brilho-t",
    )
    val base = AureaColors.HomeThumb
    val glow = AureaColors.LightSurface.copy(alpha = 0.55f)
    Box(
        modifier.drawBehind {
            drawRect(base)
            val w = size.width
            val x = -w + 2f * w * t
            drawRect(Brush.linearGradient(listOf(Color.Transparent, glow, Color.Transparent), Offset(x, 0f), Offset(x + w, size.height)))
        },
    )
}

/**
 * Cor dos ícones da barra de status e de navegação enquanto a Home está na tela
 * ([light] = ícones escuros, para fundo claro); volta ao estado anterior ao
 * sair. 0.3.3: a Home usa os tons escuros da paleta, então passa `false`.
 */
@Composable
internal fun LightSystemBars(light: Boolean) {
    val view = LocalView.current
    DisposableEffect(view, light) {
        val window = view.context.findActivity()?.window
        val controller = window?.let { WindowCompat.getInsetsController(it, view) }
        val statusBefore = controller?.isAppearanceLightStatusBars
        val navBefore = controller?.isAppearanceLightNavigationBars
        if (controller != null && !view.isInEditMode) {
            controller.isAppearanceLightStatusBars = light
            // Até o API 28 a barra de navegação tem o véu preto do edge-to-edge:
            // ícones escuros sumiriam nele. Do 29 em diante ela é transparente
            // sobre o dock claro.
            if (android.os.Build.VERSION.SDK_INT >= 29) controller.isAppearanceLightNavigationBars = light
        }
        onDispose {
            if (controller != null && statusBefore != null && navBefore != null) {
                controller.isAppearanceLightStatusBars = statusBefore
                controller.isAppearanceLightNavigationBars = navBefore
            }
        }
    }
}

internal tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

/**
 * Os componentes do Material (campo, chip, botão de texto, barra de progresso)
 * dentro da Home: o esquema escuro do app com o texto e o conteúdo nos papéis
 * da Home. 0.3.3: a Home é escura (paleta AUREA), sem esquema claro.
 */
@Composable
internal fun HomeMaterial(content: @Composable () -> Unit) {
    val scheme = androidx.compose.material3.MaterialTheme.colorScheme
    androidx.compose.material3.MaterialTheme(colorScheme = scheme) {
        androidx.compose.runtime.CompositionLocalProvider(
            androidx.compose.material3.LocalTextStyle provides AureaType.Base.copy(color = AureaColors.LightText),
            androidx.compose.material3.LocalContentColor provides AureaColors.LightText,
            content = content,
        )
    }
}
