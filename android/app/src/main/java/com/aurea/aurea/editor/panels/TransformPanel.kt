package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.TrackKey
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
import androidx.compose.material.icons.rounded.AspectRatio
import androidx.compose.material.icons.rounded.BlurOn
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
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
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
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * As faces de Transformar, na ordem do trilho direito (a do pedido do dono:
 * Posição, Escala, Rotação, Opacidade, Pivô, Desfoque de movimento). [title] vai
 * para o cabeçalho ("Transformar · Posição"); [props] é o grupo que o losango do
 * trilho crava (vazio = face sem keyframe).
 */
enum class TransformTab(val title: String, val icon: ImageVector, val railLabel: String, val props: IntArray) {
    Mover("Posição", Icons.Rounded.OpenWith, "Posição", intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y)),
    Escalar("Escala", Icons.Rounded.AspectRatio, "Escala", intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y)),
    Girar("Rotação", Icons.AutoMirrored.Rounded.RotateRight, "Rotação", intArrayOf(TrackProperty.ROTATION_Z)),
    Opacidade("Opacidade", Icons.Rounded.Opacity, "Opacidade", intArrayOf(TrackProperty.OPACITY)),
    Pivo("Pivô", Icons.Rounded.FilterCenterFocus, "Pivô (ponto de giro)", intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y)),
    Desfoque("Desfoque de movimento", Icons.Rounded.BlurOn, "Desfoque de movimento", intArrayOf()),
}

/**
 * O PAINEL DE TRANSFORMAÇÃO [A] (`PainelDeTransformacao`): trilho esquerdo
 * (‹ · ◇ · curva · ⋯), miolo com os campos no topo e UMA superfície por face
 * (almofada, dial, fitas, régua) e o trilho direito com as faces.
 */
/** Eixo do dial de Rotação: 0 = X, 1 = Y, 2 = Z (no plano da tela). */
private val rotationAxis = androidx.compose.runtime.mutableIntStateOf(2)
private val RotationProps = intArrayOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z)

/** The rail diamond keys every axis exposed by the current dimension mode. */
internal fun transformKeyProperties(tab: TransformTab, threeD: Boolean): IntArray = when {
    tab == TransformTab.Girar -> RotationProps
    threeD && tab == TransformTab.Mover -> intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y, TrackProperty.POSITION_Z)
    threeD && tab == TransformTab.Escalar -> intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y, TrackProperty.SCALE_Z)
    threeD && tab == TransformTab.Pivo -> intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y, TrackProperty.ANCHOR_Z)
    else -> tab.props
}


/** "Girar em 3D" aberto à mão numa camada 2D (X/Y e profundidade aparecem). */
private val threeDOpen = androidx.compose.runtime.mutableStateOf(false)

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

