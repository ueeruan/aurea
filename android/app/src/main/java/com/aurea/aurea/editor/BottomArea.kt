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
import androidx.compose.material.icons.automirrored.rounded.FormatAlignLeft
import androidx.compose.material.icons.automirrored.rounded.FormatAlignRight
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.rounded.Stairs
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import android.app.Application
import androidx.annotation.StringRes
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
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
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.platform.testTag
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
            stringResource(R.string.editor_toque_num_objeto_tela_editar),
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
internal data class DockLayer(
    val id: Long,
    val kind: Int,
    val locked: Boolean,
    val start: Int,
    val end: Int,
    val adjustment: Boolean,
    val vector: Boolean,
    val hasAudio: Boolean,
    val muted: Boolean,
    val text3D: Boolean,
    val effectCount: Int,
)

/**
 * As fichas da grade: ícone grande + nome curto, cada uma abre UM painel que
 * existe de verdade. Ficha sem painel não entra (nada de "em breve").
 */
internal enum class DockSection(val glyph: Char, @StringRes val label: Int, val panel: EditorPanel) {
    ColorFill(CupertinoGlyph.Paintbrush, R.string.sh_dock_color_fill, EditorPanel.Shape),
    EditShape(ShellGlyph.SliderHorizontalBelowRectangle, R.string.sh_dock_edit_shape, EditorPanel.Shape),
    EditVector(CupertinoGlyph.PencilOutline, R.string.sh_dock_edit_vector, EditorPanel.Vector),
    EditText(CupertinoGlyph.Textformat, R.string.sh_dock_edit_text, EditorPanel.Text),
    TextOptions(ShellGlyph.SliderHorizontalBelowRectangle, R.string.text_options, EditorPanel.Text),
    Particles(CupertinoGlyph.Sparkles, R.string.sh_dock_particles, EditorPanel.Particles),
    Audio(CupertinoGlyph.Speaker2, R.string.sh_add_tab_audio, EditorPanel.Audio),
    Move(CupertinoGlyph.Move, R.string.sh_dock_transform, EditorPanel.Transform),
    Blend(CupertinoGlyph.CircleLefthalfFill, R.string.sh_dock_opacity_blend, EditorPanel.Appearance),
    Environment(CupertinoGlyph.Lightbulb, R.string.sh_dock_environment, EditorPanel.Element3D),
    // Máscara, rastreio de câmera e legendas automáticas viraram EFEITOS (moram
    // no seletor de efeitos); os presets de vídeo/imagem saíram. Nada disso tem
    // ficha própria na doca.
    // Presets de TEXTO voltaram (2026-10-03): só no texto 2D/3D, abre o painel
    // de presets na aba de texto (cartões animados, salvos e da comunidade).
    Presets(CupertinoGlyph.WandStars, R.string.sh_dock_presets, EditorPanel.Presets),
    Effects(CupertinoGlyph.Sparkles, R.string.sh_dock_effects, EditorPanel.Effects),
    // Rig 2D: não abre painel — o palco vira o esqueleto (RigStage.kt).
    Rig(CupertinoGlyph.PersonCropCircle, R.string.rig_dock, EditorPanel.Transform),
}

/**
 * As fichas do TIPO, enxutas e na ordem de uso: o que é próprio do tipo
 * (editar o texto, a forma, as partículas) primeiro, depois Transformar,
 * Efeitos e Opacidade/mesclagem. O que não se aplica não existe: um som não
 * tem posição na tela, um nulo não tem cor. Velocidade, aparar, dividir e mudo
 * moram na fileira rápida; o raro (duplicar, estilo, grupo, rastrear ponto…)
 * mora no ⋯ do topo — nada aparece duas vezes.
 */
