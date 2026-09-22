package com.aurea.aurea.editor

import androidx.compose.foundation.background
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
import androidx.compose.material.icons.automirrored.rounded.FormatAlignLeft
import androidx.compose.material.icons.automirrored.rounded.FormatAlignRight
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.rounded.Stairs
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
// Doca de ferramentas da camada — `LayerToolsDock` (ref15)
// =============================================================================

/** O que a doca precisa da camada (data class: recompõe só quando muda). */
private data class DockLayer(
    val id: Long,
    val kind: Int,
    val locked: Boolean,
    val start: Int,
    val end: Int,
    val adjustment: Boolean,
    val vector: Boolean,
    val hasAudio: Boolean,
    val muted: Boolean,
)

/**
 * As fichas da grade: ícone grande + nome curto, cada uma abre UM painel que
 * existe de verdade. Ficha sem painel não entra (nada de "em breve").
 */
private enum class DockSection(val glyph: Char, val label: String, val panel: EditorPanel) {
    ColorFill(CupertinoGlyph.Paintbrush, "Cor e preenchimento", EditorPanel.Shape),
    EditShape(ShellGlyph.SliderHorizontalBelowRectangle, "Editar forma", EditorPanel.Shape),
    EditVector(CupertinoGlyph.PencilOutline, "Editar vetor", EditorPanel.Vector),
    EditText(CupertinoGlyph.Textformat, "Editar texto", EditorPanel.Text),
    Particles(CupertinoGlyph.Sparkles, "Partículas", EditorPanel.Particles),
    Audio(CupertinoGlyph.Speaker2, "Áudio", EditorPanel.Audio),
    Move(CupertinoGlyph.Move, "Transformar", EditorPanel.Transform),
    Blend(CupertinoGlyph.CircleLefthalfFill, "Opacidade e mistura", EditorPanel.Appearance),
    Environment(CupertinoGlyph.Lightbulb, "Ambiente", EditorPanel.Element3D),
    Mask(CupertinoGlyph.PencilOutline, "Máscara", EditorPanel.Mask),
    Track(ShellGlyph.Viewfinder, "Rastreio", EditorPanel.Tracking),
    Captions(CupertinoGlyph.CaptionsBubble, "Legendas", EditorPanel.Captions),
    Presets(CupertinoGlyph.WandStars, "Presets", EditorPanel.Presets),
    Effects(CupertinoGlyph.Sparkles, "Efeitos", EditorPanel.Effects),
}

/**
 * As fichas do TIPO, na ordem de uso (o que é próprio do tipo primeiro,
 * Presets e Efeitos no fim, como na ref15). O que não se aplica não existe:
 * um som não tem posição na tela, um nulo não tem cor, uma luz não tem
 * máscara. Velocidade, aparar e mudo moram na fileira rápida.
 */
