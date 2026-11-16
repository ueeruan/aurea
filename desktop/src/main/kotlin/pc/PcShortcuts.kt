package com.aurea.pc

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isAltPressed
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.isShiftPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.timeline.TimelineZoomRequests
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel

/**
 * Atalhos de teclado do Aurea PC, no padrão do After Effects e do Premiere Pro.
 *
 * Só valem no editor, e só para teclas que ninguém tratou antes: um campo de
 * texto focado consome o que digita (o atalho chega depois, por bolha).
 * Onde os dois programas divergem, ficou o do AE para animação (J/K entre
 * keyframes, [ ] e Alt+[ ] nas bordas da camada, B/N área de trabalho) e o do
 * Premiere para corte (Ctrl+K, Q/W, M, setas ↑/↓ entre pontos de edição).
 */
object PcShortcuts {
    /** A lista de atalhos (F1) aberta. */
    var helpOpen by mutableStateOf(false)

    fun handle(e: KeyEvent, store: EditorStore): Boolean {
        // Cena 3D aberta com o ponteiro na viewport: as teclas são as do Blender (PcScene3DKeys.kt).
        if (com.aurea.aurea.editor.PcScene3DKeys.handle(e, store)) return true
        if (e.type != KeyEventType.KeyDown) return false
        val ctrl = e.isCtrlPressed
        val shift = e.isShiftPressed
        val alt = e.isAltPressed
        if (e.key == Key.F1 || (ctrl && e.key == Key.Slash)) { helpOpen = !helpOpen; return true }
        if (store.screen != Screen.Editor || store.textEditLayer != null) return false
        val layer = store.primary
        // Janelinha de Curvas clicada por último: Ctrl+C copia a curva, Ctrl+V cola (Shift: na propriedade inteira).
        if (ctrl && !alt && com.aurea.aurea.editor.PcCurvePopup.open && com.aurea.aurea.editor.PcCurvePopup.hot) {
            if (e.key == Key.C && !shift) { com.aurea.aurea.editor.PcCurveClipboard.copyShown(store); return true }
            if (e.key == Key.V) { com.aurea.aurea.editor.PcCurveClipboard.paste(store, wholeProperty = shift); return true }
        }
        when {
            // AE: Shift+F3 = editor de gráficos (a janelinha de Curvas).
            e.key == Key.F3 && shift && !ctrl -> com.aurea.aurea.editor.PcCurvePopup.let {
                if (it.open) it.close() else com.aurea.aurea.editor.pcOpenCurveNear(store)
            }
            // --- Reprodução -----------------------------------------------------------
            e.key == Key.Spacebar && !ctrl -> store.togglePlayback()
            e.key == Key.DirectionLeft && !ctrl && !alt -> store.step(if (shift) -10 else -1)
            e.key == Key.DirectionRight && !ctrl && !alt -> store.step(if (shift) 10 else 1)
            e.key == Key.PageUp -> store.step(if (shift) -10 else -1)                // AE
            e.key == Key.PageDown -> store.step(if (shift) 10 else 1)                // AE
            e.key == Key.MoveHome -> store.seek(0)
            e.key == Key.MoveEnd -> store.seek((store.project.durationFrames - 1).coerceAtLeast(0))
            e.key == Key.DirectionUp && !ctrl -> store.stepTransportEdges(-1)          // Premiere
            e.key == Key.DirectionDown && !ctrl -> store.stepTransportEdges(1)         // Premiere
            e.key == Key.J && !ctrl -> store.stepToKeyframe(-1)                       // AE
            e.key == Key.K && !ctrl -> store.stepToKeyframe(1)                        // AE
            (e.key == Key.Equals || e.key == Key.NumPadAdd) && !ctrl -> TimelineZoomRequests.request(1.5f)
            (e.key == Key.Minus || e.key == Key.NumPadSubtract) && !ctrl -> TimelineZoomRequests.request(1f / 1.5f)
            e.key == Key.B && !ctrl || e.key == Key.I && !ctrl -> store.setWorkAreaStartAtPlayhead()   // AE B / Premiere I
            e.key == Key.N && !ctrl || e.key == Key.O && !ctrl -> store.setWorkAreaEndAtPlayhead()     // AE N / Premiere O
            e.key == Key.M && !ctrl || e.key == Key.NumPadMultiply -> store.toggleMarker()             // Premiere M / AE *
            e.key == Key.S && !ctrl && !shift -> store.snapping = !store.snapping                        // Premiere: ímã
            e.key == Key.NumPad0 -> if (store.preRenderPreparing || store.preRenderProgress.running) store.cancelPreRender() else store.startPreRender(1)  // AE: prévia

            // --- Keyframes (AE) --------------------------------------------------------
            e.key == Key.F9 && ctrl && shift -> store.easeTimelineKeys(2)            // AE: Easy Ease Out
            e.key == Key.F9 && shift -> store.easeTimelineKeys(1)                    // AE: Easy Ease In
            e.key == Key.F9 && !ctrl -> store.easeTimelineKeys(0)                    // AE: Easy Ease

            // --- Edição ---------------------------------------------------------------
            ctrl && shift && e.key == Key.Z -> store.redo()
            ctrl && e.key == Key.Y -> store.redo()
            ctrl && e.key == Key.Z -> store.undo()
            ctrl && e.key == Key.C -> if (!store.copyTimelineKeys()) store.copyLayers()
            ctrl && e.key == Key.X -> { store.copyLayers(); store.deleteLayers() }
            ctrl && e.key == Key.V -> if (store.keySelectMode) store.pasteTimelineKeys() else store.pasteLayers()
            ctrl && shift && e.key == Key.D -> store.splitAtPlayhead()                // AE: dividir camada
            ctrl && e.key == Key.D -> if (!store.duplicateTimelineKeys()) store.duplicateLayers()
            ctrl && e.key == Key.K -> store.splitAtPlayhead()                         // Premiere: adicionar edição
            (e.key == Key.Delete || e.key == Key.Backspace) && shift -> store.deleteLayers(ripple = true)
            e.key == Key.Delete || e.key == Key.Backspace -> if (!store.deleteTimelineKeys()) store.deleteLayers()
            e.key == Key.Q && !ctrl && layer != null -> store.trimToPlayhead(layer, start = true, live = false)   // Premiere
            e.key == Key.W && !ctrl && layer != null -> store.trimToPlayhead(layer, start = false, live = false)  // Premiere
            alt && e.key == Key.LeftBracket && layer != null -> store.trimToPlayhead(layer, start = true, live = false)   // AE
            alt && e.key == Key.RightBracket && layer != null -> store.trimToPlayhead(layer, start = false, live = false) // AE
            e.key == Key.LeftBracket && !ctrl && layer != null -> store.moveLayerToPlayhead(layer, alignEnd = false)    // AE
            e.key == Key.RightBracket && !ctrl && layer != null -> store.moveLayerToPlayhead(layer, alignEnd = true)     // AE

            // --- Camadas --------------------------------------------------------------
            ctrl && shift && e.key == Key.A -> store.clearSelection()                 // AE
            e.key == Key.F2 -> store.clearSelection()                                 // AE
            ctrl && e.key == Key.A -> store.selectAll()
            ctrl && e.key == Key.DirectionUp -> store.selectNeighbor(-1)              // AE
            ctrl && e.key == Key.DirectionDown -> store.selectNeighbor(1)             // AE
            ctrl && shift && e.key == Key.C -> store.precompose()                     // AE
            ctrl && e.key == Key.T -> store.addText()

            // --- Projeto --------------------------------------------------------------
            ctrl && e.key == Key.S -> store.saveProject { PcToasts.show(PcApp.context.getString(R.string.pc_saved), false) }
            ctrl && e.key == Key.I -> store.assistantPanel = "media"                  // abre o importador do editor
            ctrl && e.key == Key.M -> store.assistantPanel = "export"                 // AE: fila de render / Premiere: exportar
            else -> return false
        }
        return true
    }

