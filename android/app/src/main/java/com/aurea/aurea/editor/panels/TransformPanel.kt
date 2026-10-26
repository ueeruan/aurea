package com.aurea.aurea.editor.panels

import androidx.compose.ui.platform.testTag
import androidx.compose.foundation.selection.toggleable

import androidx.annotation.StringRes
import com.aurea.aurea.engine.TrackKey
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Animation
import androidx.compose.material.icons.rounded.AspectRatio
import androidx.compose.material.icons.rounded.BlurOn
import androidx.compose.material.icons.rounded.CameraAlt
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.heightIn
import com.aurea.aurea.state.EditorStore
import androidx.compose.material.icons.rounded.Opacity
import androidx.compose.material.icons.rounded.FilterCenterFocus
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.LinkOff
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.automirrored.rounded.RotateRight
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.collapse
import androidx.compose.ui.semantics.expand
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.VectorPropertyRow
import com.aurea.aurea.ui.ds.compactPropertyNumber
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/**
 * As faces de Transformar, na ordem do trilho direito (a do pedido do dono:
 * Posição, Escala, Rotação, Opacidade, Pivô, Desfoque de movimento). [title] vai
 * para o cabeçalho ("Transformar · Posição"); [props] é o grupo que o losango do
 * trilho crava (vazio = face sem keyframe).
 */
enum class TransformTab(@StringRes val title: Int, val icon: ImageVector, @StringRes val railLabel: Int, val props: IntArray) {
    Mover(R.string.fx_posicao, Icons.Rounded.OpenWith, R.string.fx_posicao, intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y)),
    Escalar(R.string.panel_escala, Icons.Rounded.AspectRatio, R.string.panel_escala, intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y)),
    Girar(R.string.panel_rotacao, Icons.AutoMirrored.Rounded.RotateRight, R.string.panel_rotacao, intArrayOf(TrackProperty.ROTATION_Z)),
    Opacidade(R.string.panel_opacidade, Icons.Rounded.Opacity, R.string.panel_opacidade, intArrayOf(TrackProperty.OPACITY)),
    Pivo(R.string.fx_pivo, Icons.Rounded.FilterCenterFocus, R.string.edt_pivot_rail, intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y)),
    Desfoque(R.string.panel_desfoque_movimento, Icons.Rounded.BlurOn, R.string.panel_desfoque_movimento, intArrayOf()),
    /** Animadores da camada (entrada/saída/wiggle; ◇ por linha, dentro do cartão). */
    Animadores(R.string.la_animators, Icons.Rounded.Animation, R.string.la_animators, intArrayOf()),
    /** Só câmera 3D: distância focal, profundidade de campo e Pick Focus (◇ por linha). */
    Lente(R.string.lens_title, Icons.Rounded.CameraAlt, R.string.lens_title, intArrayOf()),
}

/**
 * O PAINEL DE TRANSFORMAÇÃO [A] (`PainelDeTransformacao`): trilho esquerdo
 * (‹ · ◇ · curva · ⋯), régua com valores XYZ no modo 3D, almofada e fitas
 * no plano 2D e o trilho direito com as faces.
 */
private val RotationProps = intArrayOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z)

/** The rail diamond keys every axis exposed by the current dimension mode. */
internal fun transformKeyProperties(tab: TransformTab, threeD: Boolean, axis: Int = 2): IntArray = when {
    tab == TransformTab.Girar -> if (threeD) RotationProps.copyOf() else intArrayOf(RotationProps[2])
    threeD && tab == TransformTab.Mover -> intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y, TrackProperty.POSITION_Z)
    threeD && tab == TransformTab.Escalar -> intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y, TrackProperty.SCALE_Z)
    threeD && tab == TransformTab.Pivo -> intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y, TrackProperty.ANCHOR_Z)
    else -> tab.props
}


/**
 * X/Y/Z SÓ QUANDO SE APLICA: camada 3D de verdade (objeto 3D, câmera, luz, nulo
 * 3D) ou 2D que JÁ usa a terceira dimensão (inclinada em X/Y, com profundidade
 * ou com esses keyframes) — senão a face mostra só o plano da tela.
 */
internal fun uses3D(d: com.aurea.aurea.engine.LayerDetail?): Boolean {
    if (d == null) return false
    if (d.kind == LayerType.Model3D.kind || d.kind == LayerType.Camera.kind || d.kind == LayerType.Light.kind) return true
    if ((d.flags and com.aurea.aurea.engine.PodLayout.FLAG_THREE_D) != 0) return true
    if (abs(d.rotation[0]) > 0.01f || abs(d.rotation[1]) > 0.01f || abs(d.position[2]) > 0.01f) return true
    return d.isAnimated(TrackProperty.ROTATION_X) || d.isAnimated(TrackProperty.ROTATION_Y) || d.isAnimated(TrackProperty.POSITION_Z)
}

internal fun animatorRailTrack(count: Int, focused: TrackKey?, saved: TrackKey?): TrackKey? =
    listOfNotNull(focused, saved).firstOrNull {
        it.property == TrackProperty.LAYER_ANIM_PARAM && it.effectIndex in 0 until count && it.paramIndex in 0..17
    } ?: if (count > 0) TrackKey(TrackProperty.LAYER_ANIM_PARAM, 0, 0) else null