private fun sectionsFor(l: DockLayer): List<DockSection> {
    val type = LayerType.of(l.kind)
    if (l.adjustment || type == LayerType.Adjustment) {
        // Camada de ajuste não tem conteúdo: só a mistura e os efeitos que ela aplica abaixo.
        return listOf(DockSection.Blend, DockSection.Presets, DockSection.Effects)
    }
    return when (type) {
        LayerType.Shape -> if (l.vector) {
            listOf(DockSection.EditVector, DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        } else {
            listOf(DockSection.ColorFill, DockSection.EditShape, DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        }
        LayerType.Text -> listOf(DockSection.EditText, DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        LayerType.Video -> buildList {
            add(DockSection.Move)
            if (l.hasAudio) add(DockSection.Audio)
            add(DockSection.Mask)
            add(DockSection.Blend)
            add(DockSection.Track)
            if (l.hasAudio) add(DockSection.Captions)
            add(DockSection.Presets)
            add(DockSection.Effects)
        }
        LayerType.Image -> listOf(DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        LayerType.Audio -> listOf(DockSection.Audio, DockSection.Captions, DockSection.Presets, DockSection.Effects)
        LayerType.Model3D -> listOf(DockSection.Move, DockSection.Environment, DockSection.Blend, DockSection.Presets, DockSection.Effects)
        LayerType.Particles -> listOf(DockSection.Particles, DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        LayerType.Group -> listOf(DockSection.Move, DockSection.Blend, DockSection.Mask, DockSection.Presets, DockSection.Effects)
        LayerType.Null, LayerType.Camera, LayerType.Light -> listOf(DockSection.Move, DockSection.Presets)
        LayerType.Adjustment -> emptyList()
    }
}

/** Colunas da grade: 3 fichas grandes (ref15) e 4 quando o tipo tem mais de seis. */
private fun columnsFor(count: Int): Int = if (count <= 6) 3 else 4

/**
 * Doca da camada sem painel: a fileira rápida (velocidade · aparar início ·
 * dividir · aparar fim · mudo, só o que se aplica ao tipo) e a grade de fichas
 * grandes do tipo.
 */
@Composable
internal fun LayerToolsDock(store: EditorStore, ui: EditorUi, layerId: Long) {
    val layer by remember(layerId) {
        derivedStateOf {
            val d = store.detail?.takeIf { it.id == layerId }
            store.layers.firstOrNull { it.id == layerId }?.let {
                DockLayer(
                    id = it.id,
                    kind = it.kind,
                    locked = it.locked,
                    start = it.startFrame,
                    end = it.endFrame,
                    adjustment = it.adjustment,
                    vector = d != null && store.isVectorLayer,
                    hasAudio = d?.hasAudio == true,
                    muted = d?.audioMuted == true,
                )
            }
        }
    }
    val l = layer ?: return
    val type = LayerType.of(l.kind)
    val sections = sectionsFor(l)
    val columns = columnsFor(sections.size)
    val rows = sections.chunked(columns)
    BoxWithConstraints(Modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        // Fileira rápida (44 + 16 de respiro) e os vãos de 8 entre fileiras.
        val tileHeight = ((maxHeight.value - 60f - 8f * rows.size) / rows.size.coerceAtLeast(1)).coerceIn(64f, 96f)
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
                // Velocidade: só quem tem tempo de mídia (o painel é de vídeo e áudio).
                if (type == LayerType.Video || type == LayerType.Audio) {
                    DockTool(CupertinoGlyph.Speedometer, "Velocidade", 21) { openPanel(store, ui, EditorPanel.Speed) }
                }
                // As portas do grupo: entrar e desagrupar.
                if (type == LayerType.Group) {
                    DockTool(CupertinoGlyph.ArrowDownRightSquare, "Entrar no grupo", 20) { store.openPrecomp(l.id) }
                    DockTool(ShellGlyph.SquareSplit2x2, "Desagrupar", 20) { store.ungroupPrecomp(l.id) }
                }
                DockTool(CupertinoGlyph.ArrowRightToLine, "Aparar o início no cabeçote", 19) {
                    timeEdit(store, l) { store.trimStart(l.id, store.playhead) }
                }
                DockTool(CupertinoGlyph.Scissors, "Dividir no cabeçote", 19) {
                    timeEdit(store, l) { store.splitAtPlayhead(listOf(l.id)) }
                }
                DockTool(CupertinoGlyph.ArrowLeftToLine, "Aparar o fim no cabeçote", 19) {
                    timeEdit(store, l) { store.trimEnd(l.id, store.playhead) }
                }
                // Mudo: toque liga/desliga; segurar abre o volume.
                if (l.hasAudio) {
                    DockTool(
                        if (l.muted) CupertinoGlyph.SpeakerSlash else CupertinoGlyph.Speaker2,
                        if (l.muted) "Som desligado · toque para ligar, segure para o volume" else "Desligar o som · segure para o volume",
                        20,
                        tint = if (l.muted) AureaColors.Accent else AureaColors.Text,
                        onLongClick = { openPanel(store, ui, EditorPanel.Audio) },
                    ) { store.setAudioMuted(!l.muted) }
                }
            }
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(start = 10.dp, end = 10.dp, bottom = 8.dp),
            ) {
                rows.forEach { row ->
                    Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        row.forEach { s -> DockTile(s, tileHeight) { openPanel(store, ui, s.panel) } }
                        // A coluna vazia guarda o lugar: sem ela a última ficha
                        // de uma fileira incompleta esticava.
                        repeat(columns - row.size) { Spacer(Modifier.weight(1f)) }
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

@Composable
private fun RowScope.DockTool(
    glyph: Char,
    description: String,
    size: Int,
    tint: Color = AureaColors.Text,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .semantics { contentDescription = description }
            .tocavel(haptic = true, onLongClick = onLongClick, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, size.dp, tint)
    }
}

@Composable
private fun RowScope.DockTile(section: DockSection, height: Float, onClick: () -> Unit) {
    val small = height < 72f
    Column(
        Modifier
            .weight(1f)
            .height(height.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(ShellColors.DockTile)
            .semantics { contentDescription = section.label }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 4.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        val iconSize = if (small) 22.dp else 27.dp
        // "Transformar" usa o Material `open_with_rounded` (as quatro setas).
        if (section == DockSection.Move) DockVector(Icons.Rounded.OpenWith, iconSize)
        else CupertinoIcon(section.glyph, iconSize, ShellColors.DockTileContent)
        Spacer(Modifier.height(if (small) 4.dp else 7.dp))
        Text(
            section.label,
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = if (small) 10.sp else 11.sp,
                    lineHeight = 1.15.em,
                    fontWeight = FontWeight.W500,
                    color = ShellColors.DockTileContent,
                ),
            ),
        )
    }
}

@Composable
private fun DockVector(icon: ImageVector, size: androidx.compose.ui.unit.Dp) {
    Icon(icon, contentDescription = null, tint = ShellColors.DockTileContent, modifier = Modifier.size(size))
}

// =============================================================================
// Seleção múltipla — barra de baixo (ref19)
// =============================================================================

/**
 * Com várias camadas: em cima, o tempo (aparar início · dividir · aparar fim
 * no cabeçote | alinhar os inícios · escada · alinhar os fins); embaixo, a
 * tela (alinhar à composição e distribuir). Tudo num passo de desfazer.
 */
@Composable
internal fun MultiSelectionPanel(store: EditorStore, @Suppress("UNUSED_PARAMETER") ui: EditorUi) {
    val count by remember { derivedStateOf { store.selection.size } }
    Column(Modifier.fillMaxSize().padding(horizontal = 10.dp)) {
        Spacer(Modifier.height(4.dp))
        Row(
            Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowRightToLine, "Aparar o início no cabeçote") { batchTrim(store, start = true) }
            BatchTool(CupertinoGlyph.Scissors, "Dividir no cabeçote") {
                val t = store.playhead
                val covered = store.layers.any { it.id in store.selection && !it.locked && t > it.startFrame && t < it.endFrame }
                if (covered) {
                    if (store.playing) store.pause()
                    store.splitAtPlayhead(store.layers.filter { it.id in store.selection && !it.locked }.map { it.id })
                } else {
                    store.showToast("Leve o cabeçote para dentro das camadas")
                }
            }
            BatchTool(CupertinoGlyph.ArrowLeftToLine, "Aparar o fim no cabeçote") { batchTrim(store, start = false) }
            Box(Modifier.width(1.dp).height(24.dp).background(AureaColors.Border))
            BatchTool(Icons.AutoMirrored.Rounded.FormatAlignLeft, "Alinhar os inícios") { timeAlign(store, TimeAlign.Start) }
            BatchTool(Icons.Rounded.Stairs, "Em escada: uma começa quando a de cima termina") { timeAlign(store, TimeAlign.Cascade) }
            BatchTool(Icons.AutoMirrored.Rounded.FormatAlignRight, "Alinhar os fins") { timeAlign(store, TimeAlign.End) }
        }
        Spacer(Modifier.height(8.dp))
        Row(
            Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowLeftToLine, "Alinhar à esquerda da tela", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Left) }
            BatchTool(CupertinoGlyph.ArrowLeftRight, "Centralizar na horizontal", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterH) }
            BatchTool(CupertinoGlyph.ArrowRightToLine, "Alinhar à direita da tela", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Right) }
            BatchTool(CupertinoGlyph.ArrowUpToLine, "Alinhar ao topo da tela", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Top) }
            BatchTool(CupertinoGlyph.ArrowUpArrowDown, "Centralizar na vertical", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterV) }
            BatchTool(CupertinoGlyph.ArrowDownToLine, "Alinhar à base da tela", 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Bottom) }
            val three = count >= 3
            BatchTool(CupertinoGlyph.ArrowLeftRightSquare, "Distribuir na horizontal (vãos iguais)", 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = true)
            }
            BatchTool(CupertinoGlyph.ArrowUpDownSquare, "Distribuir na vertical (vãos iguais)", 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = false)
            }
        }
    }
}

