package com.aurea.aurea.editor

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.FormatAlignLeft
import androidx.compose.material.icons.automirrored.rounded.FormatAlignRight
import androidx.compose.material.icons.rounded.Stairs
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextDecoration
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
import com.aurea.aurea.editor.timeline.Keyframes
import com.aurea.aurea.editor.timeline.Thumbs
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenuDefaults
import com.aurea.aurea.ui.ds.AnchoredMenuPosition
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuColors
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.max

// =============================================================================
// Omega chrome (área 02): as barras superiores da referência — 42 dp, botões de
// 40 com alvo de 44+ (o Compose estende o toque a 48 em barras baixas).
// =============================================================================

/** O que a barra da camada mostra (data class: recompõe só quando muda). */
private data class LayerHeader(val id: Long, val name: String, val kind: Int, val locked: Boolean, val parent: Long)

/** Título do projeto e nome da camada (14 sp, a referência). */
private val TitleStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W500))

/** Pai da linha na lista de camadas (0 = solta): `parentIndex` é índice na mesma lista. */
private fun parentOf(rows: List<LayerRow>, row: LayerRow): Long = rows.getOrNull(row.parentIndex)?.id ?: 0L

// =============================================================================
// Camada escolhida (`fragment_element_actionbar`): ‹ · nome · vínculo · 🗑 · ⋮
// =============================================================================

/**
 * Fundo da barra da camada (A9). ‹ desseleciona; o nome edita ali mesmo (com ✕
 * de apagar); o vínculo de pai some em camada de áudio; a lixeira apaga sem
 * perguntar (um desfazer); o ⋮ abre o menu da camada ancorado nele. 3D e
 * desfoque de movimento ficam na barra (0.3.3) e também no ⋮; duplicar, no
 * Copiar e colar.
 */
