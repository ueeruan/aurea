package com.aurea.aurea.editor

import android.Manifest
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import kotlinx.coroutines.delay
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// Platform sources send OpenGL camera-to-world poses. Native C++ owns all motion
// conversion, smoothing, recording and keyframes on Android and iOS.
data class RealCameraSession(val layer: Long)

object RealCamera {
    var session by mutableStateOf<RealCameraSession?>(null)
    var recording by mutableStateOf(false)
    var sensitivity by mutableFloatStateOf(1f)
    var inverted by mutableStateOf(false)
    var available by mutableStateOf(true)
    var lastFrame by mutableIntStateOf(-1)
    var positional by mutableStateOf(false)
    /** Andar (botões de segurar): metros por segundo. */
    var walkSpeed by mutableFloatStateOf(1f)
    /** × do deslocamento frente/trás medido pelo AR. */
    var depthScale by mutableFloatStateOf(1f)
    internal var tracking by mutableStateOf(RealCameraTracking.Rotation)
    internal var usesArCore by mutableStateOf(false)

    fun reset() {
        recording = false
        lastFrame = -1
        positional = false
        tracking = RealCameraTracking.Rotation
        usesArCore = false
    }
}

fun realCameraActive(store: EditorStore): Boolean = RealCamera.session != null && store.layers.any { it.id == RealCamera.session?.layer }

@Composable
private fun CamChip(label: String, tag: String, on: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 34.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.Accent else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label; selected = on }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 7.dp),
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W600,
            color = if (on) AureaColors.OnAccent else if (enabled) AureaColors.Text else AureaColors.Disabled)))
    }
}

/** Botão de segurar: [onHold] true ao apertar, false ao soltar (ou cancelar). */
@Composable
private fun WalkButton(label: String, tag: String, onHold: (Boolean) -> Unit) {
    var held by remember { mutableStateOf(false) }
    Box(
        Modifier
            .heightIn(min = 34.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (held) AureaColors.Accent else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label }
            .pointerInput(Unit) {
                detectTapGestures(onPress = {
                    held = true; onHold(true)
                    try { tryAwaitRelease() } finally { held = false; onHold(false) }
                })
            }
            .padding(horizontal = 12.dp, vertical = 7.dp),
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W600,
            color = if (held) AureaColors.OnAccent else AureaColors.Text)))
    }
}

