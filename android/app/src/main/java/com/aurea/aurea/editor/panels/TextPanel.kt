package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.input.TextFieldValue
import com.aurea.aurea.ui.ds.AureaToggle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * TEXTO: o conteúdo (campo de várias linhas, aplicado enquanto digita), tamanho,
 * cor, alinhamento, contorno, entrelinha e espaçamento. O motor rasteriza com a
 * fonte do sistema na escala da tela — o texto fica nítido ampliado.
 */
@Composable
internal fun TextPanel(env: PanelEnv) {
    val store = env.store
    val t by remember(store) { derivedStateOf { store.textDetail } }
    val td = t ?: run {
        Text(stringResource(R.string.panel_selecione_camada_texto), modifier = Modifier.padding(18.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        return
    }
    var field by remember(store.primary) { mutableStateOf(TextFieldValue(td.content)) }
    // Um toque curto no campo abre o teclado; segurar seleciona um trecho para o estilo próprio.
    val fieldTouch = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
    LaunchedEffect(fieldTouch) {
        var pressedAt = 0L
        fieldTouch.interactions.collect {
            when (it) {
                is androidx.compose.foundation.interaction.PressInteraction.Press -> pressedAt = android.os.SystemClock.uptimeMillis()
                is androidx.compose.foundation.interaction.PressInteraction.Release ->
                    if (android.os.SystemClock.uptimeMillis() - pressedAt < 300L) store.openTextContentEditor()
                else -> Unit
            }
        }
    }
    val draft = field.text
    LaunchedEffect(td.content) {
        if (td.content != field.text && !store.textEditing) {
            field = TextFieldValue(td.content, TextRange(field.selection.start.coerceAtMost(td.content.length), field.selection.end.coerceAtMost(td.content.length)))
        }
    }
    // O desenho dos painéis de propriedade: a coluna fixa à esquerda, o conteúdo no centro e
    // as abas em ícones à direita. O losango e a curva valem para a propriedade escolhida.
    var tab by rememberSaveable(store.primary) { mutableIntStateOf(0) }
    // Animador aberto: o editor fica com a largura toda, sem as abas.
    var animatorOpen by remember(store.primary) { mutableStateOf(false) }
    val fullWidth = tab == 3 && animatorOpen
    DisposableEffect(store.primary) { onDispose { store.timelineFocus = null } }
    val focus = store.timelineFocus?.singleOrNull()?.takeIf { it.property in TEXT_RAIL_PROPERTIES }
    val keys = focus?.let { f -> store.primaryKeys().filter { it.property == f.property && it.effectIndex == f.effectIndex && it.paramIndex == f.paramIndex } }.orEmpty()
    val local = store.detail?.localPlayhead
    val curveFrom = store.curveStart(keys)
    Row(Modifier.fillMaxSize().testTag("text.panel")) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = when {
                keys.any { it.time == local } -> com.aurea.aurea.ui.ds.KeyframeLook.KeyHere
                keys.isNotEmpty() -> com.aurea.aurea.ui.ds.KeyframeLook.Animated
                else -> com.aurea.aurea.ui.ds.KeyframeLook.None
            },
            onKeyframe = focus?.let { f -> {
                if (f.property == com.aurea.aurea.engine.TrackProperty.TEXT_ANIM_PARAM) store.toggleTextAnimKey(f.effectIndex, f.paramIndex)
                else store.toggleTransformKeyframe(intArrayOf(f.property))
            } },
            curveAnimated = keys.isNotEmpty(),
            onCurve = if (curveFrom != null) ({
                store.primary?.let { id ->
                    store.selectKeyframe(id, curveFrom)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) else null,
        )
        Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 8.dp, end = 8.dp, bottom = 24.dp)) {
                when (tab) {
                    0 -> {
        Box(
            Modifier.fillMaxWidth().heightIn(min = 56.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        ) {
            BasicTextField(
                value = field,
                onValueChange = {
                    field = it.copy(text = td.content)
                },
                readOnly = true, // selection stays available for rich text styling; no keyboard here
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text,
                    textAlign = when (td.alignment) { 1 -> TextAlign.Center; 2 -> TextAlign.Right; else -> TextAlign.Left })),
                cursorBrush = SolidColor(AureaColors.Accent),
                interactionSource = fieldTouch,
                modifier = Modifier.fillMaxWidth().padding(end = 26.dp),
            )
            if (draft.isEmpty()) Text(stringResource(R.string.panel_digite_texto), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
            val editLabel = stringResource(R.string.sh_dock_edit_text)
            Box(Modifier.align(Alignment.TopEnd).size(36.dp).offset(x = 8.dp, y = (-6).dp).testTag("text.panel.edit")
                .semantics { contentDescription = editLabel }.tocavel(shrink = 1f) { store.openTextContentEditor() }, contentAlignment = Alignment.Center) {
                com.aurea.aurea.ui.theme.CupertinoIcon(com.aurea.aurea.ui.theme.CupertinoGlyph.Pencil, 18.dp, AureaColors.Text)
            }
        }
        // Trecho selecionado no campo: estilo próprio (rich text).
        val sel = field.selection
        if (!sel.collapsed) {
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(stringResource(R.string.panel_trecho_escolhido), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                SpanChip(stringResource(R.string.panel_cor)) {
                    val a = sel.min
                    val b = sel.max
                    env.openColor(ColorRequest(td.color.copyOf(), onChange = { r, g, bl, _ -> store.setTextSpan(a, b, floatArrayOf(r, g, bl), 0, 1f) },
                        onDone = {}))
                }
                SpanChip(stringResource(R.string.panel_negrito)) { store.setTextSpan(sel.min, sel.max, null, 700, 1f) }
                SpanChip(stringResource(R.string.panel_maior)) { store.setTextSpan(sel.min, sel.max, null, 0, 1.35f) }
                SpanChip(stringResource(R.string.panel_normal)) { store.clearTextSpans(sel.min, sel.max) }
            }
        }
        Spacer(Modifier.height(10.dp))
        val font by remember(store) { derivedStateOf { store.textFont } }
        // Omega conteúdo (área 06, P0-2): "Fonte ›" abre o navegador de fontes em tela cheia.
        var fontBrowser by remember { mutableStateOf(false) }
        if (fontBrowser) {
            com.aurea.aurea.editor.FontBrowserDialog(store, onPick = { f ->
                store.applyFontAnywhere(f)
                com.aurea.aurea.editor.FontPrefs.recordUse(f)
                fontBrowser = false
            }, onDismiss = { fontBrowser = false })
        }
        Row(
            Modifier.fillMaxWidth().height(48.dp).clip(RoundedCornerShape(10.dp)).testTag("text.options.font").tocavel(onClick = { fontBrowser = true }),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(R.string.panel_fonte), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Text(
                font?.family?.ifEmpty { null } ?: stringResource(R.string.panel_padrao_aparelho),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)),
            )
            Text("  ›", style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
        }
        TextRuler(env, stringResource(R.string.panel_tamanho), { store.textDetail?.size ?: 72f }, "${td.size.roundToInt()} px", 0.5f, 4f, com.aurea.aurea.editor.TEXT_SIZE_MAX, stringResource(R.string.panel_tamanho_texto)) {
            // 0.3.6: até o teto do motor (era 1000; o teclado numérico também).
            store.setTextSize(it)
        }
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.panel_cor), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(Color(td.color[0], td.color[1], td.color[2])) {
                store.beginGesture("cor do texto")
                env.openColor(ColorRequest(td.color.copyOf(), onChange = { r, g, b, a -> store.setTextColor(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
        }
        // O rótulo em cima e os três botões do mesmo tamanho: com as abas à direita a linha
        // não comporta os dois lado a lado em nenhum idioma.
        Text(stringResource(R.string.panel_alinhamento), modifier = Modifier.padding(top = 8.dp, bottom = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        PanelItemGrid(listOf(
            Triple(0, R.string.panel_esquerda, com.aurea.aurea.editor.ContentIconKind.AlignLeft),
            Triple(1, R.string.panel_centro, com.aurea.aurea.editor.ContentIconKind.AlignCenter),
            Triple(2, R.string.panel_direita, com.aurea.aurea.editor.ContentIconKind.AlignRight),
        ).map { (a, label, icon) -> PanelItem(com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat, stringResource(label), "text.align.$a", on = td.alignment == a, contentIcon = icon) { store.setTextAlignment(a) } }, columns = 3)
        TextDynamicSection(env)
                    }
                    1 -> {
        PanelSection(stringResource(R.string.panel_contorno), com.aurea.aurea.ui.theme.CupertinoGlyph.Circle, "text.sec.stroke") {
            ColorLine(stringResource(R.string.panel_cor_contorno), Color(td.strokeColor[0], td.strokeColor[1], td.strokeColor[2])) {
                store.beginGesture("cor do contorno do texto")
                env.openColor(ColorRequest(td.strokeColor.copyOf(), onChange = { r, g, b, a -> store.setTextStrokeColor(r, g, b, a) },
                    onDone = { store.endGesture() }))
            }
            TextRuler(env, stringResource(R.string.panel_largura_contorno), { store.textDetail?.strokeWidth ?: 0f }, "${td.strokeWidth.roundToInt()} px",
                0.1f, 0f, 60f, stringResource(R.string.panel_contorno_texto)) { store.setTextStrokeWidth(it) }
        }
        TextBackdropSection(env)
                    }
                    2 -> {
                        TextLayoutSection(env)
                        TextPathSection(env)
                    }
                    else -> {
                        TextAnimSection(env) { animatorOpen = it }
                        if (!animatorOpen) Spacer(Modifier.height(6.dp))
                        if (!animatorOpen) PanelItemRow(com.aurea.aurea.ui.theme.CupertinoGlyph.Move, stringResource(R.string.la_animators), "text.layer.animators", chevron = true) { env.onOpenAnimators() }
                    }
                }
            }
        }
        if (!fullWidth) Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        if (!fullWidth) PanelIconTabs(listOf(
            PanelIconTab(com.aurea.aurea.ui.theme.CupertinoGlyph.Textformat, stringResource(R.string.panel_texto), "text.tab.text"),
            PanelIconTab(com.aurea.aurea.ui.theme.CupertinoGlyph.Paintbrush, stringResource(R.string.tx_tab_style), "text.tab.style"),
            PanelIconTab(com.aurea.aurea.ui.theme.CupertinoGlyph.DocText, stringResource(R.string.tx_tab_layout), "text.tab.layout"),
            PanelIconTab(com.aurea.aurea.ui.theme.CupertinoGlyph.Sparkles, stringResource(R.string.tx_tab_animator), "text.tab.animator"),
        ), tab, stringResource(R.string.text_options)) { tab = it; store.timelineFocus = null }
    }
}

@Composable
private fun SpanChip(label: String, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
    }
}

/**
 * 0.4.6: texto dinâmico — contador e timecode (`TextData::dynamic`). As marcas
 * {#} {tc} {t} {f} no texto viram valores a cada quadro; o contador vai de "De"
 * a "Até" ao longo da camada.
 */
@Composable
private fun TextDynamicSection(env: PanelEnv) {
    val store = env.store
    val d = store.textDynamic ?: return
    val on = d[0] > .5
    PanelSection(stringResource(R.string.text_dynamic), com.aurea.aurea.ui.theme.CupertinoGlyph.Timer, "text.sec.dynamic", startOpen = on,
        trailing = { AureaToggle(checked = on, onCheckedChange = { store.setTextDynamicValue(0, if (it) 1.0 else 0.0) }) }) {
        val tokens = listOf(
            Triple("{#}", R.string.text_token_counter, com.aurea.aurea.ui.theme.CupertinoGlyph.Repeat), Triple("{tc}", R.string.text_token_timecode, com.aurea.aurea.ui.theme.CupertinoGlyph.Timer),
            Triple("{t}", R.string.text_token_time, com.aurea.aurea.ui.theme.CupertinoGlyph.Clock), Triple("{f}", R.string.text_token_frame, com.aurea.aurea.ui.theme.CupertinoGlyph.Film),
        )
        PanelItemGrid(tokens.map { (token, label, glyph) -> PanelItem(glyph, stringResource(label), "text.token.$token") { store.insertTextToken(token) } })
        Spacer(Modifier.height(4.dp))
        TextRuler(env, stringResource(R.string.text_counter_from), { (store.textDynamic?.get(1) ?: 0.0).toFloat() },
            com.aurea.aurea.editor.formatCounter(d[1], d[3].toInt(), d[4] > .5), 0.5f, -1e9f, 1e9f, "contador de") {
            store.setTextDynamicValue(1, kotlin.math.round(it.toDouble() * 1e6) / 1e6)
        }
        TextRuler(env, stringResource(R.string.text_counter_to), { (store.textDynamic?.get(2) ?: 100.0).toFloat() },
            com.aurea.aurea.editor.formatCounter(d[2], d[3].toInt(), d[4] > .5), 0.5f, -1e9f, 1e9f, "contador ate") {
            store.setTextDynamicValue(2, kotlin.math.round(it.toDouble() * 1e6) / 1e6)
        }
        TextRuler(env, stringResource(R.string.text_counter_decimals), { (store.textDynamic?.get(3) ?: 0.0).toFloat() },
            "${d[3].toInt()}", 0.05f, 0f, 6f, "casas do contador") {
            store.setTextDynamicValue(3, kotlin.math.round(it).toDouble())
        }
        ToggleLine(stringResource(R.string.text_counter_grouping), d[4] > .5) { store.setTextDynamicValue(4, if (it) 1.0 else 0.0) }
        // Curva do contador: Linear (1), Suave (5), Entrada (3), Saída (4).
        val eases = listOf(1 to R.string.panel_linear, 5 to R.string.pn_ease_in_out, 3 to R.string.pn_ease_in, 4 to R.string.pn_ease_out)
        ChipRow(stringResource(R.string.panel_curva), eases.map { stringResource(it.second) }, eases.indexOfFirst { it.first == d[5].toInt() }) {
            store.setTextDynamicValue(5, eases[it].first.toDouble())
        }
    }
}

/** Espaçamento e caixa de parágrafo (ver `set_text_style`). */
@Composable
private fun TextLayoutSection(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.textStyle } }
    val v = st ?: return
    if (v.size >= 20) PanelSection(stringResource(R.string.edt_prop_spacing), com.aurea.aurea.ui.theme.CupertinoGlyph.ArrowLeftRight, "text.sec.spacing") {
        TextRuler(env, stringResource(R.string.text_line_spacing), { (store.textStyle?.get(18) ?: 1.2f) * 100 },
            "${(v[18] * 100).roundToInt()}%", 0.5f, 10f, 1000f, stringResource(R.string.text_line_spacing)) {
            store.setTextStyleValue(18, it / 100)
        }
        TextRuler(env, stringResource(R.string.text_letter_spacing), { store.textStyle?.get(19) ?: 0f },
            "${v[19].roundToInt()} ‰ em", 0.25f, -1000f, 1000f, stringResource(R.string.text_letter_spacing), keyProperty = com.aurea.aurea.engine.TrackProperty.TEXT_TRACKING) {
            store.setTextStyleValue(19, it)
        }
    }
    PanelSection(stringResource(R.string.panel_caixa), com.aurea.aurea.ui.theme.CupertinoGlyph.Square, "text.sec.box") {
        PanelItemGrid(listOf(
            Triple(0, R.string.panel_livre, com.aurea.aurea.ui.theme.CupertinoGlyph.Move), Triple(1, R.string.panel_paragrafo, com.aurea.aurea.ui.theme.CupertinoGlyph.LineHorizontal3),
            Triple(2, R.string.panel_tamanho_fixo, com.aurea.aurea.ui.theme.CupertinoGlyph.Square), Triple(3, R.string.panel_encolher_caber, com.aurea.aurea.ui.theme.CupertinoGlyph.FullscreenExit),
        ).map { (m, label, glyph) -> PanelItem(glyph, stringResource(label), "text.box.$m", on = v[0].toInt() == m) { store.setTextStyleValue(0, m.toFloat()) } })
        if (v[0] >= 1f) {
            Spacer(Modifier.height(4.dp))
            TextRuler(env, stringResource(R.string.panel_largura_caixa), { store.textStyle?.get(1) ?: 800f }, "${v[1].roundToInt()} px", 2f, 10f, 20000f, stringResource(R.string.panel_largura_caixa_4a83)) {
                store.setTextStyleValue(1, it)
            }
            if (v[0] >= 2f) {
                TextRuler(env, stringResource(R.string.panel_altura_caixa), { store.textStyle?.get(2) ?: 200f }, "${v[2].roundToInt()} px", 2f, 10f, 20000f, stringResource(R.string.panel_altura_caixa_0cec)) {
                    store.setTextStyleValue(2, it)
                }
            }
        }
    }
}

