package com.aurea.aurea.home

import android.content.Context
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.R

/** Dismissal is persisted only after the reader closes the notes. */
@Composable
internal fun ReleaseNotesEntry() {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("aurea.releaseNotes", Context.MODE_PRIVATE) }
    val edition = "${BuildConfig.VERSION_CODE}:2112-1"
    var showing by rememberSaveable(edition) { mutableStateOf(prefs.getString("read", null) != edition) }
    TextButton(onClick = { showing = true }) { Text(stringResource(R.string.release_notes_title)) }
    if (showing) {
        val close = {
            prefs.edit().putString("read", edition).apply()
            showing = false
        }
        AlertDialog(
            onDismissRequest = close,
            title = { Text(stringResource(R.string.release_notes_title)) },
            text = { Text(stringResource(R.string.release_notes_body),
                modifier = Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) },
            confirmButton = { TextButton(onClick = close) { Text(stringResource(R.string.editor_fechar)) } },
        )
    }
}