@Composable
internal fun LayerTopBar(store: EditorStore, ui: EditorUi, layerId: Long) {
    val header by remember(layerId) {
        derivedStateOf {
            val rows = store.layers
            rows.firstOrNull { it.id == layerId }?.let { LayerHeader(it.id, it.name, it.kind, it.locked, parentOf(rows, it)) }
        }
    }
    var linking by remember(layerId) { mutableStateOf(false) }
    var menu by remember(layerId) { mutableStateOf(false) }
    // A barra entra descendo da borda de cima em 100 ms, desacelerando (a
    // `action_bar_show` da referência); trocar de camada não repete.
    val enter = remember { Animatable(-1f) }
    LaunchedEffect(Unit) { enter.animateTo(0f, tween(100, easing = LinearOutSlowInEasing)) }
    val h = header
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .clipToBounds()
            .graphicsLayer { translationY = enter.value * size.height }
            .background(OmegaChrome.LayerBar),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ChromeButton(
            CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_tirar_selecao),
            onClick = { shellBack(store, ui) }, modifier = Modifier.testTag("layer.back"), size = 20.dp, height = ShellDims.TopBar,
        )
        if (h == null) return@Row
        Box(Modifier.weight(1f).testTag("layer.name")) {
            InlineName(
                key = h.id,
                name = h.name,
                placeholder = stringResource(R.string.edt_unnamed_layer),
                maxLength = Int.MAX_VALUE,
                enabled = !h.locked,
                onRename = { store.renameLayer(h.id, it) },
            )
        }
        // 0.3.3 recursos: 3D e desfoque de movimento voltam à barra da camada
        // (como na 0.3.2: aceso em destaque quando ligado, apagado se travada).
        // Continuam também no ⋮.
        if (h.kind != LayerType.Audio.kind) {
            val row = store.layers.firstOrNull { it.id == h.id }
            val intrinsic = h.kind in 8..10
            val threeD = row?.isThreeD == true || intrinsic
            val motionBlur = store.detail?.takeIf { it.id == h.id }?.motionBlur == true
            val onLabel = stringResource(R.string.common_on)
            val offLabel = stringResource(R.string.common_off)
            ChromeButton(
                CupertinoGlyph.Cube, stringResource(R.string.sh_add_tab_3d),
                onClick = if (!h.locked && !intrinsic) ({ store.changeLayer3D(!threeD) }) else null,
                modifier = Modifier.testTag("trial.layer.3d").semantics { stateDescription = if (threeD) onLabel else offLabel },
                size = 20.dp, height = ShellDims.TopBar,
                tint = if (threeD) AureaColors.Accent else if (h.locked) AureaColors.Disabled else AureaColors.Text,
            )
            ChromeButton(
                CupertinoGlyph.MotionBlur, stringResource(R.string.editor_desfoque_movimento),
                onClick = if (!h.locked) ({ store.setLayerMotionBlur(h.id, !motionBlur) }) else null,
                modifier = Modifier.testTag("trial.layer.motionBlur").semantics { stateDescription = if (motionBlur) onLabel else offLabel },
                size = 20.dp, height = ShellDims.TopBar,
                tint = if (motionBlur) AureaColors.Accent else if (h.locked) AureaColors.Disabled else AureaColors.Text,
            )
        }
        // Vínculo de pai: nunca em camada de áudio (a referência o esconde).
        if (h.kind != LayerType.Audio.kind) {
            Box {
                ChromeButton(
                    if (h.parent != 0L) CupertinoGlyph.LinkCircleFill else CupertinoGlyph.Link,
                    if (h.parent != 0L) stringResource(R.string.editor_vinculada_outra_camada_trocar) else stringResource(R.string.editor_vincular_outra_camada),
                    onClick = {
                        if (store.playing) store.pause()
                        linking = true
                    },
                    modifier = Modifier.testTag("layer.parent"),
                    size = 20.dp,
                    height = ShellDims.TopBar,
                    tint = if (h.parent != 0L) AureaColors.Accent else AureaColors.Text,
                )
                if (linking) LinkMenu(store, listOf(h.id)) { linking = false }
            }
        }
        ChromeButton(
            CupertinoGlyph.Trash, stringResource(R.string.editor_excluir_camada),
            onClick = if (!h.locked) ({ LayerOps.delete(store, listOf(h.id)) }) else null,
            modifier = Modifier.testTag("layer.delete"), size = 19.dp, height = ShellDims.TopBar,
        )
        Box {
            ChromeButton(
                ShellGlyph.EllipsisVertical, stringResource(R.string.editor_mais_acoes_camada),
                onClick = {
                    if (store.playing) store.pause()
                    menu = true
                },
                modifier = Modifier.testTag("layer.menu"), size = 20.dp, height = ShellDims.TopBar,
            )
            LayerMenu(store, ui, h.id, expanded = menu) { menu = false }
        }
    }
}

// =============================================================================
// Popup de pai (`layer_parent_menu_popup`), P1-1
// =============================================================================

/**
 * "Nenhum" no topo (destacado quando sem pai), um fio e todas as camadas que
 * não são áudio, da frente para o fundo: faixa da etiqueta, miniatura, nome e,
 * só no pai atual, o ícone de vínculo com fundo de destaque. A própria camada
 * fica sublinhada; ela e as descendentes (fariam ciclo) aparecem apagadas e sem
 * toque. A lista rola até a camada atual. "Novo nulo" (extra do AUREA) no fim.
 * Vale para uma camada ou para a seleção inteira ([ids]).
 */
