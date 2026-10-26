package com.aurea.aurea.home

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AureaDropdown
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * O que a folha devolve ao criar. `fps` é livre (1–240, decimais como 29,97);
 * `background` é o fundo da composição em RGB sRGB. [aspectKey]/[resolution]
 * são nulos na medida livre; [keepRatio] é a corrente de L × A (lembrada).
 */
internal data class NewProjectSpec(
    val width: Int,
    val height: Int,
    val fps: Double,
    val title: String,
    val background: FloatArray = floatArrayOf(0f, 0f, 0f),
    /** "Transparente": sem fundo (o motor recebe alfa 0; PNG e GIF saem com alfa). */
    val transparent: Boolean = false,
    /** Interface do projeto (0.2.5): 0 = Composição (padrão), 1 = Edição. */
    val uiMode: Int = com.aurea.aurea.state.UI_MODE_COMPOSITION,
    val aspectKey: String? = null,
    val resolution: Int? = null,
    val free: Boolean = false,
    val keepRatio: Boolean = true,
)

/** O que a folha abre escolhido (a última escolha lembrada ou os padrões dos Ajustes). */
internal data class NewProjectDefaults(
    val aspectKey: String,
    val resolution: Int,
    val fps: Double,
    val free: Boolean,
    val freeWidth: Int,
    val freeHeight: Int,
    val keepRatio: Boolean,
    /** "r,g,b" (0–1), "t" (transparente) ou nulo (preto). */
    val background: String?,
)

/** As taxas da caixa de fps (as da referência) + "Personalizado…". */
internal val NewProjectFpsOptions = listOf(12, 15, 18, 20, 24, 25, 30, 48, 50, 60)

/** Os fundos de um toque (os da referência); "Outra cor…" abre o seletor. */
internal val NewProjectBackgrounds = listOf(
    R.string.sh_bg_black to floatArrayOf(0f, 0f, 0f),
    R.string.sh_bg_white to floatArrayOf(1f, 1f, 1f),
    R.string.om_new_bg_light_grey to floatArrayOf(0.8f, 0.8f, 0.8f),
    R.string.om_new_bg_green to floatArrayOf(0f, 0.5f, 0f),
    R.string.om_new_bg_blue to floatArrayOf(0f, 0f, 1f),
)

/** Medida livre: 64–7680 (a regra do AUREA, mais larga que a da referência). */
private const val FREE_MIN = 64
private const val FREE_MAX = 7680

/** Ids das proporções na folha (`newProject.aspect.16x9`, …). */
private fun aspectTag(key: String) = "newProject.aspect." + if (key == ProjectPresets.free.key) "free" else key.replace(':', 'x')

/** A ordem dos formatos na folha (a da referência): 16:9, 9:16, 4:5, 1:1, 4:3. */
private val SheetAspectOrder = listOf("16:9", "9:16", "4:5", "1:1", "4:3")

/**
 * Omega home · P1-6: a folha "Novo projeto" por dentro como a da referência.
 * Cobertura preta a 70 % + folha clara (`LightSheet`, cantos de 20) que sobe
 * 500 dp em 200 ms e desce em 200 ms. De cima para baixo: Interface (alternador
 * de 44 dp, a dica embaixo) · 35 · Nome (40 dp com ✕) · 10 · Proporções (6
 * ícones desenhados na proporção) · 16 · Resolução (caixa 180 × 45; na Livre,
 * L e A com a corrente) · 16 · Taxa de quadros · 16 · Fundo · 16 · "Criar
 * projeto" · ✕ de 60 dp. O gatilho continua o "+" do AUREA (D4).
 */
