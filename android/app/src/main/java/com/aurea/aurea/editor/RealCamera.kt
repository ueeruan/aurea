package com.aurea.aurea.editor

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
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
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.PI

// =============================================================================
//  Real Camera: a câmera 3D segue o celular. O vetor de rotação do aparelho
//  (giroscópio + acelerômetro, sem bússola) vira a rotação da camada Câmera
//  relativa à postura em que a sessão começou ("Recentrar" zera). "Gravar"
//  toca a timeline e escreve keyframes de Rotação X/Y/Z no quadro da prévia a
//  cada leitura — o movimento da mão vira animação de câmera. Só rotação: o
//  celular não mede posição sem AR.
// =============================================================================

data class RealCameraSession(val layer: Long)

object RealCamera {
    var session by mutableStateOf<RealCameraSession?>(null)
    var recording by mutableStateOf(false)
    /** Multiplica os graus do aparelho (0,5 = metade do movimento, 2 = dobro). */
    var sensitivity by mutableFloatStateOf(1f)
    /** Inverte pan e tilt (quem preferir "a câmera vai para onde o celular aponta" ao contrário). */
    var inverted by mutableStateOf(false)
    var available by mutableStateOf(true)
    /** Último quadro gravado (só para a faixa mostrar). */
    var lastFrame by mutableIntStateOf(-1)
}

fun realCameraActive(store: EditorStore): Boolean = RealCamera.session != null && store.layers.any { it.id == RealCamera.session?.layer }

/** Lê o sensor e manda a rotação ao motor (ao vivo ou gravando keyframes). */
internal class RealCameraSensor(context: Context, private val store: EditorStore) : SensorEventListener {
    private val manager = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
    private val sensor: Sensor? = manager?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
        ?: manager?.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
    private val rotation = FloatArray(9)
    private val remapped = FloatArray(9)
    private val orientation = FloatArray(3)
    private var reference: FloatArray? = null
    private var smooth = FloatArray(3)
    private var lastSentNs = 0L

    val available: Boolean get() = sensor != null

    fun start() {
        RealCamera.available = sensor != null
        val s = sensor ?: return
        manager?.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)
    }

    fun stop() { manager?.unregisterListener(this) }

    /** A postura atual vira o zero: a câmera volta a olhar para a frente. */
    fun recenter() { reference = null; smooth = FloatArray(3) }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit

    override fun onSensorChanged(event: SensorEvent) {
        val session = RealCamera.session ?: return
        SensorManager.getRotationMatrixFromVector(rotation, event.values)
        // Celular em pé, olhando "através" da tela: o eixo que aponta para
        // fora da tela vira o -Z da câmera (AXIS_X, AXIS_Z).
        SensorManager.remapCoordinateSystem(rotation, SensorManager.AXIS_X, SensorManager.AXIS_Z, remapped)
        SensorManager.getOrientation(remapped, orientation)
        val ref = reference ?: orientation.clone().also { reference = it }
        val delta = FloatArray(3) { i -> wrap(orientation[i] - ref[i]) }
        // Suaviza (filtro simples): o sensor tem ruído de ~0,2°.
        for (i in 0..2) smooth[i] += (delta[i] - smooth[i]) * 0.45f
        val sign = if (RealCamera.inverted) -1f else 1f
        val k = RealCamera.sensitivity * sign * (180f / PI.toFloat())
        // azimute = pan (Rotação Y); pitch = tilt (Rotação X); roll = Rotação Z.
        val ry = -smooth[0] * k
        val rx = smooth[1] * k
        val rz = -smooth[2] * k
        if (!rx.isFinite() || !ry.isFinite() || !rz.isFinite()) return
        // ~30 leituras/s bastam para a prévia e para um keyframe por quadro.
        val now = event.timestamp
        if (now - lastSentNs < 25_000_000L) return
        lastSentNs = now
        if (RealCamera.recording && store.playing) {
            val frame = store.playhead
            if (frame != RealCamera.lastFrame) {
                RealCamera.lastFrame = frame
                store.realCameraKey(session.layer, rx, ry, rz)
            }
        } else {
            if (RealCamera.recording && !store.playing) RealCamera.recording = false   // chegou ao fim
            store.realCameraApply(session.layer, rx, ry, rz)
        }
    }

    private fun wrap(a: Float): Float {
        var v = a
        while (v > PI) v -= (2 * PI).toFloat()
        while (v < -PI) v += (2 * PI).toFloat()
        return v
    }
}

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

/** A faixa no alto do palco enquanto a Real Camera está ligada: Gravar/Parar, Recentrar, Inverter, sensibilidade, Concluir. */
@Composable
fun RealCameraBar(store: EditorStore, modifier: Modifier) {
    val session = RealCamera.session ?: return
    if (!realCameraActive(store)) { RealCamera.session = null; RealCamera.recording = false; return }
    val context = LocalContext.current
    val sensor = remember(session) { RealCameraSensor(context, store) }
    DisposableEffect(session) {
        sensor.start()
        onDispose { sensor.stop(); if (RealCamera.recording) { RealCamera.recording = false; store.pause() } }
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
                    else -> stringResource(R.string.real_camera_hint)
                },
                maxLines = 2, style = muted, modifier = Modifier.weight(1f),
            )
        }
        Row(Modifier.padding(top = 6.dp).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CamChip(
                stringResource(if (RealCamera.recording) R.string.real_camera_stop else R.string.real_camera_record),
                "realcam.gravar", RealCamera.recording, enabled = RealCamera.available,
            ) {
                if (RealCamera.recording) {
                    RealCamera.recording = false
                    store.pause()
                } else {
                    RealCamera.lastFrame = -1
                    RealCamera.recording = true
                    store.realCameraBeginRecording(session.layer)
                }
            }
            CamChip(stringResource(R.string.real_camera_recenter), "realcam.recentrar", false) { sensor.recenter() }
            CamChip(stringResource(R.string.real_camera_invert), "realcam.inverter", RealCamera.inverted) { RealCamera.inverted = !RealCamera.inverted }
            CamChip(stringResource(R.string.real_camera_done), "realcam.concluir", false) {
                if (RealCamera.recording) { RealCamera.recording = false; store.pause() }
                RealCamera.session = null
            }
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
    }
}