@Composable
internal fun TransformPanel(env: PanelEnv, tab: TransformTab, onTab: (TransformTab) -> Unit) {
    val store = env.store
    var wholeText by remember(store.primary) { mutableStateOf(false) }
    if (tab == TransformTab.Girar && store.text3d != null && !wholeText) {
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.t3d_letters), modifier = Modifier.weight(1f), color = AureaColors.Accent)
                androidx.compose.material3.TextButton(onClick = { wholeText = true }) { Text(stringResource(R.string.t3d_whole_object)) }
            }
            Box(Modifier.weight(1f)) { EffectsPanel(env, focusedType = effectTypeId("aurea.text3d.layout")) }
        }
        return
    }
    var menu by remember { mutableStateOf(false) }
    // A face Lente só existe na câmera 3D; trocou de camada com ela aberta → volta ao Mover.
    val isCamera = store.detail?.kind == LayerType.Camera.kind
    val tabs = if (isCamera) TransformTab.entries else TransformTab.entries.filter { it != TransformTab.Lente }
    androidx.compose.runtime.LaunchedEffect(isCamera, tab) { if (tab == TransformTab.Lente && !isCamera) onTab(TransformTab.Mover) }
    val show3D by remember(store) { derivedStateOf { uses3D(store.detail) } }
    var pickedAxis by rememberSaveable(store.primary, tab) { mutableIntStateOf(if (tab == TransformTab.Girar) 2 else 0) }
    val axis = if (show3D) pickedAxis else if (tab == TransformTab.Girar) 2 else 0
    val keyProps = transformKeyProperties(tab, show3D, axis)
    val vectorTab = tab in listOf(TransformTab.Mover, TransformTab.Escalar, TransformTab.Pivo)
    val props = when {
        tab == TransformTab.Girar -> intArrayOf(RotationProps[axis])
        show3D && vectorTab -> intArrayOf(keyProps[axis])
        else -> tab.props
    }
    val animator = if (tab == TransformTab.Animadores) animatorRailTrack(store.layerAnimators.size,
        store.timelineFocus?.singleOrNull(), store.transformAnimatorFocus?.takeIf { it.first == store.primary }?.second) else null
    androidx.compose.runtime.DisposableEffect(store, store.projectGeneration, store.composition?.id, store.primary, tab, show3D, axis, animator) {
        if (animator != null) store.focusLayerAnimator(animator)
        else store.timelineFocus = keyProps.map { TrackKey(it) }
        onDispose { store.timelineFocus = null }
    }
    val canKey = props.isNotEmpty() || animator != null
    val look by remember(store, tab, axis, show3D, animator) {
        derivedStateOf {
            if (animator != null) {
                val values = store.layerAnimators.getOrNull(animator.effectIndex)
                val bit = 1 shl animator.paramIndex
                when {
                    values != null && values[25].toInt() and bit != 0 -> com.aurea.aurea.ui.ds.KeyframeLook.KeyHere
                    values != null && values[24].toInt() and bit != 0 -> com.aurea.aurea.ui.ds.KeyframeLook.Animated
                    else -> com.aurea.aurea.ui.ds.KeyframeLook.None
                }
            } else if (keyProps.isEmpty()) com.aurea.aurea.ui.ds.KeyframeLook.None else transformLook(store.detail, keyProps)
        }
    }
    val curveKeys by remember(store, tab, axis, show3D, animator) {
        derivedStateOf {
            if (animator != null) return@derivedStateOf store.primaryKeys().filter {
                it.property == animator.property && it.effectIndex == animator.effectIndex && it.paramIndex == animator.paramIndex
            }.sortedBy { it.time }
            val candidates = (props.toList() + keyProps.toList()).distinct()
            curveTrack(candidates.map { store.primaryKeys().transformTrack(it) })
        }
    }
    val exprKeys = props.map { TrackKey(it) }
    val exprLook by remember(store, tab, axis, show3D) { derivedStateOf { store.expressionLook(exprKeys) } }
    // Expressão na unidade da tela: escala e opacidade em %, ângulo em °, o resto em px.
    val exprTitle = if (tab == TransformTab.Girar) stringResource(R.string.edt_rotation_axis, "XYZ"[axis].toString()) else stringResource(tab.title)
    val exprScale = if (tab == TransformTab.Escalar || tab == TransformTab.Opacidade) 100f else 1f
    val exprUnit = when (tab) {
        TransformTab.Escalar, TransformTab.Opacidade -> "%"
        TransformTab.Girar -> "°"
        else -> "px"
    }

    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = if (canKey) ({
                if (animator != null) store.toggleLayerAnimKey(animator.effectIndex, animator.paramIndex)
                else store.toggleTransformKeyframe(keyProps)
            }) else null,
            curveAnimated = look != com.aurea.aurea.ui.ds.KeyframeLook.None,
            onCurve = if (store.curveStart(curveKeys) != null) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        store.curveStart(curveKeys)?.let { key ->
                            store.selectKeyframe(layer, key)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
            more = { RailMoreButton(active = false) { menu = true } },
            expression = exprLook,
            onExpression = if (props.isNotEmpty()) ({ store.openExpression(exprTitle, exprKeys, exprScale, exprUnit) }) else null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            if (tab == TransformTab.Girar && store.text3d != null) {
                androidx.compose.material3.TextButton(onClick = { wholeText = false }) { Text(stringResource(R.string.t3d_letters)) }
            }
            androidx.compose.runtime.key(store.projectGeneration, store.composition?.id, store.primary, tab, show3D) {
            when (tab) {
                TransformTab.Mover -> MoveFace(env, pivot = false, depth = show3D, axis = axis, onAxis = { pickedAxis = it })
                TransformTab.Girar -> {
                    if (!show3D) Open3DRow { store.enableLayer3D() }
                    RotationFace(env, depth = show3D, axis = axis, onAxis = { pickedAxis = it })
                }
                TransformTab.Escalar -> ScaleFace(env, depth = show3D, axis = axis, onAxis = { pickedAxis = it })
                TransformTab.Opacidade -> OpacityFace(env, look)
                TransformTab.Pivo -> MoveFace(env, pivot = true, depth = show3D, axis = axis, onAxis = { pickedAxis = it })
                TransformTab.Desfoque -> MotionBlurFace(env)
                TransformTab.Animadores -> Column(
                    Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 6.dp, end = 12.dp),
                ) {
                    LayerAnimSection(env)
                    if (store.detail?.kind == LayerType.Text.kind || store.text3d != null) {
                        TextAnimSection(env, showAnimatorEffect = false)
                    }
                }
                TransformTab.Lente -> LensFace(env)
            }
            }
            Spacer(Modifier.height(10.dp))
        }
        RightRail(
            modes = tabs.map { RailMode(it.icon, stringResource(it.railLabel)) },
            selected = tabs.indexOf(tab).coerceAtLeast(0),
            onSelect = { i -> onTab(tabs[i]) },
        )
    }

    if (menu) {
        AureaActionSheet(
            title = stringResource(R.string.panel_transformar),
            actions = buildList {
                add(SheetAction(stringResource(R.string.transform_copy)) { store.copyTransform() })
                if (store.clipboard and 16 != 0) add(SheetAction(stringResource(R.string.transform_paste)) { store.pasteTransform() })
                if (tab == TransformTab.Girar) {
                    val turnsTitle = "$exprTitle · ${stringResource(R.string.edt_rotation_turns)}"
                    add(SheetAction(turnsTitle) {
                        val angle = store.detail?.rotation?.get(axis) ?: 0f
                        val (turns, rest) = rotationTurns(angle)
                        val owner = store.primary
                        val generation = store.projectGeneration
                        val composition = store.composition?.id
                        env.openKeypad(KeypadRequest(turnsTitle, turns.toFloat(), "x", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 0) {
                            if (store.primary == owner && store.projectGeneration == generation && store.composition?.id == composition)
                                store.setTransform(RotationProps[axis], it.roundToInt() * 360f + rest, owner)
                        })
                    })
                }
                val navigation = store.trackNavigation(curveKeys)
                add(SheetAction(stringResource(R.string.panel_keyframe_anterior), enabled = navigation?.hasPrevious == true) {
                    navigation?.takeIf { it.hasPrevious }?.let { store.pause(); store.seek(it.previousFrame) }
                })
                add(SheetAction(stringResource(R.string.panel_proximo_keyframe), enabled = navigation?.hasNext == true) {
                    navigation?.takeIf { it.hasNext }?.let { store.pause(); store.seek(it.nextFrame) }
                })
                if (tab != TransformTab.Desfoque && tab != TransformTab.Lente && tab != TransformTab.Animadores) add(SheetAction(stringResource(R.string.panel_voltar_padrao)) { resetTab(env, tab, axis) })
                if (canKey) add(SheetAction(if (exprLook == com.aurea.aurea.engine.ExpressionLook.None) stringResource(R.string.panel_adicionar_expressao) else stringResource(R.string.panel_editar_expressao)) {
                    store.openExpression(exprTitle, exprKeys, exprScale, exprUnit)
                })
                if (store.detail?.let { it.flags and com.aurea.aurea.engine.PodLayout.FLAG_THREE_D == 0 && it.kind !in 8..10 } == true) {
                    add(SheetAction(stringResource(R.string.panel_mostrar_x_y_z_3d)) { store.enableLayer3D() })
                }
            },
            onDismiss = { menu = false },
        )
    }
}

/** "Voltar ao padrão": posição no centro da composição, rotação 0, escala 100 %, opacidade 100 %, pivô no centro da mídia. */
private fun resetTab(env: PanelEnv, tab: TransformTab, axis: Int) {
    val store = env.store
    val d = store.detail ?: return
    when (tab) {
        TransformTab.Mover -> store.setTransform2(
            TrackProperty.POSITION_X, store.project.width / 2f, TrackProperty.POSITION_Y, store.project.height / 2f,
        )
        TransformTab.Girar -> store.setTransform(RotationProps[axis], 0f)
        TransformTab.Escalar -> if (store.layers.firstOrNull { it.id == store.primary }?.isThreeD == true) store.setScale3(floatArrayOf(1f, 1f, 1f)) else store.setTransform2(TrackProperty.SCALE_X, 1f, TrackProperty.SCALE_Y, 1f)
        TransformTab.Opacidade -> store.setTransform(TrackProperty.OPACITY, 1f)
        TransformTab.Pivo -> store.setTransform2(
            TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f,
        )
        TransformTab.Desfoque -> Unit
        TransformTab.Animadores -> Unit
        TransformTab.Lente -> Unit
    }
}

