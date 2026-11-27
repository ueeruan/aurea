package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import com.aurea.aurea.R
import com.aurea.aurea.editor.RotoPaintControls
import com.aurea.aurea.state.EditorStore

internal const val BG_REMOVE_KEY = "aurea.key.background_remove"
internal val BG_REMOVE_TYPE_ID: Int by lazy { effectTypeId(BG_REMOVE_KEY) }

internal fun bgRemoveFailedText(reason: Long): Int = when (reason) {
    2L, 4L -> R.string.bg_remove_failed_model
    3L -> R.string.bg_remove_failed_decode
    else -> R.string.bg_remove_failed_frames
}

@Composable
internal fun BgRemoveControls(store: EditorStore, effectId: Int) {
    RotoPaintControls(store, effectId, automatic = true)
}
