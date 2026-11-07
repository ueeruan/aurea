package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
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

/**
 * A viewport da Cena 3D, com as cores do AUREA: as ferramentas à esquerda. Câmera,
 * enquadrar e as vistas ficam no menu da viewport. Par do iOS: EditorView.swift
 * (`Scene3DToolbar`).
 */

private val ChromeFill = Color(0xE6151C24)
private val ToolSize = 34.dp

/** Barra da esquerda: as ferramentas e os dois modos. Sem altura para todas, abre outra coluna. */
@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
internal fun SceneToolbar(store: EditorStore, modifier: Modifier) {
    val pose = rigPoseActive(store)
    val selectLabel = stringResource(R.string.s3d_menu_select)
    androidx.compose.foundation.layout.FlowColumn(modifier.padding(start = 6.dp, bottom = 6.dp).clip(RoundedCornerShape(10.dp)).background(ChromeFill).padding(3.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        if (pose) {
            val tool = when {
                store.sceneHand -> com.aurea.aurea.state.POSE_TOOL_HAND
                store.sceneSelectOnly -> POSE_TOOL_SELECT
                store.poseIk -> POSE_TOOL_IK
                store.poseMove -> POSE_TOOL_MOVE
                else -> POSE_TOOL_ROTATE
            }
            ToolSelect(selectLabel, tool == POSE_TOOL_SELECT) { store.choosePoseTool(POSE_TOOL_SELECT) }
            ToolButton(CupertinoGlyph.HandRaised, stringResource(R.string.s3d_tool_hand), tool == com.aurea.aurea.state.POSE_TOOL_HAND, "scene.tool.hand") {
                store.choosePoseTool(com.aurea.aurea.state.POSE_TOOL_HAND)
            }
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
            val tool = if (store.sceneHand) com.aurea.aurea.state.SCENE_TOOL_HAND else if (store.sceneSelectOnly) SCENE_TOOL_SELECT else store.gizmoTool
            ToolSelect(selectLabel, tool == SCENE_TOOL_SELECT) { store.chooseSceneTool(SCENE_TOOL_SELECT) }
            ToolButton(CupertinoGlyph.HandRaised, stringResource(R.string.s3d_tool_hand), tool == com.aurea.aurea.state.SCENE_TOOL_HAND, "scene.tool.hand") {
                store.chooseSceneTool(com.aurea.aurea.state.SCENE_TOOL_HAND)
            }
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
        // Objeto e pose: os dois modos da cena, como as duas últimas ferramentas.
        val canPose = store.poseBones != null && store.layers.firstOrNull { it.id == store.primary }?.kind == com.aurea.aurea.ui.theme.LayerType.Model3D.kind
        ToolButton(CupertinoGlyph.Cube, stringResource(R.string.s3d_mode_object), false, "scene.mode.object", tint = if (pose) AureaColors.Muted else AureaColors.Text) {
            if (store.rigPoseMode) store.setRigPose(false)
        }
        ToolButton(CupertinoGlyph.PersonCropCircle, stringResource(R.string.s3d_mode_pose), false, "scene.mode.pose",
            tint = if (pose) AureaColors.Text else AureaColors.Muted) { if (canPose && !store.rigPoseMode) store.setRigPose(true) }
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

/** Vista inicial e sem zoom/pan do palco. */
internal fun sceneFrameAll(store: EditorStore) {
    StageView.reset(store)
    store.resetSceneView()
}
