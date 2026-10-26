package com.aurea.aurea.ui.ds

import com.aurea.aurea.ui.theme.aureaTracking
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
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
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
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
    /** Id de acessibilidade/teste (o mesmo `accessibilityIdentifier` do iOS); null = sem. */
    val tag: String? = null,
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
    Dialog(onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        BoxWithConstraints(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).padding(8.dp),
            contentAlignment = Alignment.BottomCenter) {
            Column(
                Modifier
                    .widthIn(max = 560.dp)
                    .fillMaxWidth()
                    .heightIn(max = maxHeight),
            ) {
                Column(
                    Modifier
                        .weight(1f, fill = false)
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(14.dp))
                        .background(Color(0xF0292929))
                        .verticalScroll(rememberScrollState()),
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
                                .then(if (a.tag != null) Modifier.testTag(a.tag) else Modifier)
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

// =============================================================================
// Omega fundação · F4: DIÁLOGOS CLAROS (`am_dialog` /
// `custom_alert_dialog_4_buttons` / `input_text_popup` da referência)
//
// Fundo `LightDialog`, raio 15, título 18 médio, mensagem 15, fio de 0,5 e os
// botões numa fileira de 46 dp (até 2) ou numa pilha de 44 dp (3 ou mais, o
// positivo em cima). Tudo pelos papéis claros: claro no Omega, escuro nos
// outros temas. Par de `AureaDialog`/`AureaAlert`/`AureaDialogField` do
// Sheets.swift (mesmos parâmetros e ids).
//
//   AureaDialog(
//       title = "Sobrescrever?", message = "…", onDismiss = { open = false },
//       actions = listOf(
//           DialogAction("Novo", DialogRole.Positive) { … },
//           DialogAction("Sobrescrever", DialogRole.Neutral, destructive = true) { … },
//           DialogAction(stringResource(R.string.common_cancel), DialogRole.Negative),
//       ),
//   )
// =============================================================================

/** O papel de um botão do diálogo: define a ordem e a cor. */
enum class DialogRole { Positive, Neutral, Negative }

/**
 * Um botão de [AureaDialog]. [dismiss] = o diálogo fecha antes de [onClick]
 * (o padrão). [destructive] pinta em `LightDanger`. [tag] = `testTag`.
 */
data class DialogAction(
    val label: String,
    val role: DialogRole = DialogRole.Neutral,
    val destructive: Boolean = false,
    val enabled: Boolean = true,
    val dismiss: Boolean = true,
    val tag: String? = null,
    val onClick: () -> Unit = {},
)

/** Medidas do diálogo claro (as do `am_dialog`). */
object AureaDialogDefaults {
    val MaxWidth = 320.dp
    val Radius = 15.dp
    val RowHeight = 46.dp
    val StackHeight = 44.dp
    val Hairline = 0.5.dp
    val FieldHeight = 40.dp
    val FieldRadius = 6.dp
    /** Corpo do diálogo (15 sp, `LightText`): textos que um `content` escreve. */
    val bodyStyle: TextStyle get() = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.LightText))
    /** Nota pequena (12 sp, `LightTextMuted`). */
    val noteStyle: TextStyle get() = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.LightTextMuted))
}

/**
 * O diálogo claro, com qualquer número de botões.
 *
 * @param title título (18 médio); nulo = sem título.
 * @param message mensagem (15); rola junto com [content] quando não cabe.
 * @param actions botões. Até 2: fileira de 46 dp (Negativo | Neutro | Positivo).
 *   3 ou mais: pilha de 44 dp (Positivo, Neutro, Negativo, de cima para baixo).
 * @param onDismiss toque fora, Voltar ou um botão com `dismiss`.
 * @param tag `testTag` da caixa.
 * @param content conteúdo extra abaixo da mensagem (campo, opções). Use
 *   [AureaDialogField] e [AureaDialogDefaults.bodyStyle] para as cores certas.
 */