@Composable
internal fun LinkMenu(store: EditorStore, ids: List<Long>, onDismiss: () -> Unit) {
    val density = LocalDensity.current
    val provider = remember(density) {
        with(density) { AnchorPopupPosition(AnchoredMenuDefaults.ScreenMargin.roundToPx(), AnchoredMenuDefaults.AnchorGap.roundToPx()) }
    }
    val colors = MenuColors.of(AureaSurfaceTone.Light)
    val rows = store.layers
    val candidates = remember(rows, ids) { store.parentCandidatesForAll(ids).map { it.id }.toSet() }
    val visual = remember(rows) { rows.filter { it.kind != LayerType.Audio.kind } }
    // Pai em comum (0 = todas soltas; −1 = pais diferentes: nada marcado).
    val current = remember(rows, ids) {
        rows.filter { it.id in ids }.map { parentOf(rows, it) }.distinct().singleOrNull() ?: -1L
    }
    fun pick(parent: Long) {
        onDismiss()
        if (parent == current) return
        if (ids.size == 1) store.setParent(ids[0], parent) else store.setParentMany(ids, parent)
    }
    val scroll = rememberScrollState()
    val rowPx = with(density) { AnchoredMenuDefaults.RowHeight.roundToPx() }
    LaunchedEffect(Unit) {
        // Rola até a camada atual (as linhas têm 40 dp; o "Nenhum" e o fio vêm antes).
        val index = visual.indexOfFirst { it.id in ids }
        if (index > 2) scroll.scrollTo(rowPx * (index - 1))
    }
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        Column(
            Modifier
                .width(AnchoredMenuDefaults.Width)
                .heightIn(max = AnchoredMenuDefaults.MaxHeight)
                .clip(RoundedCornerShape(AnchoredMenuDefaults.Radius))
                .background(colors.background)
                .testTag("parent.menu")
                .verticalScroll(scroll)
                .padding(vertical = AnchoredMenuDefaults.VerticalPadding),
        ) {
            ParentRow(
                colors = colors,
                label = stringResource(R.string.editor_nenhum),
                strip = Color.Transparent,
                thumb = { CupertinoIcon(ShellGlyph.Nosign, 18.dp, colors.text) },
                current = current == 0L,
                tag = "parent.none",
            ) { pick(0L) }
            Box(Modifier.fillMaxWidth().height(1.dp).background(colors.divider))
            visual.forEach { row ->
                val type = LayerType.of(row.kind)
                val label = row.label
                ParentRow(
                    colors = colors,
                    label = row.name.ifBlank { stringResource(type.labelRes) },
                    blank = row.name.isBlank(),
                    strip = if (label in 1..ShellColors.LabelPalette.size) ShellColors.LabelPalette[label - 1] else Color.Transparent,
                    thumb = { LayerThumb(store, row, 32.dp) },
                    current = current == row.id,
                    self = row.id in ids,
                    enabled = row.id in candidates,
                    tag = "parent.row.${row.id}",
                ) { pick(row.id) }
            }
            Box(Modifier.padding(vertical = 4.dp).fillMaxWidth().height(1.dp).background(colors.divider))
            // "Novo nulo": cria o nulo no centro das escolhidas e vincula todas a ele.
            ParentRow(
                colors = colors,
                label = stringResource(R.string.editor_vincular_novo_nulo),
                strip = Color.Transparent,
                thumb = {
                    Box(
                        Modifier.size(28.dp).clip(RoundedCornerShape(5.dp)).background(LayerType.Null.color),
                        contentAlignment = Alignment.Center,
                    ) { CupertinoIcon(LayerType.Null.glyph, 16.dp, AureaColors.OnImage) }
                },
                current = false,
                tag = "parent.newNull",
            ) {
                onDismiss()
                store.parentSelectionToNewNull(ids)
            }
        }
    }
}

@Composable
private fun ParentRow(
    colors: MenuColors,
    label: String,
    strip: Color,
    thumb: @Composable () -> Unit,
    current: Boolean,
    tag: String,
    blank: Boolean = false,
    self: Boolean = false,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(AnchoredMenuDefaults.RowHeight)
            .testTag(tag)
            .background(if (current) colors.pressed else Color.Transparent)
            .semantics {
                selected = current
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, shrink = 1f, haptic = true, onClick = onClick)
            .alpha(if (enabled) 1f else AnchoredMenuDefaults.DISABLED_ALPHA),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.width(4.dp).fillMaxHeight().background(strip))
        Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) { thumb() }
        Spacer(Modifier.width(6.dp))
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 14.sp,
                    color = if (blank) colors.muted else colors.text,
                    textDecoration = if (self) TextDecoration.Underline else null,
                ),
            ),
        )
        if (current) {
            Box(Modifier.size(40.dp), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Link, 18.dp, colors.accent)
            }
        } else Spacer(Modifier.width(12.dp))
    }
}

/**
 * Miniatura da camada: vídeo e imagem pedem o quadro ao motor (o mesmo cache
 * da timeline; enquanto não chega, o selo do tipo); o resto é o selo colorido.
 */
