package com.aurea.aurea.hyper

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaScreen
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay
import java.io.File
import kotlin.math.roundToInt

// =============================================================================
//  AureaHyper — a tela da Home (substitui o "Upscale com IA"). Quatro passos,
//  no mesmo visual do antigo Upscale; o estado é o de [HyperFlow]. Sem o
//  acesso (portão por conta, Entitlements.kt) a tela mostra o que o recurso
//  faz e que ele é pago, ainda sem venda — nenhum botão de compra.
//
//  Test tags (os mesmos accessibilityIdentifier do iOS, HyperView.swift):
//    hyper.voltar · hyper.passo.indicador
//    hyper.bloqueado · hyper.bloqueado.entrar · hyper.bloqueado.verificar
//    1: hyper.escolher.galeria · hyper.escolher.gravar · hyper.erro
//    2: hyper.receita.<id> · hyper.receita.<id>.<variante> · hyper.ajustar ·
//       hyper.ajuste.<campo> · hyper.continuar
//    3: hyper.medidor · hyper.medir · hyper.previa · hyper.previa.divisor ·
//       hyper.caminho.<turbo|lite|max> · hyper.comecar
//    4: hyper.etapa.<passo> · hyper.progresso · hyper.cancelar ·
//       hyper.tentar · hyper.voltar.previa
// =============================================================================

@Composable
fun HyperScreen(store: EditorStore) {
    val flow = store.hyper
    val conta: ContaViewModel = viewModel()
    val access = HyperEntitlements.access
    var signingIn by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { conta.atualizarAcessos() }
    // Entrou na conta pela tela bloqueada: confere o acesso da conta nova.
    LaunchedEffect(conta.logado) { if (conta.logado) { signingIn = false; conta.atualizarAcessos() } }
    val running = flow.exporter.state.phase == ExportPhase.Running || flow.exporter.state.phase == ExportPhase.Publishing
    BackHandler(enabled = !running && !flow.finishing && !flow.importing) { flow.back() }

    Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
        HyperTopBar(flow, enabled = !running && !flow.finishing && !flow.importing, showStep = access == HyperAccess.Unlocked)
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 18.dp)) {
            // Um job em andamento continua visível mesmo que o acesso mude no meio.
            if (access != HyperAccess.Unlocked && flow.step != HyperFlow.Step.Working) {
                HyperLockedPanel(access, onSignIn = { signingIn = true }, onRetry = { conta.atualizarAcessos() })
            } else when (flow.step) {
                HyperFlow.Step.Choose -> ChooseStep(flow)
                HyperFlow.Step.Recipe -> RecipeStep(flow)
                HyperFlow.Step.Preview -> PreviewStep(flow)
                HyperFlow.Step.Working -> WorkingStep(flow)
            }
            Spacer(Modifier.height(24.dp))
        }
    }
    if (signingIn) Dialog(onDismissRequest = { signingIn = false }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
            TextButton(onClick = { signingIn = false }, modifier = Modifier.align(Alignment.End).testTag("conta.fechar")) {
                Text(stringResource(R.string.editor_fechar))
            }
            Box(Modifier.weight(1f)) { ContaScreen(conta) }
        }
    }
}

@Composable
private fun HyperTopBar(flow: HyperFlow, enabled: Boolean, showStep: Boolean) {
    val back = stringResource(R.string.common_back)
    Row(Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(40.dp).testTag("hyper.voltar").semantics { contentDescription = back }
            .tocavel(enabled = enabled, onClick = { flow.back() }), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, if (enabled) AureaColors.Text else AureaColors.Disabled)
        }
        Text(stringResource(R.string.hyper_title), style = AureaType.TitleMedium,
            modifier = Modifier.padding(start = 4.dp).weight(1f).semantics { heading() })
        if (showStep) {
            val n = flow.step.ordinal + 1
            Text(stringResource(R.string.hyper_step_of, n, 4), style = AureaType.of(13f, color = AureaColors.Muted),
                modifier = Modifier.padding(end = 12.dp).testTag("hyper.passo.indicador"))
        }
    }
}

// ---- Bloqueado ------------------------------------------------------------------