/** Opt into the scene camera, including at zero rotation and depth. */
@Composable
private fun Open3DRow(onOpen: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 12.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.Chip)
                .testTag("transform.enable3d")
                .tocavel(onClick = onOpen)
                .padding(horizontal = 14.dp, vertical = 6.dp),
        ) {
            Text(stringResource(R.string.panel_girar_3d_x_y), style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
        }
    }
}

// =============================================================================
// Opacidade e desfoque de movimento
// =============================================================================

/** OPACIDADE: a linha (0–100 %, ◇ no rótulo) e, embaixo, uma régua grande no mesmo valor. */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.OpacityFace(env: PanelEnv, look: com.aurea.aurea.ui.ds.KeyframeLook) {
    val store = env.store
    val opacity by remember(store) { derivedStateOf { (store.detail?.opacity ?: 1f) * 100f } }
    Column(Modifier.weight(1f).fillMaxWidth().padding(start = 2.dp, top = 6.dp, end = 10.dp)) {
        OpacityRow(env, opacity, selected = true, keyframe = look)
        Spacer(Modifier.height(6.dp))
        TickRuler(
            value = { opacity },
            unitsPerDp = 0.35f,
            active = true,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .valueDrag(
                    enabled = true,
                    start = { (store.detail?.opacity ?: 1f) * 100f },
                    unitsPerDp = { 0.35f },
                    min = 0f,
                    max = 100f,
                    onStart = { store.beginGesture("opacidade") },
                    onValue = { store.setTransform(TrackProperty.OPACITY, it / 100f) },
                    onEnd = { store.endGesture() },
                ),
        )
    }
}

