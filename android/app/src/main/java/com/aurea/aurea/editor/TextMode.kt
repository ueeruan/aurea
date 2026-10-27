package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.roundToInt

// =============================================================================
// Omega conteúdo (área 06, P0-1 / P0-3 / P1-10): o MODO DE TEXTO.
//
//  - No topo, a BARRA DE ESTILO (42 dp, no lugar da barra superior):
//    [Alinhamento 38][Fonte flex ▾][Tamanho 80 ▾][Cor 38][✓ 40] (texto 3D: só
//    alinhamento, fonte e ✓). Cada peça abre um popup preso embaixo dela; só um
//    aberto por vez; tocar fora fecha.
//  - Embaixo, o PAINEL GRANDE (do transporte até o fim) com a faixa de entrada
//    de 60 dp: cada letra vai na hora para a camada (uma rajada de 1 s por passo
//    de desfazer, EditorStore.typeText). O teclado abre sozinho; se ele cobrir a
//    faixa, a faixa sobe junto.
// Par do TextEditModeView.swift (mesmos ids).
// =============================================================================

internal enum class TextPopup { None, Font, Size, Color }

internal object TextModeState {
    var popup by mutableStateOf(TextPopup.None)
    var fontBrowser by mutableStateOf(false)
    /** Texto 3D com a faixa vazia: o ✓ fica desligado (o motor guarda o último texto válido). */
    var draftBlank by mutableStateOf(false)
}

/**
 * Os tamanhos do popup (a régua fina continua nas Opções de texto). 0.3.6: iam só
 * até 144 px — pouco num vídeo 1080p/4K ("não dá para aumentar mais o texto");
 * agora seguem até o teto do motor ([TEXT_SIZE_MAX]), e o número ao lado aceita
 * qualquer valor digitado.
 */
internal val TextSizeSteps = intArrayOf(
    5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 96, 112, 144,
    192, 256, 320, 400, 512, 640, 800, 1000, 1300, 1600, 2000,
)

/** 0.3.6: o maior tamanho de texto (px): o mesmo teto do motor (`TextSetSize`, 1…2000). */
internal const val TEXT_SIZE_MAX = 2000f

/** O tamanho digitado (aceita vírgula), preso a 1…[TEXT_SIZE_MAX]; null se não for número. */
internal fun parseTextSize(text: String): Float? =
    text.trim().replace(',', '.').toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(1f, TEXT_SIZE_MAX)

/** O passo mais perto de um tamanho (px). */
internal fun nearestSizeStep(size: Float): Int {
    var best = 0
    for (i in TextSizeSteps.indices) if (abs(TextSizeSteps[i] - size) < abs(TextSizeSteps[best] - size)) best = i
    return best
}

/** A fonte em uso na camada do modo (2D: a família; 3D: o arquivo). */
private fun currentFont(store: EditorStore): EditorStore.FontItem? {
    val t3 = store.text3d
    if (t3 != null) return store.fonts.firstOrNull { it.path == t3.fontPath && t3.fontPath.isNotEmpty() }
    val f = store.textFont ?: return null
    if (f.family.isEmpty()) return null
    return store.fonts.firstOrNull { it.family == f.family && it.weight == f.weight && it.italic == f.italic } ?: f
}

/**
 * A BARRA DE ESTILO do modo de texto (no lugar da barra superior). Os popups
 * crescem para baixo, por cima do palco (quem hospeda põe a barra acima no zIndex).
 */