/** O que o AureaHyper faz e por que está bloqueado. Também abre do editor (efeitos Hyper). */
@Composable
fun HyperLockedPanel(access: HyperAccess, onSignIn: () -> Unit, onRetry: () -> Unit, modifier: Modifier = Modifier) {
    val muted = AureaType.of(14f, color = AureaColors.Muted)
    val config = com.aurea.aurea.config.AppConfigStore.config
    val paid = config?.hyperMode == com.aurea.aurea.config.HyperMode.Entitled &&
        access != HyperAccess.NeedsConfig && access != HyperAccess.Off
    val badge = when {
        access == HyperAccess.Off -> R.string.hyper_off_badge
        paid -> R.string.hyper_locked_badge
        else -> null
    }
    val language = androidx.compose.ui.platform.LocalConfiguration.current.locales[0].language
    Column(modifier.fillMaxWidth().testTag("hyper.bloqueado")) {
        Spacer(Modifier.height(20.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(44.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent.copy(alpha = 0.18f)),
                contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.LockFill, 22.dp, AureaColors.Accent) }
            Spacer(Modifier.width(12.dp))
            Column {
                Text(stringResource(R.string.hyper_title), style = AureaType.TitleLarge, modifier = Modifier.semantics { heading() })
                // "Pago" só quando a config remota diz que o acesso é por conta.
                badge?.let { Text(stringResource(it), style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Accent)) }
            }
        }
        Spacer(Modifier.height(14.dp))
        Text(stringResource(R.string.hyper_locked_intro), style = AureaType.of(15f))
        Spacer(Modifier.height(14.dp))
        for (feature in listOf(R.string.hyper_feature_upscale, R.string.hyper_feature_clean, R.string.hyper_feature_face,
            R.string.hyper_feature_motion, R.string.hyper_feature_stable, R.string.hyper_feature_offline)) {
            Row(Modifier.padding(vertical = 5.dp), verticalAlignment = Alignment.Top) {
                CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, AureaColors.Accent)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(feature), style = AureaType.of(14f))
            }
        }
        if (paid) {
            Spacer(Modifier.height(16.dp))
            Text(stringResource(R.string.hyper_locked_payment_note), style = muted)
        }
        Spacer(Modifier.height(18.dp))
        val (status, button) = when (access) {
            HyperAccess.NeedsConfig -> R.string.hyper_needs_internet to R.string.home_retry
            HyperAccess.Off -> R.string.hyper_off_default to 0
            HyperAccess.SignedOut -> R.string.hyper_locked_signed_out to R.string.conta_entrar
            HyperAccess.Locked -> R.string.hyper_locked_not_owned to R.string.hyper_locked_check_again
            HyperAccess.NeedsCheck -> R.string.hyper_locked_offline to R.string.hyper_locked_check_again
            HyperAccess.Expired -> R.string.hyper_locked_expired to R.string.hyper_locked_check_again
            HyperAccess.Checking, HyperAccess.Unlocked -> R.string.hyper_locked_checking to 0
        }
        val statusText = if (access == HyperAccess.Off) config?.messageFor(language) ?: stringResource(status) else stringResource(status)
        Text(statusText, style = AureaType.of(14f, FontWeight.W600),
            modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface).padding(14.dp))
        if (access == HyperAccess.Checking) {
            Spacer(Modifier.height(12.dp))
            LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Surface)
        }
        if (button != 0) {
            Spacer(Modifier.height(14.dp))
            if (access == HyperAccess.SignedOut) HyperButton(stringResource(button), "hyper.bloqueado.entrar", onClick = onSignIn)
            else HyperButton(stringResource(button), "hyper.bloqueado.verificar", primary = false) {
                if (access == HyperAccess.NeedsConfig) com.aurea.aurea.config.AppConfigStore.refresh(force = true) else onRetry()
            }
        }
    }
}

/** O mesmo painel, em folha, para quem toca num efeito Hyper no editor. */
@Composable
fun HyperLockedDialog(onDismiss: () -> Unit) {
    val conta: ContaViewModel = viewModel()
    var signingIn by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { conta.atualizarAcessos() }
    LaunchedEffect(conta.logado) { if (conta.logado) { signingIn = false; conta.atualizarAcessos() } }
    val access = HyperEntitlements.access
    LaunchedEffect(access) { if (access == HyperAccess.Unlocked) onDismiss() }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
            TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End).testTag("hyper.bloqueado.fechar")) {
                Text(stringResource(R.string.editor_fechar))
            }
            if (signingIn) Box(Modifier.weight(1f)) { ContaScreen(conta) }
            else Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 18.dp)) {
                HyperLockedPanel(access, onSignIn = { signingIn = true }, onRetry = { conta.atualizarAcessos() })
                Spacer(Modifier.height(24.dp))
            }
        }
    }
}

// ---- 1. Escolher -------------------------------------------------------------

@Composable
private fun ChooseStep(flow: HyperFlow) {
    val context = LocalContext.current
    val muted = AureaType.of(13f, color = AureaColors.Muted)
    val gallery = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri -> if (uri != null) flow.pick(uri) }
    var captureUri by rememberSaveable { mutableStateOf<String?>(null) }
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.CaptureVideo()) { ok ->
        val uri = captureUri
        if (ok && uri != null) flow.pick(Uri.parse(uri))
    }
    val cameraDenied = stringResource(R.string.hyper_camera_denied)
    fun launchCamera() {
        val dir = File(context.cacheDir, "hyper-capture").apply { mkdirs() }
        val file = File(dir, "gravacao-${System.currentTimeMillis()}.mp4")
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.exports", file)
        captureUri = uri.toString()
        runCatching { camera.launch(uri) }.onFailure { flow.error = cameraDenied }
    }
    // O app declara CAMERA (Real Camera): sem a permissão concedida, o sistema
    // recusa o ACTION_VIDEO_CAPTURE — pede antes.
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) launchCamera() else flow.error = cameraDenied
    }
    flow.pending?.let { p ->
        Spacer(Modifier.height(16.dp))
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(AureaColors.Surface).padding(14.dp)
            .testTag("hyper.retomar")) {
            Text(stringResource(R.string.hyper_resume_title), style = AureaType.of(15f, FontWeight.W600))
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.hyper_resume_body, p.source.name), style = muted)
            Spacer(Modifier.height(12.dp))
            HyperButton(stringResource(R.string.hyper_resume_continue), "hyper.retomar.continuar", enabled = !flow.busy) { flow.resumePending() }
            Spacer(Modifier.height(8.dp))
            HyperButton(stringResource(R.string.hyper_resume_discard), "hyper.retomar.descartar", primary = false, enabled = !flow.busy) {
                flow.discardPending()
            }
        }
    }
    Spacer(Modifier.height(16.dp))
    Text(stringResource(R.string.hyper_choose_hint), style = muted)
    Spacer(Modifier.height(16.dp))
    SourceTile(CupertinoGlyph.PhotoOnRectangle, stringResource(R.string.hyper_choose_gallery), "hyper.escolher.galeria", !flow.busy) {
        gallery.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
    }
    Spacer(Modifier.height(12.dp))
    SourceTile(CupertinoGlyph.VideocamFill, stringResource(R.string.hyper_choose_record), "hyper.escolher.gravar", !flow.busy) {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) launchCamera()
        else permission.launch(Manifest.permission.CAMERA)
    }
    if (flow.importing) {
        Spacer(Modifier.height(18.dp))
        LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Surface)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.hyper_choose_copying), style = muted)
    }
    ErrorBox(flow.error)
    Spacer(Modifier.height(20.dp))
    Text(stringResource(R.string.hyper_offline_note), style = muted)
}

