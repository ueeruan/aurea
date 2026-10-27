package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

// =============================================================================
// Omega aparência (área 05, P1-6): OPÇÕES DA CÂMERA, na moldura comum
// ([PanelFrame], 200 dp), com as abas **Visão** · **Desfoque de foco** · **Névoa**.
//
//   Visão: título "Visão da câmera" + botão perspectiva/paralela (a paralela
//   ainda não existe no motor: o menu avisa); régua vertical CURVA do ângulo de
//   visão (FOV vertical) com a caixa 56 × 36; régua de zoom de 36 dp + caixa
//   56 × 36 (zoom ↔ focal por CameraLensMath.kt); as lentes prontas (extra do
//   AUREA); "Afeta camadas abaixo"; a dica da visão pela câmera.
//   Desfoque de foco: o que a antiga face Lente tinha (profundidade de campo,
//   foco, abertura, força, tocar para focar), com ◇ por linha.
//   Névoa: a névoa da câmera ainda não existe no motor (aviso).
// O motor grava a FOCAL; ângulo e zoom são leituras dela (4–1200 mm).
// =============================================================================

/** Losango de uma trilha da lente (transform da câmera; tempo local da camada). */
internal fun cameraTrackLook(store: EditorStore, property: Int): KeyframeLook {
    val d = store.detail ?: return KeyframeLook.None
    val keys = store.keyframes[d.id].orEmpty()
    var animated = false
    for (i in keys.indices) {
        val k = keys[i]
        if (k.property != property || k.effectIndex != EditorStore.NO_EFFECT) continue
        if (k.time == d.localPlayhead) return KeyframeLook.KeyHere
        animated = true
    }
    return if (animated) KeyframeLook.Animated else KeyframeLook.None
}

private object CameraTabs {
    const val VIEW = "view"
    const val FOCUS = "focus"
    const val FOG = "fog"
}

@Composable
internal fun CameraOptionsPanel(env: PanelEnv) {
    val store = env.store
    var tab by rememberSaveable { mutableStateOf(CameraTabs.VIEW) }
    val lens = store.cameraLens
    val isCamera = store.detail?.kind == LayerType.Camera.kind
    val dof = lens != null && lens[2] >= 0.5f
    // O ◆ da moldura: Visão = focal (ângulo e zoom); Desfoque de foco = distância de foco.
    val param: Int? = when {
        lens == null -> null
        tab == CameraTabs.VIEW -> EditorStore.CAMERA_LENS_FOCAL
        tab == CameraTabs.FOCUS && dof -> EditorStore.CAMERA_LENS_FOCUS
        else -> null
    }
    val property = param?.let { store.cameraLensTrack(it) }
    DisposableEffect(store, store.primary, property) {
        store.timelineFocus = property?.let { listOf(TrackKey(it)) }
        onDispose { store.timelineFocus = null }
    }
    val look = if (property != null) cameraTrackLook(store, property) else KeyframeLook.None
    val curveKeys = if (property != null) store.primaryKeys().transformTrack(property) else emptyList()
    val onKeyframe: (() -> Unit)? = param?.let { p -> { store.toggleCameraLensKey(p) } }
    val onDeleteAll: (() -> Unit)? = property?.let { p -> { store.primary?.let { id -> store.deleteAllKeyframes(id, listOf(TrackKey(p))) } } }
    val onCurve: (() -> Unit)? = if (property != null) {
        {
            val layer = store.primary
            val start = store.curveStart(curveKeys)
            if (layer != null && start != null) {
                store.selectKeyframe(layer, start)
                env.onOpenPanel(EditorPanel.Curve)
            }
        }
    } else null
    val tabs = listOf(
        PanelTab(CameraTabs.VIEW, stringResource(R.string.om_ap_cam_view), glyph = CupertinoGlyph.Eye),
        PanelTab(CameraTabs.FOCUS, stringResource(R.string.om_ap_cam_focus), glyph = CupertinoGlyph.CameraViewfinder),
        PanelTab(CameraTabs.FOG, stringResource(R.string.om_ap_cam_fog), glyph = CupertinoGlyph.LineHorizontal3),
    )
    Box(Modifier.fillMaxSize()) {
        PanelFrame(
            rail = PanelRail(
                onBack = env.onClose,
                keyframeLook = look,
                onKeyframe = onKeyframe,
                onDeleteAllKeyframes = onDeleteAll,
                onCurve = onCurve,
                curveReady = store.curveStart(curveKeys) != null,
            ),
            tabs = tabs,
            selectedTab = tab,
            onTab = { tab = it },
            scrollContent = false,
        ) {
            if (lens == null || !isCamera) {
                Column(Modifier.weight(1f).fillMaxWidth().padding(16.dp).testTag("camera.notCamera"), verticalArrangement = Arrangement.Center) {
                    PanelNotice(stringResource(R.string.lens_msg_not_camera))
                }
            } else {
                when (tab) {
                    CameraTabs.FOCUS -> CameraFocusTab(env, lens)
                    CameraTabs.FOG -> CameraFogTab()
                    else -> CameraViewTab(env, lens)
                }
            }
        }
        PanelTabTags(listOf("camera.tab.view", "camera.tab.focus", "camera.tab.fog"))
    }
}

