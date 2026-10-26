package com.aurea.aurea.editor.timeline

import com.aurea.aurea.ui.i18n.LocaleDirection
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTimeline
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.testTag

// Omega timeline (área 03, P1-3): as quatro aparências da régua. O motor guarda
// RGBA com o R no byte BAIXO (Marker::color); `MarkerColors` faz a conta nos dois
// sentidos — a paleta antiga de 6 cores era escrita como ARGB e lida como RGBA.
private val markerRoles: IntArray get() = IntArray(AureaTimeline.Bookmarks.size) { AureaTimeline.Bookmarks[it].toArgb() }
private fun markerPacked(index: Int): Int = MarkerColors.pack(AureaTimeline.Bookmarks[index].toArgb())
private fun markerAppearance(packed: Int): Int = MarkerColors.appearance(packed, markerRoles)

/** Opened by holding the preview anchor or tapping a marker on the ruler; a new marker only exists after Save. */
@Composable
internal fun MarkerEditor(store: EditorStore) {
    val editing = store.markerEditingFrame
    val isNew = store.markerEditingIsNew
    var failure by remember(editing) { mutableStateOf(false) }
    val markers = store.markers
    editing?.let { original ->
        val index = markers.frames.indexOf(original)
        if (index < 0 && !isNew) { LaunchedEffect(original) { store.closeMarkerEditor() }; return@let }
        var name by remember(original, isNew) { mutableStateOf(if (isNew) "" else store.markerLabel(original)) }
        var frame by remember(original, isNew) { mutableStateOf(original.toString()) }
        var color by remember(original, isNew) { mutableStateOf(if (index >= 0) markers.colors[index] else markerPacked(0)) }
        // Aberto de dentro do palco (LTR): o diálogo volta à direção do idioma.
        LocaleDirection { AlertDialog(onDismissRequest = { store.closeMarkerEditor(); failure = false },
            title = { Text(stringResource(R.string.marker_edit)) },
            text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(name, { if (it.length <= 200) name = it }, singleLine = true,
                    label = { Text(stringResource(R.string.marker_name)) },
                    placeholder = { Text(stringResource(R.string.marker_name_hint)) })
                OutlinedTextField(frame, { frame = it }, singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    label = { Text(stringResource(R.string.marker_frame)) })
                Text(frame.toIntOrNull()?.let { Timecode.format(it.coerceAtLeast(0), store.project.fps) } ?: "—")
                TextButton(onClick = { frame = store.playhead.toString() }) { Text(stringResource(R.string.marker_at_playhead)) }
                Row {
                    val chosen = markerAppearance(color)
                    AureaTimeline.Bookmarks.forEachIndexed { i, swatch ->
                        val description = stringResource(R.string.marker_color, i + 1)
                        Box(Modifier.size(44.dp).clickable { color = markerPacked(i) }.semantics { contentDescription = description }
                            .testTag("marker.color.$i"), contentAlignment = Alignment.Center) {
                            Box(Modifier.size(if (chosen == i) 32.dp else 22.dp).background(swatch, CircleShape))
                        }
                    }
                }
                Text(stringResource(if (failure) R.string.marker_error else R.string.marker_hint), fontSize = 12.sp)
                if (!isNew) {
                    TextButton(onClick = { store.deleteMarker(original); store.closeMarkerEditor(); failure = false }) { Text(stringResource(R.string.common_delete)) }
                }
            } },
            confirmButton = { TextButton(onClick = {
                val target = frame.toIntOrNull()
                if (target != null && store.editMarker(if (isNew) -1 else original, target, color, name)) { store.closeMarkerEditor(); failure = false }
                else failure = true
            }) { Text(stringResource(R.string.common_save)) } },
            dismissButton = { TextButton(onClick = { store.closeMarkerEditor(); failure = false }) { Text(stringResource(R.string.common_cancel)) } }) }
    }
}
