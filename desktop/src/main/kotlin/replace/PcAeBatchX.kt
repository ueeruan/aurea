package com.aurea.aurea.editor

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.LayerType

// =============================================================================
//  Lote X do After Effects no Aurea PC (só o PC: o celular não muda).
//
//  Entradas do menu do botão direito da camada na timeline do PC
//  (PcAeTimeline.kt chama `layerBatchXEntries`):
//    Animação ▸ Assistente de keyframes ▸ Converter áudio em keyframes
//    Camada ▸ Criar ▸ Criar formas a partir do texto / Criar máscaras a partir do texto
//    Conteúdo da forma (árvore)… (camadas de forma; abre PcShapeTree.kt)
//  As regras moram no motor (Engine::audio_to_keyframes, Engine::text_to_vector).
// =============================================================================

/** aurea::Errc::OutOfRange: texto com contornos demais para as máscaras de uma camada. */
private const val ERRC_OUT_OF_RANGE = 2L

@Composable
internal fun layerBatchXEntries(store: EditorStore, row: LayerRow): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    val out = mutableListOf<MenuEntry>()

    // --- Animação ▸ Assistente de keyframes -----------------------------------
    val amplitude = stringResource(R.string.pcx_audio_amplitude)
    val audioDone = stringResource(R.string.pcx_audio_keys_done)
    val audioFailed = stringResource(R.string.pcx_audio_keys_failed)
    val audioWorking = stringResource(R.string.pcx_audio_keys_working)
    val report: (Long) -> Unit = { made -> store.showToast(if (made > 0) audioDone else audioFailed) }
    val assistant = mutableListOf<MenuEntry>()
    assistant += MenuEntry.Item(stringResource(R.string.pcx_audio_to_keys), glyph = CupertinoGlyph.Waveform,
        enabled = !store.convertingAudio) {
        store.showToast(audioWorking)
        store.audioToKeyframes(null, amplitude, report)
    }
    if (type == LayerType.Audio || type == LayerType.Video || type == LayerType.Group) {
        assistant += MenuEntry.Item(stringResource(R.string.pcx_audio_to_keys_layer), glyph = CupertinoGlyph.Waveform,
            enabled = !store.convertingAudio) {
            store.showToast(audioWorking)
            store.audioToKeyframes(id, amplitude, report)
        }
    }
    out += MenuEntry.Item(stringResource(R.string.pcx_keyframe_assistant), glyph = CupertinoGlyph.Waveform,
        sublabel = stringResource(R.string.pcx_menu_animation), submenu = assistant)

    // --- Camada ▸ Criar ---------------------------------------------------------
    if (type == LayerType.Text) {
        val failed = stringResource(R.string.pcx_text_outline_failed)
        val tooMany = stringResource(R.string.pcx_text_masks_too_many)
        val create = mutableListOf<MenuEntry>()
        create += MenuEntry.Item(stringResource(R.string.pcx_shapes_from_text), glyph = CupertinoGlyph.Textformat) {
            if (store.textToVector(id, false) <= 0) store.showToast(failed)
        }
        create += MenuEntry.Item(stringResource(R.string.pcx_masks_from_text), glyph = CupertinoGlyph.Square) {
            val made = store.textToVector(id, true)
            if (made <= 0) store.showToast(if (made == -ERRC_OUT_OF_RANGE) tooMany else failed)
        }
        out += MenuEntry.Item(stringResource(R.string.pcx_create), glyph = CupertinoGlyph.Textformat,
            sublabel = stringResource(R.string.pcx_menu_layer), submenu = create)
    }
    // --- Conteúdo da forma: a árvore do After Effects (PcShapeTree.kt) ------------
    if (type == LayerType.Shape) {
        val notVector = stringResource(R.string.pcx_tree_not_vector)
        out += MenuEntry.Item(stringResource(R.string.pcx_tree_open), glyph = CupertinoGlyph.FolderFill, enabled = !row.locked) {
            PcShapeTree.show(store, id)
            if (!store.isVectorLayer) {
                PcShapeTree.open = false
                store.showToast(notVector)
            }
        }
    }
    return out
}