// =============================================================================
// Visão
// =============================================================================

@Composable
private fun ColumnScope.CameraViewTab(env: PanelEnv, lens: FloatArray) {
    val store = env.store
    val fov = lens[1]
    val focal = lens[0]
    val zoom = CameraLens.zoomFromFocal(focal)
    val enabled = store.detail?.locked != true
    val angleLabel = stringResource(R.string.om_ap_cam_view_angle)
    val zoomLabel = stringResource(R.string.om_ap_cam_zoom)
    fun writeFov(v: Float) {
        if (!v.isFinite()) return
        store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, CameraLens.focalFromFov(v.coerceIn(CameraLens.MIN_FOV, CameraLens.MAX_FOV)))
    }
    fun writeZoom(v: Float) {
        if (!v.isFinite() || v <= 0f) return
        store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, CameraLens.focalFromZoom(v))
    }
    fun angleKeypad() = env.openKeypad(KeypadRequest(angleLabel, fov, "°", CameraLens.MIN_FOV, CameraLens.MAX_FOV, 1) { writeFov(it) })
    fun zoomKeypad() = env.openKeypad(KeypadRequest(zoomLabel, zoom, "×", CameraLens.MIN_ZOOM, CameraLens.MAX_ZOOM, 2) { writeZoom(it) })
    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 10.dp)) {
        // Título (37,5 dp) + perspectiva/paralela.
        Row(Modifier.fillMaxWidth().height(37.5.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.om_ap_cam_view_title),
                modifier = Modifier.weight(1f),
                maxLines = 1,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
            )
            ProjectionButton()
        }
        // 0.3.4 painéis: 140 (antes 124) para o rótulo em cima do valor maior.
        Row(Modifier.fillMaxWidth().height(140.dp), verticalAlignment = Alignment.CenterVertically) {
            CurvedAngleRuler(
                value = { store.cameraLens?.get(1) ?: fov },
                min = CameraLens.MIN_FOV,
                max = CameraLens.MAX_FOV,
                enabled = enabled,
                description = angleLabel,
                state = "${CameraLens.formatFov(fov)}°",
                onStart = {
                    if (store.playing || store.preview.buffering) store.pause()
                    store.beginGesture("ângulo de visão")
                },
                onValue = ::writeFov,
                onEnd = { store.endGesture() },
                onTap = ::angleKeypad,
                modifier = Modifier.width(80.dp).fillMaxHeight().testTag("camera.view.angle"),
            )
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.Center) {
                // 0.3.4 painéis: o rótulo (13) fica em cima da caixa maior (72 × 36): numa
                // tela de 360 os dois lado a lado cortavam o rótulo.
                Text(
                    angleLabel,
                    maxLines = 1,
                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Label, color = AureaColors.Muted)),
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Spacer(Modifier.weight(1f))
                    CameraValueBox("${CameraLens.formatFov(fov)}°", angleLabel, "camera.view.angle.value", enabled, ::angleKeypad)
                }
                Spacer(Modifier.height(6.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    MarkedRuler(
                        value = { CameraLens.zoomFromFocal(store.cameraLens?.get(0) ?: focal) },
                        unitsPerDp = 0.02f,
                        active = true,
                        tag = "camera.view.zoom",
                        description = zoomLabel,
                        state = CameraLens.formatZoom(zoom),
                        min = CameraLens.MIN_ZOOM,
                        max = CameraLens.MAX_ZOOM,
                        enabled = enabled,
                        onStart = {
                            if (store.playing || store.preview.buffering) store.pause()
                            store.beginGesture("zoom da câmera")
                        },
                        onValue = ::writeZoom,
                        onEnd = { store.endGesture() },
                        modifier = Modifier.weight(1f).height(PanelMetrics.Touch),
                    )
                    Spacer(Modifier.width(6.dp))
                    CameraValueBox(CameraLens.formatZoom(zoom), zoomLabel, "camera.view.zoom.value", enabled, ::zoomKeypad)
                }
            }
        }
        // Lentes prontas (extra do AUREA): a ficha acesa é a que bate com a focal.
        Row(
            Modifier.fillMaxWidth().padding(top = 4.dp, bottom = 4.dp).horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                CameraLens.formatFocal(focal),
                modifier = Modifier.padding(end = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, color = AureaColors.Muted)),
            )
            val picked = CameraLens.presetIndex(focal)
            CameraLens.PRESETS_MM.forEachIndexed { i, mm ->
                val on = picked == i
                Box(
                    Modifier
                        .heightIn(min = 44.dp)
                        .testTag("camera.view.preset.$mm")
                        .tocavel(enabled = enabled, shrink = 1f) {
                            store.beginGesture("distância focal")
                            store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, mm.toFloat())
                            store.endGesture()
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    Box(
                        Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                            .heightIn(min = PanelMetrics.ChipHeight)
                            .padding(horizontal = PanelMetrics.ChipPad, vertical = 7.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text("$mm", style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.ChipText, fontWeight = FontWeight.W700,
                            color = if (on) AureaColors.Accent else AureaColors.Text)))
                    }
                }
            }
        }
        val affectsBelow = lens.size > 9 && lens[9] >= 0.5f
        BlurToggleRow(stringResource(R.string.camera_affects_below), stringResource(R.string.camera_affects_below_hint), affectsBelow,
            Modifier.testTag("camera.view.affectsBelow")) { on ->
            store.beginGesture("camera afeta camadas abaixo")
            store.setCameraLens(EditorStore.CAMERA_AFFECTS_BELOW, if (on) 1f else 0f)
            store.endGesture()
        }
        Text(
            stringResource(R.string.om_ap_cam_tip),
            modifier = Modifier.padding(start = 4.dp, top = 2.dp, bottom = 10.dp).testTag("camera.view.tip"),
            style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, lineHeight = 16.sp, color = AureaColors.Subtle)),
        )
    }
}