@Composable
internal fun TextStyleBar(store: EditorStore) {
    val context = LocalContext.current
    LaunchedEffect(store.textEditLayer) {
        FontPrefs.load(context)
        ContentPrefs.load(context)
        store.loadFonts()
        store.refreshTextFont()
        TextModeState.popup = TextPopup.None
    }
    val is3D = store.text3d != null
    val detail = store.textDetail
    val popup = TextModeState.popup
    val screenH = LocalConfiguration.current.screenHeightDp
    fun toggle(p: TextPopup) { TextModeState.popup = if (TextModeState.popup == p) TextPopup.None else p }
    Box(Modifier.fillMaxWidth().height(ShellDims.TopBar)) {
        Column(Modifier.fillMaxWidth().wrapContentHeight(align = Alignment.Top, unbounded = true)) {
            Row(
                Modifier.fillMaxWidth().height(ShellDims.TopBar).background(AureaColors.ContentStyleBar).padding(horizontal = 1.5.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val alignment = store.text3d?.alignment ?: detail?.alignment ?: 0
                val alignLabel = stringResource(when (alignment) {
                    1 -> R.string.om_txm_align_center_cd
                    2 -> R.string.om_txm_align_right_cd
                    else -> R.string.om_txm_align_left_cd
                })
                StylePiece(Modifier.width(38.dp), alignLabel, "text.style.align", active = false, onClick = {
                    TextModeState.popup = TextPopup.None
                    store.cycleTextAlignment()
                }) { tint ->
                    ContentIcon(when (alignment) { 1 -> ContentIconKind.AlignCenter; 2 -> ContentIconKind.AlignRight; else -> ContentIconKind.AlignLeft }, 22.dp, tint)
                }
                val font = currentFont(store)
                val fontName = font?.family?.ifEmpty { null } ?: stringResource(R.string.panel_padrao_aparelho)
                StylePiece(Modifier.weight(1f), stringResource(R.string.om_txm_font_cd, fontName), "text.style.font",
                    active = popup == TextPopup.Font, onClick = { toggle(TextPopup.Font) }) { tint ->
                    Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                        // O nome na própria fonte, ajustado entre 8 e 16 sp numa linha.
                        BasicText(fontName, Modifier.weight(1f), maxLines = 1,
                            style = TextStyle(color = tint, fontFamily = fontFamilyOf(font?.path) ?: FontFamily.Default),
                            autoSize = TextAutoSize.StepBased(minFontSize = 8.sp, maxFontSize = 16.sp, stepSize = 0.5.sp))
                        CupertinoIcon(if (popup == TextPopup.Font) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 12.dp, tint)
                    }
                }
                if (!is3D) {
                    val size = (detail?.size ?: 72f).roundToInt()
                    StylePiece(Modifier.width(80.dp), stringResource(R.string.om_txm_size_cd, size), "text.style.size",
                        active = popup == TextPopup.Size, onClick = { toggle(TextPopup.Size) }) { tint ->
                        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                            // Só o número: o AUREA trabalha em px (sem "pt").
                            Text("$size", Modifier.weight(1f), maxLines = 1, style = AureaType.of(14f, FontWeight.W600, color = tint))
                            CupertinoIcon(if (popup == TextPopup.Size) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 12.dp, tint)
                        }
                    }
                    val c = detail?.color ?: floatArrayOf(1f, 1f, 1f, 1f)
                    StylePiece(Modifier.width(38.dp), stringResource(R.string.om_txm_color_cd), "text.style.color",
                        active = popup == TextPopup.Color, onClick = { toggle(TextPopup.Color) }) { _ ->
                        ColorDot(Color(c[0], c[1], c[2], c.getOrElse(3) { 1f }))
                    }
                }
                val blank = is3D && (TextModeState.draftBlank || store.text3d?.content.isNullOrBlank() == true)
                StylePiece(Modifier.width(40.dp), stringResource(R.string.om_txm_done), "text.style.done",
                    active = false, enabled = !blank, onClick = {
                        TextModeState.popup = TextPopup.None
                        store.closeTextEdit()
                    }) { tint -> CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 20.dp, if (blank) AureaColors.Disabled else tint) }
            }
            if (popup != TextPopup.None) {
                // O conector de 8 dp e o corpo do popup, na largura toda.
                Box(Modifier.fillMaxWidth().height(8.dp).background(AureaColors.ContentStyleButton))
                when (popup) {
                    TextPopup.Font -> FontMiniPopup(store)
                    TextPopup.Size -> SizePopup(store)
                    TextPopup.Color -> ColorPopup(store)
                    TextPopup.None -> {}
                }
                // Tocar fora fecha (a camada invisível sobre o palco).
                val closeLabel = stringResource(R.string.common_close)
                Box(
                    Modifier.fillMaxWidth().height(screenH.dp).testTag("text.popup.closer")
                        .semantics { contentDescription = closeLabel }
                        .pointerInput(Unit) {
                            awaitEachGesture {
                                awaitFirstDown().consume()
                                TextModeState.popup = TextPopup.None
                            }
                        },
                )
            }
        }
    }
    if (TextModeState.fontBrowser) {
        FontBrowserDialog(store, onPick = { f ->
            store.applyFontAnywhere(f)
            FontPrefs.recordUse(f)
            TextModeState.fontBrowser = false
        }, onDismiss = { TextModeState.fontBrowser = false })
    }
}