@Composable
internal fun NewProjectSheet(
    suggestedName: String,
    defaults: NewProjectDefaults,
    onCreate: (NewProjectSpec) -> Unit,
    onDismiss: () -> Unit,
    /** O que o motor decidiu para o aparelho (nulo = motor subindo: nada a avisar). */
    device: com.aurea.aurea.engine.DeviceReport? = null,
) {
    var aspectKey by rememberSaveable { mutableStateOf(ProjectPresets.aspectByKey(defaults.aspectKey).key) }
    var free by rememberSaveable { mutableStateOf(defaults.free) }
    var resolution by rememberSaveable { mutableIntStateOf(defaults.resolution) }
    var fps by rememberSaveable { mutableDoubleStateOf(defaults.fps) }
    val initialBg = remember { parseBackground(defaults.background) }
    var background by rememberSaveable { mutableStateOf(initialBg.first) }
    var transparent by rememberSaveable { mutableStateOf(initialBg.second) }
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var pickingBackground by remember { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var freeWidth by rememberSaveable { mutableStateOf(defaults.freeWidth.toString()) }
    var freeHeight by rememberSaveable { mutableStateOf(defaults.freeHeight.toString()) }
    var keepRatio by rememberSaveable { mutableStateOf(defaults.keepRatio) }
    var lockRatio by rememberSaveable {
        mutableFloatStateOf(defaults.freeWidth.toFloat() / defaults.freeHeight.coerceAtLeast(1))
    }
    // A interface NÃO lembra a última escolha: toda folha nova abre na
    // Composição, a principal (pedido de 2026-10-18).
    var uiMode by remember { mutableIntStateOf(com.aurea.aurea.state.UI_MODE_COMPOSITION) }

    val aspect = ProjectPresets.aspectByKey(aspectKey)
    val fw = (freeWidth.toIntOrNull() ?: 1080).coerceIn(FREE_MIN, FREE_MAX)
    val fh = (freeHeight.toIntOrNull() ?: 1350).coerceIn(FREE_MIN, FREE_MAX)
    val frame = if (free) Frame(fw, fh) else frameFor(aspect.ratio, resolution)

    // O texto padrão sai do catálogo AQUI: `create()` é chamada de um onClick.
    val untitled = stringResource(R.string.new_project_untitled)
    var open by remember { mutableStateOf(false) }
    var created by remember { mutableStateOf<NewProjectSpec?>(null) }
    LaunchedEffect(Unit) { open = true }
    val close = { open = false }
    fun create() {
        val title = name.trim().ifEmpty { suggestedName.ifEmpty { untitled } }
        created = NewProjectSpec(
            frame.width, frame.height, fps, title, background.copyOf(), transparent, uiMode,
            aspectKey = if (free) null else aspectKey, resolution = if (free) null else resolution,
            free = free, keepRatio = keepRatio,
        )
        open = false
    }
    val cover by animateFloatAsState(if (open) 1f else 0f, tween(200), label = "cobertura")
    val slide by animateDpAsState(
        if (open) 0.dp else 500.dp,
        tween(200, easing = if (open) FastOutSlowInEasing else LinearOutSlowInEasing),
        label = "folha",
        finishedListener = { if (!open) {
            onDismiss()
            created?.let(onCreate)
        } },
    )

    Dialog(onDismissRequest = close, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        // A cobertura é desenhada aqui (com a animação), não pelo escurecer da janela.
        val window = (LocalView.current.parent as? DialogWindowProvider)?.window
        SideEffect { window?.setDimAmount(0f) }
        val title = stringResource(R.string.new_project_title)
        BoxWithConstraints(Modifier.fillMaxSize()) {
            Box(
                Modifier
                    .fillMaxSize()
                    .background(AureaColors.HomeCover.copy(alpha = AureaColors.HomeCover.alpha * cover))
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = close),
            )
            Column(
                Modifier
                    .align(Alignment.BottomCenter)
                    .offset(y = slide)
                    .fillMaxWidth()
                    .heightIn(max = maxHeight - 24.dp)
                    .clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                    .background(AureaColors.LightSheet)
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
                    .semantics { paneTitle = title }
                    .testTag("newProject.sheet")
                    .windowInsetsPadding(WindowInsets.navigationBars)
                    .imePadding(),
            ) {
                Column(
                    Modifier
                        .weight(1f, fill = false)
                        .verticalScroll(rememberScrollState())
                        .padding(start = 20.dp, top = 24.dp, end = 20.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    // (1) Interface: Composição | Edição.
                    UiModeToggle(uiMode) { uiMode = it }
                    Spacer(Modifier.height(6.dp))
                    Text(
                        stringResource(if (uiMode == com.aurea.aurea.state.UI_MODE_EDIT) R.string.ui_mode_edit_hint else R.string.ui_mode_composition_hint),
                        style = AureaType.of(12f, color = AureaColors.LightTextMuted),
                        textAlign = TextAlign.Center,
                        modifier = Modifier.widthIn(max = 320.dp).fillMaxWidth(),
                    )
                    Spacer(Modifier.height(29.dp))
                    // (3) Nome.
                    NameField(name, { name = it }, suggestedName.ifEmpty { stringResource(R.string.new_project_name_hint) }) { create() }
                    Spacer(Modifier.height(10.dp))
                    // (5) Proporções.
                    AspectRow(selectedKey = if (free) ProjectPresets.free.key else aspectKey) { key ->
                        if (key == ProjectPresets.free.key) free = true else { free = false; aspectKey = key }
                    }
                    Spacer(Modifier.height(16.dp))
                    // (7) Resolução (ou L × A na Livre).
                    val over = device?.takeIf { d -> d.maxExportHeight > 0 }
                    fun overLimit(w: Int, h: Int) = over != null &&
                        (minOf(w, h) > over.maxExportHeight || (over.maxExportWidth > 0 && maxOf(w, h) > over.maxExportWidth))
                    SheetRow(
                        label = stringResource(R.string.settings_resolution),
                        sub1 = stringResource(R.string.om_new_frame_size),
                        sub2 = "${frame.width} × ${frame.height}",
                    ) {
                        if (free) {
                            FreeSizeFields(
                                width = freeWidth, height = freeHeight, keepRatio = keepRatio,
                                onWidth = { v ->
                                    freeWidth = v
                                    val w = v.toIntOrNull()
                                    if (keepRatio && w != null && lockRatio > 0f) freeHeight = (w / lockRatio).roundToInt().coerceIn(1, FREE_MAX).toString()
                                },
                                onHeight = { v ->
                                    freeHeight = v
                                    val h = v.toIntOrNull()
                                    if (keepRatio && h != null) freeWidth = (h * lockRatio).roundToInt().coerceIn(1, FREE_MAX).toString()
                                },
                                onKeepRatio = {
                                    keepRatio = !keepRatio
                                    if (keepRatio) lockRatio = fw.toFloat() / fh
                                },
                            )
                        } else {
                            val overLabel = stringResource(R.string.om_new_over_limit)
                            AureaDropdown(
                                value = "${resolution}p",
                                tag = "newProject.resolution",
                                contentDescription = stringResource(R.string.settings_resolution),
                                entries = ProjectPresets.resolutions.map { r ->
                                    val f = frameFor(aspect.ratio, r)
                                    val label = "${r}p" + if (overLimit(f.width, f.height)) " $overLabel" else ""
                                    MenuEntry.Item(label, selected = r == resolution, sublabel = ProjectPresets.resolutionLabel(r),
                                        tag = "newProject.resolution.$r") { resolution = r }
                                },
                            )
                        }
                    }
                    // §109: acima do que o aparelho exporta continua escolhível, mas a
                    // folha diz o teto e o porquê.
                    if (overLimit(frame.width, frame.height) && over != null) {
                        Spacer(Modifier.height(6.dp))
                        Text(
                            stringResource(
                                R.string.app_new_project_export_cap,
                                over.exportLimitReason(LocalContext.current)
                                    ?: stringResource(R.string.app_device_export_limit, com.aurea.aurea.engine.DeviceReport.shortLabel(over.maxExportHeight)),
                                frame.width, frame.height, com.aurea.aurea.engine.DeviceReport.shortLabel(over.maxExportHeight),
                            ),
                            style = AureaType.of(11f, color = AureaColors.LightTextMuted),
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                    Spacer(Modifier.height(16.dp))
                    // (9) Taxa de quadros.
                    val customLabel = stringResource(R.string.project_fps_custom)
                    val customTitle = stringResource(R.string.project_fps_custom_title)
                    val isPreset = NewProjectFpsOptions.any { abs(it - fps) < 0.001 }
                    SheetRow(label = stringResource(R.string.settings_fps)) {
                        AureaDropdown(
                            value = "${formatFps(fps.toFloat())} fps",
                            tag = "newProject.fps",
                            contentDescription = stringResource(R.string.settings_fps),
                            entries = NewProjectFpsOptions.map { v ->
                                MenuEntry.Item("$v fps", selected = isPreset && abs(v - fps) < 0.001, tag = "newProject.fps.$v") { fps = v.toDouble() }
                            } + MenuEntry.Item(customLabel, selected = !isPreset, tag = "newProject.fps.custom") {
                                keypad = KeypadRequest(customTitle, fps.toFloat(), "fps", 1f, 240f, 3) { typed -> fps = typed.toDouble() }
                            },
                        )
                    }
                    Spacer(Modifier.height(16.dp))
                    // (11) Fundo.
                    val preset = if (transparent) null
                        else NewProjectBackgrounds.firstOrNull { (_, c) -> (0..2).all { abs(c[it] - background[it]) < 0.01f } }
                    val bgName = when {
                        transparent -> stringResource(R.string.sh_bg_transparent)
                        preset != null -> stringResource(preset.first)
                        else -> stringResource(R.string.editor_outra_cor).trimEnd('…', '.')
                    }
                    SheetRow(label = stringResource(R.string.editor_plano_fundo)) {
                        AureaDropdown(
                            value = bgName,
                            tag = "newProject.background",
                            contentDescription = stringResource(R.string.editor_plano_fundo),
                            swatch = if (transparent) Color.Transparent else Color(background[0], background[1], background[2]),
                            checkerSwatch = transparent,
                            entries = NewProjectBackgrounds.map { (res, c) ->
                                MenuEntry.Item(stringResource(res), swatch = Color(c[0], c[1], c[2]), selected = preset?.first == res) {
                                    background = c.copyOf(); transparent = false
                                }
                            } + listOf(
                                MenuEntry.Item(stringResource(R.string.sh_bg_transparent), swatch = Color.Transparent, selected = transparent) {
                                    transparent = true
                                },
                                MenuEntry.Item(stringResource(R.string.editor_outra_cor), glyph = CupertinoGlyph.Eyedropper,
                                    selected = preset == null && !transparent) { pickingBackground = true },
                            ),
                        )
                    }
                    Spacer(Modifier.height(16.dp))
                    // (13) Criar projeto (botão de texto no fim).
                    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
                        Box(
                            Modifier.heightIn(min = 48.dp).testTag("newProject.create").tocavel(shrink = 1f) { create() }.padding(horizontal = 8.dp),
                            contentAlignment = Alignment.Center,
                        ) {
                            Text(stringResource(R.string.new_project_create), style = AureaType.of(16f, FontWeight.W700, 0f, AureaColors.LightAccentInk))
                        }
                    }
                }
                // (15) ✕ de 60 dp no centro de baixo.
                val cancel = stringResource(R.string.common_cancel)
                Box(
                    Modifier
                        .align(Alignment.CenterHorizontally)
                        .padding(top = 12.dp, bottom = 8.dp)
                        .size(60.dp)
                        .testTag("newProject.cancel")
                        .semantics { contentDescription = cancel }
                        .tocavel(onClick = close),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Xmark, 24.dp, AureaColors.LightText)
                }
            }
        }
        keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
        if (pickingBackground) {
            val initial = remember { floatArrayOf(background[0], background[1], background[2], 1f) }
            ColorPickerSheet(
                initial = initial,
                withAlpha = false,
                onChange = { r, g, b, _ -> background = floatArrayOf(r, g, b); transparent = false },
                onDone = { pickingBackground = false },
            )
        }
    }
}

/** "r,g,b" → cor; "t" → transparente; qualquer outra coisa → preto. */
internal fun parseBackground(saved: String?): Pair<FloatArray, Boolean> {
    if (saved == "t") return floatArrayOf(0f, 0f, 0f) to true
    val parts = saved?.split(',')?.mapNotNull { it.trim().toFloatOrNull() }
    if (parts != null && parts.size == 3 && parts.all { it in 0f..1f }) return parts.toFloatArray() to false
    return floatArrayOf(0f, 0f, 0f) to false
}

/**
 * Linha de campo da folha: rótulo 15 à esquerda (com as sublinhas 9 da
 * resolução) e o controle à direita.
 */
@Composable
private fun SheetRow(label: String, sub1: String? = null, sub2: String? = null, control: @Composable () -> Unit) {
    Row(Modifier.widthIn(max = 480.dp).fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).padding(end = 8.dp)) {
            Text(label, style = AureaType.of(15f, FontWeight.W500, 0f, AureaColors.LightText), maxLines = 2)
            if (sub1 != null) Text(sub1, style = AureaType.of(9f, color = AureaColors.LightTextMuted), maxLines = 1)
            if (sub2 != null) Text(sub2, style = AureaType.of(9f, color = AureaColors.LightTextMuted).merge(AureaType.Tabular), maxLines = 1)
        }
        control()
    }
}

