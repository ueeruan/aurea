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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Alignment
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.selected
import androidx.compose.foundation.layout.heightIn
import com.aurea.aurea.ui.i18n.EngineText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.CupertinoGlyph

/**
 * RASTREIO do vídeo: escolher um ponto (o próximo toque no palco) e seguir o
 * detalhe pelo clipe — um Nulo que acompanha, ou o vídeo estabilizado.
 */
@Composable
internal fun TrackingPanel(env: PanelEnv) {
    val store = env.store
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf(false to R.string.cam_mode_2d, true to R.string.panel_camera_3d).forEach { (camera, label) ->
                androidx.compose.material3.FilterChip(selected = store.cameraTrackerVisible == camera,
                    onClick = { store.cameraTrackerVisible = camera }, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                    colors = androidx.compose.material3.FilterChipDefaults.filterChipColors(
                        selectedContainerColor = AureaColors.AccentDim, selectedLabelColor = AureaColors.Accent,
                        containerColor = AureaColors.Background, labelColor = AureaColors.Muted),
                    label = { Text(stringResource(label)) })
            }
        }
        Spacer(Modifier.height(16.dp))
        if (store.cameraTrackerVisible) CameraTrackSection(env) else MotionTrackSection(env)
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
    Text(stringResource(R.string.trk_title), style = AureaType.Base.merge(TextStyle(fontWeight = FontWeight.W700)))
    if (s[0].toInt() != 1 && store.pointPick == null) {
        listOf(R.string.trk_tool_point, R.string.trk_tool_two_points, R.string.trk_tool_planar, R.string.trk_tool_corner_pin, R.string.trk_tool_stabilizer)
            .map { stringResource(it) }.forEachIndexed { i, name ->
            Spacer(Modifier.height(6.dp))
            Action(name, stringResource(if (i == 4) R.string.edt_track_global_motion else R.string.edt_track_pick_points)) { store.beginMotionPick(i) }
        }
        androidx.compose.material3.TextButton(onClick = { store.motionBackward = !store.motionBackward }) { Text(stringResource(if (store.motionBackward) R.string.trk_direction_backward else R.string.trk_direction_forward)) }
        androidx.compose.material3.TextButton(onClick = { store.motionModel = (store.motionModel + 1) % 4 }) { Text(stringResource(R.string.trk_motion_fmt, stringResource(listOf(R.string.trk_auto, R.string.trk_motion_position, R.string.trk_motion_prs, R.string.trk_motion_perspective)[store.motionModel]))) }
        Text(stringResource(R.string.trk_feature_radius, store.motionFeature.toInt()))
        androidx.compose.material3.Slider(value = store.motionFeature, onValueChange = { store.motionFeature = it }, valueRange = 6f..48f)
        Text(stringResource(R.string.trk_search_radius, store.motionSearch.toInt()))
        androidx.compose.material3.Slider(value = store.motionSearch, onValueChange = { store.motionSearch = it }, valueRange = 16f..192f)
        androidx.compose.material3.TextButton(onClick = store::restoreMotion) { Text(stringResource(R.string.trk_restore)) }
    }
    if (store.pointPick != null) {
        Text(stringResource(R.string.edt_track_points_selected, store.motionPicked))
        androidx.compose.material3.TextButton(onClick = store::cancelMotionPick) { Text(stringResource(R.string.editor_cancelar_selecao)) }
    }
    if (s[0].toInt() == 1) {
        Text(stringResource(R.string.edt_track_analyzing, (s[1] * 100).toInt()))
        androidx.compose.material3.TextButton(onClick = store::cancelMotion) { Text(stringResource(R.string.common_cancel)) }
    }
    if (store.motionMessage.isNotEmpty()) Text(store.motionMessage, color = AureaColors.Muted, fontSize = 12.sp)
    if (s[0].toInt() == 2) {
        Text(stringResource(R.string.trk_stats, s[3].toInt(), s[2].toInt(), (s[7] * 100).toInt(), "%.2f".format(s[8])), fontSize = 12.sp)
        // Quadros perdidos não escondem mais as ações: ficam sem key (interpolados).
        if (s[5] > 0f) Text(stringResource(R.string.track_lost_frames_note, s[5].toInt()), color = AureaColors.Muted, fontSize = 12.sp)
        run {
            if (s[4].toInt() == 4) {
                androidx.compose.material3.TextButton(onClick = { lock = !lock }) { Text(stringResource(if (lock) R.string.trk_lock_camera else R.string.trk_smooth_motion)) }
                Text(stringResource(R.string.trk_smoothness, "%.1f".format(smooth)))
                androidx.compose.material3.Slider(value = smooth, onValueChange = { smooth = it }, valueRange = .1f..2f)
                Text(stringResource(R.string.trk_max_zoom, ((zoom - 1) * 100).toInt()))
                androidx.compose.material3.Slider(value = zoom, onValueChange = { zoom = it }, valueRange = 1f..1.5f)
                androidx.compose.material3.TextButton(onClick = { crop = (crop + 1) % 3 }) { Text(stringResource(R.string.trk_crop_fmt, stringResource(listOf(R.string.trk_crop_none, R.string.trk_crop_static, R.string.trk_crop_dynamic)[crop]))) }
                Action(stringResource(R.string.trk_apply_stabilization), stringResource(R.string.edt_track_stabilize_note)) { store.applyMotion(3, lock, smooth, zoom, crop) }
            } else {
                Action(stringResource(R.string.trk_create_null), stringResource(R.string.edt_track_null_note)) { store.applyMotion(0) }
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
    var pointDisplay by remember { mutableStateOf(false) }
    var calibration by remember { mutableStateOf(false) }
    var objectMenu by remember { mutableStateOf(false) }
    var objectKind by remember { mutableIntStateOf(1) }
    val objectTitles = listOf(R.string.cam_create_null_anchor, R.string.cam_create_camera_shape, R.string.cam_create_camera_text, R.string.cam_create_camera_solid)
    Text(stringResource(if (st?.state == 2) R.string.cam_step_place else R.string.cam_step_analyze),
        style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
    Spacer(Modifier.height(6.dp))
    if (st?.state != 1 && st?.state != 2) CameraQualityPicker(mode) { mode = it }
    Spacer(Modifier.height(8.dp))
    val s = st
    when {
        s != null && s.state == 1 -> {
            Text(stringResource(R.string.ios_analyzing_motion_pct, (s.progress * 100).toInt()), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
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
            val quality = stringResource(when { s.solved != s.frames || s.errorPx > 2f -> R.string.cam_quality_poor; s.errorPx > 1f -> R.string.cam_quality_fair; s.errorPx > 0.5f -> R.string.cam_quality_good; else -> R.string.cam_quality_excellent })
            Text(
                stringResource(R.string.cam_solve_quality, quality, store.cameraSelectedCount),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
            Spacer(Modifier.height(8.dp))
            Action(stringResource(R.string.panel_criar_camera), stringResource(R.string.cam_create_camera_desc)) { store.createCameraTrackObject(0) }
            if (!s.rotationOnly) Text(stringResource(R.string.cam_tap_hint), modifier = Modifier.padding(top = 12.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                androidx.compose.material3.FilterChip(selected = store.cameraMultiSelect, onClick = { store.cameraMultiSelect = !store.cameraMultiSelect }, label = { Text(stringResource(R.string.cam_multi_select)) })
            }
            if (!s.rotationOnly) {
                Box {
                    androidx.compose.material3.OutlinedButton(onClick = { objectMenu = true }) {
                        Text(stringResource(objectTitles[objectKind - 1]))
                        CupertinoIcon(CupertinoGlyph.ChevronDown, 14.dp, AureaColors.Text)
                    }
                    androidx.compose.material3.DropdownMenu(expanded = objectMenu, onDismissRequest = { objectMenu = false }) {
                        objectTitles.forEachIndexed { index, title ->
                            androidx.compose.material3.DropdownMenuItem(text = { Text(stringResource(title)) },
                                onClick = { objectKind = index + 1; objectMenu = false })
                        }
                    }
                }
                Action(stringResource(R.string.cam_place_selected), stringResource(R.string.cam_place_on_points), store.cameraSelectedCount > 0) { store.createCameraTrackObject(objectKind) }
            }
            androidx.compose.material3.TextButton(onClick = { pointDisplay = !pointDisplay }) { Text(stringResource(R.string.cam_point_display)) }
            if (pointDisplay) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                androidx.compose.material3.FilterChip(selected = store.cameraGoodPointsOnly, onClick = { store.cameraGoodPointsOnly = !store.cameraGoodPointsOnly }, label = { Text(stringResource(R.string.cam_good_points)) })
                }
            androidx.compose.material3.TextButton(onClick = { store.cameraTargetMode = !store.cameraTargetMode }) { Text(stringResource(if (store.cameraTargetMode) R.string.cam_drag_surface else R.string.cam_drag_box)) }
            Text(stringResource(R.string.fx_tamanho_ponto), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
            androidx.compose.material3.Slider(value = store.cameraPointSize, onValueChange = { store.cameraPointSize = it }, valueRange = 2f..8f)
            }
            if (store.cameraSelectedCount > 0 && !s.rotationOnly) {
                androidx.compose.material3.TextButton(onClick = { calibration = !calibration }) { Text(stringResource(R.string.cam_reference_controls)) }
                if (calibration) {
                androidx.compose.material3.TextButton(onClick = { store.calibrateCamera(0) }) { Text(stringResource(R.string.cam_set_origin)) }
                if (store.cameraSelectedCount >= 3) androidx.compose.material3.TextButton(onClick = { store.calibrateCamera(1) }) { Text(stringResource(R.string.cam_set_ground)) }
                if (store.cameraSelectedCount == 2) {
                    androidx.compose.material3.OutlinedTextField(value = distanceText, onValueChange = { distanceText = it }, label = { Text(stringResource(R.string.cam_distance)) }, singleLine = true)
                    androidx.compose.material3.TextButton(onClick = { distanceText.replace(',', '.').toFloatOrNull()?.let { store.calibrateCamera(2, it) } }) { Text(stringResource(R.string.cam_set_scale)) }
                }
                store.layers.filter { it.kind == 10 }.forEach { layer ->
                    androidx.compose.material3.TextButton(onClick = { store.placeTrackedModel(layer.id) }) { Text(stringResource(R.string.cam_place_3d, layer.name)) }
                }
                }
            }
            androidx.compose.material3.TextButton(onClick = { advanced = !advanced }) { Text(stringResource(if (advanced) R.string.cam_hide_advanced else R.string.panel_avancado)) }
            if (advanced) Text(stringResource(R.string.cam_stats, s.solved, s.frames, s.points, s.tracks, "%.2f".format(s.errorPx), "%.1f".format(s.fovDeg)), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            if (advanced) {
                CameraQualityPicker(mode) { mode = it }
                androidx.compose.material3.TextButton(onClick = { cameraMotion = (cameraMotion + 1) % 3 }) { Text(stringResource(R.string.cam_camera_fmt, stringResource(listOf(R.string.trk_auto, R.string.cam_free, R.string.cam_tripod)[cameraMotion]))) }
                androidx.compose.material3.TextButton(onClick = { knownFov = if (knownFov == 0f) s.fovDeg.coerceIn(10f,120f) else 0f }) { Text(if (knownFov == 0f) stringResource(R.string.cam_fov_auto) else stringResource(R.string.cam_fov_value, knownFov.toInt())) }
                if (knownFov > 0f) androidx.compose.material3.Slider(value = knownFov, onValueChange = { knownFov = it }, valueRange = 10f..120f)
                Action(stringResource(R.string.cam_resolve), stringResource(R.string.cam_resolve_desc)) { store.refineCamera(false, cameraMotion, knownFov) }
                if (store.cameraSelectedCount > 0) {
                    Spacer(Modifier.height(6.dp))
                    Action(stringResource(R.string.cam_delete_resolve), stringResource(R.string.cam_delete_resolve_desc)) { store.refineCamera(true, cameraMotion, knownFov) }
                }
                Spacer(Modifier.height(8.dp))
                Action(stringResource(R.string.panel_analisar_novo), stringResource(R.string.panel_modo_escolhido_acima)) { store.startCameraTrack(mode) }
            }
        }
        else -> {
            if (s != null && (s.state == 3 || s.state == 4)) {
                Text(
                    if (s.state == 4) stringResource(R.string.panel_analise_cancelada) else stringResource(R.string.edt_track_solve_failed, EngineText.reason(LocalContext.current, s.message)),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
                )
                Spacer(Modifier.height(8.dp))
            }
            Action(stringResource(R.string.panel_analisar_camera), stringResource(R.string.panel_acha_movimento_camera_video_roda_segundo)) { store.startCameraTrack(mode) }
        }
    }
}

@Composable
private fun Action(title: String, detail: String, enabled: Boolean = true, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
            .tocavel(enabled = enabled, shrink = 1f, onClick = onClick).padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = if (enabled) AureaColors.Text else AureaColors.Muted)))
        Spacer(Modifier.height(2.dp))
        Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}

@Composable
private fun CameraQualityPicker(mode: Int, onSelect: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        listOf(R.string.panel_rapido, R.string.panel_equilibrado, R.string.panel_alta_qualidade).forEachIndexed { index, label ->
            Box(Modifier.weight(1f).heightIn(min = 48.dp).clip(RoundedCornerShape(8.dp))
                .background(if (mode == index) AureaColors.AccentDim else AureaColors.Chip)
                .semantics { selected = mode == index }
                .tocavel(shrink = 1f, onClick = { onSelect(index) }).padding(8.dp), contentAlignment = Alignment.Center) {
                Text(stringResource(label), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
                    color = if (mode == index) AureaColors.Accent else AureaColors.Text)), textAlign = androidx.compose.ui.text.style.TextAlign.Center)
            }
        }
    }
}