@Composable
private fun SourceTile(glyph: Char, label: String, tag: String, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxWidth().heightIn(min = 104.dp).clip(RoundedCornerShape(14.dp)).background(AureaColors.Surface)
            .testTag(tag).semantics { contentDescription = label }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick).padding(18.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            CupertinoIcon(glyph, 28.dp, if (enabled) AureaColors.Accent else AureaColors.Disabled)
            Spacer(Modifier.height(8.dp))
            Text(label, style = AureaType.of(15f, FontWeight.W600), textAlign = TextAlign.Center)
        }
    }
}

// ---- 2. O que fazer -------------------------------------------------------------

@Composable
private fun RecipeStep(flow: HyperFlow) {
    val s = flow.source ?: return
    val muted = AureaType.of(13f, color = AureaColors.Muted)
    Spacer(Modifier.height(12.dp))
    Text(s.name, style = AureaType.of(15f, FontWeight.W600), maxLines = 1)
    Text(listOf("${s.width} × ${s.height}", fpsText(s.fps.toDouble()), durationText(s.durationSeconds)).joinToString(" · "), style = muted)
    Spacer(Modifier.height(16.dp))
    Text(stringResource(R.string.hyper_recipe_title), style = AureaType.TitleLarge, modifier = Modifier.semantics { heading() })
    Spacer(Modifier.height(10.dp))
    for (p in HyperPreset.entries) {
        RecipeCard(p, selected = flow.preset == p, variant = flow.variant) { v -> flow.choose(p, v) }
        Spacer(Modifier.height(10.dp))
    }
    val advancedLabel = stringResource(R.string.hyper_adjust)
    Row(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Surface)
            .testTag("hyper.ajustar").semantics { contentDescription = advancedLabel; stateDescription = if (flow.advanced) "on" else "off" }
            .tocavel(onClick = { flow.advanced = !flow.advanced }).padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.SliderHorizontal3, 18.dp, AureaColors.Text)
        Spacer(Modifier.width(10.dp))
        Text(advancedLabel, style = AureaType.of(15f, FontWeight.W600), modifier = Modifier.weight(1f))
        CupertinoIcon(if (flow.advanced) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 16.dp, AureaColors.Muted)
    }
    if (flow.advanced) AdjustPanel(flow)
    Spacer(Modifier.height(18.dp))
    if (flow.recipe.identity) Text(stringResource(R.string.hyper_recipe_pick_one), style = muted)
    Spacer(Modifier.height(10.dp))
    HyperButton(stringResource(R.string.hyper_continue), "hyper.continuar", enabled = !flow.recipe.identity) { flow.toPreview() }
}

private fun presetTitle(p: HyperPreset): Int = when (p) {
    HyperPreset.Restore -> R.string.hyper_preset_restore
    HyperPreset.Sharper -> R.string.hyper_preset_sharper
    HyperPreset.Fps60 -> R.string.hyper_preset_fps60
    HyperPreset.SlowMotion -> R.string.hyper_preset_slowmo
    HyperPreset.Anime -> R.string.hyper_preset_anime
    HyperPreset.Stabilize -> R.string.hyper_preset_stabilize
}

private fun presetBody(p: HyperPreset): Int = when (p) {
    HyperPreset.Restore -> R.string.hyper_preset_restore_body
    HyperPreset.Sharper -> R.string.hyper_preset_sharper_body
    HyperPreset.Fps60 -> R.string.hyper_preset_fps60_body
    HyperPreset.SlowMotion -> R.string.hyper_preset_slowmo_body
    HyperPreset.Anime -> R.string.hyper_preset_anime_body
    HyperPreset.Stabilize -> R.string.hyper_preset_stabilize_body
}

private fun presetGlyph(p: HyperPreset): Char = when (p) {
    HyperPreset.Restore -> CupertinoGlyph.WandStars
    HyperPreset.Sharper -> CupertinoGlyph.Sparkles
    HyperPreset.Fps60 -> CupertinoGlyph.Hare
    HyperPreset.SlowMotion -> CupertinoGlyph.Tortoise
    HyperPreset.Anime -> CupertinoGlyph.Paintbrush
    HyperPreset.Stabilize -> CupertinoGlyph.Move
}