/**
 * (1) A interface do projeto como o alternador Projeto · Elemento da
 * referência: 44 dp, contêiner branco de raio 8, dois botões de peso 1 com
 * 3 dp de margem; o ativo azul-marinho com o texto menta.
 */
@Composable
private fun UiModeToggle(selected: Int, onSelect: (Int) -> Unit) {
    val modes = listOf(com.aurea.aurea.state.UI_MODE_COMPOSITION, com.aurea.aurea.state.UI_MODE_EDIT)
    val labels = mapOf(
        com.aurea.aurea.state.UI_MODE_COMPOSITION to stringResource(R.string.ui_mode_composition),
        com.aurea.aurea.state.UI_MODE_EDIT to stringResource(R.string.ui_mode_edit),
    )
    OmegaSegmented(
        values = modes,
        selected = selected,
        label = { labels.getValue(it) },
        onChange = onSelect,
        modifier = Modifier.widthIn(max = 320.dp),
        height = 44.dp,
        radius = 8.dp,
        tag = { if (it == com.aurea.aurea.state.UI_MODE_EDIT) "newProject.uiMode.edit" else "newProject.uiMode.composition" },
    )
}

/** (3) O nome: campo de 40 dp (até 320), 15 sp, dica = nome sugerido e ✕ que limpa. */
@Composable
private fun NameField(value: String, onChange: (String) -> Unit, placeholder: String, onDone: () -> Unit) {
    val style = AureaType.of(15f, color = AureaColors.LightText)
    Row(
        Modifier
            .widthIn(max = 320.dp)
            .fillMaxWidth()
            .height(40.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(AureaColors.LightSurface)
            .padding(start = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (value.isEmpty()) Text(placeholder, style = style.copy(color = AureaColors.LightTextFaint), maxLines = 1, overflow = TextOverflow.Ellipsis)
            BasicTextField(
                value = value,
                onValueChange = { onChange(it.take(120)) },
                singleLine = true,
                textStyle = style,
                cursorBrush = SolidColor(AureaColors.LightAccentInk),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { onDone() }),
                modifier = Modifier.fillMaxWidth().testTag("newProject.name"),
            )
        }
        if (value.isNotEmpty()) {
            val clear = stringResource(R.string.om_new_clear_name)
            Box(
                Modifier.size(44.dp).testTag("newProject.name.clear").semantics { contentDescription = clear }
                    .tocavel(shrink = 1f) { onChange("") },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 18.dp, AureaColors.LightTextFaint) }
        }
    }
}

