package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.roundToInt

// =============================================================================
//  Lote X do After Effects no Aurea PC: a ÁRVORE do conteúdo da camada vetorial
//  (o "Contents" da shape layer do AE) numa janela flutuante.
//
//  Cada grupo mostra o conteúdo de CIMA para baixo, como no AE: caminhos,
//  Preencher, Contorno, Aparar, Mesclar, Repetidor, operadores de caminho e
//  grupos de dentro, em qualquer ordem. Um operador vale para o que está ACIMA
//  dele; quem está em cima é desenhado por cima. As regras moram no motor
//  (Engine::vector_tree / vector_tree_edit, vector/VectorTree.hpp); aqui é só
//  a lista, os botões de ordem e os campos de cada item.
//
//  O painel de vetor do Aurea (o mesmo do celular) continua valendo: ele edita
//  o primeiro item de cada tipo dos grupos do primeiro nível.
// =============================================================================

/** Tipos de item (aurea::VectorItemKind). */
private const val K_PATH = 0
private const val K_FILL = 1
private const val K_STROKE = 2
private const val K_TRIM = 3
private const val K_REPEATER = 4
private const val K_MERGE = 5
private const val K_MODIFIER = 6
private const val K_GROUP = 7

/** Operações (Engine::kVectorTree*). */
private const val OP_ADD = 0
private const val OP_REMOVE = 1
private const val OP_MOVE = 2
private const val OP_ENABLE = 3
private const val OP_VALUES = 4
private const val OP_GROUP_VALUES = 6
private const val OP_FREE_ORDER = 7
private const val OP_ADD_GROUP = 8

object PcShapeTree {
    var open by mutableStateOf(false)
    var x by mutableStateOf(Float.NaN)
    var y by mutableStateOf(Float.NaN)
    /** Linha escolhida (índice na lista) e a camada dela. */
    internal var selected by mutableStateOf(-1)
    internal var layer by mutableStateOf(0L)

    internal fun show(store: EditorStore, layerId: Long) {
        if (store.primary != layerId || store.selection.size != 1) store.select(layerId)
        if (layer != layerId) selected = -1
        layer = layerId
        open = true
    }
}

/** Uma linha da árvore já com o caminho do grupo dono. */
private class ShapeRow(
    val depth: Int,
    val kind: Int,
    val ordinal: Int,
    val position: Int,
    val enabled: Boolean,
    val values: FloatArray,
    val name: String,
    /** Grupo dono do item (para um grupo: o PAI dele; vazio = primeiro nível). */
    val owner: IntArray,
    /** Para grupos: o caminho do próprio grupo. */
    val self: IntArray,
)

private fun parseRows(data: FloatArray, names: List<String>): List<ShapeRow> {
    val out = ArrayList<ShapeRow>()
    if (data.size < 2) return out
    val count = data[1].toInt()
    var p = 2
    var nameAt = 0
    // Caminho do grupo aberto em cada profundidade.
    val stack = ArrayList<IntArray>()
    for (i in 0 until count) {
        if (p + 6 > data.size) break
        val depth = data[p].toInt()
        val kind = data[p + 1].toInt()
        val ordinal = data[p + 2].toInt()
        val position = data[p + 3].toInt()
        val enabled = data[p + 4] > 0.5f
        val n = data[p + 5].toInt()
        p += 6
        if (n < 0 || p + n > data.size) break
        val values = data.copyOfRange(p, p + n)
        p += n
        while (stack.size > depth) stack.removeAt(stack.size - 1)
        val owner = if (depth == 0) IntArray(0) else stack.getOrNull(depth - 1) ?: IntArray(0)
        if (kind == K_GROUP) {
            val self = owner + ordinal
            stack.add(self)
            out += ShapeRow(depth, kind, ordinal, position, enabled, values, names.getOrNull(nameAt++).orEmpty(), owner, self)
        } else {
            out += ShapeRow(depth, kind, ordinal, position, enabled, values, "", owner, IntArray(0))
        }
    }
    return out
}

