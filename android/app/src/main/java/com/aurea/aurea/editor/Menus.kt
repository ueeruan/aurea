package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaNamePrompt
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
// ⋮ da camada — `menuDaCamada` (folha)
// =============================================================================

/**
 * Tudo o que se faz com UMA camada, com rótulo (A.01): camada, etiqueta,
 * recorte e grupo, na composição, mídia, tempo e o resto. O que o motor
 * novo já faz está ligado; o que ele não faz não aparece.
 */
@Composable
internal fun LayerMenuSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val id = store.primary
    val index = store.layers.indexOfFirst { it.id == id }
    val row = store.layers.getOrNull(index)
    if (id == null || row == null) {
        onDismissLater(onDismiss)
        return
    }
    val type = LayerType.of(row.kind)
    val visual = type != LayerType.Audio
    val media = type == LayerType.Video || type == LayerType.Image
    val t = store.playhead
    val inside = t > row.startFrame && t < row.endFrame
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    fun timeAct(block: () -> Unit): () -> Unit = act {
        if (row.locked) store.showToast("Camada bloqueada: desbloqueie para editar") else block()
    }

    ShellMenuSheet(onDismiss) {
        MenuSection("Camada")
        // Bloqueada não se renomeia: o cadeado fecha a edição inteira.
        MenuItemRow(CupertinoGlyph.Pencil, "Renomear", if (row.locked) null else act { ui.sheet = ShellSheet.RenameLayer })
        // O cadeado não fecha a folha: o rótulo troca na hora.
        MenuItemRow(
            if (row.locked) ShellGlyph.LockOpenFill else CupertinoGlyph.LockFill,
            if (row.locked) "Desbloquear camada" else "Bloquear camada",
            { store.setLayerLocked(id, !row.locked) },
            detail = if (row.locked) "Volta a aceitar movimento e edição" else "Não aceita movimento, corte nem edição",
        )
        MenuItemRow(
            if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye,
            if (row.visible) "Ocultar camada" else "Mostrar camada",
            { store.setLayerVisible(id, !row.visible) },
        )
        // Solo, ajuste e guia não fecham a folha: o visto troca na hora.
        MenuItemRow(
            CupertinoGlyph.Speaker2,
            "Solo",
            { store.setLayerSolo(id, !row.solo) },
            checked = row.solo,
            detail = "Com alguma camada em solo, a prévia e o som só tocam as que estão",
        )
        if (visual) {
            MenuItemRow(
                CupertinoGlyph.SliderHorizontal3,
                "Camada de ajuste",
                { store.setLayerAdjustment(id, !row.adjustment) },
                checked = row.adjustment,
                detail = "Os efeitos desta camada valem para todas as de baixo",
            )
            MenuItemRow(
                CupertinoGlyph.Grid,
                "Guia (não exporta)",
                { store.setLayerGuide(id, !row.guide) },
                checked = row.guide,
                detail = "Aparece aqui no editor e fica fora do vídeo exportado",
            )
        }
        MenuItemRow(CupertinoGlyph.PlusSquareOnSquare, "Duplicar", act { store.duplicateLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnDoc, "Copiar camada", act { store.copyLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnClipboard, "Colar camada no cabeçote", if (store.clipboard and 1 != 0) act { store.pasteLayers() } else null)
        MenuItemRow(CupertinoGlyph.Paintbrush, "Copiar estilo", act { store.select(id); store.copyStyle() })
        MenuItemRow(ShellGlyph.PaintbrushFill, "Colar estilo", if (store.clipboard and 2 != 0) act { store.pasteStyle(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowUpToLine, "Trazer para a frente", if (index > 0) act { store.reorderLayer(id, index - 1) } else null)
        MenuItemRow(
            CupertinoGlyph.ArrowDownToLine,
            "Enviar para trás",
            if (index < store.layers.size - 1) act { store.reorderLayer(id, index + 1) } else null,
        )

        MenuSection("Etiqueta")
        LabelRow(row.label) { store.setLayerLabel(id, it) }

        if (visual) {
            MenuSection("Grupo")
            if (type != LayerType.Group) {
                MenuItemRow(CupertinoGlyph.RectangleStack, "Converter em grupo", act { store.precompose(listOf(id)) })
            } else {
                MenuItemRow(CupertinoGlyph.ArrowDownRightSquare, "Editar o grupo", act { store.openPrecomp(id) })
                MenuItemRow(ShellGlyph.SquareSplit2x2, "Desagrupar", act { store.ungroupPrecomp(id) })
            }
        }
        if (type == LayerType.Video || type == LayerType.Audio) {
            MenuSection("Mídia")
            if (type == LayerType.Video) {
                MenuItemRow(
                    CupertinoGlyph.MusicNote2,
                    "Extrair o áudio",
                    act { store.extractAudio(id) },
                    detail = "O som vira uma camada própria e o vídeo fica mudo",
                )
            }
            if (type == LayerType.Audio || type == LayerType.Video) {
                MenuItemRow(CupertinoGlyph.Speaker2, "Volume", act { openPanel(store, ui, EditorPanel.Audio) })
            }
        }

        MenuSection("Movimento")
        MenuItemRow(
            CupertinoGlyph.Speedometer,
            "Desfoque de movimento",
            act { store.setLayerMotionBlur(id, !(store.detail?.motionBlur ?: false)) },
            checked = store.detail?.motionBlur == true,
            detail = "Borra na direção do movimento (obturador nas configurações do projeto)",
        )
        if (type == LayerType.Video) {
            MenuItemRow(
                CupertinoGlyph.Speedometer,
                "Desfoque do movimento do vídeo",
                act { store.setVectorBlur(id, !(store.detail?.vectorBlur ?: false)) },
                checked = store.detail?.vectorBlur == true,
                detail = "Borra o que se mexe dentro do vídeo (pelos vetores de movimento)",
            )
        }

        MenuSection("Tempo")
        MenuItemRow(CupertinoGlyph.ArrowRightToLine, "Aparar o início no cabeçote", if (inside) timeAct { store.trimStart(id, t) } else null)
        MenuItemRow(CupertinoGlyph.Scissors, "Dividir no cabeçote", if (inside) timeAct { store.splitAtPlayhead(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowLeftToLine, "Aparar o fim no cabeçote", if (inside) timeAct { store.trimEnd(id, t) } else null)
        if (type == LayerType.Video || type == LayerType.Audio) {
            MenuItemRow(CupertinoGlyph.Speedometer, "Velocidade e remapear o tempo", act { openPanel(store, ui, EditorPanel.Speed) })
        }
        if (type == LayerType.Video) {
            MenuItemRow(ShellGlyph.Snow, "Congelar quadro", if (inside) act { store.freezeFrame(id) } else null)
            MenuSection("Rastreio")
            MenuItemRow(ShellGlyph.Viewfinder, "Rastrear um ponto", act { store.select(id); store.beginPointPick(false) },
                detail = "Cria um Nulo que segue o ponto — ligue outras camadas a ele")
            MenuItemRow(ShellGlyph.Viewfinder, "Estabilizar pelo ponto", act { store.select(id); store.beginPointPick(true) },
                detail = "Move o vídeo para o ponto ficar parado na tela")
        }

        MenuSection("Mais")
        MenuItemRow(CupertinoGlyph.Trash, "Excluir camada", act { LayerOps.delete(store, listOf(id)) }, danger = true)
    }
}

/**
 * A linha de etiquetas: "sem" e as doze cores (círculos 24 num alvo de 34).
 * `current` é a etiqueta da camada (0 = nenhuma, i = `LabelPalette[i - 1]`),
 * marcada com o anel de destaque.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LabelRow(current: Int, onPick: (Int) -> Unit) {
    FlowRow(
        Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Box(
            Modifier.size(34.dp)
                .then(if (current == 0) Modifier.border(2.dp, AureaColors.Accent, CircleShape) else Modifier)
                .tocavel(haptic = true, onClick = { onPick(0) }),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier.size(24.dp).border(1.5.dp, AureaColors.Muted, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(ShellGlyph.Nosign, 14.dp, AureaColors.Muted)
            }
        }
        ShellColors.LabelPalette.forEachIndexed { i, color ->
            Box(
                Modifier.size(34.dp)
                    .then(if (current == i + 1) Modifier.border(2.dp, AureaColors.Accent, CircleShape) else Modifier)
                    .tocavel(haptic = true, onClick = { onPick(i + 1) }),
                contentAlignment = Alignment.Center,
            ) {
                Box(Modifier.size(24.dp).background(color, CircleShape))
            }
        }
    }
}

/**
 * Busca de camadas: nome ou texto, sem diferença de maiúscula nem de acento
 * (a busca é do motor, `search_layers`). Tocar num resultado seleciona a
 * camada e fecha a folha.
 */
@Composable
internal fun SearchLayersSheet(store: EditorStore, onDismiss: () -> Unit) {
    var query by remember { mutableStateOf("") }
    val focus = remember { FocusRequester() }
    val hits = remember(query, store.layers) { store.searchLayers(query) }
    ShellMenuSheet(onDismiss, maxHeightFraction = 0.7f) {
        MenuSection("Buscar camadas")
        Box(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)
                .heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        ) {
            BasicTextField(
                value = query,
                onValueChange = { query = it },
                singleLine = true,
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth().focusRequester(focus),
            )
            if (query.isEmpty()) Text("Nome ou texto da camada", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
        }
        LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
        if (query.isNotBlank() && hits.isEmpty()) {
            Text(
                "Nenhuma camada com \"${query.trim()}\"",
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 12.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
        }
        hits.forEach { id ->
            val row = store.layers.firstOrNull { it.id == id } ?: return@forEach
            Row(
                Modifier.fillMaxWidth().height(44.dp)
                    .tocavel(onClick = { store.select(id); onDismiss() })
                    .padding(horizontal = 20.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val label = row.label
                val dot = if (label in 1..ShellColors.LabelPalette.size) ShellColors.LabelPalette[label - 1] else AureaColors.Muted
                Box(Modifier.size(10.dp).background(dot, CircleShape))
                Spacer(Modifier.width(12.dp))
                Text(
                    row.name.ifBlank { "Camada" },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = if (row.selected) AureaColors.Accent else AureaColors.Text)),
                )
            }
        }
    }
}

/** Fecha a folha fora da composição (a camada sumiu enquanto ela abria). */
@Composable
private fun onDismissLater(onDismiss: () -> Unit) {
    androidx.compose.runtime.LaunchedEffect(Unit) { onDismiss() }
}

// =============================================================================
// ⋮ da linha do tempo — `menuDaTimeline` (folha)
// =============================================================================

/**
 * O que vale para o projeto inteiro ou para a linha do tempo: seleção,
 * reprodução, modo de prévia, miniatura, marcas, cronômetro, agrupar e guia.
 */
@Composable
internal fun TimelineMenuSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val count = store.layers.size
    val now = ShellTime.short(store.playhead, store.project.fps)
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    ShellMenuSheet(onDismiss, maxHeightFraction = 0.78f) {
        MenuSection("Seleção")
        MenuItemRow(CupertinoGlyph.CheckmarkSquare, "Selecionar todas as camadas", if (count >= 2) act { store.selectAll() } else null)
        MenuItemRow(CupertinoGlyph.Square, "Limpar seleção", act { store.clearSelection() })
        MenuItemRow(CupertinoGlyph.Search, "Buscar camadas…", if (count > 0) act { ui.sheet = ShellSheet.SearchLayers } else null)

        MenuSection("Reprodução e prévia")
        MenuItemRow(CupertinoGlyph.Repeat, "Reprodução em loop", act { store.setLoop(!store.looping) }, checked = store.looping)
        MenuItemRow(
            CupertinoGlyph.Fullscreen,
            if (ui.fullscreen) "Sair da tela cheia" else "Tela cheia",
            act { ui.fullscreen = !ui.fullscreen },
        )
        MenuItemRow(
            CupertinoGlyph.Speedometer,
            "Desfoque de movimento da composição",
            { store.setCompositionMotionBlur(!store.compMotionBlur) },
            checked = store.compMotionBlur,
            detail = "As camadas com desfoque de movimento só borram com isto ligado",
        )
        if (store.compMotionBlur) {
            val shutter = store.shutterAngle.roundToInt()
            MenuItemRow(
                CupertinoGlyph.CircleLefthalfFill,
                "Obturador: $shutter°",
                {
                    val next = ShutterOptions.firstOrNull { it > shutter } ?: ShutterOptions.first()
                    store.changeShutterAngle(next.toFloat())
                },
                detail = "Toque para trocar (90°, 180°, 270°, 360°): maior = rastro mais longo",
            )
        }
        MenuItemRow(
            ShellGlyph.WaveformPathEcg,
            "Diagnóstico na tela",
            { store.toggleHud() },
            checked = store.hudVisible,
            detail = "Quadros por segundo, tempos da GPU, memória e o decodificador",
        )

        MenuSection("Edição")
        MenuItemRow(
            CupertinoGlyph.Link,
            "Timeline magnética (modo Edição)",
            act { store.toggleEditMode() },
            checked = store.editMode,
            detail = "Aparar empurra as camadas seguintes e excluir fecha o espaço",
        )
        MenuItemRow(ShellGlyph.ScissorsAlt, "Remover espaços vazios", act { store.removeGaps() })

        MenuSection("Projeto")
        MenuItemRow(
            ShellGlyph.ScissorsAlt,
            "Aparar o projeto no cabeçote",
            if (store.playhead > 0) act { store.trimProjectAtPlayhead() } else null,
            detail = "Corta tudo o que passa de $now",
        )

        MenuSection("Marcas e ritmo")
        MenuItemRow(CupertinoGlyph.Bookmark, "Marcar (ou desmarcar) este instante", act { store.toggleMarker() })
        MenuItemRow(ShellGlyph.BookmarkSolid, "Ir para a próxima marca", if (store.markers.size > 0) act { store.seekToNextMarker() } else null)
        MenuItemRow(CupertinoGlyph.MusicNote2, "Detectar batidas da camada escolhida", act { store.detectBeats() })

        MenuSection("Mais")
        MenuItemRow(
            CupertinoGlyph.RectangleStack, "Agrupar as camadas escolhidas",
            if (store.selection.isNotEmpty()) act { store.precompose() } else null,
        )
    }
}

private val ShutterOptions = listOf(90, 180, 270, 360)