@Composable
internal fun LayerThumb(store: EditorStore, row: LayerRow, size: Dp = 44.dp) {
    val type = LayerType.of(row.kind)
    val media = type == LayerType.Video || type == LayerType.Image
    val px = with(LocalDensity.current) { size.roundToPx() }.coerceIn(16, 256)
    val generation = if (media) store.thumbnailGeneration else 0
    val bitmap = remember(row.id, row.startFrame, row.offsetFrames, generation, media, px) {
        if (!media) return@remember null
        val fps = store.project.fps
        val frame = if (type == LayerType.Image) row.startFrame
        else Keyframes.toTimeline(Thumbs.requestLocalFrame(Thumbs.bucketOf(row.offsetFrames.toDouble(), fps), fps), row.startFrame, row.offsetFrames)
        store.thumbnails.get(row.id, frame, px)?.asImageBitmap()
    }
    Box(
        Modifier
            .size(size)
            .clip(RoundedCornerShape(if (size < 40.dp) 5.dp else 6.dp))
            .background(type.color),
        contentAlignment = Alignment.Center,
    ) {
        if (bitmap != null) {
            Image(bitmap, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.matchParentSize())
        } else {
            CupertinoIcon(type.glyph, size * 0.45f, AureaColors.OnImage)
        }
    }
}

/** Abaixo da âncora, alinhado pela borda de início (a regra do menu ancorado). */
internal class AnchorPopupPosition(private val margin: Int, private val gap: Int) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset = AnchoredMenuPosition.compute(
        anchorBounds, windowSize, popupContentSize,
        rtl = layoutDirection == LayoutDirection.Rtl, alignEnd = false, margin = margin, gap = gap,
    )
}

// =============================================================================
// Nada escolhido (`activity_edit_navbar`): sair · título · ⚙ · compartilhar
// =============================================================================

/**
 * Fora de grupo: [sair][título][⚙][compartilhar], sobre o fundo A10. O ⚙ abre
 * os Ajustes do projeto DIRETO (o menu da engrenagem saiu; o que era dele mora
 * na seção "Mais do AUREA" dos Ajustes e no Copiar e colar). Dentro de grupo:
 * a trilha de grupos e o ⚙ do grupo — sair, título e compartilhar somem.
 */
@Composable
internal fun ProjectTopBar(store: EditorStore, ui: EditorUi) {
    val depth by remember { derivedStateOf { store.precompDepth } }
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(OmegaChrome.Bar),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (depth > 0) {
            GroupTrail(store, depth, Modifier.weight(1f))
            ChromeButton(
                CupertinoGlyph.Gear, stringResource(R.string.om_tb_group_settings),
                onClick = { openSheet(store, ui, ShellSheet.ProjectSettings) },
                modifier = Modifier.testTag("editor.topbar.groupSettings"), size = 21.dp, height = ShellDims.TopBar,
            )
        } else {
            ChromeButton(
                ShellGlyph.ExitBox, stringResource(R.string.om_tb_exit),
                onClick = { shellBack(store, ui) },
                modifier = Modifier.testTag("editor.topbar.exit"), size = 21.dp, height = ShellDims.TopBar,
            )
            Box(Modifier.weight(1f).testTag("editor.topbar.title")) {
                val title by remember { derivedStateOf { store.project.title } }
                InlineName(
                    key = 0L,
                    name = title,
                    placeholder = stringResource(R.string.edt_untitled),
                    maxLength = 320,
                    onRename = { store.renameProject(it) },
                )
            }
            ChromeButton(
                CupertinoGlyph.Gear, stringResource(R.string.editor_ajustes_projeto),
                onClick = { openSheet(store, ui, ShellSheet.ProjectSettings) },
                modifier = Modifier.testTag("editor.projectMenu"), size = 21.dp, height = ShellDims.TopBar,
            )
            ChromeButton(
                CupertinoGlyph.SquareArrowUp, stringResource(R.string.om_tb_share),
                onClick = {
                    if (store.playing) store.pause()
                    ui.exporting = true
                },
                modifier = Modifier.testTag("editor.export"), size = 21.dp, height = ShellDims.TopBar,
            )
        }
    }
}