private val MODIFIER_NAMES = intArrayOf(
    R.string.ae_shapeop_offset, R.string.ae_shapeop_pucker, R.string.ae_shapeop_roughen, R.string.ae_shapeop_wiggle_paths,
    R.string.ae_shapeop_round_corners, R.string.ae_shapeop_twist, R.string.ae_shapeop_zigzag, R.string.ae_shapeop_wiggle_transform,
)
private val PATH_NAMES = intArrayOf(
    R.string.edt_vec_free_path, R.string.editor_retangulo, R.string.editor_elipse, R.string.editor_poligono, R.string.editor_estrela,
)

/** Rótulos dos campos de um operador de caminho, na ordem dos parâmetros do motor (kVecMod*). */
private fun modifierFields(type: Int): IntArray = when (type) {
    0 -> intArrayOf(R.string.ae_shapeop_amount, R.string.ae_shapeop_line_join, R.string.ae_shapeop_miter_limit, R.string.ae_shapeop_copies, R.string.ae_shapeop_copy_offset)
    1 -> intArrayOf(R.string.ae_shapeop_amount)
    2, 3 -> intArrayOf(R.string.ae_shapeop_size, R.string.ae_shapeop_detail, R.string.ae_shapeop_points, R.string.ae_shapeop_frequency,
        R.string.ae_shapeop_correlation, R.string.ae_shapeop_temporal_phase, R.string.ae_shapeop_spatial_phase)
    4 -> intArrayOf(R.string.ae_shapeop_radius)
    5 -> intArrayOf(R.string.ae_shapeop_angle, R.string.ae_shapeop_center_x, R.string.ae_shapeop_center_y)
    6 -> intArrayOf(R.string.ae_shapeop_size, R.string.ae_shapeop_ridges, R.string.ae_shapeop_points)
    7 -> intArrayOf(R.string.ae_shapeop_anchor_x, R.string.ae_shapeop_anchor_y, R.string.ae_shapeop_position_x, R.string.ae_shapeop_position_y,
        R.string.ae_shapeop_scale_x, R.string.ae_shapeop_scale_y, R.string.ae_shapeop_rotation, R.string.ae_shapeop_frequency,
        R.string.ae_shapeop_correlation, R.string.ae_shapeop_temporal_phase, R.string.ae_shapeop_spatial_phase)
    else -> IntArray(0)
}

/** Rótulos dos campos por tipo, na ordem dos valores da linha (vector/VectorTree.hpp). 0 = campo sem rótulo próprio (pulado). */
private fun fieldLabels(row: ShapeRow): IntArray = when (row.kind) {
    // tipo (não editável), centro x/y, tamanho x/y, arredondar, pontas, raio externo, raio interno, giro
    K_PATH -> intArrayOf(0, R.string.pcx_f_center_x, R.string.pcx_f_center_y, R.string.pcx_f_width, R.string.pcx_f_height, R.string.pcx_f_roundness,
        R.string.pcx_f_points, R.string.pcx_f_outer_radius, R.string.pcx_f_inner_radius, R.string.pcx_f_rotation)
    K_FILL -> intArrayOf(R.string.pcx_f_red, R.string.pcx_f_green, R.string.pcx_f_blue, R.string.pcx_f_opacity, R.string.pcx_f_even_odd)
    K_STROKE -> intArrayOf(R.string.pcx_f_red, R.string.pcx_f_green, R.string.pcx_f_blue, R.string.pcx_f_opacity, R.string.pcx_f_stroke_width,
        R.string.pcx_f_cap, R.string.pcx_f_join, R.string.ae_shapeop_miter_limit)
    K_TRIM -> intArrayOf(R.string.pcx_f_start, R.string.pcx_f_end, R.string.pcx_f_offset, R.string.pcx_f_trim_together)
    K_REPEATER -> intArrayOf(R.string.ae_shapeop_copies, R.string.pcx_f_offset, R.string.ae_shapeop_position_x, R.string.ae_shapeop_position_y,
        R.string.ae_shapeop_scale_x, R.string.ae_shapeop_scale_y, R.string.ae_shapeop_rotation, R.string.pcx_f_start_opacity,
        R.string.pcx_f_end_opacity, R.string.pcx_f_copies_above, R.string.ae_shapeop_anchor_x, R.string.ae_shapeop_anchor_y)
    K_MERGE -> intArrayOf(R.string.pcx_f_merge_mode)
    K_MODIFIER -> intArrayOf(0) + modifierFields(row.values.getOrNull(0)?.toInt() ?: -1)
    K_GROUP -> intArrayOf(R.string.ae_shapeop_position_x, R.string.ae_shapeop_position_y, R.string.ae_shapeop_anchor_x, R.string.ae_shapeop_anchor_y,
        R.string.ae_shapeop_scale_x, R.string.ae_shapeop_scale_y, R.string.ae_shapeop_rotation, R.string.pcx_f_opacity,
        R.string.pcx_f_skew, R.string.pcx_f_skew_axis)
    else -> IntArray(0)
}

