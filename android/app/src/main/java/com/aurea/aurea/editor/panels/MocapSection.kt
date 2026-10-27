package com.aurea.aurea.editor.panels

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import com.aurea.aurea.R
import com.aurea.aurea.mocap.MocapFlow
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import java.io.File
import kotlin.math.roundToInt

/**
 * MOTION CAPTURE (BETA) no painel 3D da camada com rig: Importar vídeo /
 * Gravar movimento → Analisar movimento (progresso real, cancelar, aviso de
 * baixa confiança) → Pré-visualizar animação (o personagem toca no preview sem
 * gravar keys) → intensidade, suavização, velocidade, alinhamento → Aplicar à
 * timeline. Sem rig humanoide: oferece o AutoRig 3D antes ([onAutoRig]).
 * Par do iOS: MocapPanel.swift (`MocapSection`).
 */
@Composable
internal fun MocapSection(env: PanelEnv, onAutoRig: () -> Unit = { env.store.startAutoRig() }) {
    val store = env.store
    val layer = store.primary ?: return
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val flow = remember(layer, store.projectGeneration) { MocapFlow(store, layer, scope, context) }
    DisposableEffect(flow) { onDispose { flow.dispose() } }
    // Depois do AutoRig 3D (EditorStore.startAutoRig): o rig apareceu?
    LaunchedEffect(flow, store.curveRevision, store.humanoidStatus, store.autoRigState) { flow.recheckRig() }

    val gallery = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri -> if (uri != null) flow.analyze(uri) }
    var captureUri by rememberSaveable { mutableStateOf<String?>(null) }
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.CaptureVideo()) { ok ->
        val uri = captureUri
        if (ok && uri != null) flow.analyze(Uri.parse(uri))
    }
    val cameraDenied = stringResource(R.string.mocap_camera_denied)
    fun launchCamera() {
        val dir = File(context.cacheDir, "mocap-capture").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }   // só a gravação mais recente fica
        val file = File(dir, "movimento-${System.currentTimeMillis()}.mp4")
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.exports", file)
        captureUri = uri.toString()
        runCatching { camera.launch(uri) }.onFailure { store.showToast(cameraDenied) }
    }
    // O app declara CAMERA (Real Camera): sem ela concedida o sistema recusa a captura.
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) launchCamera() else store.showToast(cameraDenied)
    }
    fun pickVideo() = gallery.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))

    Spacer(Modifier.height(12.dp))
    KitTitle(stringResource(R.string.mocap_title))
    when (flow.step) {
        // Esqueleto do arquivo que não é humanoide (1): associar os ossos; nenhum (0): AutoRig.
        MocapFlow.Step.NeedRig -> if (flow.status == 1) KitHint(stringResource(R.string.mocap_need_map)) else {
            KitHint(stringResource(R.string.mocap_need_rig))
            Spacer(Modifier.height(8.dp))
            if (store.autoRigState == 1) {
                LinearProgressIndicator(progress = { store.autoRigProgress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(),
                    color = AureaColors.Accent, trackColor = AureaColors.Chip)
            } else {
                Box(Modifier.testTag("panel3d.mocap.autorig")) {
                    ActionCard(stringResource(R.string.mocap_autorig), null) { onAutoRig() }
                }
            }
        }
        MocapFlow.Step.Choose -> {
            KitHint(stringResource(R.string.mocap_intro))
            Spacer(Modifier.height(8.dp))
            Box(Modifier.testTag("panel3d.mocap.import")) {
                ActionCard(stringResource(R.string.mocap_import), stringResource(R.string.mocap_import_hint)) { pickVideo() }
            }
            Spacer(Modifier.height(6.dp))
            Box(Modifier.testTag("panel3d.mocap.record")) {
                ActionCard(stringResource(R.string.mocap_record), stringResource(R.string.mocap_record_hint)) {
                    if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) launchCamera()
                    else permission.launch(Manifest.permission.CAMERA)
                }
            }
            ToggleLine(stringResource(R.string.mocap_reduced), flow.reduced) { flow.reduced = it }
            MocapError(flow.error)
        }
        MocapFlow.Step.Analyzing -> {
            val pct = (flow.progress * 100f).roundToInt().coerceIn(0, 100)
            val label = stringResource(R.string.mocap_analyzing, pct)
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)),
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(
                progress = { flow.progress.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().testTag("panel3d.mocap.progress").semantics {
                    contentDescription = label
                    progressBarRangeInfo = ProgressBarRangeInfo(flow.progress.coerceIn(0f, 1f), 0f..1f)
                },
                color = AureaColors.Accent, trackColor = AureaColors.Chip,
            )
            TextButton(onClick = { flow.cancel() }, modifier = Modifier.testTag("panel3d.mocap.cancel")) {
                Text(stringResource(R.string.mocap_cancel), color = AureaColors.Accent)
            }
        }
        MocapFlow.Step.Ready -> {
            val report = flow.report
            if (report != null) KitHint(stringResource(R.string.mocap_summary, report[0].roundToInt(), report[1]))
            val limbs = flow.uncertainLimbs()
            if (limbs.isNotEmpty()) {
                val names = limbs.map { stringResource(limbName(it)) }.joinToString(", ")
                Spacer(Modifier.height(6.dp))
                Text(stringResource(R.string.mocap_low_conf, names),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Warning)),
                    modifier = Modifier.testTag("panel3d.mocap.lowconf").semantics { liveRegion = LiveRegionMode.Polite })
            }
            Box(Modifier.testTag("panel3d.mocap.preview")) {
                ToggleLine(stringResource(R.string.mocap_preview), flow.previewOn) { flow.setPreview(it) }
            }
            val retune = flow::retune
            MocapSlider(env, stringResource(R.string.mocap_intensity), flow.intensity * 100f, 0f, 150f, 1f, "%", 0, 100f, "intensity", retune) {
                flow.intensity = it / 100f
            }
            MocapSlider(env, stringResource(R.string.mocap_smoothing), flow.smoothing * 100f, 0f, 100f, 1f, "%", 0, 50f, "smoothing", retune) {
                flow.smoothing = it / 100f
            }
            MocapSlider(env, stringResource(R.string.mocap_speed), flow.speed, 0.25f, 4f, 0.05f, "×", 2, 1f, "speed", retune) { flow.speed = it }
            MocapSlider(env, stringResource(R.string.mocap_yaw), flow.yaw, -180f, 180f, 1f, "°", 0, 0f, "yaw", retune) { flow.yaw = it }
            MocapSlider(env, stringResource(R.string.mocap_floor), flow.floor * 100f, -50f, 50f, 1f, "%", 0, 0f, "floor", retune) { flow.floor = it / 100f }
            ToggleLine(stringResource(R.string.mocap_foot_lock), flow.footLock) { flow.footLock = it; flow.retune() }
            Spacer(Modifier.height(8.dp))
            Box(Modifier.testTag("panel3d.mocap.apply")) {
                ActionCard(stringResource(R.string.mocap_apply), stringResource(R.string.mocap_apply_hint)) { flow.apply() }
            }
            if (flow.applied >= 0) {
                Spacer(Modifier.height(6.dp))
                Text(stringResource(R.string.mocap_applied, flow.applied), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)),
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            }
            MocapError(flow.error)
            Spacer(Modifier.height(6.dp))
            Box(Modifier.testTag("panel3d.mocap.another")) {
                ActionCard(stringResource(R.string.mocap_new_video), null) { pickVideo() }
            }
        }
    }
    Spacer(Modifier.height(6.dp))
    KitHint(stringResource(R.string.mocap_limits))
}