@Composable
internal fun TransformPanel(env: PanelEnv, tab: TransformTab, onTab: (TransformTab) -> Unit) {
    val store = env.store
    var menu by remember { mutableStateOf(false) }
    val show3D by remember(store) { derivedStateOf { threeDOpen.value || uses3D(store.detail) } }
    val axis = if (show3D) rotationAxis.intValue else 2
    val props = if (tab == TransformTab.Girar) intArrayOf(RotationProps[axis]) else tab.props
    // O losango da Rotação vale para X, Y e Z juntos (um keyframe só).
    val keyProps = transformKeyProperties(tab, show3D)
    androidx.compose.runtime.DisposableEffect(store, tab, show3D) {
        store.timelineFocus = keyProps.map { TrackKey(it) }
        onDispose { store.timelineFocus = null }
    }
    val canKey = props.isNotEmpty()
    val look by remember(store, tab, axis, show3D) {
        derivedStateOf { if (keyProps.isEmpty()) com.aurea.aurea.ui.ds.KeyframeLook.None else transformLook(store.detail, keyProps) }
    }
    val curveKeys by remember(store, tab, axis, show3D) {
        derivedStateOf {
            val candidates = (props.toList() + keyProps.toList()).distinct()
            curveTrack(candidates.map { store.primaryKeys().transformTrack(it) })
        }
    }
    val exprKeys = props.map { TrackKey(it) }
    val exprLook by remember(store, tab, axis, show3D) { derivedStateOf { store.expressionLook(exprKeys) } }
    // Expressão na unidade da tela: escala e opacidade em %, ângulo em °, o resto em px.
    val exprTitle = if (tab == TransformTab.Girar) "Rotação ${"XYZ"[axis]}" else tab.title
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
            onKeyframe = if (canKey) ({ store.toggleTransformKeyframe(keyProps) }) else null,
            curveAnimated = look != com.aurea.aurea.ui.ds.KeyframeLook.None,
            onCurve = if (curveKeys.isNotEmpty()) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        (curveKeys.segmentStart(t) ?: curveKeys.firstOrNull())?.let { key ->
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
            onExpression = if (canKey) ({ store.openExpression(exprTitle, exprKeys, exprScale, exprUnit) }) else null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            when (tab) {
                TransformTab.Mover -> MoveFace(env, pivot = false, depth = show3D)
                TransformTab.Girar -> {
                    if (show3D) {
                        AxisRow(axis) { rotationAxis.intValue = it }
                    } else {
                        Open3DRow { threeDOpen.value = true }
                    }
                    Box(Modifier.weight(1f).fillMaxWidth()) { RotationDial(env, axis) }
                }
                TransformTab.Escalar -> ScaleFace(env, depth = show3D)
                TransformTab.Opacidade -> OpacityFace(env, look)
                TransformTab.Pivo -> MoveFace(env, pivot = true, depth = show3D)
                TransformTab.Desfoque -> MotionBlurFace(env)
            }
            Spacer(Modifier.height(10.dp))
        }
        RightRail(
            modes = TransformTab.entries.map { RailMode(it.icon, it.railLabel) },
            selected = tab.ordinal,
            onSelect = { i -> onTab(TransformTab.entries[i]) },
        )
    }

    if (menu) {
        AureaActionSheet(
            title = stringResource(R.string.panel_transformar),
            actions = buildList {
                add(SheetAction(stringResource(R.string.panel_keyframe_anterior)) { store.pause(); store.stepToKeyframe(-1) })
                add(SheetAction(stringResource(R.string.panel_proximo_keyframe)) { store.pause(); store.stepToKeyframe(1) })
                if (tab != TransformTab.Desfoque) add(SheetAction(stringResource(R.string.panel_voltar_padrao)) { resetTab(env, tab, axis) })
                if (canKey) add(SheetAction(if (exprLook == com.aurea.aurea.engine.ExpressionLook.None) stringResource(R.string.panel_adicionar_expressao) else stringResource(R.string.panel_editar_expressao)) {
                    store.openExpression(exprTitle, exprKeys, exprScale, exprUnit)
                })
                if (!uses3D(store.detail)) {
                    add(SheetAction(if (threeDOpen.value) stringResource(R.string.panel_esconder_x_y_z_3d) else stringResource(R.string.panel_mostrar_x_y_z_3d)) { threeDOpen.value = !threeDOpen.value })
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
        TransformTab.Escalar -> store.setTransform2(TrackProperty.SCALE_X, 1f, TrackProperty.SCALE_Y, 1f)
        TransformTab.Opacidade -> store.setTransform(TrackProperty.OPACITY, 1f)
        TransformTab.Pivo -> store.setTransform2(
            TrackProperty.ANCHOR_X, d.sourceWidth / 2f, TrackProperty.ANCHOR_Y, d.sourceHeight / 2f,
        )
        TransformTab.Desfoque -> Unit
    }
}

/** Camada 2D: a Rotação é só no plano; um toque abre X/Y (inclinar em perspectiva). */
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
 * o motor só borra com os dois ligados); a intensidade é a abertura do obturador
 * do projeto em % (180° = 50 %). Vídeo tem ainda o desfoque pelo movimento de
 * dentro do próprio vídeo.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MotionBlurFace(env: PanelEnv) {
    val store = env.store
    val d = store.detail ?: return
    val on = d.motionBlur && store.compMotionBlur
    val strength = store.shutterAngle / 3.6f
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
            com.aurea.aurea.ui.ds.PropertyRow(
                label = stringResource(R.string.panel_intensidade),
                value = strength,
                unitsPerDp = 0.5f,
                min = 0f,
                max = 200f,
                format = { "${numeroPtBr(it, 0)}%" },
                selected = true,
                onSelect = {},
                onGestureStart = { store.beginGesture("intensidade do desfoque") },
                onValue = { store.changeShutterAngle(it.coerceIn(0f, 200f) * 3.6f) },
                onGestureEnd = { store.endGesture() },
                onTapValue = {
                    env.openKeypad(KeypadRequest("Intensidade do desfoque", strength, "%", 0f, 200f, 0) { store.changeShutterAngle(it * 3.6f) })
                },
            )
            Text(
                stringResource(R.string.panel_intensidade_vale_todas_camadas_desfoque_neste),
                modifier = Modifier.padding(top = 4.dp, start = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
        }
        if (d.kind == LayerType.Video.kind) {
            Spacer(Modifier.height(8.dp))
            BlurToggleRow(stringResource(R.string.panel_desfoque_movimento_video), stringResource(R.string.panel_borra_mexe_dentro_video), d.vectorBlur) { store.setVectorBlur(d.id, it) }
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

/**
 * MOVER (e PIVÔ): almofada relativa — 1 dp de dedo vale `largura/360` px da
 * composição. No Mover, os campos x · y · z moram DENTRO dos cantos (cabeçalho
 * da almofada) e o toque em z troca o arrasto para profundidade; no Pivô eles
 * ficam na fileira de cima com o botão "Centro".
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.MoveFace(env: PanelEnv, pivot: Boolean, depth: Boolean) {
    val store = env.store
    var zPicked by rememberSaveable { mutableStateOf(false) }
    // Sem 3D não há profundidade: o arrasto é sempre X/Y.
    val zMode = zPicked && depth
    var dragging by remember { mutableStateOf(false) }
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
                    KeypadRequest(if (pivot) "Pivô em X" else "Posição X", x, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_X, it + d.sourceWidth / 2f)
                        else store.setTransform(TrackProperty.POSITION_X, it)
                    },
                )
            })
            Spacer(Modifier.width(6.dp))
            ValueBox("${numeroPtBr(y, 0)}px", width = 64.dp, label = "y", onTap = {
                val d = store.detail ?: return@ValueBox
                env.openKeypad(
                    KeypadRequest(if (pivot) "Pivô em Y" else "Posição Y", y, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        if (pivot) store.setTransform(TrackProperty.ANCHOR_Y, it + d.sourceHeight / 2f)
                        else store.setTransform(TrackProperty.POSITION_Y, it)
                    },
                )
            })
            if (pivot && depth) {
                Spacer(Modifier.width(6.dp))
                val az = store.detail?.anchor?.get(2) ?: 0f
                ValueBox("${numeroPtBr(az, 0)}px", width = 64.dp, label = "z", onTap = {
                    env.openKeypad(KeypadRequest("Pivô Z", az, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
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
            } else if (depth) {
                // z: o toque ESCOLHE a profundidade para o arrasto; segurar digita.
                ValueBox(
                    "${numeroPtBr(z, 0)}px",
                    width = 64.dp,
                    label = "z",
                    color = if (zMode) AureaColors.Accent else Color.White,
                    onLongPress = {
                        env.openKeypad(KeypadRequest("Profundidade Z", z, "px", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
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
    }
    val hint = when {
        pivot -> stringResource(R.string.panel_deslize_ponto_giro_botao_centro_devolve)
        zMode -> stringResource(R.string.panel_deslize_ajustar_profundidade_toque_z_voltar)
        depth -> stringResource(R.string.panel_deslize_mover_toque_z_profundidade)
        else -> stringResource(R.string.panel_deslize_mover_camada)
    }
    val zNow by rememberUpdatedState(zMode)
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .pointerInput(store, pivot, depth) {
                var startX = 0f
                var startY = 0f
                var startZ = 0f
                var acc = Offset.Zero
                var gain = 1f
                detectDragGestures(
                    onDragStart = {
                        val d = store.detail
                        if (d != null) {
                            startX = if (pivot) d.anchor[0] else d.position[0]
                            startY = if (pivot) d.anchor[1] else d.position[1]
                            startZ = d.position[2]
                        }
                        acc = Offset.Zero
                        // O GANHO da A.01: 360 dp de dedo atravessam a largura da composição.
                        gain = max(1, store.project.width) / 360f
                        dragging = true
                        store.beginGesture(if (pivot) "mover pivô" else "mover")
                    },
                    onDragEnd = { dragging = false; store.endGesture() },
                    onDragCancel = { dragging = false; store.endGesture() },
                ) { change, delta ->
                    change.consume()
                    acc += Offset(delta.x / density, delta.y / density)
                    if (pivot) {
                        store.setTransform2(TrackProperty.ANCHOR_X, startX + acc.x * gain, TrackProperty.ANCHOR_Y, startY + acc.y * gain)
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
                }
            },
    ) {
        CornerMarks(Modifier.fillMaxSize())
        if (!pivot) {
            Box(Modifier.fillMaxWidth().padding(top = 10.dp, start = 16.dp, end = 16.dp), contentAlignment = Alignment.TopCenter) { fields() }
        }
        Text(
            hint,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .align(Alignment.Center)
                .padding(top = if (pivot) 0.dp else 36.dp, start = 16.dp, end = 16.dp),
            // A instrução some enquanto o dedo arrasta (pela cor: nada é remedido).
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (dragging) Color.Transparent else AureaColors.Muted)),
        )
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
// Girar: o dial
// =============================================================================

/** Raio do botão do dial (alvo grande: o dedo cobre o próprio alvo). */
private const val KNOB_DP = 15f

/** O miolo não tem ângulo: perto do centro um tremor vira dezenas de graus. */
private const val DEAD_ZONE_DP = 22f

/**
 * O DIAL [A] (`DialDeAngulo`): anel `#2E3548`, arco `destaque` do 0 até o botão,
 * botão branco de raio 15 e o ângulo no centro. O gesto ACUMULA o quanto o dedo
 * girou (o ângulo não enrola: 361° é mais que uma volta); o toque seco no anel
 * põe o ângulo tocado na volta em que a camada já está.
 */
/**
 * X · Y · Z: o eixo que o dial gira. X e Y inclinam a camada em perspectiva
 * (com a câmera da cena); Z gira no plano da tela.
 */
@Composable
private fun AxisRow(axis: Int, onAxis: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        listOf("X", "Y", "Z").forEachIndexed { i, label ->
            val on = axis == i
            Box(
                Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { onAxis(i) })
                    .padding(horizontal = 18.dp, vertical = 6.dp),
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700,
                    color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}

@Composable
private fun RotationDial(env: PanelEnv, axis: Int) {
    val store = env.store
    val prop = RotationProps[axis]
    val angle by remember(store, axis) { derivedStateOf { store.detail?.rotation?.get(axis) ?: 0f } }
    val current by rememberUpdatedState(angle)
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .pointerInput(store, axis) {
                awaitEachGesture {
                    val center = Offset(size.width / 2f, size.height / 2f)
                    val dead = DEAD_ZONE_DP * density
                    fun raw(p: Offset) = (atan2(p.y - center.y, p.x - center.x) * 180.0 / Math.PI).toFloat()
                    val down = awaitFirstDown(requireUnconsumed = false)
                    var last = down.position
                    var walked = 0f
                    var prevRaw: Float? = if ((down.position - center).getDistance() >= dead) raw(down.position) else null
                    var total = if (current.isFinite()) current else 0f
                    var began = false
                    while (true) {
                        val ev = awaitPointerEvent()
                        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                        if (!ch.pressed) break
                        val p = ch.position
                        walked += (p - last).getDistance()
                        last = p
                        ch.consume()
                        if ((p - center).getDistance() < dead) {
                            prevRaw = null   // sair do miolo recomeça a conta
                            continue
                        }
                        val r = raw(p)
                        val before = prevRaw
                        prevRaw = r
                        if (before == null) continue
                        var step = r - before
                        if (step > 180f) step -= 360f
                        if (step < -180f) step += 360f
                        if (step == 0f) continue
                        if (!began) {
                            began = true
                            store.beginGesture("girar")
                        }
                        total += step
                        store.setTransform(prop, total)
                    }
                    if (began) {
                        store.endGesture()
                    } else if (walked < 2f * density && (last - center).getDistance() >= dead) {
                        // Toque seco: o ângulo apontado, na volta atual.
                        val turns = floor(current / 360f)
                        store.setTransform(prop, turns * 360f + raw(last))
                    }
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val knob = KNOB_DP.dp.toPx()
            val radius = max(0f, (min(size.width, size.height) - (knob * 2 + 2.dp.toPx())) / 2f)
            if (radius <= 0f) return@Canvas
            val c = Offset(size.width / 2f, size.height / 2f)
            drawCircle(AureaColors.DialTrack, radius, c, style = Stroke(2.dp.toPx()))
            val g = if (angle.isFinite()) angle else 0f
            if (abs(g) > 0.5f) {
                val sweep = if (abs(g) > 360f) 360f else g
                drawArc(
                    color = AureaColors.Accent,
                    startAngle = 0f,
                    sweepAngle = sweep,
                    useCenter = false,
                    topLeft = Offset(c.x - radius, c.y - radius),
                    size = Rect(c, radius).size,
                    style = Stroke(3.5.dp.toPx(), cap = StrokeCap.Round),
                )
            }
            val rad = Math.toRadians(g.toDouble())
            drawCircle(Color.White, knob, Offset(c.x + cos(rad).toFloat() * radius, c.y + sin(rad).toFloat() * radius))
        }
        // O número no centro: casa decimal só quando existe ("45°", "45,5°").
        val tenth = (angle * 10f).roundToInt() / 10f
        val text = "${numeroPtBr(tenth, if (tenth == tenth.roundToInt().toFloat()) 0 else 1)}°"
        Box(
            Modifier
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.DialValueBox)
                .tocavel(shrink = 1f) {
                    env.openKeypad(KeypadRequest("Rotação ${"XYZ"[axis]}", angle, "°", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                        store.setTransform(prop, it)
                    })
                }
                .padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            Text(
                text,
                style = AureaType.Base.merge(TextStyle(fontSize = 24.sp, fontWeight = FontWeight.W700, color = AureaColors.Accent, fontFeatureSettings = "tnum")),
            )
        }
    }
}

// =============================================================================
// Escalar: duas fitas e a corrente
// =============================================================================

/**
 * ESCALAR [A] (05 / 08): `Largura 🔗 Altura` no topo, "Preencher / Ajustar" em
 * foto e vídeo, e DUAS fitas (Largura com o centro aceso, Altura com o centro
 * branco). Com a corrente travada o arrasto escala PROPORCIONAL (a razão X/Y se
 * mantém — o bug B-31 era escrever o mesmo número nos dois eixos e o campo
 * mentir); solta, cada fita cuida do seu eixo. 0,5 %/dp.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleFace(env: PanelEnv, depth: Boolean) {
    val store = env.store
    var locked by rememberSaveable { mutableStateOf(true) }
    val sx by remember(store) { derivedStateOf { (store.detail?.scale?.get(0) ?: 1f) * 100f } }
    val sy by remember(store) { derivedStateOf { (store.detail?.scale?.get(1) ?: 1f) * 100f } }
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val lockNow by rememberUpdatedState(locked)

    fun write(axisY: Boolean, v: Float, fromX: Float, fromY: Float) {
        if (lockNow) {
            val from = if (axisY) fromY else fromX
            val k = if (from != 0f) v / from else 1f
            val nx = if (axisY) (if (from != 0f) fromX * k else v) else v
            val ny = if (axisY) v else (if (from != 0f) fromY * k else v)
            store.setTransform2(TrackProperty.SCALE_X, nx / 100f, TrackProperty.SCALE_Y, ny / 100f)
        } else {
            store.setTransform(if (axisY) TrackProperty.SCALE_Y else TrackProperty.SCALE_X, v / 100f)
        }
    }

    Row(Modifier.fillMaxWidth().height(44.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
        ValueBox("${numeroPtBr(sx, 1)}%", width = 61.dp, label = stringResource(R.string.panel_largura), onTap = {
            env.openKeypad(KeypadRequest("Largura", sx, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(false, it, sx, sy) })
        })
        Box(
            Modifier
                .padding(horizontal = 5.dp)
                .size(width = 34.dp, height = 24.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.ControlButton)
                .tocavel(shrink = 1f) { locked = !locked },
            contentAlignment = Alignment.Center,
        ) {
            Icon(if (locked) Icons.Rounded.Link else Icons.Rounded.LinkOff, contentDescription = if (locked) stringResource(R.string.panel_soltar_largura_altura) else stringResource(R.string.panel_travar_largura_altura), tint = Color.White, modifier = Modifier.size(16.dp))
        }
        ValueBox("${numeroPtBr(sy, 1)}%", width = 61.dp, label = stringResource(R.string.panel_altura), color = Color.White, onTap = {
            env.openKeypad(KeypadRequest("Altura", sy, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) { write(true, it, sx, sy) })
        })
    }
    if (depth) {
        val sz = (store.detail?.scale?.get(2) ?: 1f) * 100f
        Box(Modifier.fillMaxWidth().height(44.dp), contentAlignment = Alignment.Center) {
            ValueBox("${numeroPtBr(sz, 1)}%", width = 80.dp, label = "z", onTap = {
                env.openKeypad(KeypadRequest("Escala Z", sz, "%", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 1) {
                    store.setTransform(TrackProperty.SCALE_Z, it / 100f)
                })
            })
        }
    }
    if (kind == LayerType.Video.kind || kind == LayerType.Image.kind) {
        MediaFitChips(env)
        Spacer(Modifier.height(6.dp))
    }
    val both = { Pair(sx, sy) }
    ScaleTape(active = true, value = { sx }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy -> write(false, v, fx, fy) }
    Spacer(Modifier.height(8.dp))
    ScaleTape(active = false, value = { sy }, both = both, onStart = { store.beginGesture("escala") }, onEnd = { store.endGesture() }) { v, fx, fy -> write(true, v, fx, fy) }
}

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.ScaleTape(
    active: Boolean,
    value: () -> Float,
    both: () -> Pair<Float, Float>,
    onStart: () -> Unit,
    onEnd: () -> Unit,
    onValue: (v: Float, fromX: Float, fromY: Float) -> Unit,
) {
    // A foto dos DOIS eixos no início do arrasto: a escala proporcional parte dela
    // (partir do valor que volta do motor a cada passo acumularia o arredondamento).
    var from by remember { mutableStateOf(Pair(0f, 0f)) }
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
                onValue = { v -> send(v, from.first, from.second) },
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
            if (d == null || d.sourceWidth <= 0 || d.sourceHeight <= 0 || cw <= 0f || ch <= 0f) {
                null
            } else {
                val cover = max(cw / d.sourceWidth, ch / d.sourceHeight)
                val contain = min(cw / d.sourceWidth, ch / d.sourceHeight)
                val s = d.scale
                Triple(cover, contain, if (abs(s[0] - cover) < 1e-3f && abs(s[1] - cover) < 1e-3f) 0 else if (abs(s[0] - contain) < 1e-3f && abs(s[1] - contain) < 1e-3f) 1 else -1)
            }
        }
    }
    val f = fit ?: return
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
        listOf(stringResource(R.string.panel_preencher) to f.first, stringResource(R.string.panel_ajustar) to f.second).forEachIndexed { i, (label, scale) ->
            val on = f.third == i
            Box(
                Modifier
                    .padding(horizontal = 4.dp)
                    .height(30.dp)
                    .clip(RoundedCornerShape(15.dp))
                    .background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.RailModeFill)
                    .tocavel {
                        store.beginGesture(label.lowercase())
                        store.setTransform2(TrackProperty.SCALE_X, scale, TrackProperty.SCALE_Y, scale)
                        store.setTransform2(TrackProperty.POSITION_X, store.project.width / 2f, TrackProperty.POSITION_Y, store.project.height / 2f)
                        store.endGesture()
                    }
                    .padding(horizontal = 14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}