/**
 * (5) As 6 proporções (16:9, 9:16, 4:5, 1:1, 4:3, Livre) em 56 dp: cada ícone é
 * desenhado na proporção (lado maior 31 dp, canto 2, borda 1); a escolhida fica
 * preenchida. As células ocupam a largura toda: qualquer toque escolhe a mais perto.
 */
@Composable
private fun AspectRow(selectedKey: String, onSelect: (String) -> Unit) {
    val options = SheetAspectOrder.map { ProjectPresets.aspectByKey(it) } + ProjectPresets.free
    val freeLabel = stringResource(R.string.aspect_free)
    Row(Modifier.widthIn(max = 480.dp).fillMaxWidth().height(56.dp).padding(horizontal = 4.dp)) {
        options.forEach { o ->
            val on = o.key == selectedKey
            val isFree = o.key == ProjectPresets.free.key
            val label = if (isFree) freeLabel else o.label
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxSize()
                    .testTag(aspectTag(o.key))
                    .semantics { role = Role.RadioButton; selected = on; contentDescription = label }
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { onSelect(o.key) },
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                AspectGlyph(if (isFree) 1f else o.ratio, on, isFree)
                Spacer(Modifier.height(4.dp))
                Text(label, style = AureaType.of(11f, if (on) FontWeight.W700 else FontWeight.Normal, 0f,
                    if (on) AureaColors.LightText else AureaColors.LightTextMuted), maxLines = 1)
            }
        }
    }
}

