package com.aurea.aurea.ui.ds

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.max

// =============================================================================
// Omega fundação · F2: MENU POPUP ANCORADO (o menu popup da referência)
//
// Todo menu da tela abre preso ao botão que o abriu: 250 dp (ou a largura da
// âncora), raio 8, linhas de 40 dp, no máximo 8 linhas visíveis (o resto rola,
// com setas de rolagem), abaixo da âncora alinhado pela borda de INÍCIO e para
// cima quando não cabe. Par do `AureaAnchoredMenu` do iOS (AnchoredMenu.swift):
// mesmos tipos de linha, medidas, cores e ids.
//
// USO (a âncora é o layout PAI do menu, como no DropdownMenu):
//
//   var open by remember { mutableStateOf(false) }
//   Box {
//       ChromeButton(CupertinoGlyph.Ellipsis, desc, onClick = { open = true })
//       AnchoredMenu(open, onDismiss = { open = false }, tag = "layer.menu", entries = listOf(
//           MenuEntry.Header("Camada"),
//           MenuEntry.Item("Duplicar", glyph = CupertinoGlyph.PlusSquareOnSquare, tag = "layer.menu.duplicate") { … },
//           MenuEntry.Item("Solo", checked = solo, keepOpen = true) { … },
//           MenuEntry.Item("Colar estilo", submenu = listOf(…)),
//           MenuEntry.Divider,
//           MenuEntry.Item("Excluir", destructive = true) { … },
//       ))
//   }
// =============================================================================

/**
 * Tom das peças comuns da Omega (menu, caixa suspensa, diálogo).
 * [Light] = papéis claros (`AureaColors.Light*`; escuros nos temas escuros), o
 * padrão fora do editor. [Editor] = tons do editor (variante escura S9 da
 * referência), para quem quiser o menu escuro sobre o editor.
 */
enum class AureaSurfaceTone { Light, Editor }

/** Uma linha do [AnchoredMenu]. [tag] = `testTag` (= `accessibilityIdentifier` no iOS). */
@Immutable
sealed class MenuEntry {
    open val tag: String? get() = null

    /**
     * Linha de 40 dp. Cobre os tipos da referência: simples, com ícone ([glyph]
     * ou [swatch]), com sub-rótulo, opção vigente ([selected] = barra de 4 dp no
     * início), marcada ([checked] = ✓ no fim), com "›" ([submenu] abre uma
     * subpágina dentro do mesmo menu; [chevron] só desenha a seta), desabilitada
     * (alfa 0,25, sem toque) e destrutiva. [keepOpen]: o toque não fecha o menu
     * (alternâncias que se redesenham no lugar).
     */
    data class Item(
        val label: String,
        val glyph: Char? = null,
        val swatch: Color? = null,
        val sublabel: String? = null,
        val selected: Boolean = false,
        val checked: Boolean = false,
        val submenu: List<MenuEntry>? = null,
        val chevron: Boolean = false,
        val enabled: Boolean = true,
        val destructive: Boolean = false,
        val keepOpen: Boolean = false,
        override val tag: String? = null,
        val onClick: () -> Unit = {},
    ) : MenuEntry()

    /** Fileira de ícones (40 dp; células de 34 × 40, a escolhida com borda de 2 dp). */
    data class IconRow(
        val cells: List<MenuIconCell>,
        val selectedIndex: Int = -1,
        val keepOpen: Boolean = false,
        override val tag: String? = null,
    ) : MenuEntry()

    /** Cabeçalho de grupo (texto pequeno, sem toque). */
    data class Header(val title: String, override val tag: String? = null) : MenuEntry()

    /** Fio separador entre grupos. */
    data object Divider : MenuEntry()
}

/** Uma célula de [MenuEntry.IconRow]: glifo do AUREA ou amostra de cor; [label] é o rótulo de acessibilidade. */
@Immutable
data class MenuIconCell(
    val label: String,
    val glyph: Char? = null,
    val swatch: Color? = null,
    val tag: String? = null,
    val onClick: () -> Unit,
)

/** Medidas do menu (as da referência). */
object AnchoredMenuDefaults {
    val Width = 250.dp
    val Radius = 8.dp
    val RowHeight = 40.dp
    const val MAX_VISIBLE_ROWS = 8
    val VerticalPadding = 4.dp
    /** 8 linhas + o respiro de cima e de baixo; acima disso rola. */
    val MaxHeight = RowHeight * MAX_VISIBLE_ROWS + VerticalPadding * 2
    val ScreenMargin = 8.dp
    val AnchorGap = 4.dp
    const val DISABLED_ALPHA = 0.25f
}

