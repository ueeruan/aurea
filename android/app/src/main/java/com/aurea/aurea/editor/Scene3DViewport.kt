package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GIZMO_MOVE
import com.aurea.aurea.state.GIZMO_ROTATE
import com.aurea.aurea.state.GIZMO_SCALE
import com.aurea.aurea.state.POSE_TOOL_IK
import com.aurea.aurea.state.POSE_TOOL_MOVE
import com.aurea.aurea.state.POSE_TOOL_ROTATE
import com.aurea.aurea.state.POSE_TOOL_SELECT
import com.aurea.aurea.state.SCENE_TOOL_SELECT
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.sin

/**
 * A viewport da Cena 3D no jeito do Blender no celular, com as cores do AUREA:
 * barra de ferramentas à esquerda, gizmo de eixos e navegação à direita e o
 * nome do que está escolhido no canto. Par do iOS: EditorView.swift
 * (`Scene3DToolbar`, `Scene3DNavColumn`, `Scene3DNavGizmo`).
 */

private val ChromeFill = Color(0xE6151C24)
private val ToolSize = 46.dp

/** Barra da esquerda: as ferramentas do modo (objeto ou pose) e o keyframe. */
@Composable
internal fun SceneToolbar(store: EditorStore, modifier: Modifier) {
    val pose = rigPoseActive(store)
    val selectLabel = stringResource(R.string.s3d_menu_select)
    Column(modifier.padding(start = 8.dp, top = 8.dp).clip(RoundedCornerShape(12.dp)).background(ChromeFill).padding(3.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp)) {
        if (pose) {
            val tool = when {
                store.sceneSelectOnly -> POSE_TOOL_SELECT
                store.poseIk -> POSE_TOOL_IK
                store.poseMove -> POSE_TOOL_MOVE
                else -> POSE_TOOL_ROTATE
            }
            ToolSelect(selectLabel, tool == POSE_TOOL_SELECT) { store.choosePoseTool(POSE_TOOL_SELECT) }
            ToolButton(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.gizmo_tool_rotate), tool == POSE_TOOL_ROTATE, "scene.tool.rotate") {
                store.choosePoseTool(POSE_TOOL_ROTATE)
            }
            ToolButton(CupertinoGlyph.Move, stringResource(R.string.rig3d_move), tool == POSE_TOOL_MOVE, "scene.tool.move") {
                store.choosePoseTool(POSE_TOOL_MOVE)
            }
            ToolButton(CupertinoGlyph.Link, stringResource(R.string.skeleton_ik), tool == POSE_TOOL_IK, "scene.tool.ik") {
                store.choosePoseTool(POSE_TOOL_IK)
            }
        } else {
            val tool = if (store.sceneSelectOnly) SCENE_TOOL_SELECT else store.gizmoTool
            ToolSelect(selectLabel, tool == SCENE_TOOL_SELECT) { store.chooseSceneTool(SCENE_TOOL_SELECT) }
            ToolButton(CupertinoGlyph.Move, stringResource(R.string.gizmo_tool_move), tool == GIZMO_MOVE, "scene.tool.move") {
                store.chooseSceneTool(GIZMO_MOVE)
            }
            ToolButton(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.gizmo_tool_rotate), tool == GIZMO_ROTATE, "scene.tool.rotate") {
                store.chooseSceneTool(GIZMO_ROTATE)
            }
            ToolButton(CupertinoGlyph.ArrowDownRightSquare, stringResource(R.string.gizmo_tool_scale), tool == GIZMO_SCALE, "scene.tool.scale") {
                store.chooseSceneTool(GIZMO_SCALE)
            }
        }
        Box(Modifier.width(ToolSize).height(1.dp).padding(horizontal = 6.dp).background(AureaColors.EditorBarLine))
        val layer = store.primary
        val keyed = if (pose) {
            val b = store.poseBones
            val slot = b?.let { (0 until it.size / BONE_FLOATS).firstOrNull { i -> it[i * BONE_FLOATS].toInt() == store.poseBone } }
            b != null && slot != null && b[slot * BONE_FLOATS + 6] > .5f
        } else store.detail?.takeIf { it.id == layer }?.let { d -> (0..8).any { d.hasKeyAtPlayhead(it) } } == true
        ToolButton(if (keyed) CupertinoGlyph.SuitDiamondFill else CupertinoGlyph.SuitDiamond, stringResource(R.string.s3d_insert_key_desc), false, "scene.tool.key",
            tint = if (keyed) AureaColors.Keyframe else AureaColors.Text) {
            if (pose) store.togglePoseKey(store.poseBone < 0)
            else if (layer != null) store.toggleTransformKeyframe(IntArray(9) { it }, layer)
        }
    }
}