@Composable
private fun rowTitle(row: ShapeRow): String = when (row.kind) {
    K_PATH -> stringResource(PATH_NAMES.getOrElse(row.values.getOrNull(0)?.toInt() ?: 0) { PATH_NAMES[0] })
    K_FILL -> stringResource(R.string.pcx_tree_fill)
    K_STROKE -> stringResource(R.string.pcx_tree_stroke)
    K_TRIM -> stringResource(R.string.pcx_tree_trim)
    K_REPEATER -> stringResource(R.string.pcx_tree_repeater)
    K_MERGE -> stringResource(R.string.pcx_tree_merge)
    K_MODIFIER -> stringResource(MODIFIER_NAMES.getOrElse(row.values.getOrNull(0)?.toInt() ?: 0) { MODIFIER_NAMES[0] })
    else -> row.name.ifBlank { stringResource(R.string.pcx_tree_group) }
}

private fun rowGlyph(kind: Int): Char = when (kind) {
    K_PATH -> CupertinoGlyph.Scribble
    K_FILL -> CupertinoGlyph.Circle
    K_STROKE -> CupertinoGlyph.Square
    K_GROUP -> CupertinoGlyph.FolderFill
    else -> CupertinoGlyph.SliderHorizontal3
}

private fun fmtValue(v: Float): String =
    if (abs(v - v.roundToInt()) < 0.0005f) v.roundToInt().toString() else String.format(java.util.Locale.ROOT, "%.2f", v)

/** Campo numérico: grava no Enter ou ao perder o foco. */
@Composable
private fun ValueField(label: String, value: Float, onCommit: (Float) -> Unit) {
    var text by remember(value) { mutableStateOf(fmtValue(value)) }
    fun commit() {
        val v = text.replace(',', '.').toFloatOrNull()
        if (v != null && v.isFinite() && abs(v - value) > 1e-6f) onCommit(v) else text = fmtValue(value)
    }
    Row(Modifier.fillMaxWidth().height(26.dp).padding(start = 28.dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = AureaColors.Muted, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        BasicTextField(
            value = text,
            onValueChange = { text = it.take(12) },
            singleLine = true,
            textStyle = TextStyle(color = AureaColors.Text, fontSize = 11.sp),
            cursorBrush = SolidColor(AureaColors.Accent),
            keyboardActions = KeyboardActions(onDone = { commit() }),
            modifier = Modifier.width(84.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.Hairline)
                .padding(horizontal = 6.dp, vertical = 4.dp).onFocusChanged { if (!it.isFocused) commit() },
        )
    }
}

@Composable
private fun IconButton(glyph: Char, label: String, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.size(24.dp).clip(RoundedCornerShape(5.dp)).then(if (enabled) Modifier.tocavel(shrink = 1f) { onClick() } else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 13.dp, tint = if (enabled) AureaColors.Muted else AureaColors.Hairline)
    }
}