/** Caixa de um valor da câmera ([PanelMetrics] 72 × 36, número 15 que encolhe; toque digita); 44 dp de toque. */
@Composable
private fun CameraValueBox(text: String, description: String, tag: String, enabled: Boolean, onTap: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = PanelMetrics.Touch)
            .testTag(tag)
            .semantics(mergeDescendants = true) { contentDescription = description }
            .tocavel(enabled = enabled, shrink = 1f, onClick = onTap),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.width(PanelMetrics.FieldWidth).heightIn(min = PanelMetrics.FieldHeight)
                .clip(RoundedCornerShape(PanelMetrics.FieldRadius)).background(AureaColors.Chip).padding(horizontal = 4.dp),
            contentAlignment = Alignment.Center,
        ) {
            androidx.compose.foundation.text.BasicText(
                text,
                maxLines = 1,
                autoSize = androidx.compose.foundation.text.TextAutoSize.StepBased(
                    minFontSize = PanelMetrics.FieldValueMin, maxFontSize = PanelMetrics.FieldValue, stepSize = 0.5.sp),
                style = AureaType.Base.merge(TextStyle(
                    fontSize = PanelMetrics.FieldValue, fontWeight = FontWeight.W600,
                    color = if (enabled) AureaColors.Accent else AureaColors.Muted,
                    textAlign = TextAlign.Center, fontFeatureSettings = "tnum", textDirection = TextDirection.Ltr,
                )),
            )
        }
    }
}