/**
 * Onde o menu abre (conta pura, testável na JVM). Abaixo da âncora, alinhado
 * pela borda de início (ou de fim com [alignEnd]); para cima se não couber
 * embaixo; sempre a [margin] px das bordas da janela.
 */
object AnchoredMenuPosition {
    /** Borda esquerda do menu (px). Início em LTR = esquerda; em RTL = direita; [alignEnd] inverte. */
    fun x(anchorLeft: Int, anchorRight: Int, windowWidth: Int, popupWidth: Int, rtl: Boolean, alignEnd: Boolean, margin: Int): Int {
        val alignRight = rtl != alignEnd
        val rawX = if (alignRight) anchorRight - popupWidth else anchorLeft
        return rawX.coerceIn(margin, max(margin, windowWidth - popupWidth - margin))
    }

    /** Topo do menu (px): embaixo se couber, senão em cima, senão preso dentro da janela. */
    fun y(anchorTop: Int, anchorBottom: Int, windowHeight: Int, popupHeight: Int, margin: Int, gap: Int): Int {
        val below = anchorBottom + gap
        val above = anchorTop - gap - popupHeight
        return when {
            below + popupHeight <= windowHeight - margin -> below
            above >= margin -> above
            else -> below.coerceIn(margin, max(margin, windowHeight - popupHeight - margin))
        }
    }

    fun compute(
        anchor: IntRect,
        window: IntSize,
        popup: IntSize,
        rtl: Boolean,
        alignEnd: Boolean,
        margin: Int,
        gap: Int,
    ): IntOffset = IntOffset(
        x(anchor.left, anchor.right, window.width, popup.width, rtl, alignEnd, margin),
        y(anchor.top, anchor.bottom, window.height, popup.height, margin, gap),
    )
}

private class AnchoredMenuProvider(
    private val margin: Int,
    private val gap: Int,
    private val alignEnd: Boolean,
) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset = AnchoredMenuPosition.compute(
        anchorBounds, windowSize, popupContentSize,
        rtl = layoutDirection == LayoutDirection.Rtl, alignEnd = alignEnd, margin = margin, gap = gap,
    )
}

/** As cores do menu num tom (todas por token). */
@Immutable
internal class MenuColors(
    val background: Color,
    val text: Color,
    val muted: Color,
    val pressed: Color,
    val accent: Color,
    val danger: Color,
    val divider: Color,
    val shadow: Color,
) {
    companion object {
        fun of(tone: AureaSurfaceTone): MenuColors = when (tone) {
            AureaSurfaceTone.Light -> MenuColors(
                background = AureaColors.LightSurface,
                text = AureaColors.LightText,
                muted = AureaColors.LightTextMuted,
                pressed = AureaColors.LightSurfaceAlt,
                accent = AureaColors.LightAccentInk,
                danger = AureaColors.LightDanger,
                divider = AureaColors.LightDivider,
                shadow = AureaColors.LightShadow,
            )
            AureaSurfaceTone.Editor -> MenuColors(
                background = AureaColors.Pill,
                text = AureaColors.Text,
                muted = AureaColors.Muted,
                pressed = AureaColors.ChipHigh,
                accent = AureaColors.Accent,
                danger = AureaColors.Danger,
                divider = AureaColors.Border,
                shadow = AureaColors.LightShadow,
            )
        }
    }
}

/**
 * O menu popup ancorado. Fica na composição do PAI da âncora (a âncora é o
 * layout pai, como no `DropdownMenu`); [expanded] falso não desenha nada.
 *
 * @param expanded aberto ou fechado (estado do chamador).
 * @param onDismiss fechar: toque fora, Voltar do sistema ou toque numa linha sem [MenuEntry.Item.keepOpen].
 * @param entries as linhas (ver [MenuEntry]).
 * @param width largura (padrão 250 dp); ignorada com [matchAnchorWidth].
 * @param matchAnchorWidth o menu fica com a largura da âncora (caixa suspensa).
 * @param tone [AureaSurfaceTone.Light] (padrão) ou [AureaSurfaceTone.Editor].
 * @param alignEnd alinha pela borda de fim da âncora (o padrão é o início, como a referência).
 * @param tag `testTag` da caixa do menu.
 */