/** Uma peça da barra de estilo: fundo "botão de estilo", 1,5 dp de margem. */
@Composable
private fun RowScope.StylePiece(
    modifier: Modifier,
    label: String,
    tag: String,
    active: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
    content: @Composable (Color) -> Unit,
) {
    val tint = if (active) AureaColors.Accent else AureaColors.Text
    Box(
        modifier
            .fillMaxHeight()
            .padding(1.5.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(AureaColors.ContentStyleButton)
            .testTag(tag)
            .semantics {
                contentDescription = label
                selected = active
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, shrink = 1f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { content(tint) }
}

/** Popup da fonte: o mini navegador (recentes + favoritas, 68 dp) e "Ver todas as fontes". */
@Composable
private fun FontMiniPopup(store: EditorStore) {
    val fonts by remember(store) { derivedStateOf { store.fonts } }
    val current = currentFont(store)
    val list = remember(fonts, current, FontPrefs.favorites, FontPrefs.recents) { FontPrefs.miniList(fonts, current) }
    val currentKey = current?.let { FontPrefs.key(it) }
    Column(Modifier.fillMaxWidth().background(AureaColors.ContentStyleButton)) {
        LazyRow(Modifier.fillMaxWidth().height(68.dp).testTag("text.font.mini"), verticalAlignment = Alignment.CenterVertically) {
            itemsIndexed(list) { i, (f, header) ->
                val on = if (f == null) currentKey == null else FontPrefs.key(f) == currentKey
                MiniFontItem(i, f, header, on) {
                    store.applyFontAnywhere(f)
                    if (f != null) FontPrefs.recordUse(f)
                }
            }
        }
        Row(
            Modifier.height(33.dp).testTag("text.font.viewAll").tocavel(shrink = 1f) {
                TextModeState.popup = TextPopup.None
                TextModeState.fontBrowser = true
            }.padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Textformat, 16.dp, AureaColors.Text)
            Spacer(Modifier.width(6.dp))
            Text(stringResource(R.string.om_txm_view_all_fonts), style = AureaType.of(12.5f))
        }
    }
}

@Composable
private fun MiniFontItem(index: Int, f: EditorStore.FontItem?, header: Int, on: Boolean, onClick: () -> Unit) {
    val name = f?.let { "${it.family} ${it.style}".trim() } ?: stringResource(R.string.panel_padrao_aparelho)
    val headerLabel = when (header) {
        1 -> stringResource(R.string.om_txm_recent_cd)
        2 -> stringResource(R.string.om_txm_favorites_cd)
        else -> null
    }
    Row(
        Modifier
            .width(if (header != 0) 85.dp else 65.dp)
            .fillMaxHeight()
            .padding(2.dp)
            .clip(RoundedCornerShape(5.dp))
            .background(if (on) AureaColors.ContentStyleBar else Color.Transparent)
            .testTag("text.font.mini.item.$index")
            .semantics { contentDescription = if (headerLabel != null) "$headerLabel · $name" else name; selected = on }
            .tocavel(shrink = 1f, onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (header != 0) {
            Box(Modifier.width(20.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                CupertinoIcon(if (header == 1) CupertinoGlyph.Clock else CupertinoGlyph.StarFill, 14.dp, AureaColors.Muted)
            }
        }
        Column(Modifier.weight(1f).fillMaxHeight(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            Box(Modifier.height(35.dp), contentAlignment = Alignment.Center) {
                // A amostra: 20 sp, ajustada entre 14 e 24 sp em passos de 0,5.
                BasicText(FontScripts.sample(f?.scripts ?: FontScripts.LATIN, long = false), maxLines = 1,
                    style = TextStyle(color = AureaColors.Text, fontFamily = fontFamilyOf(f?.path) ?: FontFamily.Default),
                    autoSize = TextAutoSize.StepBased(minFontSize = 14.sp, maxFontSize = 24.sp, stepSize = 0.5.sp))
            }
            Text(name, maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                style = AureaType.of(9f, color = if (on) AureaColors.Accent else AureaColors.Text, lineHeight = 1.1f))
        }
    }
}

/** Popup do tamanho: régua de passos; um arraste = um passo de desfazer. 0.3.6: e o número, para digitar. */
@Composable
private fun SizePopup(store: EditorStore) {
    val currentSize = store.textDetail?.size ?: 72f
    val index = nearestSizeStep(currentSize)
    val label = stringResource(R.string.om_txm_size_cd, currentSize.roundToInt())
    Row(
        Modifier.fillMaxWidth().height(40.dp).background(AureaColors.ContentStyleButton).padding(start = 16.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BoxWithConstraints(
            Modifier.weight(1f).fillMaxHeight().testTag("text.size.slider").semantics { contentDescription = label }
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        down.consume()
                        store.beginGesture("tamanho do texto")
                        fun at(x: Float) {
                            val i = ((x / size.width.coerceAtLeast(1)) * (TextSizeSteps.size - 1)).roundToInt().coerceIn(0, TextSizeSteps.size - 1)
                            store.setTextSize(TextSizeSteps[i].toFloat())
                        }
                        at(down.position.x)
                        while (true) {
                            val e = awaitPointerEvent()
                            val c = e.changes.firstOrNull { it.id == down.id } ?: break
                            c.consume()
                            if (!c.pressed) break
                            at(c.position.x)
                        }
                        store.endGesture()
                    }
                },
        ) {
            val w = maxWidth
            val frac = index / (TextSizeSteps.size - 1f)
            Box(Modifier.align(Alignment.CenterStart).fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)).background(AureaColors.FxTrack))
            Box(Modifier.align(Alignment.CenterStart).width(w * frac).height(3.dp).clip(RoundedCornerShape(2.dp)).background(AureaColors.Accent))
            Box(Modifier.align(Alignment.CenterStart).offset(x = w * frac - 9.dp).size(18.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Accent))
        }
        Spacer(Modifier.width(10.dp))
        SizeField(store, currentSize)
    }
}

/**
 * 0.3.6: o tamanho em número, para digitar qualquer valor até [TEXT_SIZE_MAX] (a régua
 * vai de passo em passo). Enter/✓ aplica num passo de desfazer; sair sem número volta.
 */
@Composable
private fun SizeField(store: EditorStore, size: Float) {
    val shown = size.roundToInt().toString()
    var field by remember { mutableStateOf(TextFieldValue(shown)) }
    var focused by remember { mutableStateOf(false) }
    // Fora da edição o campo segue o tamanho da camada (régua, desfazer).
    LaunchedEffect(shown, focused) { if (!focused) field = TextFieldValue(shown) }
    val focus = androidx.compose.ui.platform.LocalFocusManager.current
    val label = stringResource(R.string.panel_tamanho_texto)
    fun apply() {
        val value = parseTextSize(field.text)
        if (value != null && value.roundToInt() != size.roundToInt()) {
            store.beginGesture("tamanho do texto")
            store.setTextSize(value)
            store.endGesture()
        }
        field = TextFieldValue((value ?: size).roundToInt().toString())
    }
    BasicTextField(
        value = field,
        onValueChange = { next -> field = next.copy(text = next.text.filter { it.isDigit() || it == ',' || it == '.' }.take(6)) },
        singleLine = true,
        textStyle = TextStyle(color = AureaColors.Text, fontSize = 13.sp, textAlign = TextAlign.Center, fontFeatureSettings = "tnum"),
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
            keyboardType = androidx.compose.ui.text.input.KeyboardType.Decimal,
            imeAction = androidx.compose.ui.text.input.ImeAction.Done,
        ),
        keyboardActions = androidx.compose.foundation.text.KeyboardActions(onDone = { focus.clearFocus() }),
        modifier = Modifier
            .width(64.dp)
            .height(32.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(AureaColors.FxTrack)
            .testTag("text.size.field")
            .semantics { contentDescription = label }
            .onFocusChanged { state ->
                if (focused && !state.isFocused) apply()
                focused = state.isFocused
                if (state.isFocused) field = field.copy(selection = TextRange(0, field.text.length))
            },
        decorationBox = { inner -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { inner() } },
    )
}

/** Popup da cor: o seletor compacto, preso à direita. */
@Composable
private fun ColorPopup(store: EditorStore) {
    val c = store.textDetail?.color ?: floatArrayOf(1f, 1f, 1f, 1f)
    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.TopEnd) {
        Box(Modifier.width(281.dp).background(AureaColors.ContentStyleButton).padding(8.dp)) {
            ContentColorPicker(
                color = Color(c[0], c[1], c[2], c.getOrElse(3) { 1f }),
                tag = "text.color.picker",
                label = stringResource(R.string.om_txm_color_cd),
                onStart = { store.beginGesture("cor do texto") },
                onChange = { col -> store.setTextColor(col.red, col.green, col.blue, col.alpha) },
                onEnd = { store.endGesture() },
            )
        }
    }
}

/**
 * O PAINEL GRANDE do modo de texto: fundo do painel de edição e a faixa de
 * entrada de 60 dp no topo (sobe acima do teclado se ele cobrir a faixa).
 */
@Composable
internal fun TextEditPanel(store: EditorStore, modifier: Modifier) {
    val id = store.textEditLayer ?: return
    val initial = remember(id) { store.text3d?.content ?: store.textDetail?.content ?: "" }
    var field by remember(id) {
        mutableStateOf(TextFieldValue(initial, if (store.textEditSelectAll) TextRange(0, initial.length) else TextRange(initial.length)))
    }
    // Desfazer (ou outra mudança de fora) troca o texto: o campo acompanha.
    val content = store.text3d?.content ?: store.textDetail?.content
    LaunchedEffect(content) {
        if (content != null && content != field.text && !store.textEditing) {
            field = TextFieldValue(content, TextRange(content.length))
        }
    }
    val focus = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    LaunchedEffect(id) {
        TextModeState.draftBlank = false
        focus.requestFocus()
        keyboard?.show()
    }
    DisposableEffect(id) { onDispose { keyboard?.hide() } }
    val alignment = store.text3d?.alignment ?: store.textDetail?.alignment ?: 0
    val hint = stringResource(R.string.om_txm_hint)
    BoxWithConstraints(modifier.background(AureaColors.ContentEditPanel).testTag("text.edit.mode")) {
        val density = LocalDensity.current
        val covered = (WindowInsets.ime.getBottom(density) - WindowInsets.navigationBars.getBottom(density)).coerceAtLeast(0)
        val strip = with(density) { 60.dp.roundToPx() }
        val top = if (constraints.maxHeight - covered >= strip) 0 else constraints.maxHeight - covered - strip
        Box(
            Modifier
                .fillMaxWidth()
                .height(60.dp)
                .offset { IntOffset(0, top) }
                .background(AureaColors.ContentInputStrip)
                .padding(4.dp),
        ) {
            BasicTextField(
                value = field,
                onValueChange = { v ->
                    val changed = v.text != field.text
                    field = v
                    TextModeState.draftBlank = store.text3d != null && v.text.isBlank()
                    if (changed) store.typeText(v.text)
                },
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 18.sp, color = AureaColors.Text, textAlign = when (alignment) {
                    1 -> TextAlign.Center
                    2 -> TextAlign.End
                    else -> TextAlign.Start
                })),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier.fillMaxSize().focusRequester(focus).testTag("text.edit.input").semantics { contentDescription = hint },
                decorationBox = { inner ->
                    Box(Modifier.fillMaxSize().padding(horizontal = 8.dp), contentAlignment = Alignment.CenterStart) {
                        if (field.text.isEmpty()) Text(hint, Modifier.alpha(1f), style = AureaType.of(18f, color = AureaColors.ContentHint))
                        inner()
                    }
                },
            )
        }
        if (store.text3d != null && field.text.isBlank()) {
            Text(stringResource(R.string.text_content_3d_required), Modifier.offset { IntOffset(0, top + strip) }.padding(12.dp),
                style = AureaType.of(12.5f, color = AureaColors.Muted))
        }
    }
}
