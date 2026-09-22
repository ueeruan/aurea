package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

/** A borda superior de 1 dp do `ContextSheet` (#273442); o conteúdo começa abaixo dela. */
internal fun Modifier.drawTopHairline(): Modifier =
    this
        .drawBehind { drawRect(AureaColors.Border, size = Size(size.width, 1.dp.toPx())) }
        .padding(top = 1.dp)

/** A linha de dica quando nada está selecionado (`DicaDoPalco`). */
@Composable
internal fun StageHint() {
    Box(Modifier.fillMaxSize().background(AureaColors.EditorPanel), contentAlignment = Alignment.Center) {
        Text(
            "Toque num objeto na tela para editar.",
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Muted)),
            modifier = Modifier.padding(horizontal = 16.dp),
        )
    }
}

// =============================================================================
// Doca de ferramentas da camada — `LayerToolsDock` (03_menu_camada_dock.png)
// =============================================================================

/** O que a doca precisa da camada (data class: recompõe só quando muda). */
private data class DockLayer(val id: Long, val kind: Int, val locked: Boolean, val start: Int, val end: Int)

/** As seções da grade (`AmSecao`, na ordem do enum da A.01). */
private enum class DockSection(val glyph: Char, val label: String, val badge: String? = null) {
    Move(CupertinoGlyph.Move, "Movimentação e transformação"),
    ColorFill(CupertinoGlyph.Paintbrush, "Cor e preenchimento"),
    BorderShadow(CupertinoGlyph.SquareOnSquare, "Borda e sombra"),
    Blend(CupertinoGlyph.CircleLefthalfFill, "Mistura e opacidade"),
    Volume(CupertinoGlyph.Speaker2, "Volume"),
    EditShape(ShellGlyph.SliderHorizontalBelowRectangle, "Editar forma"),
    Clone(ShellGlyph.CircleGrid3x3, "Clonar"),
    EditText(CupertinoGlyph.Textformat, "Editar texto"),
    Particles(CupertinoGlyph.Sparkles, "Partículas"),
    Element3D(CupertinoGlyph.Videocam, "Elemento 3D"),
    Track(ShellGlyph.Viewfinder, "Rastreio"),
    Camera(CupertinoGlyph.Videocam, "Câmera", "NEW"),
    Transitions(CupertinoGlyph.ArrowRightToLine, "Entrada e saída"),
    Echo(CupertinoGlyph.SquareStack3dDownRight, "Eco e rastro"),
    Effects(CupertinoGlyph.Sparkles, "Efeitos"),
    Captions(CupertinoGlyph.Textformat, "Legendas"),
    Presets(CupertinoGlyph.WandStars, "Presets"),
    Mask(CupertinoGlyph.PencilOutline, "Máscara e recorte"),
}

/**
 * As seções que aparecem para o tipo (`secoesDe`): o que não se aplica ao
 * tipo não existe para ele — um som não tem posição na tela, um nulo não tem
 * cor.
 */
private fun sectionsFor(type: LayerType): List<DockSection> = when (type) {
    LayerType.Audio -> listOf(DockSection.Volume, DockSection.Captions, DockSection.Effects, DockSection.Presets)
    LayerType.Null -> listOf(DockSection.Move, DockSection.Clone, DockSection.Presets)
    LayerType.Camera -> listOf(DockSection.Move, DockSection.Camera, DockSection.Presets)
    else -> buildList {
        add(DockSection.Move)
        if (type == LayerType.Shape || type == LayerType.Text || type == LayerType.Model3D) add(DockSection.ColorFill)
        add(DockSection.BorderShadow)
        add(DockSection.Blend)
        // Máscara/track matte: tudo que desenha em 2D (o grupo 3D ainda não recorta).
        if (type != LayerType.Model3D && type != LayerType.Light) add(DockSection.Mask)
        if (type == LayerType.Video) add(DockSection.Volume)
        if (type == LayerType.Video) add(DockSection.Track)
        if (type == LayerType.Video) add(DockSection.Captions)
        if (type == LayerType.Shape) add(DockSection.EditShape)
        if (type == LayerType.Text) add(DockSection.EditText)
        if (type == LayerType.Particles) add(DockSection.Particles)
        if (type == LayerType.Model3D) add(DockSection.Element3D)
        add(DockSection.Transitions)
        add(DockSection.Echo)
        add(DockSection.Effects)
        add(DockSection.Presets)
    }
}

/**
 * Doca da camada sem painel: fileira de ações rápidas (44, #1E222D, raio 10)
 * e a grade de TRÊS colunas de fichas (#222634, raio 10, altura
 * `clamp((H − 70)/2, 58, 82)`).
 */