/**
 * A trilha de grupos: [projeto] › [grupo] › … › [atual]. O primeiro item tem o
 * «« e fundo S10; os do meio, pílula S9; o último, só o texto. Tocar no item i
 * sobe n−i−1 níveis. Rola até o fim.
 */
@Composable
private fun GroupTrail(store: EditorStore, depth: Int, modifier: Modifier) {
    val title = store.project.title
    val name = store.compositionName
    val names = remember(depth, name, title) {
        val trail = store.compositionTrail()
        val list = if (trail.size == depth + 1) trail.toMutableList() else MutableList(depth + 1) { "" }.also { it[depth] = name }
        list[0] = title
        list.toList()
    }
    val state = rememberLazyListState()
    LaunchedEffect(names.size) { state.scrollToItem(max(0, names.size - 1)) }
    val untitled = stringResource(R.string.edt_untitled)
    LazyRow(
        modifier.fillMaxHeight(),
        state = state,
        horizontalArrangement = Arrangement.spacedBy(2.5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        itemsIndexed(names) { i, raw ->
            val last = i == names.size - 1
            val label = raw.ifBlank { if (i == 0) untitled else "…" }
            val goTo = stringResource(R.string.om_tb_trail_item, label)
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (i == 0) Spacer(Modifier.width(6.dp)) else CupertinoIcon(CupertinoGlyph.ChevronRight, 12.dp, OmegaChrome.Secondary)
                Row(
                    Modifier
                        .padding(start = if (i == 0) 0.dp else 2.5.dp)
                        .heightIn(min = 30.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .background(
                            when {
                                last -> Color.Transparent
                                i == 0 -> OmegaChrome.TrailRoot
                                else -> OmegaChrome.Column
                            },
                        )
                        .testTag("editor.trail.$i")
                        .semantics { contentDescription = goTo }
                        .tocavel(enabled = !last, shrink = 1f, haptic = true) {
                            repeat(names.size - i - 1) { store.closePrecomp() }
                        }
                        .padding(horizontal = 6.dp, vertical = 3.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    if (i == 0) {
                        CupertinoIcon(ShellGlyph.ChevronLeft2, 13.dp, AureaColors.Text)
                        Spacer(Modifier.width(3.dp))
                    }
                    Text(
                        label,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = TitleStyle.merge(TextStyle(color = AureaColors.Text)),
                        modifier = Modifier.widthIn(max = 100.dp),
                    )
                }
            }
        }
    }
}

/**
 * Nome editável ali mesmo (`SettingEditText` da referência): tocar vira campo,
 * SEM caixa de fundo — só o cursor no destaque e o ✕ no fim, que APAGA o
 * texto (não cancela). Done ou perder o foco confirma; vazio não vale.
 */
@Composable
private fun InlineName(
    key: Long,
    name: String,
    placeholder: String,
    maxLength: Int,
    onRename: (String) -> Unit,
    enabled: Boolean = true,
) {
    var editing by remember(key) { mutableStateOf(false) }
    if (!editing) {
        val empty = name.isBlank()
        Text(
            if (empty) placeholder else name,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = TitleStyle.merge(TextStyle(color = if (empty) OmegaChrome.Secondary else AureaColors.Text)),
            modifier = Modifier
                .fillMaxWidth()
                .tocavel(enabled = enabled, shrink = 1f) { editing = true }
                .padding(vertical = 12.dp, horizontal = 4.dp),
        )
        return
    }
    var value by remember(key) { mutableStateOf(TextFieldValue(name, TextRange(0, name.length))) }
    var hadFocus by remember(key) { mutableStateOf(false) }
    val focus = remember { FocusRequester() }
    val confirm = {
        if (editing) {
            editing = false
            val n = value.text.trim()
            if (n.isNotEmpty() && n != name) onRename(n)
        }
    }
    LaunchedEffect(key) { focus.requestFocus() }
    val clearLabel = stringResource(R.string.om_field_clear)
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        BasicTextField(
            value = value,
            onValueChange = { if (it.text.length <= maxLength) value = it },
            singleLine = true,
            textStyle = TitleStyle.merge(TextStyle(color = AureaColors.Text)),
            cursorBrush = SolidColor(AureaColors.Accent),
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { confirm() }),
            modifier = Modifier
                .weight(1f)
                .padding(horizontal = 4.dp)
                .focusRequester(focus)
                .onFocusChanged {
                    if (it.isFocused) hadFocus = true
                    else if (hadFocus) confirm()
                },
        )
        if (value.text.isNotEmpty()) {
            Box(
                Modifier
                    .size(36.dp, 40.dp)
                    .testTag("field.clear")
                    .semantics { contentDescription = clearLabel }
                    .tocavel(shrink = 1f) { value = TextFieldValue("") },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, OmegaChrome.Secondary) }
        }
    }
}

