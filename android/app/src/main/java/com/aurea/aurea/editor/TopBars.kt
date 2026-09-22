package com.aurea.aurea.editor

import androidx.compose.foundation.background
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
private data class LayerHeader(val id: Long, val name: String, val kind: Int, val locked: Boolean)

private val TitleStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600))

// =============================================================================
// Camada escolhida — `BarraDaCamada`
// =============================================================================

/**
 * ‹ · selo do tipo · nome editável ali mesmo · [parentesco] · duplicar ·
 * lixeira · ⋮. Centros medidos no print: duplicar 307,4 · lixeira 347,4 ·
 * ⋮ 387,4 dp (alvos de 40 com 4 de respiro à direita).
 */
@Composable
internal fun LayerTopBar(store: EditorStore, ui: EditorUi, layerId: Long) {
    val header by remember(layerId) {
        derivedStateOf {
            store.layers.firstOrNull { it.id == layerId }?.let { LayerHeader(it.id, it.name, it.kind, it.locked) }
        }
    }
    val hasParent by remember { derivedStateOf { (store.detail?.parentId ?: 0L) != 0L } }
    val h = header
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.EditorTopBar)
            .padding(end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ChromeButton(CupertinoGlyph.ChevronLeft, "Voltar (tirar a seleção)", onClick = { shellBack(store, ui) }, width = 44.dp)
        if (h == null) return@Row
        val type = LayerType.of(h.kind)
        Box(
            Modifier
                .padding(end = 8.dp)
                .size(24.dp)
                .clip(RoundedCornerShape(7.dp))
                .background(type.color),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(type.glyph, 14.dp, Color.White)
        }
        Box(Modifier.weight(1f)) {
            InlineName(
                key = h.id,
                name = h.name,
                placeholder = "(Camada sem nome)",
                maxLength = Int.MAX_VALUE,
                onRename = { store.renameLayer(h.id, it) },
            )
        }
        // Parentesco só acende quando ESTÁ ligado: é informação, não ação de
        // todo dia.
        if (hasParent) {
            ChromeButton(
                CupertinoGlyph.LinkCircleFill,
                "Segue outra camada",
                onClick = { store.comingSoon("Parentesco") },
                size = 20.dp,
                tint = AureaColors.Accent,
            )
        }
        ChromeButton(CupertinoGlyph.PlusSquareOnSquare, "Duplicar camada", onClick = { store.duplicateLayers(listOf(h.id)) }, size = 19.dp)
        ChromeButton(CupertinoGlyph.Trash, "Excluir camada", onClick = { LayerOps.delete(store, listOf(h.id)) }, size = 19.dp)
        ChromeVectorButton(Icons.Filled.MoreVert, "Tudo o que se faz com a camada", onClick = { openSheet(store, ui, ShellSheet.LayerMenu) })
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
        // A porta com a seta (Icons.logout espelhado): sair do projeto.
        ChromeVectorButton(
            Icons.AutoMirrored.Filled.Logout,
            "Projetos",
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
        ProjectClock(store) { openSheet(store, ui, ShellSheet.GoToTime) }
        ChromeVectorButton(Icons.Filled.MoreVert, "Mais da linha do tempo", onClick = { openSheet(store, ui, ShellSheet.TimelineMenu) })
        ChromeButton(CupertinoGlyph.GearAltFill, "Projeto", onClick = { openSheet(store, ui, ShellSheet.ProjectSettings) }, size = 19.dp)
        // Exportar em destaque: a A.01 pintava em `acao` (#245D8C), 2,6:1
        // sobre o cromo (bug 27).
        ChromeButton(CupertinoGlyph.SquareArrowUp, "Exportar", onClick = { store.comingSoon("Exportar") }, tint = AureaColors.Accent)
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
// Lote — `BarraDoLote` (fundo `selecao` #123A63, duas páginas)
// =============================================================================

@Composable
internal fun BatchTopBar(store: EditorStore) {
    var layoutPage by remember { mutableStateOf(false) }
    val count by remember { derivedStateOf { store.selection.size } }
    BoxWithConstraints(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.Selection),
    ) {
        // Os botões de 30 da A.01 (bug 28) crescem até 44 quando a largura deixa.
        val pageButton: Dp = ((maxWidth.value - 36f - 2f) / 9f).coerceIn(30f, 44f).dp
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            BatchButton(CupertinoGlyph.Xmark, "Cancelar seleção", 36.dp) { store.clearSelection() }
            if (!layoutPage) {
                Text(
                    if (count >= 2) "$count selecionadas" else "Selecione ao menos duas camadas",
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)),
                    modifier = Modifier.weight(1f),
                )
                BatchButton(CupertinoGlyph.RectangleStack, "Agrupar seleção", 36.dp) { store.comingSoon("Agrupar") }
                BatchButton(CupertinoGlyph.SquareStack3dDownRightFill, "Agrupar e mascarar: a de cima mostra só o que cobre", 36.dp) {
                    store.comingSoon("Agrupar e mascarar")
                }
                BatchButton(CupertinoGlyph.SquareStack3dDownRight, "Agrupar e recortar: a de cima fura as de baixo", 36.dp) {
                    store.comingSoon("Agrupar e recortar")
                }
                BatchButton(CupertinoGlyph.Trash, "Excluir seleção", 36.dp) { LayerOps.delete(store, store.selection) }
                BatchButton(CupertinoGlyph.ChevronRight, "Alinhar e distribuir", 36.dp) { layoutPage = true }
            } else {
                BatchButton(CupertinoGlyph.ChevronLeft, "Voltar às ações do lote", pageButton) { layoutPage = false }
                Spacer(Modifier.weight(1f))
                AlignButton(store, CupertinoGlyph.ArrowLeftToLine, "Alinhar à esquerda", LayerOps.Edge.Left, pageButton)
                AlignButton(store, CupertinoGlyph.ArrowLeftRight, "Centralizar na horizontal", LayerOps.Edge.CenterH, pageButton)
                AlignButton(store, CupertinoGlyph.ArrowRightToLine, "Alinhar à direita", LayerOps.Edge.Right, pageButton)
                AlignButton(store, CupertinoGlyph.ArrowUpToLine, "Alinhar ao topo", LayerOps.Edge.Top, pageButton)
                AlignButton(store, CupertinoGlyph.ArrowUpArrowDown, "Centralizar na vertical", LayerOps.Edge.CenterV, pageButton)
                AlignButton(store, CupertinoGlyph.ArrowDownToLine, "Alinhar à base", LayerOps.Edge.Bottom, pageButton)
                val three = count >= 3
                BatchButton(
                    CupertinoGlyph.ArrowUpDownSquare,
                    "Distribuir na vertical (vãos iguais)",
                    pageButton,
                    enabled = three,
                ) { LayerOps.distribute(store, store.selection, horizontal = false) }
                BatchButton(
                    CupertinoGlyph.ArrowLeftRightSquare,
                    "Distribuir na horizontal (vãos iguais)",
                    pageButton,
                    enabled = three,
                ) { LayerOps.distribute(store, store.selection, horizontal = true) }
                Spacer(Modifier.width(2.dp))
            }
        }
    }
}

@Composable
private fun BatchButton(glyph: Char, description: String, width: Dp, enabled: Boolean = true, onLongClick: (() -> Unit)? = null, onClick: () -> Unit) {
    ChromeButton(
        glyph,
        description,
        onClick = if (enabled) onClick else null,
        size = 18.dp,
        width = width,
        tint = if (enabled) AureaColors.Text else AureaColors.Disabled,
        onLongClick = onLongClick,
    )
}

/** Alinhar (à composição); segurar abriria a folha Alinhar completa. */
@Composable
private fun AlignButton(store: EditorStore, glyph: Char, description: String, edge: LayerOps.Edge, width: Dp) {
    BatchButton(
        glyph,
        "$description · segure para mais",
        width,
        onLongClick = { store.comingSoon("Alinhar") },
    ) { LayerOps.align(store, store.selection, edge) }
}
