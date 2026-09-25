package com.aurea.aurea.ui.ds

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * Primitivos de folha e diálogo da UI aprovada. O app antigo usava a folha
 * modal do Material 3 (fundo `#151C24`, raio 18) e, para confirmações e
 * escolhas curtas, os diálogos Cupertino do SDK — reproduzidos aqui com as
 * medidas do SDK (largura 270, raio 14, ação 17 sp em `#6FAED9`).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AureaModalSheet(
    onDismiss: () -> Unit,
    topRadius: Dp = 18.dp,
    skipPartiallyExpanded: Boolean = true,
    content: @Composable () -> Unit,
) {
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = skipPartiallyExpanded)
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = state,
        containerColor = AureaColors.Surface,
        contentColor = AureaColors.Text,
        scrimColor = Color(0x8A000000),
        shape = RoundedCornerShape(topStart = topRadius, topEnd = topRadius),
        dragHandle = {
            Box(
                Modifier
                    .padding(top = 10.dp, bottom = 6.dp)
                    .size(width = 36.dp, height = 5.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(Color(0x2EFFFFFF)),
            )
        },
    ) {
        Column(Modifier.navigationBarsPadding()) { content() }
    }
}

/** Uma ação de [AureaActionSheet]. */
data class SheetAction(
    val label: String,
    val destructive: Boolean = false,
    val enabled: Boolean = true,
    val onClick: () -> Unit,
)

/** Action sheet no estilo Cupertino (margem 8, raio 14, "Cancelar" separado). */
@Composable
fun AureaActionSheet(
    title: String? = null,
    message: String? = null,
    actions: List<SheetAction>,
    cancelLabel: String? = null,   // nulo = o do catálogo, no idioma do app
    onDismiss: () -> Unit,
) {
    val cancel = cancelLabel ?: androidx.compose.ui.res.stringResource(com.aurea.aurea.R.string.common_cancel)
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.BottomCenter) {
            Column(
                Modifier
                    .fillMaxWidth()
                    .navigationBarsPadding()
                    .padding(8.dp),
            ) {
                Column(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(14.dp))
                        .background(Color(0xF0292929)),
                ) {
                    if (title != null || message != null) {
                        Column(
                            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 13.5.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            title?.let {
                                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = Color(0x96F1F1F1), textAlign = TextAlign.Center)))
                            }
                            message?.let {
                                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = Color(0x96F1F1F1), textAlign = TextAlign.Center)))
                            }
                        }
                        HorizontalDivider(thickness = 0.3.dp, color = Color(0xD57D7D7D))
                    }
                    actions.forEachIndexed { i, a ->
                        if (i > 0) HorizontalDivider(thickness = 0.3.dp, color = Color(0xD57D7D7D))
                        Box(
                            Modifier
                                .fillMaxWidth()
                                .heightIn(min = 57.dp)
                                .tocavel(enabled = a.enabled, shrink = 1f) {
                                    onDismiss()
                                    a.onClick()
                                },
                            contentAlignment = Alignment.Center,
                        ) {
                            Text(
                                a.label,
                                style = AureaType.Base.merge(
                                    TextStyle(
                                        fontSize = 17.sp,
                                        color = when {
                                            !a.enabled -> AureaColors.Disabled
                                            a.destructive -> AureaColors.DestructiveCupertino
                                            else -> AureaColors.Accent
                                        },
                                    ),
                                ),
                            )
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))
                Box(
                    Modifier
                        .fillMaxWidth()
                        .heightIn(min = 57.dp)
                        .clip(RoundedCornerShape(14.dp))
                        .background(Color(0xFF2C2C2C))
                        .tocavel(shrink = 1f, onClick = onDismiss),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(cancel, style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
                }
            }
        }
    }
}

/**
 * Diálogo de confirmação no estilo Cupertino (270 dp, raio 14).
 *
 * [cancelLabel] nulo = o rótulo do catálogo, no idioma do app. Esconder o botão
 * de cancelar é [showCancel] = false, não um rótulo nulo: com sete idiomas, um
 * `null` que significa duas coisas vira bug de tradução.
 */
@Composable
fun AureaAlert(
    title: String,
    message: String? = null,
    confirmLabel: String = "OK",
    cancelLabel: String? = null,
    showCancel: Boolean = true,
    destructive: Boolean = false,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    extra: (@Composable () -> Unit)? = null,
) {
    val cancel = cancelLabel ?: androidx.compose.ui.res.stringResource(com.aurea.aurea.R.string.common_cancel)
    Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .width(270.dp)
                .clip(RoundedCornerShape(14.dp))
                .background(Color(0xF22D2D2D)),
        ) {
            Column(
                Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 19.dp, bottom = 16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = (-0.5).sp, textAlign = TextAlign.Center)))
                message?.let {
                    Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, letterSpacing = (-0.2).sp, textAlign = TextAlign.Center)))
                }
                extra?.invoke()
            }
            HorizontalDivider(thickness = 0.3.dp, color = Color(0xD57D7D7D))
            Row(Modifier.fillMaxWidth().heightIn(min = 45.dp)) {
                if (showCancel) {
                    DialogButton(cancel, Modifier.weight(1f), bold = false, color = AureaColors.Accent, onClick = onDismiss)
                    Box(Modifier.width(0.3.dp).height(45.dp).background(Color(0xD57D7D7D)))
                }
                DialogButton(
                    confirmLabel,
                    Modifier.weight(1f),
                    bold = true,
                    color = if (destructive) AureaColors.DestructiveCupertino else AureaColors.Accent,
                ) {
                    onDismiss()
                    onConfirm()
                }
            }
        }
    }
}

@Composable
private fun DialogButton(label: String, modifier: Modifier, bold: Boolean, color: Color, onClick: () -> Unit) {
    Box(modifier.heightIn(min = 45.dp).tocavel(shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 16.8.sp, fontWeight = if (bold) FontWeight.W600 else FontWeight.Normal, color = color)))
    }
}

/** Pede um nome (renomear camada/projeto). Vazio não confirma. */
@Composable
fun AureaNamePrompt(
    title: String,
    initial: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    // Texto todo selecionado: digitar troca o nome; tocar põe o cursor onde quiser.
    var field by remember { mutableStateOf(androidx.compose.ui.text.input.TextFieldValue(initial, androidx.compose.ui.text.TextRange(0, initial.length))) }
    val text = field.text
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    AureaAlert(
        title = title,
        confirmLabel = "OK",
        onConfirm = { if (text.isNotBlank()) onConfirm(text.trim()) },
        onDismiss = onDismiss,
        extra = {
            BasicTextField(
                value = field,
                onValueChange = { field = it },
                singleLine = true,
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp)),
                cursorBrush = SolidColor(AureaColors.Accent),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    if (text.isNotBlank()) {
                        onDismiss()
                        onConfirm(text.trim())
                    }
                }),
                modifier = Modifier
                    .padding(top = 10.dp)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(7.dp))
                    .background(Color(0xFF1C1C1E))
                    .padding(horizontal = 8.dp, vertical = 7.dp)
                    .focusRequester(focus),
            )
        },
    )
}