/** Fundo e sombra do texto (ver `set_text_style`). */
@Composable
private fun TextBackdropSection(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.textStyle } }
    val v = st ?: return
    PanelSection(stringResource(R.string.panel_fundo), com.aurea.aurea.ui.theme.CupertinoGlyph.Square, "text.sec.backdrop", startOpen = v[3] > 0.5f,
        trailing = { AureaToggle(checked = v[3] > 0.5f, onCheckedChange = { store.setTextStyleValue(3, if (it) 1f else 0f) }) }) {
        ColorLine(stringResource(R.string.panel_cor), Color(v[4], v[5], v[6])) {
            store.beginGesture("cor do fundo do texto")
            env.openColor(ColorRequest(floatArrayOf(v[4], v[5], v[6], v[7]),
                onChange = { r, g, b, a -> store.setTextStyleValues(mapOf(4 to r, 5 to g, 6 to b, 7 to a)) }, onDone = { store.endGesture() }))
        }
        TextRuler(env, stringResource(R.string.panel_margem_fundo), { store.textStyle?.get(8) ?: 14f }, "${v[8].roundToInt()} px", 0.2f, 0f, 500f, stringResource(R.string.panel_margem_fundo_276c)) {
            store.setTextStyleValue(8, it)
        }
        TextRuler(env, stringResource(R.string.panel_cantos_arredondados), { store.textStyle?.get(9) ?: 10f }, "${v[9].roundToInt()} px", 0.2f, 0f, 500f, stringResource(R.string.panel_raio_fundo)) {
            store.setTextStyleValue(9, it)
        }
    }
    PanelSection(stringResource(R.string.panel_sombra), com.aurea.aurea.ui.theme.CupertinoGlyph.CircleLefthalfFill, "text.sec.shadow", startOpen = v[10] > 0.5f,
        trailing = { AureaToggle(checked = v[10] > 0.5f, onCheckedChange = { store.setTextStyleValue(10, if (it) 1f else 0f) }) }) {
        ColorLine(stringResource(R.string.panel_cor), Color(v[11], v[12], v[13])) {
            store.beginGesture("cor da sombra do texto")
            env.openColor(ColorRequest(floatArrayOf(v[11], v[12], v[13], v[14]),
                onChange = { r, g, b, a -> store.setTextStyleValues(mapOf(11 to r, 12 to g, 13 to b, 14 to a)) }, onDone = { store.endGesture() }))
        }
        TextRuler(env, stringResource(R.string.panel_distancia_x), { store.textStyle?.get(15) ?: 4f }, "${v[15].roundToInt()} px", 0.2f, -500f, 500f, stringResource(R.string.panel_sombra_x)) { store.setTextStyleValue(15, it) }
        TextRuler(env, stringResource(R.string.panel_distancia_y), { store.textStyle?.get(16) ?: 6f }, "${v[16].roundToInt()} px", 0.2f, -500f, 500f, stringResource(R.string.panel_sombra_y)) { store.setTextStyleValue(16, it) }
        TextRuler(env, stringResource(R.string.panel_desfoque), { store.textStyle?.get(17) ?: 6f }, "${v[17].roundToInt()} px", 0.1f, 0f, 200f, stringResource(R.string.panel_desfoque_sombra)) { store.setTextStyleValue(17, it) }
    }
}