@Composable
private fun ToolButton(glyph: Char, label: String, on: Boolean, tag: String, tint: Color = AureaColors.Text, onClick: () -> Unit) {
    Box(Modifier.size(ToolSize).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.Brand else Color.Transparent)
        .semantics { contentDescription = label; role = Role.RadioButton; selected = on }.testTag(tag)
        .tocavel(haptic = true, shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 21.dp, if (on) AureaColors.OnAction else tint)
    }
}

/** Selecionar: um quadrado tracejado (o conjunto de ícones do app não tem cursor). */
@Composable
private fun ToolSelect(label: String, on: Boolean, onClick: () -> Unit) {
    val ink = if (on) AureaColors.OnAction else AureaColors.Text
    Box(Modifier.size(ToolSize).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.Brand else Color.Transparent)
        .semantics { contentDescription = label; role = Role.RadioButton; selected = on }.testTag("scene.tool.select")
        .tocavel(haptic = true, shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(20.dp)) {
            val inset = 1.dp.toPx()
            drawRect(ink, Offset(inset, inset), Size(size.width - inset * 2, size.height - inset * 2),
                style = Stroke(1.6.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(3.dp.toPx(), 2.5.dp.toPx()))))
        }
    }
}

/** Direita: o gizmo de eixos e, embaixo, aproximar, mover a vista, câmera e enquadrar. */
@Composable
internal fun SceneNavColumn(store: EditorStore, modifier: Modifier) {
    Column(modifier.padding(end = 6.dp, top = 6.dp), horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        SceneNavGizmo(store)
        Column(Modifier.padding(end = 4.dp).clip(RoundedCornerShape(12.dp)).background(ChromeFill).padding(3.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            NavDrag(CupertinoGlyph.Search, stringResource(R.string.s3d_nav_zoom), "scene.nav.zoom") { _, dy ->
                store.updateSceneView(store.sceneYaw, store.scenePitch, store.sceneDistance * exp(dy / 260.dp.toPx()))
            }
            NavDrag(CupertinoGlyph.ArrowLeftRightSquare, stringResource(R.string.s3d_nav_pan), "scene.nav.pan") { dx, dy ->
                StageView.set(store, StageView.zoom, StageView.panX + dx, StageView.panY + dy)
            }
            ToolButton(if (store.sceneCameraView) CupertinoGlyph.VideocamFill else CupertinoGlyph.Videocam, stringResource(R.string.s3d_view_camera),
                store.sceneCameraView, "scene.nav.camera") { store.toggleSceneCameraView() }
            ToolButton(CupertinoGlyph.Fullscreen, stringResource(R.string.s3d_view_frame), false, "scene.nav.frame") { sceneFrameAll(store) }
        }
    }
}

/** Vista inicial e sem zoom/pan do palco. */
internal fun sceneFrameAll(store: EditorStore) {
    StageView.reset(store)
    store.resetSceneView()
}

/** Botão que se usa arrastando (zoom e pan), como os do canto do Blender. */
@Composable
private fun NavDrag(glyph: Char, label: String, tag: String, onDrag: androidx.compose.ui.unit.Density.(Float, Float) -> Unit) {
    Box(Modifier.size(ToolSize).clip(RoundedCornerShape(9.dp)).semantics { contentDescription = label }.testTag(tag)
        .pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown()
                down.consume()
                var last = down.position
                while (true) {
                    val e = awaitPointerEvent()
                    val c = e.changes.firstOrNull { it.id == down.id } ?: break
                    if (!c.pressed) break
                    c.consume()
                    onDrag(c.position.x - last.x, c.position.y - last.y)
                    last = c.position
                }
            }
        }, contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 21.dp, AureaColors.Text)
    }
}

/** Um eixo do gizmo: para onde aponta na tela, a profundidade e a vista que o toque abre. */
private class NavAxis(val x: Float, val y: Float, val depth: Float, val color: Color, val label: String?, val yaw: Float?, val pitch: Float)

/**
 * Projeta os eixos do mundo com a mesma conta do `scene_editor_camera` do motor:
 * direita = (cos yaw, 0, sen yaw); baixo = (sen p·sen yaw, cos p, −sen p·cos yaw);
 * frente = (−sen yaw·cos p, sen p, cos yaw·cos p). +Y do mundo aponta para baixo.
 */
