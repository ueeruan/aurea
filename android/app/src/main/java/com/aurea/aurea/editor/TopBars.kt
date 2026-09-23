package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.filled.MoreHoriz
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.editor.timeline.Keyframes
import com.aurea.aurea.editor.timeline.Thumbs
import com.aurea.aurea.engine.LayerRow
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Logout
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.foundation.layout.Column
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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

/** O que a barra da camada mostra (data class: recompõe só quando muda). */
private data class LayerHeader(val id: Long, val name: String, val kind: Int, val locked: Boolean, val parent: Long)

private val TitleStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600))

/** Pai da linha na lista de camadas (0 = solta): `parentIndex` é índice na mesma lista. */
private fun parentOf(rows: List<LayerRow>, row: LayerRow): Long = rows.getOrNull(row.parentIndex)?.id ?: 0L

// =============================================================================
// Camada escolhida — `BarraDaCamada` (ref15)
// =============================================================================

/**
 * ‹ · nome editável ali mesmo · VINCULAR · lixeira · ⋯. O vincular mora aqui
 * (pedido do dono, ref20): toque abre a lista "Nenhum" + camadas com
 * miniatura; aceso quando a camada já segue outra. Duplicar fica no
 * transporte, um lugar só.
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
    val h = header
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.EditorTopBar)
            .padding(end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ChromeButton(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_tirar_selecao), onClick = { shellBack(store, ui) }, width = 44.dp)
        if (h == null) return@Row
        Box(Modifier.weight(1f)) {
            InlineName(
                key = h.id,
                name = h.name,
                placeholder = "(Camada sem nome)",
                maxLength = Int.MAX_VALUE,
                onRename = { store.renameLayer(h.id, it) },
            )
        }
        Box {
            ChromeButton(
                if (h.parent != 0L) CupertinoGlyph.LinkCircleFill else CupertinoGlyph.Link,
                if (h.parent != 0L) stringResource(R.string.editor_vinculada_outra_camada_trocar) else stringResource(R.string.editor_vincular_outra_camada),
                onClick = {
                    if (store.playing) store.pause()
                    linking = true
                },
                size = 20.dp,
                width = 44.dp,
                tint = if (h.parent != 0L) AureaColors.Accent else AureaColors.Text,
            )
            if (linking) LinkMenu(store, listOf(h.id)) { linking = false }
        }
        ChromeButton(CupertinoGlyph.Trash, stringResource(R.string.editor_excluir_camada), onClick = { LayerOps.delete(store, listOf(h.id)) }, size = 19.dp, width = 44.dp)
        ChromeVectorButton(Icons.Filled.MoreHoriz, stringResource(R.string.editor_mais_acoes_camada), onClick = { openSheet(store, ui, ShellSheet.LayerMenu) }, size = 22.dp, width = 44.dp)
    }
}

// =============================================================================
// VINCULAR — a lista que desce do ícone (ref20)
// =============================================================================

/**
 * "Nenhum" + as camadas que podem ser pai de TODAS as `ids` (nenhuma delas
 * nem descendente delas: seria um ciclo), com miniatura e nome, da frente
 * para o fundo como na timeline. Tocar vincula (um passo de desfazer) e fecha.
 * O motor compensa: a camada fica onde está na tela e passa a seguir o pai.
 */
@Composable
internal fun LinkMenu(store: EditorStore, ids: List<Long>, onDismiss: () -> Unit) {
    val density = LocalDensity.current
    val provider = remember(density) { with(density) { LinkMenuPosition(8.dp.roundToPx()) } }
    val rows = store.layers
    val candidates = remember(rows, ids) { store.parentCandidatesForAll(ids) }
    // Pai em comum (0 = todas soltas; −1 = pais diferentes: nada marcado).
    val current = remember(rows, ids) {
        val parents = rows.filter { it.id in ids }.map { parentOf(rows, it) }.distinct()
        parents.singleOrNull() ?: -1L
    }
    fun pick(parent: Long) {
        onDismiss()
        if (parent == current) return
        if (ids.size == 1) store.setParent(ids[0], parent) else store.setParentMany(ids, parent)
    }
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        Column(
            Modifier
                .width(300.dp)
                .heightIn(max = 420.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(AureaColors.Pill)
                .border(1.dp, AureaColors.Border, RoundedCornerShape(12.dp))
                .verticalScroll(rememberScrollState()),
        ) {
            LinkRow(
                thumb = {
                    Box(Modifier.size(44.dp), contentAlignment = Alignment.Center) {
                        CupertinoIcon(ShellGlyph.Nosign, 22.dp, AureaColors.Text)
                    }
                },
                label = stringResource(R.string.editor_nenhum),
                bold = true,
                on = current == 0L,
                background = AureaColors.Chip,
            ) { pick(0L) }
            candidates.forEach { row ->
                val type = LayerType.of(row.kind)
                LinkRow(
                    thumb = { LayerThumb(store, row) },
                    label = row.name.ifBlank { type.label },
                    on = current == row.id,
                ) { pick(row.id) }
            }
            if (candidates.isEmpty()) {
                Text(
                    stringResource(R.string.editor_nenhuma_outra_camada_seguir_crie_nulo),
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 17.sp, color = AureaColors.Muted)),
                )
            }
        }
    }
}