@Composable
fun AureaDialog(
    title: String?,
    actions: List<DialogAction>,
    onDismiss: () -> Unit,
    message: String? = null,
    tag: String? = null,
    content: (@Composable () -> Unit)? = null,
) {
    Dialog(onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        BoxWithConstraints(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).padding(24.dp),
            contentAlignment = Alignment.Center) {
            Column(
                Modifier
                    .widthIn(max = AureaDialogDefaults.MaxWidth).fillMaxWidth()
                    .heightIn(max = maxHeight)
                    .clip(RoundedCornerShape(AureaDialogDefaults.Radius))
                    .background(AureaColors.LightDialog)
                    .then(if (tag != null) Modifier.testTag(tag) else Modifier),
            ) {
                Column(
                    Modifier.weight(1f, fill = false).fillMaxWidth().verticalScroll(rememberScrollState())
                        .padding(top = 10.dp, bottom = 15.dp),
                ) {
                    if (title != null) {
                        Text(
                            title,
                            modifier = Modifier.padding(start = 15.dp, end = 15.dp, top = 15.dp).semantics { heading() },
                            style = AureaType.Base.merge(TextStyle(fontSize = 18.sp, fontWeight = FontWeight.W500, color = AureaColors.LightText)),
                        )
                    }
                    message?.let {
                        Text(it, modifier = Modifier.padding(start = 15.dp, end = 15.dp, top = 10.dp), style = AureaDialogDefaults.bodyStyle)
                    }
                    if (content != null) {
                        Column(Modifier.fillMaxWidth().padding(start = 15.dp, end = 15.dp, top = 10.dp)) { content() }
                    }
                }
                HorizontalDivider(thickness = AureaDialogDefaults.Hairline, color = AureaColors.LightDivider)
                val ordered = DialogRole.entries.flatMap { role -> actions.filter { it.role == role } }
                val fire = { a: DialogAction ->
                    if (a.dismiss) onDismiss()
                    a.onClick()
                }
                if (actions.size <= 2) {
                    // Fileira: Negativo | Neutro | Positivo (ordem de leitura; espelha em RTL).
                    Row(Modifier.fillMaxWidth().height(AureaDialogDefaults.RowHeight)) {
                        ordered.asReversed().forEachIndexed { i, a ->
                            if (i > 0) Box(Modifier.width(AureaDialogDefaults.Hairline).fillMaxHeight().background(AureaColors.LightDivider))
                            DialogButton(a, Modifier.weight(1f).fillMaxHeight()) { fire(a) }
                        }
                    }
                } else {
                    // Pilha: Positivo em cima.
                    ordered.forEachIndexed { i, a ->
                        if (i > 0) HorizontalDivider(thickness = AureaDialogDefaults.Hairline, color = AureaColors.LightDivider)
                        DialogButton(a, Modifier.fillMaxWidth().height(AureaDialogDefaults.StackHeight)) { fire(a) }
                    }
                }
            }
        }
    }
}

/**
 * Diálogo de confirmação (claro; [AureaDialog] com Cancelar | Confirmar).
 * Mesma assinatura de sempre: quem já chamava continua igual.
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
    val actions = buildList {
        if (showCancel) add(DialogAction(cancel, DialogRole.Negative, tag = "dialog.cancel"))
        add(DialogAction(confirmLabel, DialogRole.Positive, destructive = destructive, tag = "dialog.confirm", onClick = onConfirm))
    }
    AureaDialog(title = title, message = message, actions = actions, onDismiss = onDismiss, content = extra)
}

@Composable
private fun DialogButton(action: DialogAction, modifier: Modifier, onClick: () -> Unit) {
    val positive = action.role == DialogRole.Positive
    val color = when {
        !action.enabled -> AureaColors.LightTextFaint
        action.destructive -> AureaColors.LightDanger
        positive -> AureaColors.LightAccentInk
        else -> AureaColors.LightText
    }
    Box(
        modifier
            .then(action.tag?.let { Modifier.testTag(it) } ?: Modifier)
            .tocavel(enabled = action.enabled, shrink = 1f, onClick = onClick)
            .padding(horizontal = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            action.label,
            maxLines = 2,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = if (positive) FontWeight.W500 else FontWeight.Normal, color = color)),
        )
    }
}

/**
 * O campo de texto do diálogo (`input_text_popup`/`am_dialog`): 40 dp, fundo
 * `LightSurface`, raio 6, dica em `LightTextFaint` e ✕ que apaga o texto.
 * [textStyle] soma ao estilo do campo (ex.: `AureaType.Tabular`).
 */