/** Aparar as escolhidas que cobrem o cabeçote (as bloqueadas ficam), num passo só. */
private fun batchTrim(store: EditorStore, start: Boolean) {
    val t = store.playhead
    val rows = store.layers.filter { it.id in store.selection }
    val targets = rows.filter { !it.locked && t > it.startFrame && t < it.endFrame }
    if (targets.isEmpty()) {
        store.showToast(
            if (rows.isNotEmpty() && rows.all { it.locked }) "Camadas bloqueadas: desbloqueie para editar"
            else "Leve o cabeçote para dentro das camadas",
        )
        return
    }
    if (store.playing) store.pause()
    store.beginGesture(if (start) "aparar início" else "aparar fim")
    targets.forEach { if (start) store.trimStart(it.id, t) else store.trimEnd(it.id, t) }
    store.endGesture()
}

private enum class TimeAlign { Start, Cascade, End }

/**
 * Arruma as escolhidas no TEMPO, sem mudar a duração de nenhuma: inícios
 * juntos, fins juntos ou em escada (na ordem da timeline, de cima para baixo;
 * a primeira fica onde está). Bloqueadas não andam.
 */
private fun timeAlign(store: EditorStore, mode: TimeAlign) {
    val rows = store.layers.filter { it.id in store.selection && !it.locked }
    if (rows.size < 2) {
        store.showToast("Escolha ao menos duas camadas desbloqueadas")
        return
    }
    if (store.playing) store.pause()
    val moves: List<Pair<Long, Int>> = when (mode) {
        TimeAlign.Start -> {
            val s = rows.minOf { it.startFrame }
            rows.map { it.id to s - it.startFrame }
        }
        TimeAlign.End -> {
            val e = rows.maxOf { it.endFrame }
            rows.map { it.id to e - it.endFrame }
        }
        TimeAlign.Cascade -> {
            var cursor = rows.first().startFrame
            rows.map { r ->
                val d = cursor - r.startFrame
                cursor += r.endFrame - r.startFrame
                r.id to d
            }
        }
    }.filter { it.second != 0 }
    if (moves.isEmpty()) return
    store.beginGesture(
        when (mode) {
            TimeAlign.Start -> "alinhar inícios"
            TimeAlign.End -> "alinhar fins"
            TimeAlign.Cascade -> "escada"
        },
    )
    moves.forEach { (id, d) -> store.moveLayers(listOf(id), d) }
    store.endGesture()
}

@Composable
private fun RowScope.BatchTool(glyph: Char, description: String, size: Int = 20, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .semantics { contentDescription = description }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, size.dp, if (enabled) AureaColors.Text else AureaColors.Disabled)
    }
}

@Composable
private fun RowScope.BatchTool(icon: ImageVector, description: String, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .semantics { contentDescription = description }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(22.dp))
    }
}