@Composable
private fun LinkRow(
    thumb: @Composable () -> Unit,
    label: String,
    on: Boolean,
    bold: Boolean = false,
    background: Color = Color.Transparent,
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(56.dp)
            .background(if (on) AureaColors.AccentDim else background)
            .tocavel(shrink = 1f, haptic = true, onClick = onClick)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        thumb()
        Spacer(Modifier.width(14.dp))
        Text(
            label,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 15.sp,
                    fontWeight = if (bold) FontWeight.W700 else FontWeight.W500,
                    color = if (on) AureaColors.Accent else AureaColors.Text,
                ),
            ),
        )
        if (on) CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, AureaColors.Accent)
    }
}

/**
 * Miniatura da camada: vídeo e imagem pedem o quadro ao motor (o mesmo cache
 * da timeline; enquanto não chega, o selo do tipo); o resto é o selo colorido.
 */
@Composable
private fun LayerThumb(store: EditorStore, row: LayerRow) {
    val type = LayerType.of(row.kind)
    val media = type == LayerType.Video || type == LayerType.Image
    val px = with(LocalDensity.current) { 44.dp.roundToPx() }.coerceIn(16, 256)
    val generation = if (media) store.thumbnailGeneration else 0
    val bitmap = remember(row.id, row.startFrame, row.offsetFrames, generation, media) {
        if (!media) return@remember null
        val fps = store.project.fps
        val frame = if (type == LayerType.Image) row.startFrame
        else Keyframes.toTimeline(Thumbs.requestLocalFrame(Thumbs.bucketOf(row.offsetFrames.toDouble(), fps), fps), row.startFrame, row.offsetFrames)
        store.thumbnails.get(row.id, frame, px)?.asImageBitmap()
    }
    Box(
        Modifier
            .size(44.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(type.color),
        contentAlignment = Alignment.Center,
    ) {
        if (bitmap != null) {
            Image(bitmap, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.matchParentSize())
        } else {
            CupertinoIcon(type.glyph, 20.dp, Color.White)
        }
    }
}

/** Abaixo do ícone, alinhada pela direita dele, presa a `margin` das bordas. */
private class LinkMenuPosition(private val margin: Int) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        val x = (anchorBounds.right - popupContentSize.width)
            .coerceIn(margin, (windowSize.width - popupContentSize.width - margin).coerceAtLeast(margin))
        val y = anchorBounds.bottom.coerceAtMost((windowSize.height - popupContentSize.height - margin).coerceAtLeast(margin))
        return IntOffset(x, y)
    }
}

// =============================================================================
// Nada escolhido — `BarraDoProjeto`
// =============================================================================

/**
 * sair · título editável · tempo "m:ss.cc" (toque: ir para o tempo) · ⋮ da
 * linha do tempo · ⚙ projeto · exportar.
 */
@Composable
internal fun ProjectTopBar(store: EditorStore, ui: EditorUi) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.EditorTopBar)
            .padding(end = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val nested by remember { derivedStateOf { store.precompDepth > 0 } }
        if (nested) {
            // Dentro de um grupo: ‹ volta para a composição de cima.
            ChromeButton(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_composicao_principal), onClick = { store.closePrecomp() }, width = 44.dp)
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.editor_editando_grupo), style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Accent)))
                Text(store.compositionName, maxLines = 1, style = TitleStyle)
            }
        } else {
            // A porta com a seta (Icons.logout espelhado): sair do projeto.
            ChromeVectorButton(
                Icons.AutoMirrored.Filled.Logout,
                stringResource(R.string.editor_projetos),
                onClick = { shellBack(store, ui) },
                size = 20.dp,
                width = 44.dp,
                mirror = true,
            )
            Box(Modifier.weight(1f)) {
                val title by remember { derivedStateOf { store.project.title } }
                InlineName(
                    key = 0L,
                    name = title,
                    placeholder = "(Sem título)",
                    maxLength = 320,
                    onRename = { store.renameProject(it) },
                )
            }
        }
        ProjectClock(store) { openSheet(store, ui, ShellSheet.GoToTime) }
        ChromeVectorButton(Icons.Filled.MoreVert, stringResource(R.string.editor_mais_linha_tempo), onClick = { openSheet(store, ui, ShellSheet.TimelineMenu) })
        ChromeButton(CupertinoGlyph.GearAltFill, stringResource(R.string.editor_projeto_cbe9), onClick = { openSheet(store, ui, ShellSheet.ProjectSettings) }, size = 19.dp)
        // Exportar em destaque: a A.01 pintava em `acao` (#245D8C), 2,6:1
        // sobre o cromo (bug 27).
        ChromeButton(CupertinoGlyph.SquareArrowUp, stringResource(R.string.editor_exportar), onClick = {
            if (store.playing) store.pause()
            ui.exporting = true
        }, tint = AureaColors.Accent)
    }
}