/**
 * DESFOQUE DE MOVIMENTO: liga na camada (e no projeto, se estava desligado lá —
 * o motor só borra com os dois ligados). A composição controla ângulo e fase
 * em graus, além das amostras. O rastro local e o fluxo óptico do vídeo são
 * controles separados.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MotionBlurFace(env: PanelEnv) {
    val store = env.store
    val d = store.detail ?: return
    val on = d.motionBlur && store.compMotionBlur
    var advanced by rememberSaveable { mutableStateOf(false) }
    Column(
        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 6.dp, end = 12.dp),
    ) {
        BlurToggleRow(stringResource(R.string.panel_desfoque_movimento), stringResource(R.string.panel_borra_camada_direcao_ela_move), on) { v ->
            if (v) {
                if (!store.compMotionBlur) store.setCompositionMotionBlur(true)
                if (!d.motionBlur) store.setLayerMotionBlur(d.id, true)
            } else {
                store.setLayerMotionBlur(d.id, false)
            }
        }
        if (on) {
            MotionBlurSetting(env, stringResource(R.string.mb_shutter_angle), store.shutterAngle,
                "°", 0f, 720f, 1, "motionblur.shutter", store::changeShutterAngle)
            Text(
                stringResource(R.string.mb_comp_settings_hint),
                modifier = Modifier.padding(top = 4.dp, start = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
            // Comprimento do rastro SÓ desta camada (× o obturador do projeto).
            val lengthLabel = stringResource(R.string.la_motion_blur_length)
            val length = store.layerMotionBlurLength * 100f
            com.aurea.aurea.ui.ds.PropertyRow(
                label = lengthLabel,
                value = length,
                unitsPerDp = 0.5f,
                min = 0f,
                max = 400f,
                format = { "${numeroPtBr(it, 0)}%" },
                selected = false,
                onSelect = {},
                onGestureStart = { store.beginGesture("comprimento do desfoque") },
                onValue = { store.changeLayerMotionBlurLength(it.coerceIn(0f, 400f) / 100f) },
                onGestureEnd = { store.endGesture() },
                onTapValue = {
                    env.openKeypad(KeypadRequest(lengthLabel, length, "%", 0f, 400f, 0) { store.changeLayerMotionBlurLength(it / 100f) })
                },
            )
            Text(
                stringResource(R.string.la_motion_blur_length_hint),
                modifier = Modifier.padding(top = 4.dp, start = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
            val advancedLabel = stringResource(R.string.mb_advanced)
            com.aurea.aurea.ui.ds.AdvancedToggle(advanced, 3, { advanced = !advanced },
                Modifier.heightIn(min = 48.dp).testTag("motionblur.advanced").semantics {
                    contentDescription = advancedLabel
                    if (advanced) collapse { advanced = false; true }
                    else expand { advanced = true; true }
                })
            if (advanced) {
                MotionBlurSetting(env, stringResource(R.string.mb_shutter_phase), store.shutterPhase,
                    "°", -360f, 360f, 1, "motionblur.phase", store::changeShutterPhase)
                androidx.compose.material3.TextButton(onClick = store::centerMotionBlurExposure,
                    modifier = Modifier.heightIn(min = 48.dp).testTag("motionblur.center")) {
                    Text(stringResource(R.string.mb_center_exposure))
                }
                Text(stringResource(R.string.mb_phase_hint), style = AureaType.BodySmall, color = AureaColors.Muted)
                MotionBlurSetting(env, stringResource(R.string.mb_samples_per_frame), store.motionBlurSamples.toFloat(),
                    "", 2f, minOf(64, store.motionBlurAdaptiveLimit).toFloat(), 0, "motionblur.samples") {
                    store.changeMotionBlurSamples(it.roundToInt())
                }
                MotionBlurSetting(env, stringResource(R.string.mb_adaptive_limit), store.motionBlurAdaptiveLimit.toFloat(),
                    "", maxOf(2, store.motionBlurSamples).toFloat(), 256f, 0, "motionblur.adaptive") {
                    store.changeMotionBlurAdaptiveLimit(it.roundToInt())
                }
                Text(stringResource(R.string.mb_quality_hint), style = AureaType.BodySmall, color = AureaColors.Muted)
                // 0.3.2: motion blur 3D rápido (vetores) × alta qualidade (amostras).
                val vectorLabel = stringResource(R.string.mb_vector_3d)
                val vectorHint = stringResource(R.string.mb_vector_3d_hint)
                Row(
                    Modifier.fillMaxWidth().heightIn(min = 48.dp).padding(vertical = 6.dp).padding(start = 4.dp)
                        .testTag("motionblur.vector3d")
                        .toggleable(value = store.motionBlurVector, role = androidx.compose.ui.semantics.Role.Switch,
                            onValueChange = store::changeMotionBlurVector)
                        .semantics(mergeDescendants = true) { contentDescription = "$vectorLabel. $vectorHint" },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(vectorLabel, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
                        Text(vectorHint, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
                    }
                    Spacer(Modifier.width(10.dp))
                    com.aurea.aurea.ui.ds.AureaToggle(checked = store.motionBlurVector, onCheckedChange = store::changeMotionBlurVector)
                }
            }
        }
        if (d.kind == LayerType.Video.kind) {
            Spacer(Modifier.height(8.dp))
            BlurToggleRow(stringResource(R.string.panel_desfoque_movimento_video), stringResource(R.string.panel_borra_mexe_dentro_video), d.vectorBlur) { store.setVectorBlur(d.id, it) }
        }
    }
}

@Composable
private fun MotionBlurSetting(env: PanelEnv, label: String, value: Float, unit: String,
                              min: Float, max: Float, decimals: Int, tag: String, onValue: (Float) -> Unit) {
    val edit = { env.openKeypad(KeypadRequest(label, value, unit, min, max, decimals, onConfirm = onValue)) }
    com.aurea.aurea.ui.ds.PropertyRow(
        label = label, value = value, unitsPerDp = if (decimals == 0) .1f else .5f,
        min = min, max = max, format = { "${numeroPtBr(it, decimals)}$unit" },
        selected = false, onSelect = {}, modifier = Modifier.testTag(tag).semantics(mergeDescendants = true) {
            contentDescription = label
            stateDescription = "${numeroPtBr(value, decimals)}$unit"
            onClick { edit(); true }
        },
        onGestureStart = { env.store.beginGesture(label) }, onValue = onValue,
        onGestureEnd = { env.store.endGesture() },
        onTapValue = edit,
    )
}

/**
 * LENTE (só câmera 3D): a distância focal em mm manda na projeção (FOV =
 * 2·atan(24 / 2f), mostrado ao lado) com lentes prontas em fichas; a
 * profundidade de campo liga a distância de foco, a abertura e a força do
 * desfoque; "Tocar para focar" arma o próximo toque no palco, que mede a
 * distância até o 3D sob o dedo. Cada linha tem o seu ◇ (trilha própria).
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.LensFace(env: PanelEnv) {
    val store = env.store
    val d = store.detail ?: return
    val lens = store.cameraLens ?: return
    val focal = lens[0]
    val dof = lens[2] >= 0.5f
    val keys = store.keyframes[d.id].orEmpty()
    fun look(param: Int): com.aurea.aurea.ui.ds.KeyframeLook {
        val p = store.cameraLensTrack(param) ?: return com.aurea.aurea.ui.ds.KeyframeLook.None
        val own = keys.filter { it.property == p && it.effectIndex == EditorStore.NO_EFFECT }
        return when {
            own.any { it.time == d.localPlayhead } -> com.aurea.aurea.ui.ds.KeyframeLook.KeyHere
            own.isNotEmpty() -> com.aurea.aurea.ui.ds.KeyframeLook.Animated
            else -> com.aurea.aurea.ui.ds.KeyframeLook.None
        }
    }
    val focalLabel = stringResource(R.string.lens_focal)
    val focusLabel = stringResource(R.string.lens_focus_distance)
    val apertureLabel = stringResource(R.string.lens_aperture)
    val blurLabel = stringResource(R.string.lens_blur)
    Column(
        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 6.dp, end = 12.dp),
    ) {
        com.aurea.aurea.ui.ds.PropertyRow(
            label = focalLabel,
            value = focal,
            unitsPerDp = 0.5f,
            min = CameraLens.DRAG_MIN_MM,
            max = CameraLens.DRAG_MAX_MM,
            format = { CameraLens.formatFocal(it) },
            selected = true,
            onSelect = { store.toggleCameraLensKey(EditorStore.CAMERA_LENS_FOCAL) },
            onGestureStart = { store.beginGesture("distância focal") },
            onValue = { store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, it.coerceIn(CameraLens.DRAG_MIN_MM, CameraLens.DRAG_MAX_MM)) },
            onGestureEnd = { store.endGesture() },
            onTapValue = {
                env.openKeypad(KeypadRequest(focalLabel, focal, "mm", CameraLens.KEYPAD_MIN_MM, CameraLens.KEYPAD_MAX_MM, 0) {
                    store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, it)
                })
            },
            keyframe = look(EditorStore.CAMERA_LENS_FOCAL),
        )
        // FOV derivado + lentes prontas (a ficha acesa é a que bate com a focal).
        Row(
            Modifier.fillMaxWidth().padding(top = 2.dp, bottom = 6.dp).horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                stringResource(R.string.lens_fov_readout, CameraLens.formatFov(lens[1])),
                modifier = Modifier.padding(start = 4.dp, end = 6.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
            )
            val picked = CameraLens.presetIndex(focal)
            CameraLens.PRESETS_MM.forEachIndexed { i, mm ->
                val on = picked == i
                Box(
                    Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(onClick = {
                            store.beginGesture("distância focal")
                            store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, mm.toFloat())
                            store.endGesture()
                        })
                        .heightIn(min = 36.dp)
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                ) {
                    Text("$mm", style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W700,
                        color = if (on) AureaColors.Accent else AureaColors.Text)))
                }
            }
        }
        val affectsBelow = lens.size > 9 && lens[9] >= 0.5f
        BlurToggleRow(stringResource(R.string.camera_affects_below), stringResource(R.string.camera_affects_below_hint), affectsBelow) { on ->
            store.beginGesture("camera afeta camadas abaixo")
            store.setCameraLens(EditorStore.CAMERA_AFFECTS_BELOW, if (on) 1f else 0f)
            store.endGesture()
        }
        BlurToggleRow(stringResource(R.string.lens_dof), stringResource(R.string.lens_dof_hint), dof) { on ->
            store.beginGesture("profundidade de campo")
            store.setCameraLens(EditorStore.CAMERA_LENS_DOF, if (on) 1f else 0f)
            store.endGesture()
        }
        if (dof) {
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
                keyframe = look(EditorStore.CAMERA_LENS_FOCUS),
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
                keyframe = look(EditorStore.CAMERA_LENS_APERTURE),
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
                keyframe = look(EditorStore.CAMERA_LENS_BLUR),
            )
            // "Tocar para focar": ≥ 48 dp; arma o próximo toque no palco.
            val armed = store.focusPick
            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp, start = 4.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (armed) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { if (armed) store.cancelFocusPick() else store.armPickFocus() })
                    .heightIn(min = 48.dp)
                    .padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(Icons.Rounded.FilterCenterFocus, contentDescription = null, tint = if (armed) AureaColors.Accent else AureaColors.Text, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.lens_pick_focus), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = if (armed) AureaColors.Accent else AureaColors.Text)))
                    Text(stringResource(R.string.lens_pick_focus_hint), style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
                }
            }
        }
    }
}

@Composable
private fun BlurToggleRow(label: String, hint: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp).padding(start = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
            Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
        }
        Spacer(Modifier.width(10.dp))
        com.aurea.aurea.ui.ds.AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

// =============================================================================
// Mover e Pivô: a almofada com cantos em L
// =============================================================================

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MoveVectorFace(env: PanelEnv, pivot: Boolean, axis: Int, onAxis: (Int) -> Unit) {
    val store = env.store
    val detail = store.detail ?: return
    val owner = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    fun currentOwner() = store.primary == owner && store.projectGeneration == generation && store.composition?.id == composition
    if (pivot) androidx.compose.runtime.DisposableEffect(Unit) {
        com.aurea.aurea.editor.PivotStage.active = true
        onDispose { com.aurea.aurea.editor.PivotStage.active = false }
    }
    val label = stringResource(if (pivot) R.string.fx_pivo else R.string.fx_posicao)
    val properties = if (pivot) intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y, TrackProperty.ANCHOR_Z)
        else intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y, TrackProperty.POSITION_Z)
    val values = if (pivot) listOf(detail.anchor[0] - detail.sourceWidth / 2f,
        detail.anchor[1] - detail.sourceHeight / 2f, detail.anchor[2]) else detail.position.toList()
    fun write(component: Int, value: Float) {
        if (!currentOwner()) return
        val current = store.detail ?: return
        // Numeric anchors retain their local, center-relative units. The stage
        // and preset controls keep their existing compensated pivot commands.
        val engineValue = if (pivot) when (component) {
            0 -> value + current.sourceWidth / 2f
            1 -> value + current.sourceHeight / 2f
            else -> value
        } else value
        store.setTransform(properties[component], engineValue)
    }
    val expressionKeys = listOf(TrackKey(properties[axis]))
    FocusedVectorFace(extraHeight = if (pivot) 104.dp else 0.dp) { controlHeight ->
        VectorPropertyRow(label, values, listOf("X", "Y", "Z"), axis,
            unitsPerDp = max(1, store.project.width) / 360f,
            min = Float.NEGATIVE_INFINITY, max = Float.POSITIVE_INFINITY,
            format = { "${numeroPtBr(it, 1)}px" }, selected = true,
            onSelectAxis = onAxis,
            onGestureStart = {
                if (store.playing || store.preview.buffering) store.pause()
                store.beginGesture(if (pivot) "mover pivô" else "mover")
            }, onValue = ::write, onGestureEnd = store::endGesture,
            onTapValue = { component ->
                onAxis(component)
                env.openKeypad(KeypadRequest("$label · ${"XYZ"[component]}", values[component], "px",
                    Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(component, it) })
            }, keyframes = properties.map { transformLook(detail, intArrayOf(it)) },
            enabled = !detail.locked, expression = store.expressionLook(expressionKeys),
            onExpression = { store.openExpression("$label · ${"XYZ"[axis]}", expressionKeys, 1f, "px") },
            tag = if (pivot) "transform.pivot" else "transform.position", displayFormat = { compactPropertyNumber(it, 1) },
            modifier = Modifier.height(controlHeight), focusedPanel = true)
        if (pivot) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.heightIn(min = 44.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.ControlButton)
                    .tocavel(enabled = !detail.locked) {
                        val current = store.detail ?: return@tocavel
                        store.setTransform2(TrackProperty.ANCHOR_X, current.sourceWidth / 2f,
                            TrackProperty.ANCHOR_Y, current.sourceHeight / 2f)
                    }.padding(horizontal = 14.dp), contentAlignment = Alignment.Center) {
                    Text(stringResource(R.string.panel_centro), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
                        fontWeight = FontWeight.W600, color = Color.White)))
                }
                Spacer(Modifier.width(10.dp))
                AnchorPresetGrid { fx, fy -> store.primary?.let { com.aurea.aurea.editor.LayerOps.presetAnchor(store, it, fx, fy) } }
            }
            Text(stringResource(R.string.panel_pivo_arraste_preview), textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
        }
    }
}

/**
 * MOVER (e PIVÔ): almofada relativa — 1 dp de dedo vale `largura/360` px da
 * composição. No Mover, os campos x · y · z moram DENTRO dos cantos (cabeçalho
 * da almofada) e o toque em z troca o arrasto para profundidade; no Pivô eles
 * ficam na fileira de cima com o botão "Centro".
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MoveFace(env: PanelEnv, pivot: Boolean, depth: Boolean, axis: Int, onAxis: (Int) -> Unit) {
    // Posição usa sempre a almofada (também em 3D: Z fica na caixa ao lado);
    // só o Pivô 3D continua na fileira XYZ com régua.
    if (depth && pivot) {
        MoveVectorFace(env, pivot, axis, onAxis)
        return
    }
    val store = env.store
    val xLabel = stringResource(if (pivot) R.string.panel_pivo_x else R.string.panel_posicao_x)
    val yLabel = stringResource(if (pivot) R.string.panel_pivo_y else R.string.panel_posicao_y)
    val pivotZLabel = stringResource(R.string.edt_pivot_z)
    val depthZLabel = stringResource(R.string.panel_profundidade_z)
    var zPicked by rememberSaveable { mutableStateOf(false) }
    // Sem 3D não há profundidade: o arrasto é sempre X/Y.
    val zMode = zPicked && depth
    var dragging by remember { mutableStateOf(false) }
    // Face Pivô aberta: o palco passa a mover o pivô (Stage.kt `pivotGesture`).
    if (pivot) {
        androidx.compose.runtime.DisposableEffect(Unit) {
            com.aurea.aurea.editor.PivotStage.active = true
            onDispose { com.aurea.aurea.editor.PivotStage.active = false }
        }
    }
    val x by remember(store, pivot) {
        derivedStateOf { store.detail?.let { if (pivot) it.anchor[0] - it.sourceWidth / 2f else it.position[0] } ?: 0f }
    }
    val y by remember(store, pivot) {
        derivedStateOf { store.detail?.let { if (pivot) it.anchor[1] - it.sourceHeight / 2f else it.position[1] } ?: 0f }
    }
    val z by remember(store) { derivedStateOf { store.detail?.position?.get(2) ?: 0f } }

    val fields: @Composable () -> Unit = {
        Row(verticalAlignment = Alignment.Top, horizontalArrangement = Arrangement.Center) {
            ValueBox("${numeroPtBr(x, 0)}px", width = 64.dp, label = "x", onTap = {
                val d = store.detail ?: return@ValueBox
                env.openKeypad(
                    KeypadRequest(xLabel, x, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_X, it + d.sourceWidth / 2f)
                        else store.setTransform(TrackProperty.POSITION_X, it)
                    },
                )
            })
            Spacer(Modifier.width(6.dp))
            ValueBox("${numeroPtBr(y, 0)}px", width = 64.dp, label = "y", onTap = {
                val d = store.detail ?: return@ValueBox
                env.openKeypad(
                    KeypadRequest(yLabel, y, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_Y, it + d.sourceHeight / 2f)
                        else store.setTransform(TrackProperty.POSITION_Y, it)
                    },
                )
            })
            if (pivot && depth) {
                Spacer(Modifier.width(6.dp))
                val az = store.detail?.anchor?.get(2) ?: 0f
                ValueBox("${numeroPtBr(az, 0)}px", width = 64.dp, label = "z", onTap = {
                    env.openKeypad(KeypadRequest(pivotZLabel, az, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        store.setTransform(TrackProperty.ANCHOR_Z, it)
                    })
                })
            }
            Spacer(Modifier.width(14.dp))
            if (pivot) {
                Box(
                    Modifier
                        .height(30.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(AureaColors.ControlButton)
                        .tocavel {
                            val d = store.detail ?: return@tocavel
                            store.setTransform2(TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f)
                        }
                        .padding(horizontal = 14.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(stringResource(R.string.panel_centro), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = Color.White)))
                }
                Spacer(Modifier.width(10.dp))
                // Âncora predefinida (app antigo): 9 pontos da mídia; a camada fica no lugar.
                AnchorPresetGrid { fx, fy -> store.primary?.let { com.aurea.aurea.editor.LayerOps.presetAnchor(store, it, fx, fy) } }
            } else if (depth) {
                // z: o toque ESCOLHE a profundidade para o arrasto; segurar digita.
                ValueBox(
                    "${numeroPtBr(z, 0)}px",
                    width = 64.dp,
                    label = "z",
                    color = if (zMode) AureaColors.Accent else Color.White,
                    onLongPress = {
                        env.openKeypad(KeypadRequest(depthZLabel, z, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                            store.setTransform(TrackProperty.POSITION_Z, it)
                        })
                    },
                    onTap = { zPicked = !zPicked },
                )
            }
        }
    }

    if (pivot) {
        Box(Modifier.fillMaxWidth().height(44.dp), contentAlignment = Alignment.Center) { fields() }
        Text(
            stringResource(R.string.panel_pivo_arraste_preview),
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 2.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
    }
    val controlledTracks = when {
        pivot -> listOf(TrackKey(TrackProperty.ANCHOR_X), TrackKey(TrackProperty.ANCHOR_Y), TrackKey(TrackProperty.POSITION_X), TrackKey(TrackProperty.POSITION_Y))
        zMode -> listOf(TrackKey(TrackProperty.POSITION_Z))
        else -> listOf(TrackKey(TrackProperty.POSITION_X), TrackKey(TrackProperty.POSITION_Y))
    }
    val driven = store.expressionLook(controlledTracks).let {
        it == com.aurea.aurea.engine.ExpressionLook.On || it == com.aurea.aurea.engine.ExpressionLook.Error
    }
    val expressionNotice = stringResource(R.string.transform_expression_controlled)
    val expressionNoticeNow by rememberUpdatedState(if (driven) expressionNotice else null)
    val hint = when {
        driven -> expressionNotice
        pivot -> stringResource(R.string.panel_deslize_ponto_giro_botao_centro_devolve)
        zMode -> stringResource(R.string.panel_deslize_ajustar_profundidade_toque_z_voltar)
        depth -> stringResource(R.string.panel_deslize_mover_toque_z_profundidade)
        else -> stringResource(R.string.panel_deslize_mover_camada)
    }
    val zNow by rememberUpdatedState(zMode)
    // Preciso (app antigo): o dedo anda 4× mais para o mesmo deslocamento.
    var fine by rememberSaveable { mutableStateOf(false) }
    val fineNow by rememberUpdatedState(fine)
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .testTag(if (pivot) "transform.pivot.pad" else "transform.move.pad")
            .pointerInput(store, store.primary, pivot, depth) {
                var startX = 0f
                var startY = 0f
                var startZ = 0f
                var acc = Offset.Zero
                var gain = 1f
                var pivotDrag: com.aurea.aurea.editor.PivotDragSession? = null
                var gestureOpen = false
                fun finishGesture() {
                    dragging = false
                    if (gestureOpen) {
                        gestureOpen = false
                        store.endGesture()
                    }
                }
                try { detectDragGestures(
                    onDragStart = {
                        if (store.playing || store.preview.buffering) store.pause()
                        expressionNoticeNow?.let(store::showToast)
                        val d = store.detail
                        if (d != null) {
                            startX = if (pivot) d.anchor[0] else d.position[0]
                            startY = if (pivot) d.anchor[1] else d.position[1]
                            startZ = d.position[2]
                            pivotDrag = if (pivot) {
                                val p = FloatArray(2)
                                d.parentToComp(d.position[0], d.position[1], p)
                                com.aurea.aurea.editor.PivotDragSession(store, d, p[0], p[1])
                            } else null
                        }
                        acc = Offset.Zero
                        // O GANHO da A.01: 360 dp de dedo atravessam a largura da composição.
                        gain = max(1, store.project.width) / 360f * (if (fineNow) 0.25f else 1f)
                        dragging = true
                        gestureOpen = true
                        store.beginGesture(if (pivot) "mover pivô" else "mover")
                    },
                    onDragEnd = { finishGesture() },
                    onDragCancel = { finishGesture() },
                ) { change, delta ->
                    change.consume()
                    acc += Offset(delta.x / density, delta.y / density)
                    if (pivot) {
                        // O pivô anda na composição e a posição compensa: a imagem fica.
                        pivotDrag?.let { it.moveTo(it.startX + acc.x * gain, it.startY + acc.y * gain) }
                        return@detectDragGestures
                    }
                    if (zNow) {
                        store.setTransform(TrackProperty.POSITION_Z, startZ - acc.y * gain)
                        return@detectDragGestures
                    }
                    var tx = startX + acc.x * gain
                    var ty = startY + acc.y * gain
                    // Gesto quase reto segue o eixo inicial; perto do centro, encaixa.
                    if (abs(acc.x) > 12f && abs(acc.y) < 6f) ty = startY
                    else if (abs(acc.y) > 12f && abs(acc.x) < 6f) tx = startX
                    val cx = store.project.width / 2f
                    val cy = store.project.height / 2f
                    if (abs(tx - cx) < 5f * gain) tx = cx
                    if (abs(ty - cy) < 5f * gain) ty = cy
                    store.setTransform2(TrackProperty.POSITION_X, tx, TrackProperty.POSITION_Y, ty)
                } } finally { finishGesture() }
            },
    ) {
        CornerMarks(Modifier.fillMaxSize())
        if (!pivot) {
            Box(Modifier.fillMaxWidth().padding(top = 10.dp, start = 16.dp, end = 16.dp), contentAlignment = Alignment.TopCenter) {
                PositionFields(env, x, y, z, zActive = zMode, depth = depth, onDepthPick = { zPicked = !zPicked })
            }
        }
        Text(
            hint,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .align(Alignment.Center)
                .padding(top = if (pivot) 0.dp else 36.dp, start = 16.dp, end = 16.dp),
            // A instrução some enquanto o dedo arrasta (pela cor: nada é remedido).
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (driven) AureaColors.Text else if (dragging) Color.Transparent else AureaColors.Muted)),
        )
        if (!pivot) {
            Box(
                Modifier
                    .align(Alignment.BottomEnd)
                    .padding(end = 14.dp, bottom = 10.dp)
                    .height(28.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(if (fine) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.RailModeFill)
                    .tocavel { fine = !fine }
                    .padding(horizontal = 12.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    stringResource(R.string.panel_mover_preciso),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (fine) AureaColors.Accent else AureaColors.Text)),
                )
            }
        }
    }
}

/**
 * Campos da Posição dentro da almofada: X e Y numa caixa só (verde), Z numa
 * caixa separada, uma casa decimal e o rótulo do eixo embaixo. O toque em X/Y
 * digita; em Z escolhe a profundidade para o arrasto (camada 3D) ou digita (2D);
 * segurar Z sempre digita.
 */