// =============================================================================
// Seleção múltipla (`activity_edit_selectbar`): destaque, a partir de 1 camada
// =============================================================================

/**
 * ✕ · "N camadas selecionadas" · Agrupar · Agrupar e mascarar · Agrupar e
 * excluir · 🗑 · ›. O `›` troca a fileira pela de ALINHAMENTO NA TELA: ‹ + 6
 * alinhamentos + 2 distribuições, todos do mesmo tamanho. Mascarar/excluir
 * seguem a regra do motor (≥ 2 camadas visuais destravadas); distribuir pede
 * ≥ 3. Fora da regra, o botão fica apagado e o toque explica o porquê. As
 * outras ações (escolher uma, desagrupar, duplicar, vincular) moram no Copiar
 * e colar do transporte.
 */
@Composable
internal fun BatchTopBar(store: EditorStore, ui: EditorUi) {
    val count by remember { derivedStateOf { store.selection.size } }
    val canMask by remember { derivedStateOf { store.selection.size >= 2 && store.canGroupAndMask(store.selection.toList()) } }
    var aligning by remember { mutableStateOf(false) }
    val ink = AureaColors.OnAccent
    val dim = ink.copy(alpha = 0.38f)
    val needTwo = stringResource(R.string.batch_need_two_visual)
    val needThree = stringResource(R.string.om_sel_need_three)
    fun finish() = store.changeLayerSelectMode(false)
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.Accent),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ChromeButton(CupertinoGlyph.Xmark, stringResource(R.string.editor_cancelar_selecao), onClick = {
            store.clearSelection()
            finish()
        }, Modifier.testTag("batch.cancel"), size = 18.dp, height = ShellDims.TopBar, tint = ink)
        AnimatedContent(
            targetState = aligning,
            modifier = Modifier.weight(1f),
            transitionSpec = {
                // Entrar no alinhamento: de +50 % da largura (acelerando); voltar: de −50 % (desacelerando).
                if (targetState) {
                    (slideInHorizontally(tween(200, easing = FastOutLinearInEasing)) { it / 2 } + fadeIn(tween(200))) togetherWith fadeOut(tween(120))
                } else {
                    (slideInHorizontally(tween(200, easing = LinearOutSlowInEasing)) { -it / 2 } + fadeIn(tween(200))) togetherWith fadeOut(tween(120))
                }
            },
            label = "batchRow",
        ) { align ->
            if (!align) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        if (count >= 1) pluralStringResource(R.plurals.om_layers_selected, count, count)
                        else stringResource(R.string.editor_selecione_menos_duas_camadas),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = ink)),
                        modifier = Modifier.weight(1f),
                    )
                    ChromeButton(ShellGlyph.FolderBadgePlus, stringResource(R.string.editor_agrupar), onClick = {
                        store.precompose()
                        finish()
                    }, Modifier.testTag("batch.group"), size = 19.dp, height = ShellDims.TopBar, tint = ink)
                    ChromeButton(CupertinoGlyph.CircleLefthalfFill, stringResource(R.string.batch_group_mask_a11y), onClick = {
                        if (canMask) store.groupAndMask(false) else store.showToast(needTwo)
                    }, Modifier.testTag("batch.groupMask"), size = 19.dp, height = ShellDims.TopBar, tint = if (canMask) ink else dim)
                    ChromeButton(CupertinoGlyph.MinusCircle, stringResource(R.string.batch_group_exclude_a11y), onClick = {
                        if (canMask) store.groupAndMask(true) else store.showToast(needTwo)
                    }, Modifier.testTag("batch.groupExclude"), size = 19.dp, height = ShellDims.TopBar, tint = if (canMask) ink else dim)
                    ChromeButton(CupertinoGlyph.Trash, stringResource(R.string.editor_excluir_selecao), onClick = {
                        LayerOps.delete(store, store.selection)
                        finish()
                    }, Modifier.testTag("batch.delete"), size = 19.dp, height = ShellDims.TopBar, tint = ink)
                    ChromeButton(CupertinoGlyph.ChevronRight, stringResource(R.string.om_sel_align), onClick = { aligning = true },
                        Modifier.testTag("batch.align"), size = 17.dp, height = ShellDims.TopBar, tint = ink)
                }
            } else {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    val three = count >= 3
                    AlignCell(CupertinoGlyph.ChevronLeft, stringResource(R.string.batch_group_actions), "batch.align.back", ink, 17.dp) { aligning = false }
                    AlignCell(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_alinhar_esquerda_tela), "batch.align.left", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.Left)
                    }
                    AlignCell(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.editor_centralizar_horizontal), "batch.align.hcenter", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.CenterH)
                    }
                    AlignCell(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_alinhar_direita_tela), "batch.align.right", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.Right)
                    }
                    AlignCell(CupertinoGlyph.ArrowUpToLine, stringResource(R.string.editor_alinhar_topo_tela), "batch.align.top", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.Top)
                    }
                    AlignCell(CupertinoGlyph.ArrowUpArrowDown, stringResource(R.string.editor_centralizar_vertical), "batch.align.vcenter", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.CenterV)
                    }
                    AlignCell(CupertinoGlyph.ArrowDownToLine, stringResource(R.string.editor_alinhar_base_tela), "batch.align.bottom", ink) {
                        LayerOps.align(store, store.selection, LayerOps.Edge.Bottom)
                    }
                    AlignCell(CupertinoGlyph.ArrowUpDownSquare, stringResource(R.string.editor_distribuir_vertical_vaos_iguais), "batch.align.distV",
                        if (three) ink else dim) {
                        if (three) LayerOps.distribute(store, store.selection, horizontal = false) else store.showToast(needThree)
                    }
                    AlignCell(CupertinoGlyph.ArrowLeftRightSquare, stringResource(R.string.editor_distribuir_horizontal_vaos_iguais), "batch.align.distH",
                        if (three) ink else dim) {
                        if (three) LayerOps.distribute(store, store.selection, horizontal = true) else store.showToast(needThree)
                    }
                }
            }
        }
    }
}