/** O relógio da barra do projeto: única peça do topo que recompõe com o tempo. */
@Composable
private fun ProjectClock(store: EditorStore, onClick: () -> Unit) {
    Box(
        Modifier
            .semantics { contentDescription = "Ir para o tempo" }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 6.dp, vertical = 12.dp),
    ) {
        Text(
            ShellTime.short(store.playhead, store.project.fps),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = ShellColors.White40)).merge(AureaType.Tabular),
        )
    }
}

/**
 * Nome editável ali mesmo (`_NomeDaCamadaEditavel` / `_TituloEditavel`):
 * tocar vira campo; confirmar no Enter ou ao perder o foco; vazio não vale.
 */
@Composable
private fun InlineName(
    key: Long,
    name: String,
    placeholder: String,
    maxLength: Int,
    onRename: (String) -> Unit,
) {
    var editing by remember(key) { mutableStateOf(false) }
    if (!editing) {
        val empty = name.isBlank()
        Text(
            if (empty) placeholder else name,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = TitleStyle.merge(TextStyle(color = if (empty) ShellColors.White40 else AureaColors.Text)),
            modifier = Modifier
                .fillMaxWidth()
                .tocavel(shrink = 1f) { editing = true }
                .padding(vertical = 12.dp),
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
    BasicTextField(
        value = value,
        onValueChange = { if (it.text.length <= maxLength) value = it },
        singleLine = true,
        textStyle = TitleStyle,
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { confirm() }),
        modifier = Modifier
            .fillMaxWidth()
            .padding(end = 6.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(AureaColors.Chip)
            .padding(horizontal = 8.dp, vertical = 6.dp)
            .focusRequester(focus)
            .onFocusChanged {
                if (it.isFocused) hadFocus = true
                else if (hadFocus) confirm()
            },
    )
}

// =============================================================================
// Lote — "N camadas selecionadas" (ref19): faixa em destaque
// =============================================================================

/**
 * ✕ · "N camadas selecionadas" · vincular · agrupar · desagrupar (só com grupo
 * na seleção) · lixeira · play. Dividir/aparar e alinhar ficam na barra de
 * baixo ([MultiSelectionPanel]).
 */
@Composable
internal fun BatchTopBar(store: EditorStore) {
    val count by remember { derivedStateOf { store.selection.size } }
    val groups by remember {
        derivedStateOf { store.layers.filter { it.id in store.selection && it.kind == LayerType.Group.kind }.map { it.id } }
    }
    var linking by remember { mutableStateOf(false) }
    val ink = AureaColors.OnAccent
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.Accent)
            .padding(end = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ChromeButton(CupertinoGlyph.Xmark, stringResource(R.string.editor_cancelar_selecao), onClick = { store.clearSelection() }, size = 18.dp, width = 44.dp, tint = ink)
        Text(
            if (count >= 2) "$count camadas selecionadas" else stringResource(R.string.editor_selecione_menos_duas_camadas),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = ink)),
            modifier = Modifier.weight(1f),
        )
        Box {
            ChromeButton(CupertinoGlyph.Link, stringResource(R.string.editor_vincular_escolhidas_camada), onClick = {
                if (store.playing) store.pause()
                linking = true
            }, size = 19.dp, tint = ink)
            if (linking) LinkMenu(store, store.selection.toList()) { linking = false }
        }
        ChromeButton(ShellGlyph.FolderBadgePlus, stringResource(R.string.editor_agrupar), onClick = { store.precompose() }, size = 19.dp, tint = ink)
        if (groups.isNotEmpty()) {
            ChromeButton(ShellGlyph.SquareSplit2x2, stringResource(R.string.editor_desagrupar), onClick = {
                val ids = groups
                ids.forEach { store.ungroupPrecomp(it) }
            }, size = 19.dp, tint = ink)
        }
        ChromeButton(CupertinoGlyph.Trash, stringResource(R.string.editor_excluir_selecao), onClick = { LayerOps.delete(store, store.selection) }, size = 19.dp, tint = ink)
        ChromeButton(
            if (store.playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill,
            if (store.playing) stringResource(R.string.editor_pausar) else stringResource(R.string.editor_reproduzir),
            onClick = { store.togglePlayback() },
            size = 20.dp,
            tint = ink,
        )
    }
}
