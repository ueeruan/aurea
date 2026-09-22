package com.aurea.aurea.home

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

/** O que a folha devolve ao criar. */
internal data class NewProjectSpec(val width: Int, val height: Int, val fps: Int, val title: String)

private val EaseOutCubic = CubicBezierEasing(0.33f, 1f, 0.68f, 1f)

/**
 * A folha "Novo projeto" da A.01 (`new_project_sheet.dart@762dbfe`, spec
 * §5.17): o FORMATO se escolhe olhando — a moldura no alto é desenhada na
 * proporção real e anima de uma forma para a outra. Nome, resolução e fps
 * vêm depois, e o nome já vem sugerido ("Projeto N").
 *
 * Os padrões (proporção, resolução, fps) vêm dos Ajustes ([HomeViewModel]).
 */
@Composable
internal fun NewProjectSheet(
    suggestedName: String,
    defaultAspectKey: String,
    defaultResolution: Int,
    defaultFps: Int,
    onCreate: (NewProjectSpec) -> Unit,
    onDismiss: () -> Unit,
) {
    var aspectKey by rememberSaveable { mutableStateOf(defaultAspectKey) }
    var free by rememberSaveable { mutableStateOf(false) }
    var resolution by rememberSaveable { mutableIntStateOf(defaultResolution) }
    var fps by rememberSaveable { mutableIntStateOf(defaultFps) }
    var name by rememberSaveable { mutableStateOf("") }
    var freeWidth by rememberSaveable { mutableStateOf("1080") }
    var freeHeight by rememberSaveable { mutableStateOf("1350") }

    val aspect = ProjectPresets.aspectByKey(aspectKey)
    // Medida livre: lida com clamp [64, 7680]; inválido → o padrão 1080 × 1350.
    val fw = (freeWidth.toIntOrNull() ?: 1080).coerceIn(64, 7680)
    val fh = (freeHeight.toIntOrNull() ?: 1350).coerceIn(64, 7680)
    val frame = if (free) Frame(fw, fh) else frameFor(aspect.ratio, resolution)
    val ratioOnScreen = if (free) (fw.toFloat() / fh).coerceIn(0.2f, 5f) else aspect.ratio

    fun create() {
        val title = name.trim().ifEmpty { suggestedName.ifEmpty { "Projeto sem titulo" } }
        onDismiss()
        onCreate(NewProjectSpec(frame.width, frame.height, fps, title))
    }

    AureaModalSheet(onDismiss = onDismiss, topRadius = HomeDims.SheetTopRadius) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(start = 20.dp, top = 12.dp, end = 20.dp, bottom = 16.dp),
        ) {
            Row(verticalAlignment = Alignment.Bottom) {
                Text("Novo projeto", style = AureaType.TitleLarge, modifier = Modifier.weight(1f))
                // A ficha, viva: muda com cada escolha.
                Text("${frame.width} × ${frame.height} · $fps fps", style = HomeType.SheetSpec)
            }
            Spacer(Modifier.height(16.dp))
            AspectPreviewFrame(
                ratio = ratioOnScreen,
                label = if (free) "$fw × $fh" else aspect.label,
                hint = if (free) "Medida livre" else aspect.hint,
            )
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth()) {
                ProjectPresets.aspects.forEach { option ->
                    AspectOptionItem(option, selected = !free && option.key == aspectKey, modifier = Modifier.weight(1f)) {
                        free = false
                        aspectKey = option.key
                    }
                }
                AspectOptionItem(ProjectPresets.free, selected = free, modifier = Modifier.weight(1f)) { free = true }
            }
            if (free) {
                Spacer(Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    DimensionField("Largura", freeWidth, Modifier.weight(1f)) { freeWidth = it }
                    Text("×", style = HomeType.Times, modifier = Modifier.padding(horizontal = 10.dp, vertical = 10.dp))
                    DimensionField("Altura", freeHeight, Modifier.weight(1f)) { freeHeight = it }
                }
            }
            Spacer(Modifier.height(20.dp))
            CapsLabel("Nome")
            Spacer(Modifier.height(8.dp))
            SheetTextField(
                value = name,
                onValueChange = { name = it },
                placeholder = suggestedName.ifEmpty { "Nome do projeto" },
                textStyle = HomeType.NameField,
                placeholderStyle = HomeType.NamePlaceholder,
                keyboard = KeyboardOptions(capitalization = KeyboardCapitalization.Sentences, imeAction = ImeAction.Done),
                onDone = { create() },
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(AureaColors.SurfaceHigh)
                    .padding(horizontal = 14.dp, vertical = 13.dp),
            )
            if (!free) {
                Spacer(Modifier.height(18.dp))
                CapsLabel("Resolução")
                Spacer(Modifier.height(8.dp))
                AureaSegmented(
                    ProjectPresets.resolutions, resolution, ProjectPresets::resolutionLabel, { resolution = it },
                    AureaColors.SurfaceHigh, AureaColors.Background, 7.dp,
                )
            }
            Spacer(Modifier.height(18.dp))
            CapsLabel("Quadros por segundo")
            Spacer(Modifier.height(8.dp))
            AureaSegmented(
                ProjectPresets.fpsOptions, fps, { "$it fps" }, { fps = it },
                AureaColors.SurfaceHigh, AureaColors.Background, 7.dp,
            )
            Spacer(Modifier.height(24.dp))
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(52.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(AureaColors.Accent)
                    .pressHighlight { create() },
                contentAlignment = Alignment.Center,
            ) {
                Text("Criar projeto", style = HomeType.CreateButton)
            }
        }
    }
}

