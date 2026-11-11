package com.aurea.aurea.editor.panels

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.PointF
import android.media.FaceDetector
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.mocap.MocapDetector
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.pose.PoseDetection
import com.google.mlkit.vision.pose.accurate.AccuratePoseDetectorOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Headbop (0.5.2): as ferramentas do topo do cartão — Analisar/Reanalisar (pose
 * do ML Kit + rostos do FaceDetector na foto; pose por quadro no vídeo, o mesmo
 * detector do Motion Capture), progresso, tap tempo e "Converter em keyframes".
 * Estilos (Bop/Aceno/...) são as fichas de preset do motor; o pivô do pescoço é
 * o ponto do efeito no palco (EffectPointStage). Par do iOS: EffectsView.swift
 * (`HeadbopCardTools`).
 */
private const val FRAME_FLOATS = 2 + 33 * 4

@Composable
private fun HbChip(label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 44.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.Text.copy(alpha = 0.18f) else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
    }
}

private fun uriOf(path: String): Uri = if (path.contains("://")) Uri.parse(path) else Uri.fromFile(File(path))

/** Foto: pose (pessoa 0) + rostos extras (pessoas 1..3) no formato do motor. */
private suspend fun detectPhoto(context: Context, path: String): Pair<FloatArray, Float>? = withContext(Dispatchers.Default) {
    val uri = uriOf(path)
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@withContext null
    var sample = 1
    while (max(bounds.outWidth, bounds.outHeight) / (sample * 2) >= 768) sample *= 2
    val opts = BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.RGB_565 }
    val bmp = context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) } ?: return@withContext null
    try {
        val w = bmp.width.toFloat()
        val h = bmp.height.toFloat()
        val rows = ArrayList<FloatArray>()
        val detector = PoseDetection.getClient(
            AccuratePoseDetectorOptions.Builder().setDetectorMode(AccuratePoseDetectorOptions.SINGLE_IMAGE_MODE).build())
        try {
            val pose = Tasks.await(detector.process(InputImage.fromBitmap(bmp, 0)))
            if (pose.allPoseLandmarks.isNotEmpty()) {
                val f = FloatArray(FRAME_FLOATS)
                for (lm in pose.allPoseLandmarks) {
                    val j = lm.landmarkType
                    if (j !in 0 until 33) continue
                    val o = 2 + j * 4
                    f[o] = lm.position3D.x / w; f[o + 1] = lm.position3D.y / h; f[o + 2] = lm.position3D.z / w
                    f[o + 3] = lm.inFrameLikelihood.coerceIn(0f, 1f)
                }
                rows.add(f)
            }
        } finally {
            detector.close()
        }
        // Mais pessoas: os rostos (olhos + giro). FaceDetector pede largura par em RGB_565.
        val even = if (bmp.width % 2 == 0) bmp else Bitmap.createScaledBitmap(bmp, bmp.width - 1, bmp.height, true)
        try {
            val faces = arrayOfNulls<FaceDetector.Face>(4)
            val found = FaceDetector(even.width, even.height, 4).findFaces(even, faces)
            val ew = even.width.toFloat()
            val eh = even.height.toFloat()
            for (k in 0 until found) {
                val face = faces[k] ?: continue
                if (face.confidence() < 0.3f) continue
                val mid = PointF().also { face.getMidPoint(it) }
                val d = face.eyesDistance()
                val yaw = face.pose(FaceDetector.Face.EULER_Y)
                val f = FloatArray(FRAME_FLOATS)
                f[1] = (rows.size).toFloat()
                fun put(i: Int, x: Float, y: Float) { val o = 2 + i * 4; f[o] = x / ew; f[o + 1] = y / eh; f[o + 3] = 0.9f }
                put(2, mid.x - d / 2, mid.y); put(5, mid.x + d / 2, mid.y)
                put(0, mid.x + d * 0.45f * kotlin.math.sin(Math.toRadians(yaw.toDouble())).toFloat(), mid.y + d * 0.55f)
                put(9, mid.x - d * 0.3f, mid.y + d * 1.0f); put(10, mid.x + d * 0.3f, mid.y + d * 1.0f)
                rows.add(f)
                if (rows.size >= 4) break
            }
        } finally {
            if (even !== bmp) even.recycle()
        }
        val out = FloatArray(rows.size * FRAME_FLOATS)
        rows.forEachIndexed { i, r -> r.copyInto(out, i * FRAME_FLOATS) }
        out to (w / h)
    } finally {
        bmp.recycle()
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun HeadbopCardTools(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    val kind = store.layers.firstOrNull { it.id == layer }?.let { LayerType.of(it.kind) }
    val video = kind == LayerType.Video
    val media = video || kind == LayerType.Image
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var status by remember(layer, effectId) { mutableStateOf(LongArray(9)) }
    var detecting by remember(layer, effectId) { mutableStateOf(-1f) }
    var job by remember(layer, effectId) { mutableStateOf<Job?>(null) }
    val taps = remember(layer, effectId) { mutableStateListOf<Double>() }
    var tapStart by remember(layer, effectId) { mutableStateOf(0L to 0.0) }
    LaunchedEffect(layer, effectId) {
        while (true) {
            val next = store.engineForStress.headbopStatus(layer, effectId)
            if (next[8] != 0L) status = next
            delay(if (status[0] == 1L || detecting >= 0f) 250 else 900)
        }
    }
    val running = job?.isActive == true
    val muted = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted))
    Column(Modifier.fillMaxWidth().padding(bottom = 6.dp)) {
        Text(stringResource(R.string.headbop_note), style = muted)
        val rig = status[5]
        val stateText = when {
            running -> stringResource(R.string.headbop_analyzing)
            rig >= 2L -> stringResource(R.string.headbop_people_found, (rig - 1).toInt())
            rig == 1L -> stringResource(R.string.headbop_no_person)
            else -> stringResource(R.string.headbop_not_analyzed)
        }
        Text(stateText, Modifier.padding(top = 6.dp).testTag("fx.headbop.state").semantics { liveRegion = LiveRegionMode.Polite },
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
        if (status[6] > 0L) Text(stringResource(R.string.headbop_bpm_value, (status[6] / 100.0).roundToInt()),
            Modifier.padding(top = 2.dp).testTag("fx.headbop.bpm"), style = muted)
        if (running) {
            val p = if (detecting >= 0f) detecting * 0.6f else 0.6f + 0.4f * (status[1] / 1000f)
            LinearProgressIndicator(progress = { p.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth().padding(top = 6.dp)
                .testTag("fx.headbop.progress"))
        }
        if (status[0] == 3L && !running) Text(stringResource(R.string.headbop_failed), Modifier.padding(top = 2.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Danger)))
        FlowRow(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (running) {
                HbChip(stringResource(R.string.headbop_cancel), "fx.headbop.cancel", true) {
                    store.engineForStress.headbopCancel()
                    job?.cancel()
                }
            } else {
                HbChip(stringResource(if (rig > 0L) R.string.headbop_reanalyze else R.string.headbop_analyze), "fx.headbop.analyze", false) {
                    if (store.playing) store.pause()
                    val engine = store.engineForStress
                    val path = store.layerSourcePath(layer)
                    job = scope.launch {
                        try {
                            if (!engine.headbopBegin(layer, effectId)) return@launch
                            if (media && path != null) {
                                detecting = 0f
                                runCatching {
                                    if (video) {
                                        MocapDetector(context.applicationContext).run(uriOf(path), false,
                                            { detecting = it }) { data, n, aspect -> engine.headbopPushFrames(data, n, aspect) }
                                    } else {
                                        detectPhoto(context.applicationContext, path)?.let { (data, aspect) ->
                                            if (data.isNotEmpty()) engine.headbopPushFrames(data, data.size / FRAME_FLOATS, aspect)
                                        }
                                    }
                                }
                            }
                            detecting = -1f
                            withContext(Dispatchers.Default) { engine.headbopAnalyze() }
                            store.headbopEdited()
                        } finally {
                            detecting = -1f
                        }
                    }
                }
            }
            HbChip(stringResource(R.string.headbop_tap) + if (taps.size >= 2) " · ${taps.size}" else "", "fx.headbop.tap", taps.isNotEmpty()) {
                // Tempo da composição do toque: o cabeçote no 1º toque + o relógio desde então.
                val now = System.nanoTime()
                val fps = store.project?.fps?.takeIf { it > 0f } ?: 30f
                if (taps.isEmpty() || (now - tapStart.first) / 1e9 - (taps.last() - tapStart.second) > 3.0) {
                    taps.clear()
                    tapStart = now to store.playhead / fps.toDouble()
                }
                taps.add(tapStart.second + (now - tapStart.first) / 1e9)
                if (taps.size >= 2 && store.engineForStress.headbopSetTaps(layer, effectId, taps.toDoubleArray())) store.headbopEdited()
            }
            HbChip(stringResource(R.string.headbop_bake), "fx.headbop.bake", status[7] == 1L) {
                if (store.engineForStress.headbopBake(layer, effectId) > 0) store.headbopEdited()
            }
        }
    }
}