    /** Linhas da lista (teclas, texto). */
    private val groups: List<Pair<Int, List<Pair<String, Int>>>> = listOf(
        R.string.pc_group_playback to listOf(
            "Space" to R.string.pc_sc_play,
            "← / →  ·  PgUp / PgDn" to R.string.pc_sc_frame,
            "Shift + ← / →" to R.string.pc_sc_frame10,
            "Home / End" to R.string.pc_sc_home_end,
            "↑ / ↓" to R.string.pc_sc_edit_points,
            "J / K" to R.string.pc_sc_keyframes,
            "= / −  ·  Alt + Scroll" to R.string.pc_sc_zoom,
            "B / N  ·  I / O" to R.string.pc_sc_work_area,
            "M  ·  Num *" to R.string.pc_sc_marker,
            "S" to R.string.i18n_snapping,
            "Num 0" to R.string.prerender_forward,
        ),
        R.string.pc_group_edit to listOf(
            "Ctrl + Z" to R.string.pc_sc_undo,
            "Ctrl + Shift + Z  ·  Ctrl + Y" to R.string.pc_sc_redo,
            "Ctrl + C / X / V" to R.string.pc_sc_copy,
            "Ctrl + D" to R.string.pc_sc_duplicate,
            "Delete" to R.string.pc_sc_delete,
            "Shift + Delete" to R.string.pc_sc_ripple_delete,
            "Ctrl + K  ·  Ctrl + Shift + D" to R.string.pc_sc_split,
            "Q / W  ·  Alt + [ / ]" to R.string.pc_sc_trim,
            "[ / ]" to R.string.pc_sc_move_to_playhead,
            "F9  ·  Shift + F9  ·  Ctrl + Shift + F9" to R.string.aet_easy_ease,
        ),
        R.string.pc_group_layers to listOf(
            "Ctrl + A" to R.string.pc_sc_select_all,
            "Ctrl + Shift + A  ·  F2" to R.string.pc_sc_deselect,
            "Ctrl + ↑ / ↓" to R.string.pc_sc_neighbor,
            "Ctrl + Shift + C" to R.string.pc_sc_precompose,
            "Ctrl + T" to R.string.pc_sc_text,
        ),
        R.string.pc3d_group to listOf(
            "MMB  ·  Shift + MMB  ·  Scroll / Ctrl + MMB  ·  Alt + LMB" to R.string.pc3d_sc_nav,
            "Num 1 / 3 / 7  ·  Num 5  ·  Num 0" to R.string.pc3d_sc_views,
            "Num .  ·  Home" to R.string.pc3d_sc_frame,
            "G / R / S" to R.string.pc3d_sc_transform,
            "X Y Z  ·  Shift + X Y Z  ·  0-9  ·  Enter  ·  Esc" to R.string.pc3d_sc_axis,
            "A  ·  Alt + A  ·  Ctrl + I  ·  H  ·  Alt + H" to R.string.pc3d_sc_select,
            "Shift + A  ·  Shift + D  ·  X" to R.string.pc3d_sc_add,
            "Ctrl + P  ·  Alt + P  ·  I" to R.string.pc3d_sc_object,
            "Z  ·  T  ·  N  ·  F3  ·  Ctrl + Tab" to R.string.pc3d_sc_panels,
        ),
        R.string.pc_group_project to listOf(
            "Ctrl + I" to R.string.pc_sc_import,
            "Ctrl + S" to R.string.pc_sc_save,
            "Ctrl + M" to R.string.pc_sc_export,
            "F11" to R.string.pc_sc_fullscreen,
            "Esc" to R.string.pc_sc_back,
            "F1" to R.string.pc_sc_help,
        ),
    )

