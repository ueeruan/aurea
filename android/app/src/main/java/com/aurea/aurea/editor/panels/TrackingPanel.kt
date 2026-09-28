package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableFloatStateOf
import kotlinx.coroutines.delay
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

/**
 * RASTREIO do vídeo: escolher um ponto (o próximo toque no palco) e seguir o
 * detalhe pelo clipe — um Nulo que acompanha, ou o vídeo estabilizado.
 */
@Composable
internal fun TrackingPanel(env: PanelEnv) {
    val store = env.store
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        MotionTrackSection(env)
        Spacer(Modifier.height(18.dp))
        CameraTrackSection(env)
    }
}

@Composable
private fun MotionTrackSection(env: PanelEnv) {
    val store = env.store
    var lock by remember { mutableStateOf(false) }
    var smooth by remember { mutableFloatStateOf(.5f) }
    var zoom by remember { mutableFloatStateOf(1.15f) }
    var crop by remember { mutableIntStateOf(1) }
    LaunchedEffect(store) { while (true) { store.refreshMotionStatus(); delay(400) } }
    val s = store.motionStatus
    Text("Motion Tracking", style = AureaType.Base.merge(TextStyle(fontWeight = FontWeight.W700)))
    if (s[0].toInt() != 1 && store.pointPick == null) {
        listOf("Point", "Two Points", "Planar", "Corner Pin", "Stabilizer").forEachIndexed { i, name ->
            Spacer(Modifier.height(6.dp))
            Action(name, if (i == 4) "Movimento global com vários pontos" else "Escolha os pontos no preview, no frame atual") { store.beginMotionPick(i) }
        }
        androidx.compose.material3.TextButton(onClick = { store.motionBackward = !store.motionBackward }) { Text(if (store.motionBackward) "Direction: Backward" else "Direction: Forward") }
        androidx.compose.material3.TextButton(onClick = { store.motionModel = (store.motionModel + 1) % 4 }) { Text("Motion: " + listOf("Auto", "Position", "Position / Rotation / Scale", "Perspective")[store.motionModel]) }
        Text("Feature radius: ${store.motionFeature.toInt()} px")
        androidx.compose.material3.Slider(value = store.motionFeature, onValueChange = { store.motionFeature = it }, valueRange = 6f..48f)
        Text("Search radius: ${store.motionSearch.toInt()} px")
        androidx.compose.material3.Slider(value = store.motionSearch, onValueChange = { store.motionSearch = it }, valueRange = 16f..192f)
        androidx.compose.material3.TextButton(onClick = store::restoreMotion) { Text("Restore saved analysis") }
    }
    if (store.pointPick != null) {
        Text("Pontos selecionados: ${store.motionPicked}. Toque no preview.")
        androidx.compose.material3.TextButton(onClick = store::cancelMotionPick) { Text("Cancel selection") }
    }
    if (s[0].toInt() == 1) {
        Text("Analisando… ${(s[1] * 100).toInt()}%")
        androidx.compose.material3.TextButton(onClick = store::cancelMotion) { Text("Cancel") }
    }
    if (store.motionMessage.isNotEmpty()) Text(store.motionMessage, color = AureaColors.Muted, fontSize = 12.sp)
    if (s[0].toInt() == 2) {
        Text("${s[3].toInt()}/${s[2].toInt()} frames · confidence ${(s[7] * 100).toInt()}% · RMS ${"%.2f".format(s[8])} px", fontSize = 12.sp)
        // Quadros perdidos não escondem mais as ações: ficam sem key (interpolados).
        if (s[5] > 0f) Text(stringResource(R.string.track_lost_frames_note, s[5].toInt()), color = AureaColors.Muted, fontSize = 12.sp)
        run {
            if (s[4].toInt() == 4) {
                androidx.compose.material3.TextButton(onClick = { lock = !lock }) { Text(if (lock) "Lock camera" else "Smooth motion") }
                Text("Smoothness: ${"%.1f".format(smooth)} s")
                androidx.compose.material3.Slider(value = smooth, onValueChange = { smooth = it }, valueRange = .1f..2f)
                Text("Maximum zoom: ${((zoom - 1) * 100).toInt()}%")
                androidx.compose.material3.Slider(value = zoom, onValueChange = { zoom = it }, valueRange = 1f..1.5f)
                androidx.compose.material3.TextButton(onClick = { crop = (crop + 1) % 3 }) { Text("Crop: " + listOf("None", "Static", "Dynamic")[crop]) }
                Action("Apply stabilization", "Um passo de desfazer; preserva os transforms do vídeo") { store.applyMotion(3, lock, smooth, zoom, crop) }
            } else {
                Action("Create Null", "Vincule uma camada ao nulo para seguir o movimento") { store.applyMotion(0) }
                // Alvo escolhido AQUI: a camada que recebe o rastreio não tem o
                // painel de Rastreio (é do vídeo), então "a camada selecionada"
                // era sempre o próprio vídeo e o motor recusava.
                val source = store.primary
                val targets = store.layers.filter {
                    it.id != source && !it.isThreeD && !it.hasParent && !it.adjustment &&
                        it.kind in listOf(LayerType.Video.kind, LayerType.Image.kind, LayerType.Text.kind, LayerType.Shape.kind,
                            LayerType.Null.kind, LayerType.Particles.kind, LayerType.Group.kind)
                }
                Spacer(Modifier.height(6.dp))
                if (targets.isEmpty()) Text(stringResource(R.string.track_no_target), color = AureaColors.Muted, fontSize = 12.sp)
                targets.forEach { layer ->
                    androidx.compose.material3.TextButton(onClick = { store.applyMotion(1, target = layer.id) }) { Text(stringResource(R.string.track_apply_to, layer.name)) }
                }
                if (s[4].toInt() <= 1) {
                    Spacer(Modifier.height(6.dp))
                    Action(stringResource(R.string.editor_estabilizar_pelo_ponto), stringResource(R.string.editor_move_video_ponto_ficar_parado_tela)) {
                        store.applyMotion(3, lock = true, smooth = .5f, maxScale = 1f, crop = 0)
                    }
                }
                if (s[4].toInt() >= 2) {
                    Spacer(Modifier.height(6.dp))
                    targets.filter { it.kind == LayerType.Video.kind || it.kind == LayerType.Image.kind }.forEach { layer ->
                        androidx.compose.material3.TextButton(onClick = { store.applyMotion(2, target = layer.id) }) { Text(stringResource(R.string.track_corner_pin_to, layer.name)) }
                    }
                }
            }
        }
    }
}