/** O que dá para acrescentar a um grupo (menu "Adicionar"). */
@Composable
private fun addEntries(onAdd: (kind: Int, variant: Int) -> Unit): List<MenuEntry> {
    val paths = (1..4).map { v -> MenuEntry.Item(stringResource(PATH_NAMES[v])) { onAdd(K_PATH, v) } } +
        MenuEntry.Item(stringResource(PATH_NAMES[0])) { onAdd(K_PATH, 0) }
    val ops = MODIFIER_NAMES.mapIndexed { i, res -> MenuEntry.Item(stringResource(res)) { onAdd(K_MODIFIER, i) } }
    return listOf(
        MenuEntry.Item(stringResource(R.string.pcx_tree_path), glyph = CupertinoGlyph.Scribble, submenu = paths),
        MenuEntry.Item(stringResource(R.string.pcx_tree_fill), glyph = CupertinoGlyph.Circle) { onAdd(K_FILL, 0) },
        MenuEntry.Item(stringResource(R.string.pcx_tree_stroke), glyph = CupertinoGlyph.Square) { onAdd(K_STROKE, 0) },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pcx_tree_trim)) { onAdd(K_TRIM, 0) },
        MenuEntry.Item(stringResource(R.string.pcx_tree_merge)) { onAdd(K_MERGE, 0) },
        MenuEntry.Item(stringResource(R.string.pcx_tree_repeater)) { onAdd(K_REPEATER, 0) },
        MenuEntry.Item(stringResource(R.string.pcx_tree_operator), glyph = CupertinoGlyph.SliderHorizontal3, submenu = ops),
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pcx_tree_group), glyph = CupertinoGlyph.FolderFill) { onAdd(K_GROUP, 0) },
    )
}