internal fun sectionsFor(l: DockLayer): List<DockSection> {
    val type = LayerType.of(l.kind)
    if (l.adjustment || type == LayerType.Adjustment) {
        // Camada de ajuste não tem conteúdo: só os efeitos que ela aplica abaixo e a mistura.
        return listOf(DockSection.Effects, DockSection.Blend)
    }
    // Ordem do AM: o que é do tipo e a mistura na fileira de cima; mover,
    // editar e efeitos na de baixo (7 → 3 + 4; 5 → 2 + 3).
    val common = listOf(DockSection.Blend, DockSection.Move, DockSection.Effects)
    return when (type) {
        LayerType.Shape -> if (l.vector) listOf(DockSection.Blend, DockSection.Move, DockSection.EditVector, DockSection.Effects)
            else listOf(DockSection.ColorFill, DockSection.Blend, DockSection.Move, DockSection.EditShape, DockSection.Effects)
        // Texto: editar, opções e presets em cima; mistura, mover e efeitos embaixo (6 → 3 + 3).
        LayerType.Text -> listOf(DockSection.EditText, DockSection.TextOptions, DockSection.Presets) + common
        LayerType.Video -> if (l.hasAudio) listOf(DockSection.Blend, DockSection.Move, DockSection.Audio, DockSection.Effects) else common
        LayerType.Image -> common + DockSection.Rig
        LayerType.Audio -> listOf(DockSection.Audio, DockSection.Effects)
        LayerType.Model3D -> if (l.text3D) listOf(DockSection.EditText, DockSection.TextOptions, DockSection.Presets) + common
            else listOf(DockSection.Move, DockSection.Environment, DockSection.Effects, DockSection.Blend)
        LayerType.Particles -> listOf(DockSection.Particles) + common
        LayerType.Group -> common
        LayerType.Camera -> listOf(DockSection.Move, DockSection.Environment)
        LayerType.Light -> if (l.effectCount > 0) common else listOf(DockSection.Move)
        LayerType.Null -> listOf(DockSection.Move)
        LayerType.Adjustment -> emptyList()
    }
}

/** A camada como a doca a vê (ou nula, se ela sumiu). */
private fun dockLayerOf(store: EditorStore, layerId: Long): DockLayer? {
    val d = store.detail?.takeIf { it.id == layerId }
    return store.layers.firstOrNull { it.id == layerId }?.let {
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
            text3D = store.text3d != null,
            effectCount = it.effectCount,
        )
    }
}

/**
 * Quantas fileiras de fichas a doca da camada escolhida usa (1 ou 2): a
 * altura da doca é a do conteúdo (`EditorLayout.dock`), não sobra faixa vazia.
 */
internal fun dockTileRowCount(store: EditorStore): Int {
    val id = store.primary ?: return 1
    val l = dockLayerOf(store, id) ?: return 1
    return dockRows(sectionsFor(l).size).size.coerceIn(1, 2)
}

/**
 * No máximo duas fileiras, a de baixo com a metade maior (7 → 3 + 4, 8 → 4 + 4,
 * 6 → 3 + 3): a doca tem sempre a mesma altura, e cada fileira reparte a
 * largura inteira entre as suas fichas.
 */
internal fun dockRows(count: Int): List<Int> =
    if (count <= 4) listOf(count) else listOf(count / 2, count - count / 2)

/**
 * Doca da camada sem painel, compacta: a fileira rápida só de ícones
 * (velocidade · aparar início | dividir | aparar fim | puxar · mudo, só o que
 * se aplica ao tipo) e as fichas do tipo em até duas fileiras baixas.
 * A altura é a do conteúdo (`EditorLayout.dock`), não uma fração da tela:
 * o que sobra fica para a timeline.
 */