@Composable
private fun RecipeCard(p: HyperPreset, selected: Boolean, variant: Int, onChoose: (Int) -> Unit) {
    val title = stringResource(presetTitle(p))
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
            .background(if (selected) AureaColors.Accent.copy(alpha = 0.16f) else AureaColors.Surface)
            .border(1.dp, if (selected) AureaColors.Accent else Color.Transparent, RoundedCornerShape(14.dp))
            .testTag("hyper.receita.${p.id}")
            .semantics { role = Role.RadioButton; this.selected = selected; contentDescription = title }
            .tocavel(haptic = true, onClick = { onChoose(if (selected) variant else p.variants.firstOrNull() ?: 0) })
            .padding(14.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(presetGlyph(p), 20.dp, if (selected) AureaColors.Accent else AureaColors.Text)
            Spacer(Modifier.width(10.dp))
            Text(title, style = AureaType.of(15f, FontWeight.W600), modifier = Modifier.weight(1f))
            if (selected) CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 18.dp, AureaColors.Accent)
        }
        Spacer(Modifier.height(4.dp))
        Text(stringResource(presetBody(p)), style = AureaType.of(13f, color = AureaColors.Muted))
        if (p == HyperPreset.SlowMotion) {
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.hyper_audio_muted), style = AureaType.of(12f, color = AureaColors.Muted))
        }
        if (p == HyperPreset.Restore) {
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.hyper_face_risk_short), style = AureaType.of(12f, color = AureaColors.Warning))
        }
        if (p.variants.isNotEmpty()) {
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (v in p.variants) Chip("${v}×", "hyper.receita.${p.id}.$v", on = selected && variant == v) { onChoose(v) }
            }
        }
    }
}

@Composable
private fun AdjustPanel(flow: HyperFlow) {
    val r = flow.recipe
    val muted = AureaType.of(12f, color = AureaColors.Muted)
    Column(Modifier.fillMaxWidth().padding(top = 10.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Surface).padding(14.dp)) {
        Section(R.string.hyper_adjust_upscale)
        ChipRow(HyperRecipe.UpscaleFactors.map { if (it == 1) stringResource(R.string.common_off) else "${it}×" },
            HyperRecipe.UpscaleFactors.indexOf(r.upscale), "hyper.ajuste.upscale") { flow.edit(r.copy(upscale = HyperRecipe.UpscaleFactors[it])) }
        if (r.upscale > 1) {
            Spacer(Modifier.height(8.dp))
            ChipRow(listOf(stringResource(R.string.hyper_mode_general), stringResource(R.string.hyper_mode_anime)), r.upscaleMode.ordinal,
                "hyper.ajuste.modo") { flow.edit(r.copy(upscaleMode = HyperUpscaleMode.entries[it])) }
        }
        Section(R.string.hyper_adjust_clean)
        SliderRow(stringResource(R.string.hyper_adjust_denoise), r.denoise, "hyper.ajuste.ruido") { flow.edit(r.copy(denoise = it)) }
        SwitchRow(stringResource(R.string.hyper_adjust_deblock), r.deblock, "hyper.ajuste.blocos") { flow.edit(r.copy(deblock = it)) }
        Text(stringResource(R.string.hyper_adjust_deinterlace), style = AureaType.of(14f), modifier = Modifier.padding(top = 6.dp, bottom = 6.dp))
        ChipRow(listOf(stringResource(R.string.common_off), stringResource(R.string.common_automatic), stringResource(R.string.hyper_always)),
            r.deinterlace.coerceIn(0, 2), "hyper.ajuste.entrelacado") { flow.edit(r.copy(deinterlace = it)) }
        Section(R.string.hyper_adjust_face)
        SwitchRow(stringResource(R.string.hyper_adjust_face_on), r.face, "hyper.ajuste.rosto") { flow.edit(r.copy(face = it)) }
        Text(stringResource(R.string.hyper_face_risk), style = AureaType.of(12f, color = if (r.face) AureaColors.Warning else AureaColors.Muted))
        if (r.face && !flow.faceReady) Text(stringResource(R.string.hyper_face_model_missing),
            style = AureaType.of(12f, color = AureaColors.Muted), modifier = Modifier.padding(top = 4.dp).testTag("hyper.rosto.sem.modelo"))
        if (r.face) SliderRow(stringResource(R.string.hyper_adjust_strength), r.faceStrength, "hyper.ajuste.rosto.forca") {
            flow.edit(r.copy(faceStrength = it))
        }
        Section(R.string.hyper_adjust_motion)
        Text(stringResource(R.string.hyper_adjust_target_fps), style = AureaType.of(14f), modifier = Modifier.padding(bottom = 6.dp))
        ChipRow(HyperRecipe.TargetFpsChoices.map { if (it <= 0.0) stringResource(R.string.hyper_keep) else fpsText(it) },
            HyperRecipe.TargetFpsChoices.indexOf(r.targetFps).coerceAtLeast(0), "hyper.ajuste.fps") {
            flow.edit(r.copy(targetFps = HyperRecipe.TargetFpsChoices[it]))
        }
        Text(stringResource(R.string.hyper_adjust_slowmo), style = AureaType.of(14f), modifier = Modifier.padding(top = 10.dp, bottom = 6.dp))
        ChipRow(HyperRecipe.SlowMotionFactors.map { if (it == 1) stringResource(R.string.common_off) else "${it}×" },
            HyperRecipe.SlowMotionFactors.indexOf(r.slowMotion).coerceAtLeast(0), "hyper.ajuste.lento") {
            flow.edit(r.copy(slowMotion = HyperRecipe.SlowMotionFactors[it]))
        }
        Section(R.string.hyper_adjust_stable)
        SwitchRow(stringResource(R.string.hyper_adjust_stable_on), r.stabilize, "hyper.ajuste.estabilizar") { flow.edit(r.copy(stabilize = it)) }
        if (r.stabilize) {
            SliderRow(stringResource(R.string.hyper_adjust_strength), r.stabilizeStrength, "hyper.ajuste.estabilizar.forca") {
                flow.edit(r.copy(stabilizeStrength = it))
            }
            SwitchRow(stringResource(R.string.hyper_adjust_stable_keyframes), r.stabilizeToKeyframes, "hyper.ajuste.estabilizar.keyframes") {
                flow.edit(r.copy(stabilizeToKeyframes = it))
            }
            Text(stringResource(R.string.hyper_adjust_stable_keyframes_note), style = muted)
        }
    }
}