/** Uma régua do mocap: ao soltar (ou digitar), o motor resolve de novo e a prévia acompanha. */
@Composable
private fun MocapSlider(
    env: PanelEnv, label: String, value: Float, min: Float, max: Float, step: Float, unit: String, decimals: Int,
    default: Float, tag: String, onDone: () -> Unit, onValue: (Float) -> Unit,
) {
    Box(Modifier.testTag("panel3d.mocap.$tag")) {
        HumanRow(env, label, value, step, min, max, unit, decimals, default,
            onStart = {}, onValue = { onValue(it.coerceIn(min, max)) }, onEnd = onDone,
            onCommit = { onValue(it.coerceIn(min, max)); onDone() }, keyTag = "mocap.$tag")
    }
}

@Composable
private fun MocapError(code: Int) {
    val id = when (code) {
        1 -> R.string.mocap_err_no_person
        2 -> R.string.mocap_err_video
        3 -> R.string.mocap_err_apply
        else -> return
    }
    Spacer(Modifier.height(6.dp))
    Text(stringResource(id), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)),
        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
}

private fun limbName(i: Int): Int = when (i) {
    0 -> R.string.mocap_limb_torso
    1 -> R.string.mocap_limb_head
    2 -> R.string.mocap_limb_left_arm
    3 -> R.string.mocap_limb_right_arm
    4 -> R.string.mocap_limb_left_leg
    else -> R.string.mocap_limb_right_leg
}