/** O ícone de uma proporção (desenhado em código; a Livre só com as marcas de canto). */
@Composable
private fun AspectGlyph(ratio: Float, on: Boolean, free: Boolean) {
    val ink = if (on) AureaColors.LightInk else AureaColors.LightTextMuted
    val side: Dp = 31.dp
    val w = if (ratio >= 1f) side else side * ratio
    val h = if (ratio >= 1f) side / ratio else side
    Box(Modifier.size(side), contentAlignment = Alignment.Center) {
        Box(
            Modifier.size(w, h).drawBehind {
                val r = CornerRadius(2.dp.toPx())
                val stroke = 1.dp.toPx()
                if (free) {
                    // Marcas de canto de 5,5 dp.
                    val m = 5.5.dp.toPx()
                    val corners = listOf(Offset(0f, 0f) to Offset(1f, 1f), Offset(size.width, 0f) to Offset(-1f, 1f),
                        Offset(0f, size.height) to Offset(1f, -1f), Offset(size.width, size.height) to Offset(-1f, -1f))
                    corners.forEach { (p, d) ->
                        drawLine(ink, p, Offset(p.x + d.x * m, p.y), stroke * if (on) 2f else 1f)
                        drawLine(ink, p, Offset(p.x, p.y + d.y * m), stroke * if (on) 2f else 1f)
                    }
                } else {
                    if (on) drawRoundRect(ink, cornerRadius = r)
                    else drawRoundRect(ink, cornerRadius = r, style = Stroke(stroke), topLeft = Offset(stroke / 2, stroke / 2),
                        size = Size(size.width - stroke, size.height - stroke))
                }
            },
        )
    }
}