@Composable
private fun Section(title: Int) {
    Text(stringResource(title), style = AureaType.of(13f, FontWeight.W700, color = AureaColors.Muted),
        modifier = Modifier.padding(top = 14.dp, bottom = 8.dp).semantics { heading() })
}

@Composable
private fun ChipRow(labels: List<String>, selected: Int, tag: String, onSelect: (Int) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        labels.forEachIndexed { i, label -> Chip(label, "$tag.$i", on = i == selected) { onSelect(i) } }
    }
}

@Composable
private fun SliderRow(label: String, value: Float, tag: String, onChange: (Float) -> Unit) {
    val percent = (value * 100).roundToInt()
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = AureaType.of(14f), modifier = Modifier.weight(1f))
            Text("$percent%", style = AureaType.of(13f, color = AureaColors.Muted))
        }
        Slider(value = value, onValueChange = { onChange((it * 20).roundToInt() / 20f) }, valueRange = 0f..1f,
            modifier = Modifier.fillMaxWidth().testTag(tag).semantics { contentDescription = label; stateDescription = "$percent%" },
            colors = SliderDefaults.colors(thumbColor = AureaColors.Accent, activeTrackColor = AureaColors.Accent,
                inactiveTrackColor = AureaColors.Chip))
    }
}

@Composable
private fun SwitchRow(label: String, on: Boolean, tag: String, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = AureaType.of(14f), modifier = Modifier.weight(1f))
        Switch(checked = on, onCheckedChange = onChange, modifier = Modifier.testTag(tag).semantics { contentDescription = label },
            colors = SwitchDefaults.colors(checkedTrackColor = AureaColors.Accent, checkedThumbColor = AureaColors.OnAccent))
    }
}

// ---- 3. Prévia e estimativa ---------------------------------------------------------

@Composable
private fun PreviewStep(flow: HyperFlow) {
    val muted = AureaType.of(13f, color = AureaColors.Muted)
    Spacer(Modifier.height(12.dp))
    if (flow.measuring) MeterPanel(flow.measureStartedAt)
    BeforeAfter(flow)
    Spacer(Modifier.height(16.dp))
    Text(stringResource(R.string.hyper_path_title), style = AureaType.TitleMedium, modifier = Modifier.semantics { heading() })
    Text(stringResource(R.string.hyper_path_hint), style = muted)
    Spacer(Modifier.height(10.dp))
    for (p in HyperPath.Choices) {
        PathRow(flow, p)
        Spacer(Modifier.height(8.dp))
    }
    flow.estimate?.takeIf { it.outputWidth > 0 }?.let { e ->
        Text(stringResource(R.string.hyper_output, "${e.outputWidth} × ${e.outputHeight}", fpsText(e.outputFps)), style = muted)
    }
    if (flow.estimating) {
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.hyper_estimating), style = muted)
    }
    Spacer(Modifier.height(6.dp))
    TextButton(onClick = { flow.measure() }, enabled = !flow.measuring, modifier = Modifier.testTag("hyper.medir")) {
        Text(stringResource(R.string.hyper_measure_again), color = if (flow.measuring) AureaColors.Disabled else AureaColors.Accent)
    }
    ErrorBox(flow.error)
    Spacer(Modifier.height(10.dp))
    val canStart = !flow.measuring && !flow.estimating && !flow.checkingConfig && flow.estimate?.available(flow.path) != false
    HyperButton(stringResource(R.string.hyper_start), "hyper.comecar", enabled = canStart) { flow.start() }
}

/** O medidor (~10 s na primeira vez): barra pelo tempo esperado, nunca "100%" antes de acabar. */
@Composable
private fun MeterPanel(startedAt: Long) {
    var elapsed by remember { mutableLongStateOf(0L) }
    LaunchedEffect(startedAt) {
        while (true) { elapsed = android.os.SystemClock.elapsedRealtime() - startedAt; delay(200) }
    }
    val fraction = (elapsed / METER_EXPECTED_MS.toFloat()).coerceIn(0f, 0.95f)
    val seconds = ((METER_EXPECTED_MS - elapsed) / 1000).coerceAtLeast(1)
    Column(Modifier.fillMaxWidth().padding(bottom = 14.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Surface)
        .padding(14.dp).testTag("hyper.medidor")) {
        Text(stringResource(R.string.hyper_meter_title), style = AureaType.of(15f, FontWeight.W600))
        Spacer(Modifier.height(4.dp))
        Text(stringResource(R.string.hyper_meter_body), style = AureaType.of(13f, color = AureaColors.Muted))
        Spacer(Modifier.height(10.dp))
        LinearProgressIndicator(progress = { fraction }, modifier = Modifier.fillMaxWidth(), color = AureaColors.Accent,
            trackColor = AureaColors.Chip)
        Spacer(Modifier.height(6.dp))
        Text(if (elapsed < METER_EXPECTED_MS) stringResource(R.string.hyper_meter_left, seconds.toInt()) else stringResource(R.string.hyper_meter_almost),
            style = AureaType.of(12f, color = AureaColors.Muted))
    }
}