/**
 * CÂMERA 3D: analisa o movimento da câmera do vídeo (pontos, rastreio,
 * solve) em segundo plano e cria uma câmera 3D animada — modelos 3D ligados
 * ao Nulo da cena ficam presos no vídeo.
 */
@Composable
private fun CameraTrackSection(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.cameraTrack } }
    var mode by remember { mutableIntStateOf(1) }
    var advanced by remember { mutableStateOf(false) }
    var cameraMotion by remember { mutableIntStateOf(0) }
    var knownFov by remember { mutableFloatStateOf(0f) }
    var distanceText by remember { mutableStateOf("100") }
    Text(stringResource(R.string.panel_camera_3d), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(0 to stringResource(R.string.panel_rapido), 1 to stringResource(R.string.panel_equilibrado), 2 to stringResource(R.string.panel_alta_qualidade)).forEach { (m, label) ->
            val on = mode == m
            Box(
                Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { mode = m }).padding(horizontal = 10.dp, vertical = 6.dp),
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
    Spacer(Modifier.height(8.dp))
    val s = st
    when {
        s != null && s.state == 1 -> {
            Text("Analisando o movimento… ${(s.progress * 100).toInt()}%", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Spacer(Modifier.height(6.dp))
            Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(AureaColors.Chip)) {
                Box(Modifier.fillMaxWidth(s.progress.coerceIn(0f, 1f)).fillMaxHeight().background(AureaColors.Accent))
            }
            Spacer(Modifier.height(8.dp))
            Action(stringResource(R.string.panel_cancelar), stringResource(R.string.panel_analise_projeto_nao_muda)) { store.cancelCameraTrack() }
        }
        s != null && s.state == 2 -> {
            val kind = if (s.rotationOnly) stringResource(R.string.panel_camera_so_gira_lugar_sem_profundidade) else stringResource(R.string.panel_movimento_camera_encontrado)
            Text(kind + if (s.cached) stringResource(R.string.panel_analise_guardada) else "", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)))
            Spacer(Modifier.height(4.dp))
            val quality = when { s.solved != s.frames || s.errorPx > 2f -> "Poor"; s.errorPx > 1f -> "Fair"; s.errorPx > 0.5f -> "Good"; else -> "Excellent" }
            Text(
                "Solve quality: $quality · ${store.cameraSelectedCount} selected",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                androidx.compose.material3.FilterChip(selected = store.cameraMultiSelect, onClick = { store.cameraMultiSelect = !store.cameraMultiSelect }, label = { Text("Multi-select") })
                androidx.compose.material3.FilterChip(selected = store.cameraGoodPointsOnly, onClick = { store.cameraGoodPointsOnly = !store.cameraGoodPointsOnly }, label = { Text("Good points") })
            }
            androidx.compose.material3.TextButton(onClick = { store.cameraTargetMode = !store.cameraTargetMode }) { Text(if (store.cameraTargetMode) "Drag: Surface target" else "Drag: Selection box") }
            Text("Point size", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
            androidx.compose.material3.Slider(value = store.cameraPointSize, onValueChange = { store.cameraPointSize = it }, valueRange = 2f..8f)
            Action("Create Camera", "Reuse the solved camera when it already exists") { store.createCameraTrackObject(0) }
            if (store.cameraSelectedCount > 0) {
                androidx.compose.material3.TextButton(onClick = { store.calibrateCamera(0) }) { Text("Set Origin") }
                if (store.cameraSelectedCount >= 3) androidx.compose.material3.TextButton(onClick = { store.calibrateCamera(1) }) { Text("Set Ground Plane + Origin") }
                if (store.cameraSelectedCount == 2) {
                    androidx.compose.material3.OutlinedTextField(value = distanceText, onValueChange = { distanceText = it }, label = { Text("Distance (scene units)") }, singleLine = true)
                    androidx.compose.material3.TextButton(onClick = { distanceText.replace(',', '.').toFloatOrNull()?.let { store.calibrateCamera(2, it) } }) { Text("Set Scale") }
                }
                store.layers.filter { it.kind == 10 }.forEach { layer ->
                    androidx.compose.material3.TextButton(onClick = { store.placeTrackedModel(layer.id) }) { Text("Place 3D: ${layer.name}") }
                }
            }
            if (!s.rotationOnly && store.cameraSelectedCount > 0) {
                listOf(1 to "Create Null / Anchor", 2 to "Create Camera + Shape", 3 to "Create Camera + Text", 4 to "Create Camera + Solid").forEach { (kind, title) ->
                    Spacer(Modifier.height(8.dp))
                    Action(title, "Place on the selected 3D points") { store.createCameraTrackObject(kind) }
                }
            } else if (!s.rotationOnly) {
                Text("Tap a point or drag a selection box on the video.", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
            androidx.compose.material3.TextButton(onClick = { advanced = !advanced }) { Text(if (advanced) "Hide advanced" else "Advanced") }
            if (advanced) Text("${s.solved}/${s.frames} frames · ${s.points}/${s.tracks} inliers · RMS ${"%.2f".format(s.errorPx)} px · FOV ${"%.1f".format(s.fovDeg)}°", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            if (advanced) {
                androidx.compose.material3.TextButton(onClick = { cameraMotion = (cameraMotion + 1) % 3 }) { Text("Camera: " + listOf("Auto", "Free camera", "Tripod")[cameraMotion]) }
                androidx.compose.material3.TextButton(onClick = { knownFov = if (knownFov == 0f) s.fovDeg.coerceIn(10f,120f) else 0f }) { Text(if (knownFov == 0f) "FOV: Auto" else "FOV: ${knownFov.toInt()}°") }
                if (knownFov > 0f) androidx.compose.material3.Slider(value = knownFov, onValueChange = { knownFov = it }, valueRange = 10f..120f)
                Action("Re-solve", "Reuse observations with these camera constraints") { store.refineCamera(false, cameraMotion, knownFov) }
                if (store.cameraSelectedCount > 0) {
                    Spacer(Modifier.height(6.dp))
                    Action("Delete selected points + Re-solve", "Remove unwanted motion from this analysis") { store.refineCamera(true, cameraMotion, knownFov) }
                }
            }
            Spacer(Modifier.height(8.dp))
            Action(stringResource(R.string.panel_analisar_novo), stringResource(R.string.panel_modo_escolhido_acima)) { store.startCameraTrack(mode) }
        }
        else -> {
            if (s != null && (s.state == 3 || s.state == 4)) {
                Text(
                    if (s.state == 4) stringResource(R.string.panel_analise_cancelada) else "Não deu para resolver: ${s.message}",
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
                )
                Spacer(Modifier.height(8.dp))
            }
            Action(stringResource(R.string.panel_analisar_camera), stringResource(R.string.panel_acha_movimento_camera_video_roda_segundo)) { store.startCameraTrack(mode) }
        }
    }
}

@Composable
private fun Action(title: String, detail: String, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        Spacer(Modifier.height(2.dp))
        Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}