/**
 * Na Livre: L e A (68 dp cada) e a corrente que mantém a proporção (o ícone de
 * elo; ligado = tinta forte).
 */
@Composable
private fun FreeSizeFields(
    width: String,
    height: String,
    keepRatio: Boolean,
    onWidth: (String) -> Unit,
    onHeight: (String) -> Unit,
    onKeepRatio: () -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        DimensionField(width, stringResource(R.string.new_project_width), "newProject.width", onWidth)
        Text("×", style = AureaType.of(14f, color = AureaColors.LightTextMuted), modifier = Modifier.padding(horizontal = 4.dp))
        DimensionField(height, stringResource(R.string.new_project_height), "newProject.height", onHeight)
        val label = stringResource(R.string.om_new_keep_ratio)
        Box(
            Modifier
                .size(44.dp)
                .testTag("newProject.keepRatio")
                .semantics { role = Role.Switch; selected = keepRatio; contentDescription = label }
                .tocavel(shrink = 1f, onClick = onKeepRatio),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier.size(30.dp).clip(RoundedCornerShape(8.dp))
                    .background(if (keepRatio) AureaColors.LightInk else Color.Transparent)
                    .border(1.dp, if (keepRatio) AureaColors.LightInk else AureaColors.LightDivider, RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Link, 16.dp, if (keepRatio) AureaColors.LightOnInk else AureaColors.LightTextMuted)
            }
        }
    }
}