/** Uma célula da fileira de alinhamento (peso 1: a fileira ocupa a largura depois do ✕). */
@Composable
private fun RowScope.AlignCell(glyph: Char, description: String, tag: String, tint: Color, size: Dp = 18.dp, onClick: () -> Unit) {
    ChromeButton(glyph, description, onClick = onClick, modifier = Modifier.weight(1f).testTag(tag), size = size, height = ShellDims.TopBar, tint = tint)
}

// =============================================================================
// Barra inferior flutuante da seleção (`activity_edit_select_bottomvar`)
// =============================================================================

/**
 * 42 dp, margens de 6, fundo S9 com raio 5, presa embaixo por cima da timeline.
 * À esquerda (120 dp) a variante pelo cabeçote (regra do motor,
 * `query_multi_time_actions`): cruza as escolhidas = aparar início · dividir ·
 * aparar fim; todas depois = estender/levar o início; todas antes =
 * estender/levar o fim. À direita: alinhar inícios · distribuir no tempo ·
 * alinhar fins. Cada ação é um passo de desfazer.
 */
@Composable
internal fun MultiSelectBottomBar(store: EditorStore, modifier: Modifier) {
    val playhead = store.playhead
    val selection = store.selection
    val layers = store.layers
    val variant = remember(playhead, selection, layers) { store.multiTimeVariant() }
    val intoLayers = stringResource(R.string.editor_leve_cabecote_dentro_camadas)
    val lockedText = stringResource(R.string.sh_layers_locked_unlock_to_edit)
    Row(
        modifier
            .padding(start = 6.dp, end = 6.dp, bottom = 6.dp)
            .fillMaxWidth()
            .height(42.dp)
            .clip(RoundedCornerShape(5.dp))
            .background(OmegaChrome.Column)
            .testTag("batch.bottom"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(Modifier.width(120.dp).fillMaxHeight(), verticalAlignment = Alignment.CenterVertically) {
            when (variant) {
                1 -> {
                    BottomCell(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.om_sel_extend_start), "batch.bottom.extendStart") {
                        store.extendSelectionToPlayhead(start = true)
                    }
                    BottomCell(ShellGlyph.ArrowLeftSquare, stringResource(R.string.om_sel_move_start), "batch.bottom.moveStart") {
                        store.arrangeLayerTimes(5)
                    }
                }
                2 -> {
                    BottomCell(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.om_sel_extend_end), "batch.bottom.extendEnd") {
                        store.extendSelectionToPlayhead(start = false)
                    }
                    BottomCell(ShellGlyph.ArrowRightSquare, stringResource(R.string.om_sel_move_end), "batch.bottom.moveEnd") {
                        store.arrangeLayerTimes(6)
                    }
                }
                else -> {
                    BottomCell(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_aparar_inicio_cabecote), "batch.bottom.trimStart") {
                        batchTrim(store, start = true, intoLayers, lockedText)
                    }
                    BottomCell(CupertinoGlyph.Scissors, stringResource(R.string.editor_dividir_cabecote), "batch.bottom.split") {
                        val t = store.playhead
                        val targets = store.layers.filter { it.id in store.selection && !it.locked && t > it.startFrame && t < it.endFrame }.map { it.id }
                        if (targets.isEmpty()) store.showToast(intoLayers)
                        else {
                            if (store.playing) store.pause()
                            store.splitAtPlayhead(targets)
                        }
                    }
                    BottomCell(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_aparar_fim_cabecote), "batch.bottom.trimEnd") {
                        batchTrim(store, start = false, intoLayers, lockedText)
                    }
                }
            }
        }
        Spacer(Modifier.weight(1f))
        BottomVectorCell(Icons.AutoMirrored.Rounded.FormatAlignLeft, stringResource(R.string.editor_alinhar_inicios), "batch.bottom.alignStarts") {
            store.arrangeLayerTimes(0)
        }
        BottomVectorCell(Icons.Rounded.Stairs, stringResource(R.string.om_sel_distribute_time), "batch.bottom.distributeTime") {
            store.arrangeLayerTimes(3)
        }
        BottomVectorCell(Icons.AutoMirrored.Rounded.FormatAlignRight, stringResource(R.string.editor_alinhar_fins), "batch.bottom.alignEnds") {
            store.arrangeLayerTimes(2)
        }
    }
}