@Composable
private fun PositionFields(env: PanelEnv, x: Float, y: Float, z: Float, zActive: Boolean, depth: Boolean, onDepthPick: () -> Unit) {
    val store = env.store
    val xLabel = stringResource(R.string.panel_posicao_x)
    val yLabel = stringResource(R.string.panel_posicao_y)
    val zLabel = stringResource(R.string.panel_profundidade_z)
    val enabled = store.detail?.locked != true
    fun keypad(label: String, value: Float, property: Int) = env.openKeypad(
        KeypadRequest(label, value, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { store.setTransform(property, it) })
    Row(verticalAlignment = Alignment.Top, horizontalArrangement = Arrangement.Center) {
        Box {
            Box(Modifier.matchParentSize().padding(bottom = 18.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip))
            Row {
                PositionAxis(numeroPtBr(x, 1), "X", xLabel, AureaColors.Accent, enabled, "transform.position.x",
                    onTap = { keypad(xLabel, x, TrackProperty.POSITION_X) })
                PositionAxis(numeroPtBr(y, 1), "Y", yLabel, AureaColors.Accent, enabled, "transform.position.y",
                    onTap = { keypad(yLabel, y, TrackProperty.POSITION_Y) })
            }
        }
        // Z só com profundidade (camada 3D): em 2D a posição é X e Y, como no
        // app antigo (0.2.7). A profundidade de uma camada 2D liga pelo "3D".
        if (depth) {
            Spacer(Modifier.width(28.dp))
            Box {
                Box(Modifier.matchParentSize().padding(bottom = 18.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip))
                PositionAxis(numeroPtBr(z, 1), "Z", zLabel, if (zActive) AureaColors.Accent else Color.White, enabled, "transform.position.z",
                    onLongPress = { keypad(zLabel, z, TrackProperty.POSITION_Z) },
                    onTap = onDepthPick)
            }
        }
    }
}

@Composable
private fun PositionAxis(text: String, axis: String, description: String, color: Color, enabled: Boolean, tag: String,
                         onLongPress: (() -> Unit)? = null, onTap: () -> Unit) {
    Column(
        Modifier.width(76.dp).heightIn(min = 52.dp).testTag(tag)
            .semantics(mergeDescendants = true) { contentDescription = "$description $text" }
            .tocavel(enabled = enabled, shrink = 1f, onLongClick = onLongPress, onClick = onTap),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.fillMaxWidth().height(34.dp).padding(horizontal = 4.dp), contentAlignment = Alignment.Center) {
            Text(text, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600,
                color = if (enabled) color else AureaColors.Muted,
                textDecoration = androidx.compose.ui.text.style.TextDecoration.Underline)))
        }
        Text(axis, modifier = Modifier.padding(top = 2.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Muted)))
    }
}