@Composable
fun AnchoredMenu(
    expanded: Boolean,
    onDismiss: () -> Unit,
    entries: List<MenuEntry>,
    width: Dp = AnchoredMenuDefaults.Width,
    matchAnchorWidth: Boolean = false,
    tone: AureaSurfaceTone = AureaSurfaceTone.Light,
    alignEnd: Boolean = false,
    tag: String? = null,
) {
    val density = LocalDensity.current
    // A âncora é o layout pai: mede a largura dela para `matchAnchorWidth`.
    var anchorWidthPx by remember { mutableIntStateOf(0) }
    if (matchAnchorWidth) {
        Box(Modifier.size(0.dp).onGloballyPositioned { c -> anchorWidthPx = c.parentLayoutCoordinates?.size?.width ?: 0 })
    }
    if (!expanded) return
    val provider = remember(density, alignEnd) {
        with(density) {
            AnchoredMenuProvider(AnchoredMenuDefaults.ScreenMargin.roundToPx(), AnchoredMenuDefaults.AnchorGap.roundToPx(), alignEnd)
        }
    }
    val menuWidth = if (matchAnchorWidth && anchorWidthPx > 0) with(density) { anchorWidthPx.toDp() } else width
    val colors = MenuColors.of(tone)
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        // Caminho das subpáginas abertas (índices): resolvido a cada composição,
        // então uma alternância `keepOpen` dentro da subpágina se redesenha.
        var path by remember { mutableStateOf(emptyList<Int>()) }
        val shape = RoundedCornerShape(AnchoredMenuDefaults.Radius)
        Box(
            Modifier
                .width(menuWidth)
                .shadow(8.dp, shape, ambientColor = colors.shadow, spotColor = colors.shadow)
                .clip(shape)
                .background(colors.background)
                // A janela do popup recorta a sombra: o fio marca a borda no claro.
                .border(0.5.dp, colors.divider, shape)
                .then(if (tag != null) Modifier.testTag(tag) else Modifier),
        ) {
            AnimatedContent(
                targetState = path,
                transitionSpec = {
                    val forward = targetState.size > initialState.size
                    // Subpágina: entra deslizando meia largura em 250 ms com
                    // repique; a que sai some em 200 ms acelerando.
                    (slideInHorizontally(tween(250, easing = OvershootEasing)) { if (forward) it / 2 else -it / 2 } +
                        fadeIn(tween(150))) togetherWith
                        (slideOutHorizontally(tween(200, easing = FastOutLinearInEasing)) { if (forward) -it / 2 else it / 2 } +
                            fadeOut(tween(200, easing = FastOutLinearInEasing)))
                },
                label = "menu-pagina",
            ) { current ->
                val (title, page) = resolvePage(entries, current)
                MenuPage(
                    title = title,
                    entries = page,
                    colors = colors,
                    onBack = { path = current.dropLast(1) },
                    onOpenSubmenu = { index -> path = current + index },
                    onDismiss = onDismiss,
                )
            }
        }
    }
}

/** Curva de repique da subpágina (o `OvershootInterpolator` da referência). */
private val OvershootEasing = CubicBezierEasing(0.34f, 1.4f, 0.64f, 1f)

/** A página no fim de [path] e o título dela (nulo na raiz). Caminho inválido volta à raiz. */
private fun resolvePage(root: List<MenuEntry>, path: List<Int>): Pair<String?, List<MenuEntry>> {
    var page = root
    var title: String? = null
    for (i in path) {
        val item = page.getOrNull(i) as? MenuEntry.Item ?: return null to root
        val sub = item.submenu ?: return null to root
        page = sub
        title = item.label
    }
    return title to page
}

@Composable
private fun MenuPage(
    title: String?,
    entries: List<MenuEntry>,
    colors: MenuColors,
    onBack: () -> Unit,
    onOpenSubmenu: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val scroll = rememberScrollState()
    Box {
        Column(
            Modifier
                .fillMaxWidth()
                .heightIn(max = AnchoredMenuDefaults.MaxHeight)
                .verticalScroll(scroll)
                .padding(vertical = AnchoredMenuDefaults.VerticalPadding),
        ) {
            if (title != null) BackRow(title, colors, onBack)
            entries.forEachIndexed { index, entry ->
                when (entry) {
                    is MenuEntry.Item -> ItemRow(entry, colors) {
                        when {
                            entry.submenu != null -> onOpenSubmenu(index)
                            entry.keepOpen -> entry.onClick()
                            else -> {
                                onDismiss()
                                entry.onClick()
                            }
                        }
                    }
                    is MenuEntry.IconRow -> IconRowView(entry, colors, onDismiss)
                    is MenuEntry.Header -> HeaderRow(entry, colors)
                    MenuEntry.Divider -> Box(
                        Modifier.padding(vertical = 4.dp).fillMaxWidth().height(1.dp).background(colors.divider),
                    )
                }
            }
        }
        // Setas de rolagem: só quando há mais lista acima ou abaixo.
        if (scroll.value > 0) ScrollCue(CupertinoGlyph.ChevronUp, colors, Modifier.align(Alignment.TopCenter))
        if (scroll.value < scroll.maxValue) ScrollCue(CupertinoGlyph.ChevronDown, colors, Modifier.align(Alignment.BottomCenter))
    }
}