@Composable
internal fun LayerToolsDock(store: EditorStore, ui: EditorUi, layerId: Long) {
    val layer by remember(layerId) {
        derivedStateOf {
            store.layers.firstOrNull { it.id == layerId }?.let { DockLayer(it.id, it.kind, it.locked, it.startFrame, it.endFrame) }
        }
    }
    val hasParent by remember { derivedStateOf { (store.detail?.parentId ?: 0L) != 0L } }
    val l = layer ?: return
    val type = LayerType.of(l.kind)
    BoxWithConstraints(Modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        val tileHeight = ((maxHeight.value - 70f) / 2f).coerceIn(58f, 82f)
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier
                    .padding(start = 10.dp, top = 8.dp, end = 10.dp, bottom = 8.dp)
                    .fillMaxWidth()
                    .height(44.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(ShellColors.DockRow),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // As portas do grupo: entrar, desagrupar e o tempo dele.
                if (type == LayerType.Group) {
                    DockTool(CupertinoGlyph.ArrowDownRightSquare, "Entrar no grupo", 20) { store.openPrecomp(l.id) }
                    DockTool(ShellGlyph.SquareSplit2x2, "Desagrupar", 20) { store.ungroupPrecomp(l.id) }
                    DockTool(CupertinoGlyph.Timer, "Tempo", 20) { store.comingSoon("Tempo do grupo") }
                }
                if (type == LayerType.Audio) {
                    DockTool(CupertinoGlyph.Speedometer, "Velocidade", 20) { openPanel(store, ui, EditorPanel.Speed) }
                }
                DockTool(CupertinoGlyph.ArrowRightToLine, "Aparar o início no cabeçote", 19) {
                    timeEdit(store, l) { store.trimStart(l.id, store.playhead) }
                }
                DockTool(CupertinoGlyph.Scissors, "Dividir camada", 19) {
                    timeEdit(store, l) { store.splitAtPlayhead(listOf(l.id)) }
                }
                DockTool(CupertinoGlyph.ArrowLeftToLine, "Aparar o fim no cabeçote", 19) {
                    timeEdit(store, l) { store.trimEnd(l.id, store.playhead) }
                }
                if (type != LayerType.Group) {
                    DockTool(CupertinoGlyph.Speaker2, "Volume / Áudio", 20) { openPanel(store, ui, EditorPanel.Audio) }
                }
                DockTool(
                    if (hasParent) CupertinoGlyph.LinkCircleFill else CupertinoGlyph.Link,
                    "Vincular (Parentear)",
                    20,
                    tint = if (hasParent) AureaColors.Accent else AureaColors.Text,
                ) { openPanel(store, ui, EditorPanel.Parent) }
            }
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(start = 10.dp, end = 10.dp, bottom = 8.dp),
            ) {
                sectionsFor(type).chunked(3).forEach { row ->
                    Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (k in 0 until 3) {
                            val s = row.getOrNull(k)
                            // A coluna vazia guarda o lugar: sem ela a última
                            // ficha de uma fileira incompleta esticava.
                            if (s == null) Spacer(Modifier.weight(1f))
                            else DockTile(s, tileHeight) { onSection(store, ui, s) }
                        }
                    }
                }
            }
        }
    }
}

/** Ferramenta de tempo: cadeado e cabeçote fora da camada dizem por que não. */
private inline fun timeEdit(store: EditorStore, l: DockLayer, action: () -> Unit) {
    val t = store.playhead
    when {
        l.locked -> store.showToast("Camada bloqueada: desbloqueie para editar")
        t <= l.start || t >= l.end -> store.showToast("Leve o cabeçote para dentro da camada")
        else -> {
            if (store.playing) store.pause()
            action()
        }
    }
}

private fun onSection(store: EditorStore, ui: EditorUi, s: DockSection) {
    when (s) {
        DockSection.Move -> openPanel(store, ui, EditorPanel.Transform)
        DockSection.Blend -> openPanel(store, ui, EditorPanel.Appearance)
        DockSection.Volume -> openPanel(store, ui, EditorPanel.Audio)
        DockSection.Effects -> openPanel(store, ui, EditorPanel.Effects)
        DockSection.Particles -> openPanel(store, ui, EditorPanel.Particles)
        DockSection.Transitions -> openPanel(store, ui, EditorPanel.Transitions)
        DockSection.Track -> openPanel(store, ui, EditorPanel.Tracking)
        DockSection.Element3D -> openPanel(store, ui, EditorPanel.Element3D)
        DockSection.Echo -> openPanel(store, ui, EditorPanel.Echo)
        DockSection.Captions -> openPanel(store, ui, EditorPanel.Captions)
        DockSection.Presets -> openPanel(store, ui, EditorPanel.Presets)
        DockSection.Mask -> openPanel(store, ui, EditorPanel.Mask)
        DockSection.ColorFill, DockSection.EditShape, DockSection.EditText -> when (store.detail?.kind) {
            com.aurea.aurea.ui.theme.LayerType.Shape.kind -> openPanel(store, ui, EditorPanel.Shape)
            com.aurea.aurea.ui.theme.LayerType.Text.kind -> openPanel(store, ui, EditorPanel.Text)
            else -> store.comingSoon(s.label)
        }
        else -> store.comingSoon(s.label)
    }
}