/** 3 × 3 pontos da caixa da mídia (âncora predefinida); o toque escolhe a célula. */
@Composable
private fun AnchorPresetGrid(onPick: (Float, Float) -> Unit) {
    val label = stringResource(R.string.panel_ancora_predefinida)
    Box(
        Modifier
            .size(30.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(AureaColors.ControlButton)
            .semantics { contentDescription = label }
            .pointerInput(Unit) {
                detectTapGestures { p ->
                    val cx = (p.x / size.width * 3f).toInt().coerceIn(0, 2)
                    val cy = (p.y / size.height * 3f).toInt().coerceIn(0, 2)
                    onPick(cx * 0.5f, cy * 0.5f)
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val r = 1.6.dp.toPx()
            for (i in 0..2) for (j in 0..2) {
                drawCircle(Color.White, if (i == 1 && j == 1) r * 1.4f else r, Offset(size.width * (i + 0.5f) / 3f, size.height * (j + 0.5f) / 3f))
            }
        }
    }
}

/**
 * OS QUATRO CANTOS EM L (`_PintorDosCantos`): folga 12, braço 26 (encolhe em área
 * pequena), traço 2 muted a 45 %, pontas redondas. Canto, e não caixa fechada.
 */
@Composable
private fun CornerMarks(modifier: Modifier) {
    Canvas(modifier) {
        val gap = 12.dp.toPx()
        val l = gap
        val r = size.width - gap
        val t = gap
        val b = size.height - gap
        if (r <= l || b <= t) return@Canvas
        val arm = min(26.dp.toPx(), min((r - l) / 2f, (b - t) / 2f))
        val p = Path().apply {
            moveTo(l + arm, t); lineTo(l, t); lineTo(l, t + arm)
            moveTo(r - arm, t); lineTo(r, t); lineTo(r, t + arm)
            moveTo(l + arm, b); lineTo(l, b); lineTo(l, b - arm)
            moveTo(r - arm, b); lineTo(r, b); lineTo(r, b - arm)
        }
        drawPath(p, AureaColors.Muted.copy(alpha = 0.45f), style = Stroke(2.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
    }
}

// =============================================================================
// Girar: a régua de ângulo
// =============================================================================

/** Total degrees stay unbounded; the shared ruler edits one visible axis at a time. */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.RotationFace(env: PanelEnv, depth: Boolean, axis: Int, onAxis: (Int) -> Unit) {
    val store = env.store
    val owner = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    fun write(component: Int, value: Float) {
        if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition) return
        store.setTransform(RotationProps[component], value, owner)
    }
    val exposed = if (depth) listOf(0, 1, 2) else listOf(2)
    val selected = exposed.indexOf(axis).coerceAtLeast(0)
    val label = stringResource(R.string.panel_rotacao)
    val values = exposed.map { store.detail?.rotation?.get(it) ?: 0f }
    val keys = exposed.map { TrackKey(RotationProps[it]) }
    val expressionKeys = listOf(keys[selected])
    FocusedVectorFace { controlHeight ->
    VectorPropertyRow(
        label = label, values = values, axisLabels = exposed.map { "XYZ"[it].toString() }, selectedAxis = selected,
        unitsPerDp = 1f, min = Float.NEGATIVE_INFINITY, max = Float.POSITIVE_INFINITY,
        format = { "${numeroPtBr(it, 1)}°" }, selected = true,
        onSelectAxis = { onAxis(exposed[it]) },
        onGestureStart = { store.beginGesture("girar") },
        onValue = { component, value -> write(exposed[component], value) },
        onGestureEnd = store::endGesture,
        onTapValue = { component ->
            val currentAxis = exposed[component]
            onAxis(currentAxis)
            env.openKeypad(KeypadRequest("$label · ${"XYZ"[currentAxis]}", values[component], "°",
                Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(currentAxis, it) })
        },
        keyframes = exposed.map { transformLook(store.detail, intArrayOf(RotationProps[it])) },
        enabled = store.detail?.locked != true,
        expression = store.expressionLook(expressionKeys),
        onExpression = { store.openExpression("$label · ${"XYZ"[axis]}", expressionKeys, 1f, "°") },
        modifier = Modifier.height(controlHeight), tag = "transform.rotation",
        displayFormat = { compactPropertyNumber(it, 1) }, focusedPanel = true,
    )
    }
}

/** Give the active transform a broad ruler below its values; small panels can scroll. */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.FocusedVectorFace(
    extraHeight: androidx.compose.ui.unit.Dp = 0.dp,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.(androidx.compose.ui.unit.Dp) -> Unit,
) {
    BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
        val controlHeight = (maxHeight - 16.dp - extraHeight).coerceAtLeast(180.dp)
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 6.dp, vertical = 8.dp)) {
            content(controlHeight)
        }
    }
}