private fun navAxes(yawDeg: Float, pitchDeg: Float): List<NavAxis> {
    val yaw = Math.toRadians(yawDeg.toDouble()).toFloat()
    val pitch = Math.toRadians(pitchDeg.toDouble()).toFloat()
    val sy = sin(yaw); val cy = cos(yaw); val sp = sin(pitch); val cp = cos(pitch)
    val dim = { c: Color -> c.copy(alpha = .45f) }
    return listOf(
        NavAxis(cy, sp * sy, -sy * cp, PoseAxisColors[0], "X", 90f, 0f),
        NavAxis(-cy, -sp * sy, sy * cp, dim(PoseAxisColors[0]), null, -90f, 0f),
        NavAxis(0f, cp, sp, PoseAxisColors[1], "Y", null, -80f),
        NavAxis(0f, -cp, -sp, dim(PoseAxisColors[1]), null, null, 80f),
        NavAxis(sy, -sp * cy, cy * cp, PoseAxisColors[2], "Z", 180f, 0f),
        NavAxis(-sy, sp * cy, -cy * cp, dim(PoseAxisColors[2]), null, 0f, 0f),
    )
}

/** O gizmo de eixos: arrastar orbita; tocar numa bola vira a vista para aquele lado. */
@Composable
private fun SceneNavGizmo(store: EditorStore) {
    val measurer = rememberTextMeasurer()
    val label = stringResource(R.string.s3d_nav_orbit)
    Canvas(Modifier.size(92.dp).semantics { contentDescription = label }.testTag("scene.nav.gizmo").pointerInput(Unit) {
        val degPerPx = 0.7f / 1.dp.toPx()
        awaitEachGesture {
            val down = awaitFirstDown()
            down.consume()
            var last = down.position
            var moved = false
            while (true) {
                val e = awaitPointerEvent()
                val c = e.changes.firstOrNull { it.id == down.id } ?: break
                if (!c.pressed) break
                c.consume()
                if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < viewConfiguration.touchSlop) continue
                moved = true
                store.updateSceneView(store.sceneYaw - (c.position.x - last.x) * degPerPx, store.scenePitch + (c.position.y - last.y) * degPerPx, store.sceneDistance)
                last = c.position
            }
            if (!moved) {
                val r = size.width * .36f
                val cx = size.width / 2f
                val cy = size.height / 2f
                navAxes(store.sceneYaw, store.scenePitch).sortedBy { it.depth }
                    .firstOrNull { hypot(down.position.x - (cx + it.x * r), down.position.y - (cy + it.y * r)) < 18.dp.toPx() }
                    ?.let { store.snapSceneView(it.yaw ?: store.sceneYaw, it.pitch) }
            }
        }
    }) {
        val c = Offset(size.width / 2f, size.height / 2f)
        val r = size.width * .36f
        val ball = 10.dp.toPx()
        drawCircle(Color(0x55151C24), size.width / 2f, c)
        val axes = navAxes(store.sceneYaw, store.scenePitch)
        // De trás para a frente: maior profundidade = mais longe do olho.
        axes.sortedByDescending { it.depth }.forEach { a ->
            val p = Offset(c.x + a.x * r, c.y + a.y * r)
            if (a.label != null) drawLine(a.color, c, p, 2.dp.toPx())
            drawCircle(a.color, if (a.label != null) ball else ball * .8f, p)
            a.label?.let {
                val t = measurer.measure(it, TextStyle(color = Color(0xFF0F141A), fontSize = 11.sp, fontWeight = FontWeight.Bold))
                drawText(t, topLeft = Offset(p.x - t.size.width / 2f, p.y - t.size.height / 2f))
            }
        }
    }
}

/** O texto do canto, como no Blender: a vista, o quadro e o que está escolhido. */
@Composable
internal fun SceneViewLabel(store: EditorStore, selectedName: String?, modifier: Modifier) {
    val view = stringResource(if (store.sceneCameraView) R.string.s3d_view_camera else R.string.s3d_view_perspective)
    val bone = store.poseBone.takeIf { it >= 0 && rigPoseActive(store) }?.let { store.modelNodeName(it) }
    val detail = listOfNotNull(selectedName, bone?.ifBlank { null }).joinToString(" · ")
    Column(modifier.padding(start = 66.dp, top = 10.dp, end = 104.dp)) {
        Text(view, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text("(${store.playhead})" + if (detail.isEmpty()) "" else " $detail", color = AureaColors.Muted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