    /** A folha de atalhos (F1), com o visual do Aurea. */
    @Composable
    fun HelpOverlay() {
        if (!helpOpen) return
        Box(
            Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.55f)).tocavel(shrink = 1f) { helpOpen = false },
            contentAlignment = Alignment.Center,
        ) {
            val card = RoundedCornerShape(20.dp)
            Column(
                Modifier.widthIn(max = 880.dp).padding(24.dp)
                    .background(AureaColors.EditorPanel, card)
                    .border(1.dp, AureaColors.Hairline, card)
                    .tocavel(shrink = 1f) { }
                    .padding(24.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(stringResource(R.string.pc_shortcuts), color = AureaColors.Text, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
                        Text(stringResource(R.string.pc_shortcuts_hint), color = AureaColors.Muted, fontSize = 13.sp)
                    }
                    Text(stringResource(R.string.pc_close), color = AureaColors.Accent, fontSize = 14.sp,
                        modifier = Modifier.tocavel { helpOpen = false }.padding(8.dp))
                }
                Spacer(Modifier.height(16.dp))
                Row(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    groups.chunked(2).forEach { column ->
                        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                            column.forEach { (title, rows) ->
                                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                                    Text(stringResource(title).uppercase(), color = AureaColors.Subtle, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
                                    rows.forEach { (keys, label) ->
                                        Row(verticalAlignment = Alignment.CenterVertically) {
                                            Text(stringResource(label), color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.weight(1f))
                                            Text(keys, color = AureaColors.Keyframe, fontSize = 12.sp, fontFamily = FontFamily.Monospace,
                                                modifier = Modifier.background(AureaColors.Chip, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 3.dp))
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Spacer(Modifier.width(1.dp))
            }
        }
    }
}
