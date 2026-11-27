package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionLookTarget
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors

/**
 * ESTILO: o da faixa toda ou só o das legendas escolhidas na aba Legendas
 * (estilo próprio por bloco, que vale por cima do da faixa). Modelos do AUREA,
 * meus modelos e da comunidade (salvar e publicar), destaque, caixa alta,
 * Personalizar, Texto e caixa, e a posição/tamanho da faixa.
 */
@Composable
internal fun CaptionStyleTab(cap: CaptionsState, store: EditorStore, onSaved: () -> Unit) {
    val chosen = cap.selectedBlocks
    val track = cap.track
    var onChosen by remember(track?.layer) { mutableStateOf(chosen.isNotEmpty()) }
    val blocks = remember(chosen, track) { if (chosen.isNotEmpty() && track != null) cap.blockLook(chosen) else null }
    val onBlocks = onChosen && blocks != null
    val target: CaptionLookTarget = if (onBlocks && blocks != null) blocks else cap
    val s = target.settings
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp)) {
        item(key = "status") { CaptionJobStatus(cap) }
        if (track != null && chosen.isNotEmpty()) item(key = "escopo") {
            Column {
                CapLabel(stringResource(R.string.cap053_apply_to))
                CapChips(listOf(stringResource(R.string.cap053_scope_track), stringResource(R.string.cap053_scope_selected, chosen.size)),
                    if (onBlocks) 1 else 0) { onChosen = it == 1 }
            }
        }
        item(key = "ritmo") {
            Column {
                if (!onBlocks) {
                    CapLabel(stringResource(R.string.pn_caption_words_per_caption))
                    CapChips((1..8).map(Int::toString), if (s.mode == 1) 0 else s.maxWords - 1,
                        enabled = { cap.lookEnabled }) { cap.setWordsPerBlock(it + 1) }
                    CaptionSavePresetButton(cap, onSaved)
                }
                CapLabel(stringResource(R.string.caption_star_motion))
                val motion = if (s.style == 5) 2 else if (s.highlight) 1 else 0
                CapChips(listOf(stringResource(R.string.caption_star_cut), stringResource(R.string.pn_caption_style_highlight),
                    stringResource(R.string.edt_tp_pop)), motion, enabled = { target.lookEnabled }) { index ->
                    target.update { it.copy(style = listOf(0, 2, 5)[index], highlight = index != 0) }
                }
                CapAction(stringResource(R.string.caption_star_impact), enabled = target.lookEnabled) { target.applyTemplate("comic") }
            }
        }
        item(key = "aparencia") {
            Column {
                CapToggle(stringResource(R.string.pn_caption_highlight_word), s.highlight) { v -> target.update(custom = false) { it.copy(highlight = v) } }
                CapToggle(stringResource(R.string.pn_caption_uppercase), s.uppercase) { v -> target.update(custom = false) { it.copy(uppercase = v) } }
                CaptionCustomize(target, store, startOpen = true)
                CapSection(stringResource(R.string.cap053_text_box))
                CaptionTextBoxControls(target)
                if (onBlocks && blocks != null) {
                    val own = track?.segments?.any { it.id in chosen && it.look != null } == true
                    CapAction(stringResource(R.string.cap046_block_use_track), enabled = own && blocks.lookEnabled) { blocks.clear() }
                }
            }
        }
        if (!onBlocks) item(key = "posicao") {
            Column {
                CapSection(stringResource(R.string.cap053_position))
                CapLabel(stringResource(R.string.pn_caption_screen_height))
                val ys = listOf(0.2f, 0.5f, 0.78f)
                CapChips(listOf(stringResource(R.string.pn_caption_pos_top), stringResource(R.string.pn_caption_pos_middle), stringResource(R.string.pn_caption_pos_bottom)),
                    ys.indexOfFirst { kotlin.math.abs(it - cap.settings.posY) < 0.01f }) { i -> cap.update(custom = false) { it.copy(posY = ys[i]) } }
                CapLabel(stringResource(R.string.panel_tamanho))
                val sizes = listOf(0.045f, 0.065f, 0.09f)
                CapChips(listOf(stringResource(R.string.pn_size_small_short), stringResource(R.string.pn_size_medium_short), stringResource(R.string.pn_size_large_short)),
                    sizes.indexOfFirst { kotlin.math.abs(it - cap.settings.sizeFrac) < 0.001f }) { i -> cap.update(custom = false) { it.copy(sizeFrac = sizes[i]) } }
            }
        }
        item(key = "fim") { Spacer(Modifier.height(24.dp)) }
    }
}