@Composable
internal fun LayerToolsDock(store: EditorStore, ui: EditorUi, layerId: Long) {
    val layer by remember(layerId) { derivedStateOf { dockLayerOf(store, layerId) } }
    val l = layer ?: return
    val type = LayerType.of(l.kind)
    val sections = sectionsFor(l)
    val rows = buildList {
        var at = 0
        for (n in dockRows(sections.size)) { add(sections.subList(at, at + n)); at += n }
    }
    // Igual ao Alight Motion: folha de cantos arredondados em cima; fileira
    // rápida com a velocidade e o som em quadrados nas pontas e o bloco
    // aparar início | dividir | aparar fim no meio; fichas grandes embaixo.
    Column(
        Modifier
            .fillMaxSize()
            .padding(top = 4.dp)
            .clip(RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp))
            .background(ShellColors.DockSheet)
            .padding(horizontal = 12.dp),
    ) {
        Spacer(Modifier.height(10.dp))
        Row(
            Modifier.fillMaxWidth().height(EditorLayout.DOCK_QUICK.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (type == LayerType.Group) {
                // As portas do grupo no lugar da velocidade: entrar e desagrupar.
                DockSquare {
                    DockTool(CupertinoGlyph.ArrowDownRightSquare, stringResource(R.string.editor_entrar_grupo), 20) { store.openPrecomp(l.id) }
                }
                DockSquare {
                    DockTool(ShellGlyph.SquareSplit2x2, stringResource(R.string.editor_desagrupar), 20) { store.ungroupPrecomp(l.id) }
                }
            } else {
                // Velocidade sempre no mesmo lugar (como no AM); apagada quando o tipo não tem tempo de mídia.
                val speedOk = type == LayerType.Video || type == LayerType.Audio
                DockSquare {
                    DockTool(
                        CupertinoGlyph.Speedometer, stringResource(R.string.editor_velocidade), 22,
                        tint = if (speedOk) AureaColors.Text else ShellColors.DockDisabled,
                    ) {
                        if (speedOk) openPanel(store, ui, EditorPanel.Speed) else store.toastRes(R.string.dock2_media_only)
                    }
                }
            }
            // O tempo num bloco só, com os colchetes do AM: a parte tracejada é a que sai.
            Row(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(10.dp))
                    .background(ShellColors.DockRow),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                DockTrimTool(TrimGlyph.Start, stringResource(R.string.editor_aparar_inicio_cabecote)) {
                    timeEdit(store, l) { if (!store.trimStart(l.id, store.playhead)) store.toastRes(R.string.timeline_cut_failed) }
                }
                DockDivider()
                DockTrimTool(TrimGlyph.Split, stringResource(R.string.editor_dividir_cabecote)) {
                    timeEdit(store, l) { store.splitAtPlayhead(listOf(l.id)) }
                }
                DockDivider()
                DockTrimTool(TrimGlyph.End, stringResource(R.string.editor_aparar_fim_cabecote)) {
                    timeEdit(store, l) { if (!store.trimEnd(l.id, store.playhead)) store.toastRes(R.string.timeline_cut_failed) }
                }
            }
            // Som: sempre no canto direito; apagado sem áudio. Toque liga/desliga; segurar abre o volume.
            DockSquare {
                DockTool(
                    if (l.muted || !l.hasAudio) CupertinoGlyph.SpeakerSlash else CupertinoGlyph.Speaker2,
                    if (l.muted) stringResource(R.string.editor_som_desligado_toque_ligar_segure_volume) else stringResource(R.string.editor_desligar_som_segure_volume),
                    22,
                    tint = when {
                        !l.hasAudio -> ShellColors.DockDisabled
                        l.muted -> AureaColors.Accent
                        else -> AureaColors.Text
                    },
                    onLongClick = if (l.hasAudio) ({ openPanel(store, ui, EditorPanel.Audio) }) else null,
                ) {
                    if (l.hasAudio) store.setAudioMuted(!l.muted) else store.toastRes(R.string.dock2_media_only)
                }
            }
        }
        rows.forEach { row ->
            Spacer(Modifier.height(10.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                row.forEach { s -> DockTile(s, EditorLayout.DOCK_TILE) {
                    if (s == DockSection.EditText) store.openTextContentEditor()
                    else if (s == DockSection.Rig) RigStage.open(store)
                    else openPanel(store, ui, panelFor(store, s))
                } }
            }
        }
    }
}

/** Os três colchetes da fileira de tempo (desenho próprio, no jeito do AM). */
internal enum class TrimGlyph { Start, Split, End }

