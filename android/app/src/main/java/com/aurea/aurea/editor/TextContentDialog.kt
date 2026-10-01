package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

/** Own window: the keyboard resizes this editor, never the preview or timeline. */
@Composable
internal fun TextContentDialog(
    request: EditorStore.TextContentRequest,
    onSave: (String) -> Boolean,
    onDismiss: () -> Unit,
) {
    var field by rememberSaveable(request.layerId, stateSaver = TextFieldValue.Saver) {
        mutableStateOf(TextFieldValue(request.content,
            if (request.selectAll) TextRange(0, request.content.length) else TextRange(request.content.length)))
    }
    val focus = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    val valid = !request.is3D || field.text.isNotBlank()
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxWidth().padding(16.dp)
            .background(AureaColors.EditorPanel, RoundedCornerShape(18.dp)).padding(16.dp)) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.sh_dock_edit_text), Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, color = AureaColors.Text)))
                TextButton(onClick = onDismiss, modifier = Modifier.testTag("text.content.cancel")) {
                    Text(stringResource(R.string.common_cancel), color = AureaColors.Muted)
                }
                TextButton(onClick = { if (onSave(field.text)) keyboard?.hide() }, enabled = valid,
                    modifier = Modifier.testTag("text.content.done")) {
                    Text(stringResource(R.string.editor_concluir), color = if (valid) AureaColors.Accent else AureaColors.Muted)
                }
            }
            BasicTextField(value = field, onValueChange = { field = it },
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 20.sp, color = AureaColors.Text,
                    textAlign = when (request.alignment) { 1 -> TextAlign.Center; 2 -> TextAlign.Right; else -> TextAlign.Left })),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxWidth().heightIn(min = 96.dp, max = 240.dp)
                    .background(AureaColors.Chip, RoundedCornerShape(10.dp)).padding(12.dp)
                    .verticalScroll(rememberScrollState()).focusRequester(focus).testTag("text.content.input"))
            if (!valid) Text(stringResource(R.string.text_content_3d_required),
                Modifier.padding(top = 8.dp), color = AureaColors.Muted)
        }
        LaunchedEffect(request.layerId) {
            // Wait for the dialog's own focus owner before opening the IME.
            withFrameNanos { }
            focus.requestFocus()
            keyboard?.show()
        }
    }
}