@Composable
private fun RowScope.BottomCell(glyph: Char, description: String, tag: String, onClick: () -> Unit) {
    ChromeButton(glyph, description, onClick = onClick, modifier = Modifier.weight(1f).testTag(tag), size = 19.dp, height = 42.dp)
}

@Composable
private fun BottomVectorCell(icon: ImageVector, description: String, tag: String, onClick: () -> Unit) {
    ChromeVectorButton(icon, description, onClick = onClick, modifier = Modifier.testTag(tag), size = 20.dp, width = 40.dp, height = 42.dp)
}

/** Aparar as escolhidas que cobrem o cabeçote (as travadas ficam), num passo só. */
private fun batchTrim(store: EditorStore, start: Boolean, intoLayers: String, lockedText: String) {
    val t = store.playhead
    val rows = store.layers.filter { it.id in store.selection }
    val targets = rows.filter { !it.locked && t > it.startFrame && t < it.endFrame }
    if (targets.isEmpty()) {
        store.showToast(if (rows.isNotEmpty() && rows.all { it.locked }) lockedText else intoLayers)
        return
    }
    if (store.playing) store.pause()
    store.beginGesture(if (start) "aparar início" else "aparar fim")
    targets.forEach { if (start) store.trimStart(it.id, t) else store.trimEnd(it.id, t) }
    store.endGesture()
}