@Composable
private fun RowScope.DockTrimTool(kind: TrimGlyph, description: String, onClick: () -> Unit) {
    Column(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .testTag("timeline.cut.${kind.name.lowercase()}")
            .semantics { contentDescription = description }
            .tocavel(haptic = true, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        val color = AureaColors.Text
        androidx.compose.foundation.Canvas(Modifier.size(22.dp)) {
            val u = size.width / 24f
            val stroke = 1.8f * u
            val dashed = androidx.compose.ui.graphics.PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u))
            fun bracket(xOuter: Float, xInner: Float, dash: Boolean) {
                val path = androidx.compose.ui.graphics.Path().apply {
                    moveTo(xOuter * u, 6f * u); lineTo(xInner * u, 6f * u); lineTo(xInner * u, 18f * u); lineTo(xOuter * u, 18f * u)
                }
                drawPath(
                    path, color,
                    style = androidx.compose.ui.graphics.drawscope.Stroke(
                        width = stroke,
                        cap = androidx.compose.ui.graphics.StrokeCap.Round,
                        join = androidx.compose.ui.graphics.StrokeJoin.Round,
                        pathEffect = if (dash) dashed else null,
                    ),
                )
            }
            bracket(3f, 9f, kind == TrimGlyph.Start)
            bracket(21f, 15f, kind == TrimGlyph.End)
            drawLine(
                color, androidx.compose.ui.geometry.Offset(12f * u, 3.5f * u), androidx.compose.ui.geometry.Offset(12f * u, 20.5f * u),
                strokeWidth = stroke, cap = androidx.compose.ui.graphics.StrokeCap.Round,
            )
        }
        Text(stringResource(when(kind) {
            TrimGlyph.Start -> R.string.timeline_cut_left
            TrimGlyph.Split -> R.string.dock_short_split
            TrimGlyph.End -> R.string.timeline_cut_right
        }), fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, color = AureaColors.Text)
    }
}