/** A faixa no alto do palco enquanto a Real Camera está ligada: Gravar/Parar, Recentrar, Inverter, sensibilidade, Concluir. */
@Composable
fun RealCameraBar(store: EditorStore, modifier: Modifier) {
    val session = RealCamera.session ?: return
    if (!realCameraActive(store)) {
        LaunchedEffect(session) { store.realCameraStop() }
        return
    }
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    var motion by remember(session) { mutableStateOf<RealCameraMotion?>(null) }
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        motion?.permissionResult(granted)
    }
    DisposableEffect(session, owner) {
        val controller = RealCameraMotion(context, store, session.layer) { permission.launch(Manifest.permission.CAMERA) }
        motion = controller
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> android.os.Handler(android.os.Looper.getMainLooper()).post {
                    if (owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) controller.resume()
                }
                Lifecycle.Event.ON_PAUSE -> controller.pause()
                else -> Unit
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose {
            owner.lifecycle.removeObserver(observer)
            controller.close()
            if (RealCamera.session == session) store.realCameraStop()
        }
    }
    LaunchedEffect(session) {
        while (RealCamera.session == session) {
            store.syncRealCameraStatus()
            motion?.updateDisplayGeometry()
            delay(50L)
        }
    }
    val muted = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted))
    Column(
        modifier
            .clip(RoundedCornerShape(10.dp))
            .background(AureaColors.EditorPanelHigh)
            .border(1.dp, ShellColors.AccentHalf, RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp)
            .testTag("realcam.bar"),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.CameraFill, 13.dp, AureaColors.Accent)
            Spacer(Modifier.width(7.dp))
            Text(stringResource(R.string.real_camera_title), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)))
            Spacer(Modifier.width(8.dp))
            Text(
                when {
                    !RealCamera.available -> stringResource(R.string.real_camera_no_sensor)
                    RealCamera.recording -> stringResource(R.string.real_camera_recording, RealCamera.lastFrame.coerceAtLeast(0))
                    else -> stringResource(when (RealCamera.tracking) {
                        RealCameraTracking.Position -> R.string.real_camera_tracking_position
                        RealCameraTracking.Limited -> R.string.real_camera_tracking_limited
                        RealCameraTracking.Permission -> R.string.real_camera_camera_permission
                        RealCameraTracking.Failed -> R.string.real_camera_tracking_failed
                        RealCameraTracking.Rotation -> R.string.real_camera_tracking_rotation
                    })
                },
                maxLines = 2, style = muted, modifier = Modifier.weight(1f),
            )
        }
        Row(Modifier.padding(top = 6.dp).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CamChip(
                stringResource(if (RealCamera.recording) R.string.real_camera_stop else R.string.real_camera_record),
                "realcam.gravar", RealCamera.recording, enabled = RealCamera.recording || RealCamera.available,
            ) {
                store.realCameraRecord(!RealCamera.recording)
            }
            CamChip(stringResource(R.string.real_camera_recenter), "realcam.recentrar", false) { store.realCameraRecenter() }
            CamChip(stringResource(R.string.real_camera_invert), "realcam.inverter", RealCamera.inverted) { RealCamera.inverted = !RealCamera.inverted }
            CamChip(stringResource(R.string.real_camera_done), "realcam.concluir", false) { store.realCameraStop() }
        }
        if (RealCamera.recording) {
            Text(stringResource(when (RealCamera.tracking) {
                RealCameraTracking.Position -> R.string.real_camera_tracking_position
                RealCameraTracking.Limited -> R.string.real_camera_tracking_limited
                RealCameraTracking.Permission -> R.string.real_camera_camera_permission
                RealCameraTracking.Failed -> R.string.real_camera_tracking_failed
                RealCameraTracking.Rotation -> R.string.real_camera_tracking_rotation
            }), style = muted, modifier = Modifier.testTag("realcam.tracking"))
        }
        if (RealCamera.tracking == RealCameraTracking.Permission) {
            Text(stringResource(R.string.real_camera_camera_permission_prompt), style = muted)
        }
        Row(Modifier.padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            val sens = stringResource(R.string.real_camera_sensitivity)
            Text(sens, style = muted)
            Spacer(Modifier.width(8.dp))
            Slider(
                value = RealCamera.sensitivity, onValueChange = { RealCamera.sensitivity = it }, valueRange = 0.25f..3f,
                modifier = Modifier.weight(1f).heightIn(max = 28.dp).testTag("realcam.sens").semantics { contentDescription = sens },
            )
            Spacer(Modifier.width(6.dp))
            Text("${(RealCamera.sensitivity * 100).toInt()}%", style = muted)
        }
        // Andar em Z: segurar anda para frente/trás no rumo da câmera (sem AR também).
        Row(Modifier.padding(top = 4.dp).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.real_camera_walk), style = muted)
            WalkButton(stringResource(R.string.real_camera_forward), "realcam.frente") { store.realCameraWalk(if (it) 1 else 0) }
            WalkButton(stringResource(R.string.real_camera_back), "realcam.tras") { store.realCameraWalk(if (it) -1 else 0) }
        }
        Row(Modifier.padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            val speed = stringResource(R.string.real_camera_walk_speed)
            Text(speed, style = muted)
            Spacer(Modifier.width(8.dp))
            Slider(
                value = RealCamera.walkSpeed, onValueChange = { RealCamera.walkSpeed = it }, valueRange = 0.2f..3f,
                modifier = Modifier.weight(1f).heightIn(max = 28.dp).testTag("realcam.walkspeed").semantics { contentDescription = speed },
            )
            Spacer(Modifier.width(6.dp))
            Text(String.format(java.util.Locale.ROOT, "%.1f m/s", RealCamera.walkSpeed), style = muted)
        }
        Row(Modifier.padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically) {
            val depth = stringResource(R.string.real_camera_depth)
            Text(depth, style = muted)
            Spacer(Modifier.width(8.dp))
            Slider(
                value = RealCamera.depthScale,
                onValueChange = { RealCamera.depthScale = it; store.realCameraWalk(0) }, valueRange = 0.5f..5f,
                modifier = Modifier.weight(1f).heightIn(max = 28.dp).testTag("realcam.depth").semantics { contentDescription = depth },
            )
            Spacer(Modifier.width(6.dp))
            Text(String.format(java.util.Locale.ROOT, "%.1f×", RealCamera.depthScale), style = muted)
        }
        if (RealCamera.usesArCore) {
            Text(stringResource(R.string.real_camera_arcore_privacy), style = muted, modifier = Modifier.padding(top = 4.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.real_camera_google_terms), style = muted,
                    modifier = Modifier.testTag("realcam.google.terms").tocavel {
                        runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://policies.google.com/terms"))) }
                    })
                Text(stringResource(R.string.real_camera_google_privacy), style = muted,
                    modifier = Modifier.testTag("realcam.google.privacy").tocavel {
                        runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://policies.google.com/privacy"))) }
                    })
            }
        }
    }
}