private const val METER_EXPECTED_MS = 10_000L

@Composable
private fun BeforeAfter(flow: HyperFlow) {
    val preview = flow.preview
    var split by rememberSaveable { mutableFloatStateOf(0.5f) }
    val description = stringResource(R.string.hyper_preview_a11y)
    BoxWithConstraints(
        Modifier.fillMaxWidth().aspectRatio(
            preview?.let { it.after.width.toFloat() / it.after.height.coerceAtLeast(1) }?.coerceIn(0.5f, 2.4f) ?: (16f / 9f))
            .clip(RoundedCornerShape(14.dp)).background(Color.Black).testTag("hyper.previa")
            .semantics { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) {
        val widthPx = with(LocalDensity.current) { maxWidth.toPx() }
        if (preview != null) {
            val after = remember(preview) { preview.after.asImageBitmap() }
            val before = remember(preview) { preview.before.asImageBitmap() }
            Image(after, null, Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
            Image(before, null, Modifier.fillMaxSize().drawWithContent {
                clipRect(right = size.width * split) { this@drawWithContent.drawContent() }
            }, contentScale = ContentScale.Fit)
            // O traço: arrastar em qualquer ponto da prévia move o corte.
            Box(Modifier.fillMaxSize().pointerInput(widthPx) {
                detectHorizontalDragGestures { change, _ -> split = (change.position.x / widthPx).coerceIn(0f, 1f) }
            })
            Box(Modifier.fillMaxHeight().width(2.dp).offset { IntOffset((widthPx * split).roundToInt() - 1, 0) }
                .align(Alignment.CenterStart).background(Color.White))
            Row(Modifier.align(Alignment.TopCenter).fillMaxWidth().padding(8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                Label(stringResource(R.string.fx_browser_before))
                Label(stringResource(R.string.fx_browser_after))
            }
        }
        if (flow.previewLoading) CircularProgressIndicator(color = AureaColors.Accent, modifier = Modifier.size(28.dp))
        else if (preview == null) Text(stringResource(if (flow.previewFailed) R.string.hyper_preview_failed else R.string.hyper_preview_waiting),
            style = AureaType.of(13f, color = Color.White), textAlign = TextAlign.Center, modifier = Modifier.padding(16.dp))
    }
    if (preview != null) {
        // O mesmo corte por um controle acessível (TalkBack ajusta com volume).
        val splitLabel = stringResource(R.string.hyper_preview_split)
        Slider(value = split, onValueChange = { split = it }, valueRange = 0f..1f,
            modifier = Modifier.fillMaxWidth().testTag("hyper.previa.divisor")
                .semantics { contentDescription = splitLabel; stateDescription = "${(split * 100).roundToInt()}%" },
            colors = SliderDefaults.colors(thumbColor = Color.White, activeTrackColor = AureaColors.Muted, inactiveTrackColor = AureaColors.Chip))
        if (preview.path != flow.path) Text(stringResource(R.string.hyper_preview_updating), style = AureaType.of(12f, color = AureaColors.Muted))
    }
}

@Composable
private fun Label(text: String) {
    Text(text, style = AureaType.of(11f, FontWeight.W700, color = Color.White),
        modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(Color.Black.copy(alpha = 0.55f)).padding(horizontal = 6.dp, vertical = 2.dp))
}

private fun pathName(p: HyperPath): Int = when (p) {
    HyperPath.Lite -> R.string.hyper_path_lite
    HyperPath.Max -> R.string.hyper_path_max
    else -> R.string.hyper_path_turbo
}

private fun pathBody(p: HyperPath): Int = when (p) {
    HyperPath.Lite -> R.string.hyper_path_lite_body
    HyperPath.Max -> R.string.hyper_path_max_body
    else -> R.string.hyper_path_turbo_body
}

@Composable
private fun PathRow(flow: HyperFlow, p: HyperPath) {
    val estimate = flow.estimate
    val block = HyperPathRules.block(p, estimate, flow.profile)
    val available = estimate == null || block == HyperPathBlock.None
    val on = flow.path == p
    val name = stringResource(pathName(p))
    val detail = when {
        flow.estimating || estimate == null -> stringResource(R.string.hyper_estimating)
        block != HyperPathBlock.None -> stringResource(when (block) {
            HyperPathBlock.NotMeasured -> R.string.hyper_block_not_measured
            HyperPathBlock.Needs64Bit -> R.string.hyper_block_32bit
            HyperPathBlock.NoGpuBackend -> R.string.hyper_block_no_gpu
            else -> R.string.hyper_block_unavailable
        })
        else -> stringResource(R.string.hyper_estimate_time, timeText(estimate.secondsFor(p)))
    }
    val suggested = flow.profile?.defaultPath == p
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp))
            .background(if (on && available) AureaColors.Accent.copy(alpha = 0.16f) else AureaColors.Surface)
            .border(1.dp, if (on && available) AureaColors.Accent else Color.Transparent, RoundedCornerShape(12.dp))
            .testTag("hyper.caminho.${p.name.lowercase()}")
            .semantics { role = Role.RadioButton; selected = on; contentDescription = "$name. $detail" }
            .tocavel(enabled = available && estimate != null, onClick = { flow.selectPath(p) })
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(name, style = AureaType.of(15f, FontWeight.W600, color = if (available) AureaColors.Text else AureaColors.Disabled))
                if (suggested) {
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.hyper_path_suggested), style = AureaType.of(11f, FontWeight.W700, color = AureaColors.Accent))
                }
            }
            Text(stringResource(pathBody(p)), style = AureaType.of(12f, color = AureaColors.Muted))
            Text(detail, style = AureaType.of(13f, FontWeight.W600, color = if (available) AureaColors.Text else AureaColors.Warning))
        }
        CupertinoIcon(if (on && available) CupertinoGlyph.CheckmarkCircleFill else CupertinoGlyph.Circle, 20.dp,
            if (on && available) AureaColors.Accent else AureaColors.Muted)
    }
}

