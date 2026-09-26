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
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
    val lockedMsg = stringResource(R.string.editor_camada_bloqueada_desbloqueie_editar)
    fun timeAct(block: () -> Unit): () -> Unit = act {
        if (row.locked) store.showToast(lockedMsg) else block()
    }

    ShellMenuSheet(onDismiss) {
        MenuSection(stringResource(R.string.editor_camada))
        // Bloqueada não se renomeia: o cadeado fecha a edição inteira.
        MenuItemRow(CupertinoGlyph.Pencil, stringResource(R.string.editor_renomear), if (row.locked) null else act { ui.sheet = ShellSheet.RenameLayer })
        // O cadeado não fecha a folha: o rótulo troca na hora.
        MenuItemRow(
            if (row.locked) ShellGlyph.LockOpenFill else CupertinoGlyph.LockFill,
            if (row.locked) stringResource(R.string.editor_desbloquear_camada) else stringResource(R.string.editor_bloquear_camada),
            { store.setLayerLocked(id, !row.locked) },
            detail = if (row.locked) stringResource(R.string.editor_volta_aceitar_movimento_edicao) else stringResource(R.string.editor_nao_aceita_movimento_corte_nem_edicao),
        )
        MenuItemRow(
            if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye,
            if (row.visible) stringResource(R.string.editor_ocultar_camada) else stringResource(R.string.editor_mostrar_camada),
            { store.setLayerVisible(id, !row.visible) },
        )
        // Solo, ajuste e guia não fecham a folha: o visto troca na hora.
        MenuItemRow(
            CupertinoGlyph.Speaker2,
            stringResource(R.string.editor_solo),
            { store.setLayerSolo(id, !row.solo) },
            checked = row.solo,
            detail = stringResource(R.string.editor_alguma_camada_solo_previa_som_so),
        )
        if (visual) {
            MenuItemRow(
                CupertinoGlyph.SliderHorizontal3,
                stringResource(R.string.editor_camada_ajuste),
                { store.setLayerAdjustment(id, !row.adjustment) },
                checked = row.adjustment,
                detail = stringResource(R.string.editor_efeitos_desta_camada_valem_todas_baixo),
            )
            MenuItemRow(
                CupertinoGlyph.Grid,
                stringResource(R.string.editor_guia_nao_exporta),
                { store.setLayerGuide(id, !row.guide) },
                checked = row.guide,
                detail = stringResource(R.string.editor_aparece_aqui_editor_fica_fora_video),
            )
        }
        MenuItemRow(CupertinoGlyph.PlusSquareOnSquare, stringResource(R.string.editor_duplicar), act { store.duplicateLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnDoc, stringResource(R.string.editor_copiar_camada), act { store.copyLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnClipboard, stringResource(R.string.editor_colar_camada_cabecote), if (store.clipboard and 1 != 0) act { store.pasteLayers() } else null)
        MenuItemRow(CupertinoGlyph.Paintbrush, stringResource(R.string.editor_copiar_estilo), act { store.select(id); store.copyStyle() })
        MenuItemRow(ShellGlyph.PaintbrushFill, stringResource(R.string.editor_colar_estilo), if (store.clipboard and 2 != 0) act { store.pasteStyle(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowUpToLine, stringResource(R.string.editor_trazer_frente), if (index > 0) act { store.reorderLayer(id, index - 1) } else null)
        MenuItemRow(
            CupertinoGlyph.ArrowDownToLine,
            stringResource(R.string.editor_enviar_tras),
            if (index < store.layers.size - 1) act { store.reorderLayer(id, index + 1) } else null,
        )

        MenuSection(stringResource(R.string.editor_etiqueta))
        LabelRow(row.label) { store.setLayerLabel(id, it) }

        if (visual) {
            MenuSection(stringResource(R.string.editor_grupo))
            if (type != LayerType.Group) {
                MenuItemRow(CupertinoGlyph.RectangleStack, stringResource(R.string.editor_converter_grupo), act { store.precompose(listOf(id)) })
            } else {
                MenuItemRow(CupertinoGlyph.ArrowDownRightSquare, stringResource(R.string.editor_editar_grupo), act { store.openPrecomp(id) })
                MenuItemRow(ShellGlyph.SquareSplit2x2, stringResource(R.string.editor_desagrupar), act { store.ungroupPrecomp(id) })
            }
        }
        if (type == LayerType.Video || type == LayerType.Audio) {
            MenuSection(stringResource(R.string.sh_menu_media))
            if (type == LayerType.Video) {
                MenuItemRow(
                    CupertinoGlyph.MusicNote2,
                    stringResource(R.string.editor_extrair_audio),
                    act { store.extractAudio(id) },
                    detail = stringResource(R.string.editor_som_vira_camada_propria_video_fica),
                )
            }
            if (type == LayerType.Audio || type == LayerType.Video) {
                MenuItemRow(CupertinoGlyph.Speaker2, stringResource(R.string.sh_menu_volume), act { openPanel(store, ui, EditorPanel.Audio) })
            }
        }

        MenuSection(stringResource(R.string.editor_movimento))
        MenuItemRow(
            CupertinoGlyph.Speedometer,
            stringResource(R.string.editor_desfoque_movimento),
            act { store.setLayerMotionBlur(id, !(store.detail?.motionBlur ?: false)) },
            checked = store.detail?.motionBlur == true,
            detail = stringResource(R.string.editor_borra_direcao_movimento_obturador_nas_configuracoes),
        )
        if (type == LayerType.Video) {
            MenuItemRow(
                CupertinoGlyph.Speedometer,
                stringResource(R.string.editor_desfoque_movimento_video),
                act { store.setVectorBlur(id, !(store.detail?.vectorBlur ?: false)) },
                checked = store.detail?.vectorBlur == true,
                detail = stringResource(R.string.editor_borra_mexe_dentro_video_pelos_vetores),
            )
        }

        MenuSection(stringResource(R.string.editor_tempo))
        MenuItemRow(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.editor_aparar_inicio_cabecote), if (inside) timeAct { store.trimStart(id, t) } else null)
        MenuItemRow(CupertinoGlyph.Scissors, stringResource(R.string.editor_dividir_cabecote), if (inside) timeAct { store.splitAtPlayhead(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowLeftToLine, stringResource(R.string.editor_aparar_fim_cabecote), if (inside) timeAct { store.trimEnd(id, t) } else null)
        if (type == LayerType.Video || type == LayerType.Audio) {
            MenuItemRow(CupertinoGlyph.Speedometer, stringResource(R.string.sh_menu_speed_remap), act { openPanel(store, ui, EditorPanel.Speed) })
            MenuItemRow(CupertinoGlyph.Scissors, "Slip · Roll · Slide", act { openPanel(store, ui, EditorPanel.ClipEdit) })
        }
        if (type == LayerType.Video) {
            MenuItemRow(ShellGlyph.Snow, stringResource(R.string.sh_menu_freeze_frame), if (inside) act { store.freezeFrame(id) } else null)
            MenuSection(stringResource(R.string.editor_rastreio))
            MenuItemRow(ShellGlyph.Viewfinder, stringResource(R.string.editor_rastrear_ponto), act { store.select(id); store.beginPointPick(false) },
                detail = stringResource(R.string.editor_cria_nulo_segue_ponto_ligue_outras))
            MenuItemRow(ShellGlyph.Viewfinder, stringResource(R.string.editor_estabilizar_pelo_ponto), act { store.select(id); store.beginPointPick(true) },
                detail = stringResource(R.string.editor_move_video_ponto_ficar_parado_tela))
        }

        MenuSection(stringResource(R.string.editor_mais))
        MenuItemRow(CupertinoGlyph.Trash, stringResource(R.string.editor_excluir_camada), act { LayerOps.delete(store, listOf(id)) }, danger = true)
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
        MenuSection(stringResource(R.string.sh_menu_search_layers))
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
            if (query.isEmpty()) Text(stringResource(R.string.editor_nome_ou_texto_camada), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
        }
        LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
        if (query.isNotBlank() && hits.isEmpty()) {
            Text(
                stringResource(R.string.sh_menu_no_layer_matches, query.trim()),
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
                    row.name.ifBlank { stringResource(R.string.editor_camada) },
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
        MenuSection(stringResource(R.string.sh_menu_selection))
        MenuItemRow(CupertinoGlyph.CheckmarkSquare, stringResource(R.string.editor_selecionar_todas_camadas), if (count >= 2) act { store.selectAll() } else null)
        MenuItemRow(CupertinoGlyph.Square, stringResource(R.string.editor_limpar_selecao), act { store.clearSelection() })
        MenuItemRow(CupertinoGlyph.Search, stringResource(R.string.editor_buscar_camadas), if (count > 0) act { ui.sheet = ShellSheet.SearchLayers } else null)

        MenuSection(stringResource(R.string.editor_reproducao_previa))
        MenuItemRow(CupertinoGlyph.Repeat, stringResource(R.string.editor_reproducao_loop), act { store.setLoop(!store.looping) }, checked = store.looping)
        MenuItemRow(
            CupertinoGlyph.Fullscreen,
            if (ui.fullscreen) stringResource(R.string.editor_sair_tela_cheia) else stringResource(R.string.editor_tela_cheia),
            act { ui.fullscreen = !ui.fullscreen },
        )
        MenuItemRow(
            CupertinoGlyph.Speedometer,
            stringResource(R.string.editor_desfoque_movimento_composicao),
            { store.setCompositionMotionBlur(!store.compMotionBlur) },
            checked = store.compMotionBlur,
            detail = stringResource(R.string.editor_camadas_desfoque_movimento_so_borram_isto),
        )
        if (store.compMotionBlur) {
            val shutter = store.shutterAngle.roundToInt()
            MenuItemRow(
                CupertinoGlyph.CircleLefthalfFill,
                stringResource(R.string.sh_menu_shutter, shutter),
                {
                    val next = ShutterOptions.firstOrNull { it > shutter } ?: ShutterOptions.first()
                    store.changeShutterAngle(next.toFloat())
                },
                detail = stringResource(R.string.editor_toque_trocar_90_180_270_360),
            )
        }
        MenuItemRow(
            ShellGlyph.WaveformPathEcg,
            stringResource(R.string.editor_diagnostico_tela),
            { store.toggleHud() },
            checked = store.hudVisible,
            detail = stringResource(R.string.editor_quadros_segundo_tempos_gpu_memoria_decodificador),
        )

        MenuSection(stringResource(R.string.editor_edicao))
        MenuItemRow(
            CupertinoGlyph.Link,
            stringResource(R.string.editor_timeline_magnetica_modo_edicao),
            act { store.toggleEditMode() },
            checked = store.editMode,
            detail = stringResource(R.string.editor_aparar_empurra_camadas_seguintes_excluir_fecha),
        )
        MenuItemRow(ShellGlyph.ScissorsAlt, stringResource(R.string.editor_remover_espacos_vazios), act { store.removeGaps() })

        MenuSection(stringResource(R.string.editor_projeto_cbe9))
        MenuItemRow(
            ShellGlyph.ScissorsAlt,
            stringResource(R.string.editor_aparar_projeto_cabecote),
            if (store.playhead > 0) act { store.trimProjectAtPlayhead() } else null,
            detail = stringResource(R.string.sh_menu_cut_after, now),
        )

        MenuSection(stringResource(R.string.editor_marcas_ritmo))
        MenuItemRow(CupertinoGlyph.Bookmark, stringResource(R.string.editor_marcar_ou_desmarcar_este_instante), act { store.toggleMarker() })
        MenuItemRow(ShellGlyph.BookmarkSolid, stringResource(R.string.editor_ir_proxima_marca), if (store.markers.size > 0) act { store.seekToNextMarker() } else null)
        MenuItemRow(CupertinoGlyph.MusicNote2, stringResource(R.string.editor_detectar_batidas_camada_escolhida), act { store.detectBeats() })

        MenuSection(stringResource(R.string.editor_mais))
        MenuItemRow(
            CupertinoGlyph.RectangleStack, stringResource(R.string.editor_agrupar_camadas_escolhidas),
            if (store.selection.isNotEmpty()) act { store.precompose() } else null,
        )
    }
}

private val ShutterOptions = listOf(90, 180, 270, 360)