/**
 * Rotação em voltas + resto, como o AE mostra ("2x +30°"): as voltas truncam
 * para o zero e o resto tem o MESMO sinal (−405° = "-1x -45°"). Conta em
 * décimos de grau para 719,99° não virar "1x +360°".
 */
internal fun rotationTurns(angle: Float): Pair<Int, Float> {
    val tenths = (angle.toDouble() * 10.0).roundToLong()
    val turns = tenths / 3600L
    return turns.toInt() to ((tenths - turns * 3600L) / 10.0).toFloat()
}

// =============================================================================
// Escalar: duas fitas e a corrente
// =============================================================================

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleVectorFace(env: PanelEnv, axis: Int, onAxis: (Int) -> Unit) {
    val store = env.store
    val detail = store.detail ?: return
    val owner = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    val label = stringResource(R.string.panel_escala)
    val properties = intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y, TrackProperty.SCALE_Z)
    val values = detail.scale.map { it * 100f }
    var gestureFrom by remember(store.primary) { mutableStateOf<List<Float>?>(null) }
    val locked = store.scaleAxesLinked
    fun write(component: Int, value: Float, from: List<Float>) {
        if (store.primary != owner || store.projectGeneration != generation || store.composition?.id != composition) return
        if (store.scaleAxesLinked) {
            val base = from[component]
            val scale = if (base != 0f) com.aurea.aurea.engine.AureaEngine.gestureScale3D(detail.kind,
                from[0] / 100f, from[1] / 100f, from[2] / 100f, 3, value / base)
                else com.aurea.aurea.state.linkedScale(from.toFloatArray(), component, value).map { it / 100f }.toFloatArray()
            store.setScale3(scale)
        } else store.setTransform(properties[component], value / 100f)
    }
    val expressionKeys = listOf(TrackKey(properties[axis]))
    val hasFit = com.aurea.aurea.editor.LayerGeometry.hasSize(detail) && detail.kind != LayerType.Audio.kind
    FocusedVectorFace(extraHeight = if (hasFit) 104.dp else 48.dp) { controlHeight ->
        VectorPropertyRow(label, values, listOf("X", "Y", "Z"), axis, unitsPerDp = .5f,
            min = Float.NEGATIVE_INFINITY, max = Float.POSITIVE_INFINITY,
            format = { "${numeroPtBr(it, 1)}%" }, selected = true, onSelectAxis = onAxis,
            onGestureStart = { gestureFrom = values; store.beginGesture("escala") },
            onValue = { component, value -> write(component, value, gestureFrom ?: values) },
            onGestureEnd = { gestureFrom = null; store.endGesture() },
            onTapValue = { component ->
                onAxis(component)
                env.openKeypad(KeypadRequest("$label · ${"XYZ"[component]}", values[component], "%",
                    Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(component, it, values) })
            }, keyframes = properties.map { transformLook(detail, intArrayOf(it)) }, enabled = !detail.locked,
            expression = store.expressionLook(expressionKeys),
            onExpression = { store.openExpression("$label · ${"XYZ"[axis]}", expressionKeys, 100f, "%") },
            tag = "transform.scale", displayFormat = { compactPropertyNumber(it, 1) },
            modifier = Modifier.height(controlHeight), focusedPanel = true)
        val linkLabel = stringResource(if (locked) R.string.panel_soltar_largura_altura else R.string.panel_travar_largura_altura)
        Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).testTag("transform.scale.link")
            .tocavel(enabled = !detail.locked) { store.scaleAxesLinked = !store.scaleAxesLinked }
            .semantics { contentDescription = linkLabel; stateDescription = if (locked) "X Y Z" else "X / Y / Z" },
            horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
            Icon(if (locked) Icons.Rounded.Link else Icons.Rounded.LinkOff, contentDescription = null,
                tint = Color.White, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(linkLabel, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
        }
        if (hasFit) MediaFitChips(env)
    }
}