// ---- 4. Trabalhando ---------------------------------------------------------

@Composable
private fun WorkingStep(flow: HyperFlow) {
    val st = flow.exporter.state
    val muted = AureaType.of(13f, color = AureaColors.Muted)
    LaunchedEffect(st.phase) { if (st.phase == ExportPhase.Done) flow.finishIfDone() }
    Spacer(Modifier.height(20.dp))
    val title = when {
        st.phase == ExportPhase.Done -> R.string.hyper_working_done
        st.phase == ExportPhase.Failed -> R.string.hyper_working_failed
        st.phase == ExportPhase.Cancelled -> R.string.hyper_working_cancelled
        flow.cancelRequested -> R.string.hyper_working_cancelling
        else -> R.string.hyper_working_title
    }
    Text(stringResource(title), style = AureaType.TitleLarge, modifier = Modifier.semantics { heading() })
    Spacer(Modifier.height(14.dp))
    val info = HyperJobInfo.decode(flow.exporter.hyperSteps)?.takeIf { it.active }
    for ((step, state) in visibleSteps(flow, info)) StepRow(step, state, info?.takeIf { it.present(step) }?.path(step))
    if (info != null && info.passCount > 1) {
        Spacer(Modifier.height(6.dp))
        Text(stringResource(R.string.hyper_pass, info.pass.coerceAtLeast(1), info.passCount), style = muted)
    }
    if (info?.audioMuted == true || flow.recipe.slowMotion > 1) {
        Spacer(Modifier.height(6.dp))
        Text(stringResource(R.string.hyper_audio_muted), style = muted)
    }
    Spacer(Modifier.height(14.dp))
    if (st.phase == ExportPhase.Running || st.phase == ExportPhase.Publishing) {
        LinearProgressIndicator(progress = { st.fraction }, modifier = Modifier.fillMaxWidth().testTag("hyper.progresso"),
            color = AureaColors.Accent, trackColor = AureaColors.Surface)
        Spacer(Modifier.height(8.dp))
        val parts = buildList {
            add("${(st.fraction * 100).toInt()}%")
            if (st.framesTotal > 0) add(stringResource(R.string.hyper_frames, st.framesDone, st.framesTotal))
            if (st.etaSeconds > 0) add(stringResource(R.string.hyper_time_left, timeText(st.etaSeconds.toDouble())))
        }
        Text(parts.joinToString(" · "), style = AureaType.of(14f, FontWeight.W600))
        if (st.notice.isNotBlank()) { Spacer(Modifier.height(6.dp)); Text(st.notice, style = muted) }
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.hyper_can_leave_android), style = muted)
        Spacer(Modifier.height(18.dp))
        HyperButton(stringResource(if (flow.cancelRequested) R.string.hyper_working_cancelling else R.string.common_cancel),
            "hyper.cancelar", primary = false, enabled = !flow.cancelRequested) { flow.cancel() }
    }
    if (st.phase == ExportPhase.Done) {
        if (flow.finishing) {
            LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Surface)
            Spacer(Modifier.height(8.dp))
            Text(stringResource(R.string.hyper_creating_project), style = muted)
        }
        ErrorBox(flow.error)
        if (flow.error != null && !flow.finishing) {
            Spacer(Modifier.height(12.dp))
            HyperButton(stringResource(R.string.home_retry), "hyper.tentar") { flow.error = null; flow.finishIfDone() }
        }
    }
    if (st.phase == ExportPhase.Failed || st.phase == ExportPhase.Cancelled) {
        if (st.message.isNotBlank()) ErrorBox(st.message, warning = st.phase == ExportPhase.Cancelled)
        Spacer(Modifier.height(16.dp))
        HyperButton(stringResource(R.string.home_retry), "hyper.tentar") { flow.retry() }
        Spacer(Modifier.height(10.dp))
        HyperButton(stringResource(R.string.hyper_back_to_preview), "hyper.voltar.previa", primary = false) { flow.back() }
    }
}

/** Os passos desta receita, com o estado do motor (ou deduzido do progresso se o motor não disser). */
private fun visibleSteps(flow: HyperFlow, info: HyperJobInfo?): List<Pair<HyperStep, HyperStepState>> {
    val r = flow.recipe
    val st = flow.exporter.state
    // O motor diz quais passos existem e quais terminaram (hyper_job_steps).
    if (info != null) return HyperStep.entries.filter { info.present(it) }.map { step ->
        step to if (st.phase == ExportPhase.Done) HyperStepState.Done else info.state(step)
    }
    val wanted = HyperStep.entries.filter {
        when (it) {
            HyperStep.Clean -> r.denoise > 0f || r.deblock || r.deinterlace > 0
            HyperStep.Stable -> r.stabilize
            HyperStep.Upscale -> r.upscale > 1
            HyperStep.Face -> r.face
            HyperStep.Motion -> r.targetFps > 0.0 || r.slowMotion > 1
            else -> true
        }
    }
    return wanted.map { step ->
        val state = when {
            st.phase == ExportPhase.Done -> HyperStepState.Done
            st.phase != ExportPhase.Running && st.phase != ExportPhase.Publishing -> HyperStepState.Waiting
            step == HyperStep.Prepare -> if (st.framesDone > 0) HyperStepState.Done else HyperStepState.Running
            step == HyperStep.Finish -> HyperStepState.Waiting
            st.framesDone > 0 -> HyperStepState.Running
            else -> HyperStepState.Waiting
        }
        step to state
    }
}