@Composable
private fun DimensionField(value: String, label: String, tag: String, onChange: (String) -> Unit) {
    BasicTextField(
        value = value,
        onValueChange = { v -> onChange(v.filter { it.isDigit() }.take(5)) },
        singleLine = true,
        textStyle = AureaType.of(15f, color = AureaColors.LightText).merge(AureaType.Tabular).copy(textAlign = TextAlign.Center),
        cursorBrush = SolidColor(AureaColors.LightAccentInk),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
        modifier = Modifier
            .size(68.dp, 40.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(AureaColors.LightSurface)
            .testTag(tag)
            .semantics { contentDescription = label },
        decorationBox = { inner -> Box(Modifier.fillMaxSize().padding(horizontal = 6.dp), contentAlignment = Alignment.Center) { inner() } },
    )
}

/** O `CupertinoTextField` antigo (ainda usado por quem pede texto simples na Home). */
@Composable
internal fun SheetTextField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    textStyle: androidx.compose.ui.text.TextStyle,
    placeholderStyle: androidx.compose.ui.text.TextStyle,
    keyboard: KeyboardOptions,
    onDone: (() -> Unit)?,
    modifier: Modifier,
) {
    val focusedAction = remember(onDone) { KeyboardActions(onDone = { onDone?.invoke() }) }
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle = textStyle,
        cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = keyboard,
        keyboardActions = if (onDone != null) focusedAction else KeyboardActions.Default,
        modifier = modifier,
        decorationBox = { inner ->
            Box {
                if (value.isEmpty() && placeholder.isNotEmpty()) Text(placeholder, style = placeholderStyle, maxLines = 1)
                inner()
            }
        },
    )
}

/**
 * A INTERFACE do projeto (0.2.5) em dois cartões (Composição, com o selo
 * "Padrão", e Edição). Fica para os Ajustes do projeto no editor (escuro); a
 * folha "Novo projeto" usa o alternador da referência ([UiModeToggle]).
 */
@Composable
internal fun UiModeChoice(selected: Int, onSelect: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        listOf(
            Triple(com.aurea.aurea.state.UI_MODE_COMPOSITION, R.string.ui_mode_composition, R.string.ui_mode_composition_hint),
            Triple(com.aurea.aurea.state.UI_MODE_EDIT, R.string.ui_mode_edit, R.string.ui_mode_edit_hint),
        ).forEach { (mode, title, hint) ->
            val active = mode == selected
            val name = stringResource(title)
            val detail = stringResource(hint)
            Column(
                Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(14.dp))
                    .background(if (active) AureaColors.AccentDim else AureaColors.SurfaceHigh)
                    .border(if (active) 2.dp else 1.dp, if (active) AureaColors.Accent else AureaColors.Hairline, RoundedCornerShape(14.dp))
                    .semantics(mergeDescendants = true) {
                        role = Role.RadioButton
                        this.selected = active
                        contentDescription = "$name. $detail"
                    }
                    .testTag(if (mode == com.aurea.aurea.state.UI_MODE_EDIT) "newProject.uiMode.edit" else "newProject.uiMode.composition")
                    .pressHighlight { onSelect(mode) }
                    .padding(horizontal = 12.dp, vertical = 12.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(name, style = AureaType.of(15f, FontWeight.W700, color = if (active) AureaColors.Accent else AureaColors.Text),
                        modifier = Modifier.weight(1f, fill = false))
                    if (mode == com.aurea.aurea.state.UI_MODE_COMPOSITION) {
                        Spacer(Modifier.width(6.dp))
                        Text(stringResource(R.string.ui_mode_default_badge), style = AureaType.of(10.5f, FontWeight.W700, color = AureaColors.Background),
                            modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(AureaColors.Accent).padding(horizontal = 5.dp, vertical = 1.dp))
                    }
                }
                Spacer(Modifier.height(4.dp))
                Text(detail, style = AureaType.of(12f, color = AureaColors.Muted, lineHeight = 1.3f))
            }
        }
    }
}