/**
 * `_Moldura`: 150 de altura, a proporção escolhida animada em 240 ms
 * (easeOutCubic), degradê e borda no destaque, o rótulo encolhe para caber
 * (uma medida livre bem magra deixa a moldura com ~30 dp de largura).
 */
@Composable
private fun AspectPreviewFrame(ratio: Float, label: String, hint: String) {
    val r by animateFloatAsState(ratio, tween(240, easing = EaseOutCubic), label = "moldura")
    Box(Modifier.fillMaxWidth().height(150.dp), contentAlignment = Alignment.Center) {
        // aspectRatio sem preencher = o `AspectRatio` do Flutter: o maior
        // retângulo na razão que cabe em (largura, 150).
        Box(
            Modifier
                .aspectRatio(r)
                .clip(RoundedCornerShape(12.dp))
                .background(FrameGradient)
                .border(1.2.dp, AureaColors.Accent.copy(alpha = 0.6f), RoundedCornerShape(12.dp)),
        ) {
            ScaleDownToFit(Modifier.fillMaxSize()) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(label, style = HomeType.FrameLabel)
                    Text(hint, style = HomeType.FrameHint)
                }
            }
        }
    }
}

private val FrameGradient = Brush.linearGradient(
    listOf(AureaColors.Accent.copy(alpha = 0.22f), AureaColors.Accent.copy(alpha = 0.06f)),
)

/**
 * `_FormatoItem`: mini-moldura (lado maior 28) na proporção do formato,
 * borda 1,2 muted / 2 destaque + fundo quando escolhido (160 ms), rótulo e dica.
 */
@Composable
private fun AspectOptionItem(option: AspectOption, selected: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val side = 28.dp
    val r = option.ratio
    val w by animateDpAsState(if (r >= 1f) side else side * r, tween(160), label = "formato-l")
    val h by animateDpAsState(if (r >= 1f) side / r else side, tween(160), label = "formato-a")
    val borderColor by animateColorAsState(if (selected) AureaColors.Accent else AureaColors.Muted, tween(160), label = "formato-borda")
    val fill by animateColorAsState(if (selected) AureaColors.Accent.copy(alpha = 0.2f) else Color.Transparent, tween(160), label = "formato-fundo")
    val borderWidth by animateDpAsState(if (selected) 2.dp else 1.2.dp, tween(160), label = "formato-traco")
    Column(
        modifier
            .clickable(interactionSource = null, indication = null, role = Role.RadioButton, onClick = onClick)
            .padding(vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.height(side), contentAlignment = Alignment.Center) {
            Box(
                Modifier
                    .size(w, h)
                    .clip(RoundedCornerShape(4.dp))
                    .background(fill)
                    .border(borderWidth, borderColor, RoundedCornerShape(4.dp)),
            )
        }
        Spacer(Modifier.height(7.dp))
        Text(option.label, style = HomeType.FormatLabel, color = if (selected) AureaColors.Accent else AureaColors.Text)
        Text(option.hint, style = HomeType.FormatHint, maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
    }
}

/** `_CampoDeMedida`: rótulo 11 + campo numérico (fundo #1B2530, raio 10). */
@Composable
private fun DimensionField(label: String, value: String, modifier: Modifier, onChange: (String) -> Unit) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = HomeType.DimLabel)
        SheetTextField(
            value = value,
            onValueChange = { v -> onChange(v.filter { it.isDigit() }.take(5)) },
            placeholder = "",
            textStyle = HomeType.DimField,
            placeholderStyle = HomeType.DimField,
            keyboard = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
            onDone = null,
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(10.dp))
                .background(AureaColors.SurfaceHigh)
                .padding(horizontal = 12.dp, vertical = 10.dp),
        )
    }
}

/** O `CupertinoTextField` da folha: texto e placeholder no mesmo estilo base. */
@Composable
internal fun SheetTextField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    textStyle: TextStyle,
    placeholderStyle: TextStyle,
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