/** Perspectiva (a única que o motor tem) / Paralela (desabilitada, com o aviso). */
@Composable
private fun ProjectionButton() {
    var open by remember { mutableStateOf(false) }
    val perspective = stringResource(R.string.om_ap_cam_perspective)
    val parallel = stringResource(R.string.om_ap_cam_parallel)
    val soon = stringResource(R.string.om_ap_cam_parallel_soon)
    Box {
        Box(
            Modifier
                .size(44.dp)
                .testTag("camera.view.projection")
                .semantics { contentDescription = perspective }
                .tocavel(shrink = 1f) { open = true },
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier.size(width = 40.dp, height = PanelMetrics.FieldHeight).clip(RoundedCornerShape(TfFieldDims.Radius)).background(AureaColors.Chip),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Cube, 22.dp, AureaColors.Accent)
            }
        }
        AnchoredMenu(
            expanded = open,
            onDismiss = { open = false },
            alignEnd = true,
            tag = "camera.view.projection.menu",
            entries = listOf(
                MenuEntry.Item(perspective, glyph = CupertinoGlyph.Cube, selected = true, checked = true) {},
                MenuEntry.Item(parallel, glyph = CupertinoGlyph.Square, sublabel = soon, enabled = false),
            ),
        )
    }
}

/**
 * A RÉGUA CURVA do ângulo de visão: riscos num arco em volta da câmera
 * (desenhada à direita, com o cone do ângulo atual). Arrasto VERTICAL: os
 * riscos seguem o dedo (descer diminui o ângulo); 0,25°/dp. Toque digita.
 */