/** As propriedades do painel de texto que o ◆ e a curva da coluna alcançam. */
private val TEXT_RAIL_PROPERTIES = setOf(
    com.aurea.aurea.engine.TrackProperty.TEXT_ANIM_PARAM,
    com.aurea.aurea.engine.TrackProperty.TEXT_TRACKING,
    com.aurea.aurea.engine.TrackProperty.TEXT_PATH_FIRST_MARGIN,
    com.aurea.aurea.engine.TrackProperty.TEXT_PATH_LAST_MARGIN,
)

@Composable
internal fun TextRuler(
    env: PanelEnv,
    label: String,
    value: () -> Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    gesture: String,
    keyProperty: Int? = null,
    keyTag: String = "text.tracking.key",
    onValue: (Float) -> Unit,
) {
    val store = env.store
    // Como no Alight, o losango fica no rótulo: tocar nele escolhe a propriedade e o ◆ da
    // coluna marca o keyframe.
    val track = keyProperty?.let { com.aurea.aurea.engine.TrackKey(it) }
    val id = store.primary
    val row = store.layers.firstOrNull { it.id == id }
    val local = store.playhead - (row?.startFrame ?: 0) + (row?.offsetFrames ?: 0)
    val keys = if (keyProperty != null) store.keyframes[id].orEmpty().filter { it.property == keyProperty } else emptyList()
    val look = when {
        keys.any { it.time == local } -> com.aurea.aurea.ui.ds.KeyframeLook.KeyHere
        keys.isNotEmpty() -> com.aurea.aurea.ui.ds.KeyframeLook.Animated
        else -> com.aurea.aurea.ui.ds.KeyframeLook.None
    }
    PropertyCustomRow(label, selected = track != null && store.timelineFocus?.singleOrNull() == track,
        onSelect = { if (track != null) store.timelineFocus = listOf(track) }, keyframe = look) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp).then(if (track != null) Modifier.testTag(keyTag) else Modifier)) {
                TickRuler(
                    value = value,
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { if (track != null) store.timelineFocus = listOf(track); store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = {
                env.openKeypad(KeypadRequest(label, value(), when { text.endsWith("%") -> "%"; text.endsWith("‰ em") -> "‰ em"; else -> "px" }, min, max, 1) {
                    store.beginGesture(gesture); onValue(it.coerceIn(min, max)); store.endGesture()
                })
            })
        }
    }
}