private fun stepName(s: HyperStep): Int = when (s) {
    HyperStep.Prepare -> R.string.hyper_step_prepare
    HyperStep.Clean -> R.string.hyper_step_clean
    HyperStep.Stable -> R.string.hyper_step_stable
    HyperStep.Upscale -> R.string.hyper_step_upscale
    HyperStep.Face -> R.string.hyper_step_face
    HyperStep.Motion -> R.string.hyper_step_motion
    HyperStep.Encode -> R.string.hyper_step_encode
    HyperStep.Finish -> R.string.hyper_step_finish
}

@Composable
private fun StepRow(step: HyperStep, state: HyperStepState, path: HyperPath?) {
    val name = stringResource(stepName(step))
    // O caminho que a etapa usa DE FATO (Lite/Max caem para Turbo sem a rede no aparelho).
    val pathLabel = path?.takeIf { it != HyperPath.Auto && step in ProcessingSteps }?.let { stringResource(pathName(it)) }
    val stateText = stringResource(when (state) {
        HyperStepState.Done -> R.string.hyper_state_done
        HyperStepState.Running -> R.string.hyper_state_running
        HyperStepState.Skipped -> R.string.hyper_state_skipped
        HyperStepState.Waiting -> R.string.hyper_state_waiting
    })
    Row(Modifier.fillMaxWidth().heightIn(min = 36.dp).testTag("hyper.etapa.${step.name.lowercase()}")
        .semantics(mergeDescendants = true) { contentDescription = "$name: $stateText" + (pathLabel?.let { " ($it)" } ?: "") },
        verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(22.dp), contentAlignment = Alignment.Center) {
            when (state) {
                HyperStepState.Done -> CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 20.dp, AureaColors.Success)
                HyperStepState.Running -> CircularProgressIndicator(Modifier.size(16.dp), color = AureaColors.Accent, strokeWidth = 2.dp)
                HyperStepState.Skipped -> CupertinoIcon(CupertinoGlyph.Minus, 16.dp, AureaColors.Muted)
                HyperStepState.Waiting -> CupertinoIcon(CupertinoGlyph.Circle, 18.dp, AureaColors.Muted)
            }
        }
        Spacer(Modifier.width(12.dp))
        Text(name, style = AureaType.of(15f, if (state == HyperStepState.Running) FontWeight.W600 else FontWeight.Normal,
            color = if (state == HyperStepState.Waiting || state == HyperStepState.Skipped) AureaColors.Muted else AureaColors.Text),
            modifier = Modifier.weight(1f))
        if (pathLabel != null) Text(pathLabel, style = AureaType.of(12f, FontWeight.W600, color = AureaColors.Muted))
    }
}

private val ProcessingSteps = setOf(HyperStep.Clean, HyperStep.Stable, HyperStep.Upscale, HyperStep.Face, HyperStep.Motion)

// ---- peças ---------------------------------------------------------------------

@Composable
private fun ErrorBox(message: String?, warning: Boolean = false) {
    if (message.isNullOrBlank()) return
    Spacer(Modifier.height(14.dp))
    Text(message, style = AureaType.of(14f, color = if (warning) AureaColors.Warning else AureaColors.Danger),
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface).padding(14.dp).testTag("hyper.erro"))
}

@Composable
private fun Chip(label: String, tag: String, on: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.Accent.copy(alpha = 0.22f) else AureaColors.Chip)
            .testTag(tag).semantics { role = Role.Button; contentDescription = label; selected = on }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick).padding(horizontal = 16.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.of(14f, FontWeight.W600, color = if (on) AureaColors.Text else AureaColors.Muted))
    }
}

@Composable
private fun HyperButton(label: String, tag: String, primary: Boolean = true, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(12.dp))
            .background(when { !enabled -> AureaColors.Surface; primary -> AureaColors.Accent; else -> AureaColors.Chip })
            .testTag(tag).semantics { role = Role.Button; contentDescription = label }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.of(15f, FontWeight.W600,
            color = when { !enabled -> AureaColors.Disabled; primary -> AureaColors.OnAccent; else -> AureaColors.Text }))
    }
}

@Composable
private fun timeText(seconds: Double): String {
    val s = seconds.coerceAtLeast(1.0).roundToInt()
    return when {
        s < 60 -> stringResource(R.string.hyper_time_seconds, s)
        s < 3600 -> stringResource(R.string.hyper_time_minutes, (s + 30) / 60)
        else -> stringResource(R.string.hyper_time_hours, s / 3600, (s % 3600) / 60)
    }
}

private fun fpsText(fps: Double): String {
    val r = (fps * 100).roundToInt() / 100.0
    return (if (r == r.toLong().toDouble()) r.toLong().toString() else r.toString()) + " fps"
}

private fun durationText(seconds: Double): String {
    val s = seconds.roundToInt().coerceAtLeast(0)
    return if (s >= 3600) "%d:%02d:%02d".format(s / 3600, (s % 3600) / 60, s % 60) else "%d:%02d".format(s / 60, s % 60)
}