@Composable
private fun RowScope.DockTool(glyph: Char, description: String, size: Int, tint: Color = AureaColors.Text, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .semantics { contentDescription = description }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, size.dp, tint)
    }
}

@Composable
private fun RowScope.DockTile(section: DockSection, height: Float, onClick: () -> Unit) {
    val small = height < 65f
    Box(
        Modifier
            .weight(1f)
            .height(height.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(ShellColors.DockTile)
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 4.dp, vertical = if (small) 2.dp else 4.dp),
    ) {
        Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
            val iconSize = if (small) 18.dp else 21.dp
            // "Movimentação" usava o Material `open_with_rounded` (as quatro setas).
            if (section == DockSection.Move) DockVector(Icons.Rounded.OpenWith, iconSize)
            else CupertinoIcon(section.glyph, iconSize, ShellColors.DockTileContent)
            Spacer(Modifier.height(if (small) 2.dp else 4.dp))
            Text(
                section.label,
                textAlign = TextAlign.Center,
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = if (small) 9.5.sp else 10.5.sp,
                        lineHeight = 1.12.em,
                        fontWeight = FontWeight.W500,
                        color = ShellColors.DockTileContent,
                    ),
                ),
            )
        }
        section.badge?.let {
            Text(
                it,
                style = AureaType.Base.merge(TextStyle(fontSize = 8.5.sp, fontWeight = FontWeight.W900, color = Color.Black)),
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .clip(RoundedCornerShape(4.dp))
                    .background(ShellColors.BadgeNew)
                    .padding(horizontal = 5.dp, vertical = 1.5.dp),
            )
        }
    }
}

@Composable
private fun DockVector(icon: ImageVector, size: androidx.compose.ui.unit.Dp) {
    Icon(icon, contentDescription = null, tint = ShellColors.DockTileContent, modifier = Modifier.size(size))
}

// =============================================================================
// Seleção múltipla — `MultiSelectionPanel` (12 + 124)
// =============================================================================

/** "N camadas" e o que se faz com um conjunto. */
@Composable
internal fun MultiSelectionPanel(store: EditorStore, ui: EditorUi) {
    val count by remember { derivedStateOf { store.selection.size } }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Spacer(Modifier.width(12.dp))
            Box(
                Modifier.size(26.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.Selection),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.SquareStack3dUp, 15.dp, Color.White)
            }
            Spacer(Modifier.width(10.dp))
            Text(
                "$count camadas",
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)),
                modifier = Modifier.weight(1f),
            )
            ChromeButton(CupertinoGlyph.Xmark, "Limpar seleção", onClick = { store.clearSelection() }, size = 20.dp, width = 44.dp)
            Spacer(Modifier.width(4.dp))
        }
        Row(
            Modifier
                .fillMaxWidth()
                .height(72.dp)
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            BatchAction(ShellGlyph.FolderBadgePlus, "Agrupar") { store.precompose() }
            BatchAction(CupertinoGlyph.Link, "Vincular") { openPanel(store, ui, EditorPanel.Parent) }
            BatchAction(CupertinoGlyph.ChartBarAltFill, "Cascata") { store.comingSoon("Cascata") }
            BatchAction(ShellGlyph.SquareGrid3x2, "Alinhar") { store.comingSoon("Alinhar") }
            BatchAction(CupertinoGlyph.Scissors, "Dividir") {
                val t = store.playhead
                val covered = store.layers.any { it.id in store.selection && t > it.startFrame && t < it.endFrame }
                if (covered) store.splitAtPlayhead() else store.showToast("Leve o cabeçote para dentro da camada")
            }
            BatchAction(CupertinoGlyph.Trash, "Excluir", danger = true) { LayerOps.delete(store, store.selection) }
        }
    }
}

@Composable
private fun BatchAction(glyph: Char, label: String, danger: Boolean = false, onClick: () -> Unit) {
    val color = if (danger) AureaColors.Danger else AureaColors.Text
    Column(
        Modifier
            .size(84.dp, 64.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .tocavel(haptic = true, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(glyph, 22.dp, color)
        Spacer(Modifier.height(4.dp))
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = color)))
    }
}
