package com.aurea.aurea.editor.timeline

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

private val markerPalette = intArrayOf(0xFFF7C34F.toInt(), 0xFF4D6EFF.toInt(), 0xFF70D56B.toInt(),
    0xFFFFB75B.toInt(), 0xFFD67BDB.toInt(), 0xFFFFFFFF.toInt())
private fun markerColor(packed: Int) = Color(packed and 255, (packed ushr 8) and 255, (packed ushr 16) and 255)

/** Opened by holding the preview anchor; normal tap marks the current frame. */
@Composable
internal fun MarkerEditor(store: EditorStore) {
    val editing = store.markerEditingFrame
    var failure by remember(editing) { mutableStateOf(false) }
    val markers = store.markers
    editing?.let { original ->
        val index = markers.frames.indexOf(original)
        if (index < 0) { LaunchedEffect(original) { store.markerEditingFrame = null }; return@let }
        var name by remember(original) { mutableStateOf(store.markerLabel(original)) }
        var frame by remember(original) { mutableStateOf(original.toString()) }
        var color by remember(original) { mutableStateOf(markers.colors[index]) }
        AlertDialog(onDismissRequest = { store.markerEditingFrame = null; failure = false },
            title = { Text(stringResource(R.string.marker_edit)) },
            text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(name, { if (it.length <= 200) name = it }, singleLine = true,
                    label = { Text(stringResource(R.string.new_project_name)) })
                OutlinedTextField(frame, { frame = it }, singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    label = { Text(stringResource(R.string.marker_frame)) })
                Text(frame.toIntOrNull()?.let { Timecode.format(it.coerceAtLeast(0), store.project.fps) } ?: "—")
                TextButton(onClick = { frame = store.playhead.toString() }) { Text(stringResource(R.string.marker_at_playhead)) }
                Row {
                    markerPalette.forEachIndexed { i, packed ->
                        val description = stringResource(R.string.marker_color, i + 1)
                        Box(Modifier.size(40.dp).clickable { color = packed }.semantics { contentDescription = description }, contentAlignment = Alignment.Center) {
                            Box(Modifier.size(if (color == packed) 32.dp else 22.dp).background(markerColor(packed), CircleShape))
                        }
                    }
                }
                Text(stringResource(if (failure) R.string.marker_error else R.string.marker_hint), fontSize = 12.sp)
                TextButton(onClick = { store.deleteMarker(original); store.markerEditingFrame = null; failure = false }) { Text(stringResource(R.string.common_delete)) }
            } },
            confirmButton = { TextButton(onClick = {
                val target = frame.toIntOrNull()
                if (target != null && store.editMarker(original, target, color, name)) { store.markerEditingFrame = null; failure = false }
                else failure = true
            }) { Text(stringResource(R.string.common_save)) } },
            dismissButton = { TextButton(onClick = { store.markerEditingFrame = null; failure = false }) { Text(stringResource(R.string.common_cancel)) } })
    }
}