/**
 * ESCALAR [A] (05 / 08): `Largura 🔗 Altura` no topo, "Preencher / Ajustar" em
 * foto e vídeo, e DUAS fitas (Largura com o centro aceso, Altura com o centro
 * branco). Com a corrente travada o arrasto escala PROPORCIONAL (a razão X/Y se
 * mantém — o bug B-31 era escrever o mesmo número nos dois eixos e o campo
 * mentir); solta, cada fita cuida do seu eixo. 0,5 %/dp.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleFace(env: PanelEnv, depth: Boolean, axis: Int, onAxis: (Int) -> Unit) {
    if (depth) {
        ScaleVectorFace(env, axis, onAxis)
        return
    }
    val store = env.store
    val widthLabel = stringResource(R.string.panel_largura)
    val heightLabel = stringResource(R.string.panel_altura)
    val scaleZLabel = stringResource(R.string.edt_scale_z)
    val locked = store.scaleAxesLinked
    val sx by remember(store) { derivedStateOf { (store.detail?.scale?.get(0) ?: 1f) * 100f } }
    val sy by remember(store) { derivedStateOf { (store.detail?.scale?.get(1) ?: 1f) * 100f } }
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val sz by remember(store) { derivedStateOf { (store.detail?.scale?.get(2) ?: 1f) * 100f } }
    val lockNow by rememberUpdatedState(locked)

    fun write(axisY: Boolean, v: Float, fromX: Float, fromY: Float, fromZ: Float) {
        if (lockNow) {
            val from = if (axisY) fromY else fromX
            val k = if (from != 0f) v / from else 1f
            val nx = if (axisY) (if (from != 0f) fromX * k else v) else v
            val ny = if (axisY) v else (if (from != 0f) fromY * k else v)
            // Volume: zoom uniforme sem esticar (regra do motor: Z de conteúdo acompanha X).
            if (depth) store.setScale3(if (from != 0f) com.aurea.aurea.engine.AureaEngine.gestureScale3D(kind, fromX / 100f, fromY / 100f, fromZ / 100f, 3, k)
                else com.aurea.aurea.state.linkedScale(floatArrayOf(fromX, fromY, fromZ), if (axisY) 1 else 0, v).map { it / 100f }.toFloatArray())
            else store.setTransform2(TrackProperty.SCALE_X, nx / 100f, TrackProperty.SCALE_Y, ny / 100f)
        } else {
            store.setTransform(if (axisY) TrackProperty.SCALE_Y else TrackProperty.SCALE_X, v / 100f)
        }
    }

    Row(Modifier.fillMaxWidth().height(44.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
        ValueBox("${numeroPtBr(sx, 1)}%", width = 61.dp, label = stringResource(R.string.panel_largura), onTap = {
            env.openKeypad(KeypadRequest(widthLabel, sx, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(false, it, sx, sy, sz) })
        })
        Box(
            Modifier
                .padding(horizontal = 5.dp)
                .size(width = 34.dp, height = 24.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.ControlButton)
                .tocavel(shrink = 1f) { store.scaleAxesLinked = !locked },
            contentAlignment = Alignment.Center,
        ) {
            Icon(if (locked) Icons.Rounded.Link else Icons.Rounded.LinkOff, contentDescription = if (locked) stringResource(R.string.panel_soltar_largura_altura) else stringResource(R.string.panel_travar_largura_altura), tint = Color.White, modifier = Modifier.size(16.dp))
        }
        ValueBox("${numeroPtBr(sy, 1)}%", width = 61.dp, label = stringResource(R.string.panel_altura), color = Color.White, onTap = {
            env.openKeypad(KeypadRequest(heightLabel, sy, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(true, it, sx, sy, sz) })
        })
    }
    if (depth) {
        Box(Modifier.fillMaxWidth().height(44.dp), contentAlignment = Alignment.Center) {
            ValueBox("${numeroPtBr(sz, 1)}%", width = 80.dp, label = "z", onTap = {
                env.openKeypad(KeypadRequest(scaleZLabel, sz, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                    if (lockNow) store.setScale3(if (sz != 0f) com.aurea.aurea.engine.AureaEngine.gestureScale3D(kind, sx / 100f, sy / 100f, sz / 100f, 3, it / sz)
                        else com.aurea.aurea.state.linkedScale(floatArrayOf(sx, sy, sz), 2, it).map { v -> v / 100f }.toFloatArray())
                    else store.setTransform(TrackProperty.SCALE_Z, it / 100f)
                })
            })
        }
    }
    val sized by remember(store) { derivedStateOf { store.detail?.let { com.aurea.aurea.editor.LayerGeometry.hasSize(it) } == true } }
    if (sized && kind != LayerType.Audio.kind) {
        MediaFitChips(env)
        Spacer(Modifier.height(6.dp))
    }
    val both = { Triple(sx, sy, sz) }
    ScaleTape(active = true, value = { sx }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy, fz -> write(false, v, fx, fy, fz) }
    Spacer(Modifier.height(8.dp))
    ScaleTape(active = false, value = { sy }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy, fz -> write(true, v, fx, fy, fz) }
}

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleTape(
    active: Boolean,
    value: () -> Float,
    both: () -> Triple<Float, Float, Float>,
    onStart: () -> Unit,
    onEnd: () -> Unit,
    onValue: (v: Float, fromX: Float, fromY: Float, fromZ: Float) -> Unit,
) {
    // A foto dos DOIS eixos no início do arrasto: a escala proporcional parte dela
    // (partir do valor que volta do motor a cada passo acumularia o arredondamento).
    var from by remember { mutableStateOf(Triple(0f, 0f, 0f)) }
    val read by rememberUpdatedState(value)
    val snap by rememberUpdatedState(both)
    val send by rememberUpdatedState(onValue)
    val begin by rememberUpdatedState(onStart)
    val end by rememberUpdatedState(onEnd)
    TickRuler(
        value = value,
        unitsPerDp = 0.5f,
        active = active,
        modifier = Modifier
            .weight(1f)
            .fillMaxWidth()
            .testTag(if (active) "transform.scale.x" else "transform.scale.y")
            .valueDrag(
                enabled = true,
                start = { read() },
                unitsPerDp = { 0.5f },
                min = Float.NEGATIVE_INFINITY,
                max = Float.POSITIVE_INFINITY,
                onStart = {
                    from = snap()
                    begin()
                },
                onValue = { v -> send(v, from.first, from.second, from.third) },
                onEnd = { end() },
            ),
    )
}

/**
 * PREENCHER / AJUSTAR (só foto e vídeo): um toque põe a mídia cobrindo a
 * composição inteira ou cabendo inteira nela, no centro — UM passo de desfazer.
 * Aceso quando a escala atual já é aquela.
 */
@Composable
private fun MediaFitChips(env: PanelEnv) {
    val store = env.store
    val fit by remember(store) {
        derivedStateOf {
            val d = store.detail
            val cw = store.project.width.toFloat()
            val ch = store.project.height.toFloat()
            val w = d?.let { com.aurea.aurea.editor.LayerGeometry.width(it) } ?: 0f
            val h = d?.let { com.aurea.aurea.editor.LayerGeometry.height(it) } ?: 0f
            if (d == null || w <= 0f || h <= 0f || cw <= 0f || ch <= 0f) {
                null
            } else {
                val cover = max(cw / w, ch / h)
                val contain = min(cw / w, ch / h)
                val s = d.scale
                Triple(cover, contain, if (abs(abs(s[0]) - cover) < 1e-3f && abs(abs(s[1]) - cover) < 1e-3f) 0 else if (abs(abs(s[0]) - contain) < 1e-3f && abs(abs(s[1]) - contain) < 1e-3f) 1 else -1)
            }
        }
    }
    val f = fit ?: return
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
        // Preencher / Ajustar à tela e, do app antigo, "Tamanho da composição" (a composição vira o quadro da camada).
        listOf(stringResource(R.string.panel_preencher), stringResource(R.string.panel_ajustar), stringResource(R.string.panel_tamanho_composicao)).forEachIndexed { i, label ->
            val on = f.third == i
            Box(
                Modifier
                    .padding(horizontal = 4.dp)
                    .height(30.dp)
                    .clip(RoundedCornerShape(15.dp))
                    .background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.RailModeFill)
                    .tocavel {
                        val id = store.primary ?: return@tocavel
                        if (i == 2) com.aurea.aurea.editor.LayerOps.makeCompositionSize(store, id)
                        else com.aurea.aurea.editor.LayerOps.fitToCanvas(store, listOf(id), fill = i == 0)
                    }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}