/** Quadrado da fileira rápida para uma ferramenta sozinha (velocidade, mudo, grupo). */
@Composable
private fun DockSquare(content: @Composable RowScope.() -> Unit) {
    Row(
        Modifier
            .size(EditorLayout.DOCK_QUICK.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(ShellColors.DockRow),
        verticalAlignment = Alignment.CenterVertically,
        content = content,
    )
}

@Composable
private fun DockDivider() {
    Box(Modifier.width(1.dp).height(20.dp).background(AureaColors.Border))
}

/** Ferramenta de tempo: cadeado e cabeçote fora da camada dizem por que não. */
private inline fun timeEdit(store: EditorStore, l: DockLayer, action: () -> Unit) {
    val t = store.playhead
    when {
        l.locked -> store.toastRes(R.string.editor_camada_bloqueada_desbloqueie_editar)
        t <= l.start || t >= l.end -> store.toastRes(R.string.sh_playhead_into_layer)
        else -> {
            if (store.playing) store.pause()
            action()
        }
    }
}

/**
 * O painel da ficha depende da camada: "Editar forma" abre o editor da
 * silhueta (ou o Vetor, na camada vetorial) e "Cor e preenchimento" muda
 * entre forma, vetor e texto.
 */
private fun panelFor(store: EditorStore, s: DockSection): EditorPanel = when (s) {
    DockSection.TextOptions -> if (store.text3d != null) EditorPanel.Element3D else EditorPanel.Text
    DockSection.EditShape -> if (store.isVectorLayer) EditorPanel.Vector else EditorPanel.ShapeEdit
    DockSection.ColorFill -> when (store.detail?.kind) {
        com.aurea.aurea.ui.theme.LayerType.Text.kind -> EditorPanel.Text
        else -> if (store.isVectorLayer) EditorPanel.Vector else EditorPanel.Shape
    }
    else -> s.panel
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
    // Só o ícone, como na fileira de tempo do editor antigo; a descrição
    // completa fica no leitor de tela.
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
    val label = stringResource(section.label)
    Column(
        Modifier
            .weight(1f)
            .height(height.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(ShellColors.DockTile)
            .semantics { contentDescription = label }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 6.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        // Como no AM: ícone claro em cima e o nome cinza, em até duas linhas
        // (4 + 26 + 6 + 2 × 13 + 4 cabe nos 72 da ficha).
        val iconSize = 26.dp
        if (section == DockSection.Move) DockVector(Icons.Rounded.OpenWith, iconSize)
        else CupertinoIcon(section.glyph, iconSize, ShellColors.DockTileIcon)
        Spacer(Modifier.height(6.dp))
        Text(
            label,
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(
                TextStyle(
                    fontSize = 11.5.sp,
                    lineHeight = 1.15.em,
                    fontWeight = FontWeight.W400,
                    color = ShellColors.DockTileContent,
                ),
            ),
        )
    }
}

@Composable
private fun DockVector(icon: ImageVector, size: androidx.compose.ui.unit.Dp) {
    Icon(icon, contentDescription = null, tint = ShellColors.DockTileIcon, modifier = Modifier.size(size))
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
    val intoLayers = stringResource(R.string.editor_leve_cabecote_dentro_camadas)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 10.dp)) {
        Spacer(Modifier.height(4.dp))
        Row(
            Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_aparar_inicio_cabecote)) { batchTrim(store, start = true) }
            BatchTool(CupertinoGlyph.Scissors, stringResource(R.string.editor_dividir_cabecote)) {
                val t = store.playhead
                val covered = store.layers.any { it.id in store.selection && !it.locked && t > it.startFrame && t < it.endFrame }
                if (covered) {
                    if (store.playing) store.pause()
                    store.splitAtPlayhead(store.layers.filter { it.id in store.selection && !it.locked }.map { it.id })
                } else {
                    store.showToast(intoLayers)
                }
            }
            BatchTool(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_aparar_fim_cabecote)) { batchTrim(store, start = false) }
            Box(Modifier.width(1.dp).height(24.dp).background(AureaColors.Border))
            BatchTool(Icons.AutoMirrored.Rounded.FormatAlignLeft, stringResource(R.string.editor_alinhar_inicios)) { store.arrangeLayerTimes(0) }
            BatchTool(Icons.Rounded.Stairs, stringResource(R.string.editor_escada_comeca_quando_cima_termina)) { store.arrangeLayerTimes(1) }
            BatchTool(Icons.AutoMirrored.Rounded.FormatAlignRight, stringResource(R.string.editor_alinhar_fins)) { store.arrangeLayerTimes(2) }
        }
        Spacer(Modifier.height(8.dp))
        TimelineArrangementRow(store)
        Spacer(Modifier.height(8.dp))
        Row(
            Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BatchTool(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_alinhar_esquerda_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Left) }
            BatchTool(CupertinoGlyph.ArrowLeftRight, stringResource(R.string.editor_centralizar_horizontal), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterH) }
            BatchTool(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_alinhar_direita_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Right) }
            BatchTool(CupertinoGlyph.ArrowUpToLine, stringResource(R.string.editor_alinhar_topo_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Top) }
            BatchTool(CupertinoGlyph.ArrowUpArrowDown, stringResource(R.string.editor_centralizar_vertical), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.CenterV) }
            BatchTool(CupertinoGlyph.ArrowDownToLine, stringResource(R.string.editor_alinhar_base_tela), 18) { LayerOps.align(store, store.selection, LayerOps.Edge.Bottom) }
            val three = count >= 3
            BatchTool(CupertinoGlyph.ArrowLeftRightSquare, stringResource(R.string.editor_distribuir_horizontal_vaos_iguais), 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = true)
            }
            BatchTool(CupertinoGlyph.ArrowUpDownSquare, stringResource(R.string.editor_distribuir_vertical_vaos_iguais), 18, enabled = three) {
                LayerOps.distribute(store, store.selection, horizontal = false)
            }
            // Ajustar / preencher a tela (app antigo), junto dos alinhamentos.
            BatchTool(CupertinoGlyph.FullscreenExit, stringResource(R.string.editor_ajustar_tela), 18) { LayerOps.fitToCanvas(store, store.selection, fill = false) }
            BatchTool(CupertinoGlyph.Fullscreen, stringResource(R.string.editor_preencher_tela), 18) { LayerOps.fitToCanvas(store, store.selection, fill = true) }
        }
        Spacer(Modifier.height(8.dp))
        StaggerRow(store)
    }
}