@Composable
private fun ScrollCue(glyph: Char, colors: MenuColors, modifier: Modifier) {
    Box(
        modifier.fillMaxWidth().height(14.dp).background(colors.background.copy(alpha = 0.92f)),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(glyph, 12.dp, colors.text) }
}

@Composable
private fun BackRow(title: String, colors: MenuColors, onBack: () -> Unit) {
    val back = stringResource(R.string.om_menu_back)
    Row(
        Modifier
            .fillMaxWidth()
            .height(AnchoredMenuDefaults.RowHeight)
            .semantics { contentDescription = back }
            .tocavel(shrink = 1f, onClick = onBack)
            .padding(start = 8.dp, end = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.ChevronLeft, 16.dp, colors.text)
        Spacer(Modifier.width(8.dp))
        Text(
            title,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = colors.text)),
        )
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(colors.divider))
}

@Composable
private fun ItemRow(item: MenuEntry.Item, colors: MenuColors, onTap: () -> Unit) {
    val ink = if (item.destructive) colors.danger else colors.text
    Row(
        Modifier
            .fillMaxWidth()
            .height(AnchoredMenuDefaults.RowHeight)
            .then(item.tag?.let { Modifier.testTag(it) } ?: Modifier)
            .background(if (item.selected) colors.pressed else Color.Transparent)
            .semantics { selected = item.selected || item.checked }
            .tocavel(enabled = item.enabled, shrink = 1f, haptic = true, onClick = onTap)
            .alpha(if (item.enabled) 1f else AnchoredMenuDefaults.DISABLED_ALPHA),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Barra de 4 dp da opção vigente.
        Box(Modifier.width(4.dp).height(24.dp).background(if (item.selected) colors.accent else Color.Transparent))
        when {
            item.swatch != null -> Box(
                Modifier.padding(start = 6.dp).size(24.dp).clip(RoundedCornerShape(6.dp)).background(item.swatch)
                    .border(1.dp, colors.divider, RoundedCornerShape(6.dp)),
            )
            item.glyph != null -> Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) {
                CupertinoIcon(item.glyph, 20.dp, ink)
            }
        }
        Spacer(Modifier.width(if (item.glyph != null) 2.dp else 10.dp))
        Column(Modifier.weight(1f)) {
            Text(
                item.label,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = ink)),
            )
            if (item.sublabel != null) {
                Text(
                    item.sublabel,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = colors.muted)),
                )
            }
        }
        if (item.checked) CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, colors.accent, Modifier.padding(start = 6.dp))
        if (item.submenu != null || item.chevron) CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, colors.muted, Modifier.padding(start = 6.dp))
        Spacer(Modifier.width(12.dp))
    }
}

@Composable
private fun IconRowView(row: MenuEntry.IconRow, colors: MenuColors, onDismiss: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(AnchoredMenuDefaults.RowHeight)
            .then(row.tag?.let { Modifier.testTag(it) } ?: Modifier)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 6.dp),
        horizontalArrangement = Arrangement.Start,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        row.cells.forEachIndexed { i, cell ->
            val on = i == row.selectedIndex
            Box(
                Modifier
                    .size(34.dp, 40.dp)
                    .then(cell.tag?.let { Modifier.testTag(it) } ?: Modifier)
                    .semantics {
                        contentDescription = cell.label
                        selected = on
                    }
                    .tocavel(shrink = 1f, haptic = true) {
                        if (!row.keepOpen) onDismiss()
                        cell.onClick()
                    },
                contentAlignment = Alignment.Center,
            ) {
                val cellShape = RoundedCornerShape(4.dp)
                Box(
                    Modifier
                        .size(28.dp)
                        .then(if (on) Modifier.border(2.dp, colors.text, cellShape) else Modifier),
                    contentAlignment = Alignment.Center,
                ) {
                    when {
                        cell.swatch != null -> Box(Modifier.size(20.dp).clip(RoundedCornerShape(3.dp)).background(cell.swatch))
                        cell.glyph != null -> CupertinoIcon(cell.glyph, 20.dp, colors.text)
                    }
                }
            }
        }
    }
}

@Composable
private fun HeaderRow(header: MenuEntry.Header, colors: MenuColors) {
    Text(
        header.title,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = colors.muted)),
        modifier = Modifier
            .fillMaxWidth()
            .then(header.tag?.let { Modifier.testTag(it) } ?: Modifier)
            .semantics { heading() }
            .padding(start = 14.dp, end = 14.dp, top = 8.dp, bottom = 4.dp),
    )
}