@Composable
private fun CurvedAngleRuler(
    value: () -> Float,
    min: Float,
    max: Float,
    enabled: Boolean,
    description: String,
    state: String,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    onTap: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val read by rememberUpdatedState(value)
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    val tap by rememberUpdatedState(onTap)
    val unitsPerDp = 0.25f
    Box(
        modifier
            .semantics {
                contentDescription = description
                stateDescription = state
                onClick { if (enabled) tap(); true }
            }
            .then(if (enabled) Modifier.tocavel(shrink = 1f, role = null) { tap() } else Modifier)
            .pointerInput(enabled, min, max) {
                if (!enabled) return@pointerInput
                var from = 0f
                var walked = 0f
                detectVerticalDragGestures(
                    onDragStart = {
                        from = read().let { if (it.isFinite()) it else 0f }
                        walked = 0f
                        begin()
                    },
                    onDragEnd = { finish() },
                    onDragCancel = { finish() },
                ) { change, dy ->
                    change.consume()
                    walked += dy / density
                    send((from - walked * unitsPerDp).coerceIn(min, max))
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val v = read().let { if (it.isFinite()) it else 0f }
            val camX = size.width - 12.dp.toPx()
            val camY = size.height / 2f
            val radius = (size.width - 22.dp.toPx()).coerceAtLeast(8.dp.toPx())
            val arcPerValue = 2f            // 1° de ângulo = 2° de arco
            val visible = 62f               // meia abertura do arco desenhado (°)
            // Cone do ângulo atual (preso à metade da tela).
            val half = (v.coerceIn(0f, 150f) / 2f) * (PI.toFloat() / 180f)
            val coneInk = AureaColors.Muted.copy(alpha = 0.35f)
            drawLine(coneInk, Offset(camX, camY), Offset(camX - radius * cos(half), camY - radius * sin(half)), 1.dp.toPx())
            drawLine(coneInk, Offset(camX, camY), Offset(camX - radius * cos(half), camY + radius * sin(half)), 1.dp.toPx())
            // Riscos a cada 5° (maiores a cada 15°), na mesma conta: arco = (risco − valor) × 2.
            val first = (kotlin.math.floor((v - visible / arcPerValue) / 5f) * 5f).toInt()
            val last = (kotlin.math.ceil((v + visible / arcPerValue) / 5f) * 5f).toInt()
            var t = first
            while (t <= last) {
                if (t >= 0 && t <= 180) {
                    val a = (t - v) * arcPerValue
                    if (abs(a) <= visible) {
                        val rad = a * (PI.toFloat() / 180f)
                        val major = t % 15 == 0
                        val len = if (major) 12.dp.toPx() else 7.dp.toPx()
                        val fade = 1f - abs(a) / visible
                        val ox = camX - radius * cos(rad)
                        val oy = camY + radius * sin(rad)
                        val ix = camX - (radius - len) * cos(rad)
                        val iy = camY + (radius - len) * sin(rad)
                        drawLine(AureaColors.Text.copy(alpha = 0.25f + 0.6f * fade), Offset(ox, oy), Offset(ix, iy), 1.dp.toPx())
                    }
                }
                t += 5
            }
            // Agulha (o valor atual) no meio do arco.
            drawLine(AureaColors.Accent, Offset(camX - radius - 2.dp.toPx(), camY), Offset(camX - radius + 16.dp.toPx(), camY), 2.dp.toPx(), cap = StrokeCap.Round)
        }
        CupertinoIcon(CupertinoGlyph.CameraFill, 16.dp, AureaColors.Text, Modifier.align(Alignment.CenterEnd))
    }
}

// =============================================================================
// Desfoque de foco
// =============================================================================

@Composable
private fun ColumnScope.CameraFocusTab(env: PanelEnv, lens: FloatArray) {
    val store = env.store
    val d = store.detail ?: return
    val dof = lens[2] >= 0.5f
    fun look(param: Int): KeyframeLook = store.cameraLensTrack(param)?.let { cameraTrackLook(store, it) } ?: KeyframeLook.None
    val focusLabel = stringResource(R.string.lens_focus_distance)
    val apertureLabel = stringResource(R.string.lens_aperture)
    val blurLabel = stringResource(R.string.lens_blur)
    Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 2.dp, end = 12.dp, bottom = 10.dp)) {
        BlurToggleRow(stringResource(R.string.om_ap_cam_focus), stringResource(R.string.lens_dof_hint), dof,
            Modifier.heightIn(min = 37.5.dp).testTag("camera.focus.switch")) { on ->
            store.beginGesture("profundidade de campo")
            store.setCameraLens(EditorStore.CAMERA_LENS_DOF, if (on) 1f else 0f)
            store.endGesture()
        }
        if (!dof) return@Column
        val focus = lens[3]
        // 1 dp de dedo ≈ 2 cm da cena (px por metro vem do motor).
        val focusPerDp = (lens[6] / 50f).let { if (it.isFinite() && it > 0f) it else 1f }
        com.aurea.aurea.ui.ds.PropertyRow(
            label = focusLabel,
            value = focus,
            unitsPerDp = focusPerDp,
            min = 0f,
            max = Float.POSITIVE_INFINITY,
            format = { "${numeroPtBr(it, 0)} px" },
            selected = true,
            onSelect = { store.toggleCameraLensKey(EditorStore.CAMERA_LENS_FOCUS) },
            onGestureStart = { store.beginGesture("distância de foco") },
            onValue = { store.setCameraLens(EditorStore.CAMERA_LENS_FOCUS, max(0f, it)) },
            onGestureEnd = { store.endGesture() },
            onTapValue = {
                env.openKeypad(KeypadRequest(focusLabel, focus, "px", 0f, Float.POSITIVE_INFINITY, 0) {
                    store.setCameraLens(EditorStore.CAMERA_LENS_FOCUS, max(0f, it))
                })
            },
            modifier = Modifier.testTag("camera.focus.distance"),
            keyframe = look(EditorStore.CAMERA_LENS_FOCUS),
            enabled = !d.locked,
        )
        val aperture = lens[4]
        com.aurea.aurea.ui.ds.PropertyRow(
            label = apertureLabel,
            value = aperture,
            unitsPerDp = 0.05f,
            min = CameraLens.APERTURE_MIN,
            max = CameraLens.APERTURE_MAX,
            format = { CameraLens.formatAperture(it) },
            selected = true,
            onSelect = { store.toggleCameraLensKey(EditorStore.CAMERA_LENS_APERTURE) },
            onGestureStart = { store.beginGesture("abertura") },
            onValue = { store.setCameraLens(EditorStore.CAMERA_LENS_APERTURE, it.coerceIn(CameraLens.APERTURE_MIN, CameraLens.APERTURE_MAX)) },
            onGestureEnd = { store.endGesture() },
            onTapValue = {
                env.openKeypad(KeypadRequest(apertureLabel, aperture, "f/", CameraLens.APERTURE_MIN, CameraLens.APERTURE_MAX, 1) {
                    store.setCameraLens(EditorStore.CAMERA_LENS_APERTURE, it)
                })
            },
            modifier = Modifier.testTag("camera.focus.aperture"),
            keyframe = look(EditorStore.CAMERA_LENS_APERTURE),
            enabled = !d.locked,
        )
        val blur = lens[5] * 100f
        com.aurea.aurea.ui.ds.PropertyRow(
            label = blurLabel,
            value = blur,
            unitsPerDp = 1f,
            min = 0f,
            max = CameraLens.BLUR_MAX_PERCENT,
            format = { "${numeroPtBr(it, 0)}%" },
            selected = true,
            onSelect = { store.toggleCameraLensKey(EditorStore.CAMERA_LENS_BLUR) },
            onGestureStart = { store.beginGesture("força do desfoque") },
            onValue = { store.setCameraLens(EditorStore.CAMERA_LENS_BLUR, it.coerceIn(0f, CameraLens.BLUR_MAX_PERCENT) / 100f) },
            onGestureEnd = { store.endGesture() },
            onTapValue = {
                env.openKeypad(KeypadRequest(blurLabel, blur, "%", 0f, CameraLens.BLUR_MAX_PERCENT, 0) {
                    store.setCameraLens(EditorStore.CAMERA_LENS_BLUR, it / 100f)
                })
            },
            modifier = Modifier.testTag("camera.focus.blur"),
            keyframe = look(EditorStore.CAMERA_LENS_BLUR),
            enabled = !d.locked,
        )
        // "Tocar para focar": ≥ 48 dp; arma o próximo toque no palco.
        val armed = store.focusPick
        Row(
            Modifier
                .fillMaxWidth()
                .padding(top = 8.dp, start = 4.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(if (armed) AureaColors.AccentDim else AureaColors.Chip)
                .testTag("camera.focus.pick")
                .tocavel(onClick = { if (armed) store.cancelFocusPick() else store.armPickFocus() })
                .heightIn(min = 48.dp)
                .padding(horizontal = 14.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.CameraViewfinder, 20.dp, if (armed) AureaColors.Accent else AureaColors.Text)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.lens_pick_focus), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600,
                    color = if (armed) AureaColors.Accent else AureaColors.Text)))
                Text(stringResource(R.string.lens_pick_focus_hint), style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, lineHeight = 16.sp,
                    color = AureaColors.Muted)))
            }
        }
    }
}

// =============================================================================
// Névoa
// =============================================================================

@Composable
private fun ColumnScope.CameraFogTab() {
    Column(
        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            stringResource(R.string.om_ap_cam_fog),
            modifier = Modifier.padding(bottom = 6.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        )
        Text(
            stringResource(R.string.om_ap_cam_fog_soon),
            modifier = Modifier.testTag("camera.fog.notice"),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, lineHeight = 17.sp, color = AureaColors.Text)),
        )
    }
}