@Composable
fun AureaDialogField(
    value: TextFieldValue,
    onValueChange: (TextFieldValue) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    textStyle: TextStyle? = null,
    keyboardOptions: KeyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
    keyboardActions: KeyboardActions = KeyboardActions.Default,
    tag: String? = null,
    focusRequester: FocusRequester? = null,
) {
    val clear = androidx.compose.ui.res.stringResource(R.string.om_field_clear)
    val style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.LightText)).let { if (textStyle != null) it.merge(textStyle) else it }
    Row(
        modifier
            .fillMaxWidth()
            .height(AureaDialogDefaults.FieldHeight)
            .clip(RoundedCornerShape(AureaDialogDefaults.FieldRadius))
            .background(AureaColors.LightSurface)
            .padding(start = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (value.text.isEmpty() && placeholder != null) {
                Text(placeholder, maxLines = 1, style = style.merge(TextStyle(color = AureaColors.LightTextFaint)))
            }
            BasicTextField(
                value = value,
                onValueChange = onValueChange,
                singleLine = true,
                textStyle = style,
                cursorBrush = SolidColor(AureaColors.LightAccentInk),
                keyboardOptions = keyboardOptions,
                keyboardActions = keyboardActions,
                modifier = Modifier.fillMaxWidth()
                    .then(tag?.let { Modifier.testTag(it) } ?: Modifier)
                    .then(focusRequester?.let { Modifier.focusRequester(it) } ?: Modifier),
            )
        }
        if (value.text.isNotEmpty()) {
            Box(
                Modifier
                    .size(40.dp)
                    .semantics { contentDescription = clear }
                    .tocavel(shrink = 1f) { onValueChange(TextFieldValue("")) },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.LightTextFaint) }
        } else {
            Spacer(Modifier.width(10.dp))
        }
    }
}

/** [AureaDialogField] para texto simples (sem seleção). */
@Composable
fun AureaDialogField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    textStyle: TextStyle? = null,
    keyboardOptions: KeyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
    keyboardActions: KeyboardActions = KeyboardActions.Default,
    tag: String? = null,
    focusRequester: FocusRequester? = null,
) {
    // Guarda a seleção/cursor entre recomposições; o texto vem sempre de [value].
    var field by remember { mutableStateOf(TextFieldValue(value, TextRange(value.length))) }
    val current = if (field.text == value) field else field.copy(text = value, selection = TextRange(value.length))
    AureaDialogField(
        value = current,
        onValueChange = { field = it; if (it.text != value) onValueChange(it.text) },
        modifier = modifier,
        placeholder = placeholder,
        textStyle = textStyle,
        keyboardOptions = keyboardOptions,
        keyboardActions = keyboardActions,
        tag = tag,
        focusRequester = focusRequester,
    )
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
    var field by remember { mutableStateOf(TextFieldValue(initial, TextRange(0, initial.length))) }
    val text = field.text
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    AureaAlert(
        title = title,
        confirmLabel = "OK",
        onConfirm = { if (text.isNotBlank()) onConfirm(text.trim()) },
        onDismiss = onDismiss,
        extra = {
            AureaDialogField(
                value = field,
                onValueChange = { field = it },
                keyboardActions = KeyboardActions(onDone = {
                    if (text.isNotBlank()) {
                        onDismiss()
                        onConfirm(text.trim())
                    }
                }),
                tag = "dialog.field",
                focusRequester = focus,
            )
        },
    )
}