@Composable
internal fun PcShapeTreeHost(store: EditorStore, maxW: Float, maxH: Float) {
    val layer = store.primary
    // A janela é da camada vetorial escolhida: troca de camada ou de tipo fecha.
    LaunchedEffect(layer, store.isVectorLayer) { if (layer != PcShapeTree.layer || !store.isVectorLayer) PcShapeTree.open = false }
    if (!PcShapeTree.open || layer == null || !store.isVectorLayer || store.screen != com.aurea.aurea.state.Screen.Editor) return
    val density = LocalDensity.current
    val w = 360f
    val h = 520f
    if (PcShapeTree.x.isNaN()) {
        PcShapeTree.x = (maxW - w - 780f).coerceAtLeast(20f)
        PcShapeTree.y = 90f
    }
    val x = PcShapeTree.x.coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
    val y = PcShapeTree.y.coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
    val shape = RoundedCornerShape(12.dp)
    // Relê a cada edição (da árvore, do painel de vetor, desfazer).
    val rows = remember(layer, store.vectorTreeRevision, store.vectorDoc) {
        store.vectorTreeRows()?.let { parseRows(it.first, it.second) } ?: emptyList()
    }
    val selected = PcShapeTree.selected.takeIf { it in rows.indices }
    val newGroupName = stringResource(R.string.pcx_tree_group)
    var addMenu by remember { mutableStateOf(false) }
    Column(
        Modifier.offset { IntOffset((x * density.density).roundToInt(), (y * density.density).roundToInt()) }
            .size(w.dp, h.dp).shadow(18.dp, shape).clip(shape)
            .background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape),
    ) {
        Row(
            Modifier.fillMaxWidth().height(40.dp)
                .pointerInput(Unit) {
                    detectDragGestures { c, d ->
                        c.consume()
                        PcShapeTree.x = (PcShapeTree.x + d.x / density.density).coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
                        PcShapeTree.y = (PcShapeTree.y + d.y / density.density).coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
                    }
                }
                .padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(R.string.pcx_tree_title), color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            Box {
                IconButton(CupertinoGlyph.Plus, stringResource(R.string.pcx_tree_add)) { addMenu = true }
                // Acrescenta no grupo da linha escolhida (logo abaixo dela); sem escolha, um grupo novo no primeiro nível.
                AnchoredMenu(
                    expanded = addMenu, onDismiss = { addMenu = false },
                    entries = addEntries { kind, variant ->
                        addMenu = false
                        val row = selected?.let { rows[it] }
                        when {
                            row == null && kind == K_GROUP -> store.editVectorTree(IntArray(0), OP_ADD_GROUP, name = newGroupName)
                            row == null -> rows.firstOrNull { it.kind == K_GROUP }?.let { store.editVectorTree(it.self, OP_ADD, kind, 0, variant, name = newGroupName) }
                            row.kind == K_GROUP -> store.editVectorTree(row.self, OP_ADD, kind, 0, variant, name = newGroupName)
                            else -> store.editVectorTree(row.owner, OP_ADD, kind, row.position + 1, variant, name = newGroupName)
                        }
                    },
                )
            }
            IconButton(CupertinoGlyph.Xmark, stringResource(R.string.pc_close)) { PcShapeTree.open = false }
        }
        Text(stringResource(R.string.pcx_tree_hint), color = AureaColors.Muted, fontSize = 10.5.sp, lineHeight = 13.sp,
            modifier = Modifier.padding(start = 12.dp, end = 12.dp, bottom = 6.dp))
        Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState())) {
            rows.forEachIndexed { index, row ->
                val isSelected = selected == index
                val title = rowTitle(row)
                // Um item de um grupo ainda na ordem de sempre: mexer na ordem passa o grupo para a ordem livre (o motor cuida).
                val siblings = rows.count { it.depth == row.depth && it.owner.contentEquals(row.owner) && row.depth > 0 }
                Row(
                    Modifier.fillMaxWidth().height(28.dp)
                        .background(if (isSelected) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.EditorPanel)
                        .tocavel(shrink = 1f) { PcShapeTree.selected = if (isSelected) -1 else index }
                        .padding(start = (8 + row.depth * 14).dp, end = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CupertinoIcon(rowGlyph(row.kind), 12.dp, tint = if (row.enabled) AureaColors.Muted else AureaColors.Hairline)
                    Spacer(Modifier.width(6.dp))
                    Text(title, color = if (row.enabled) AureaColors.Text else AureaColors.Muted, fontSize = 12.sp,
                        fontWeight = if (row.kind == K_GROUP) FontWeight.SemiBold else FontWeight.Normal,
                        maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                    if (row.depth > 0) {
                        if (row.kind != K_PATH) IconButton(if (row.enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, stringResource(R.string.ae_shapeop_enabled)) {
                            store.editVectorTree(row.owner, OP_ENABLE, row.position, if (row.enabled) 0 else 1)
                        }
                        IconButton(CupertinoGlyph.ChevronUp, stringResource(R.string.ae_shapeop_move_up), enabled = row.position > 0) {
                            if (store.editVectorTree(row.owner, OP_MOVE, row.position, row.position - 1)) PcShapeTree.selected = -1
                        }
                        IconButton(CupertinoGlyph.ChevronDown, stringResource(R.string.ae_shapeop_move_down), enabled = siblings > 1) {
                            if (store.editVectorTree(row.owner, OP_MOVE, row.position, row.position + 1)) PcShapeTree.selected = -1
                        }
                        IconButton(CupertinoGlyph.Trash, stringResource(R.string.ae_shapeop_remove)) {
                            if (store.editVectorTree(row.owner, OP_REMOVE, row.position)) PcShapeTree.selected = -1
                        }
                    }
                }
                if (isSelected) {
                    val labels = fieldLabels(row)
                    labels.forEachIndexed { i, res ->
                        if (res == 0 || i >= row.values.size) return@forEachIndexed
                        ValueField(stringResource(res), row.values[i]) { v ->
                            val next = row.values.copyOf()
                            next[i] = v
                            if (row.kind == K_GROUP) {
                                // O transform do próprio grupo (primeiro nível ou de dentro).
                                store.editVectorTree(row.self, OP_GROUP_VALUES, values = next)
                            } else {
                                store.editVectorTree(row.owner, OP_VALUES, row.position, values = next)
                            }
                        }
                    }
                    if (row.kind == K_GROUP) {
                        Text(stringResource(R.string.pcx_tree_free_order), color = AureaColors.Accent, fontSize = 11.sp,
                            modifier = Modifier.padding(start = 28.dp, top = 4.dp, bottom = 6.dp).clip(RoundedCornerShape(5.dp))
                                .tocavel(shrink = 1f) { store.editVectorTree(row.self, OP_FREE_ORDER, 1) })
                    }
                }
            }
        }
    }
}