/**
 * "Escalonar": − N + quadros e dois toques que aplicam — as camadas inteiras
 * ou só os keyframes — em cascata na ordem da timeline (a de cima fica; com
 * passo negativo, a de baixo). Tipografia/AMV: palavra por palavra em 3 q.
 */
@Composable
private fun StaggerRow(store: EditorStore) {
    var step by androidx.compose.runtime.saveable.rememberSaveable { androidx.compose.runtime.mutableIntStateOf(com.aurea.aurea.editor.timeline.Stagger.DEFAULT) }
    val label = stringResource(R.string.editor_escalonar)
    Row(
        Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow)
            .testTag("stagger_row"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            modifier = Modifier.padding(start = 12.dp, end = 4.dp),
            maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        )
        BatchTool(CupertinoGlyph.Minus, stringResource(R.string.editor_escalonar_menos), 16) {
            step = com.aurea.aurea.editor.timeline.Stagger.step(step, -1)
        }
        Text(
            stringResource(R.string.editor_escalonar_quadros, step),
            modifier = Modifier.width(52.dp).testTag("stagger_step"),
            textAlign = TextAlign.Center,
            maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)),
        )
        BatchTool(CupertinoGlyph.Plus, stringResource(R.string.editor_escalonar_mais), 16) {
            step = com.aurea.aurea.editor.timeline.Stagger.step(step, 1)
        }
        Box(Modifier.width(1.dp).height(24.dp).background(AureaColors.Border))
        StaggerApply(stringResource(R.string.editor_escalonar_camadas), "stagger_layers") { store.staggerSelection(step, keysOnly = false) }
        StaggerApply(stringResource(R.string.editor_escalonar_keyframes), "stagger_keys") { store.staggerSelection(step, keysOnly = true) }
    }
}

@Composable
private fun RowScope.StaggerApply(text: String, tag: String, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1.4f)
            .fillMaxHeight()
            .testTag(tag)
            .semantics { contentDescription = text }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
    }
}

/** Aparar as escolhidas que cobrem o cabeçote (as bloqueadas ficam), num passo só. */
private fun batchTrim(store: EditorStore, start: Boolean) {
    val t = store.playhead
    val rows = store.layers.filter { it.id in store.selection }
    val targets = rows.filter { !it.locked && t > it.startFrame && t < it.endFrame }
    if (targets.isEmpty()) {
        store.showToast(
            store.appText(
                if (rows.isNotEmpty() && rows.all { it.locked }) R.string.sh_layers_locked_unlock_to_edit
                else R.string.editor_leve_cabecote_dentro_camadas,
            ),
        )
        return
    }
    if (store.playing) store.pause()
    store.beginGesture(if (start) "aparar início" else "aparar fim")
    targets.forEach { if (start) store.trimStart(it.id, t) else store.trimEnd(it.id, t) }
    store.endGesture()
}

/** Toast fora do Compose: no idioma escolhido no app (ver [AppText]). */
private fun EditorStore.appText(@StringRes id: Int): String = AppText.get(getApplication<Application>(), id)

private fun EditorStore.toastRes(@StringRes id: Int) = showToast(appText(id))

/** Visible labels and scroll preserve 48 dp targets on narrow phones. */
@Composable
private fun TimelineArrangementRow(store: EditorStore) {
    val unlocked = store.layers.count { it.id in store.selection && !it.locked }
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(ShellColors.DockRow)
            .horizontalScroll(rememberScrollState()),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        listOf(
            3 to R.string.timeline_distribute_starts,
            4 to R.string.timeline_distribute_gaps,
            5 to R.string.timeline_starts_at_playhead,
            6 to R.string.timeline_ends_at_playhead,
        ).forEach { (mode, title) ->
            val enabled = unlocked >= if (mode <= 4) 3 else 1
            val label = stringResource(title)
            Box(
                Modifier.height(48.dp).testTag("timeline.arrange.$mode")
                    .tocavel(enabled = enabled, haptic = true) { store.arrangeLayerTimes(mode) }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, fontSize = 12.sp, maxLines = 1,
                    color = if (enabled) AureaColors.Accent else AureaColors.Disabled)
            }
        }
    }
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
