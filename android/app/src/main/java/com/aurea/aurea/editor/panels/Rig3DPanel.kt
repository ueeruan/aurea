package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

/** Floats por osso canônico de `Engine::query_humanoid_joints`. */
internal const val HUMANOID_JOINT_FLOATS = 8
/** Floats por membro de `Engine::query_humanoid_ik`. */
internal const val HUMANOID_IK_FLOATS = 7

/** Nome de cada osso canônico, na ordem de `rig::HumanoidBone`. */
internal val HumanoidBoneNames = intArrayOf(
    R.string.rig3d_bone_hips, R.string.rig3d_bone_spine, R.string.rig3d_bone_chest, R.string.rig3d_bone_neck,
    R.string.rig3d_bone_head, R.string.rig3d_bone_left_upper_arm, R.string.rig3d_bone_left_lower_arm,
    R.string.rig3d_bone_left_hand, R.string.rig3d_bone_right_upper_arm, R.string.rig3d_bone_right_lower_arm,
    R.string.rig3d_bone_right_hand, R.string.rig3d_bone_left_upper_leg, R.string.rig3d_bone_left_lower_leg,
    R.string.rig3d_bone_left_foot, R.string.rig3d_bone_right_upper_leg, R.string.rig3d_bone_right_lower_leg,
    R.string.rig3d_bone_right_foot,
)

private val LimbNames = intArrayOf(R.string.rig3d_limb_0, R.string.rig3d_limb_1, R.string.rig3d_limb_2, R.string.rig3d_limb_3)

/**
 * ESQUELETO do modelo 3D importado (0.3.6 BETA). Par do iOS: Panel3DView.swift
 * (`RigTab`). Tudo no motor (rig/AutoRig.hpp): aqui só os comandos.
 *  - AutoRig 3D (sem esqueleto): progresso real e Cancelar.
 *  - Corrigir esqueleto: AutoRig = arrastar as juntas na Cena 3D; rig do
 *    arquivo = escolher, para cada osso canônico, a junta do arquivo.
 *  - Ossos: tocar abre a Cena 3D no modo de pose com o osso escolhido.
 *  - IK de braços e pernas: liga/desliga e keyframe do controlador.
 */
@Composable
internal fun Rig3DTab(env: PanelEnv) {
    val store = env.store
    val status = store.humanoidStatus
    val working = store.autoRigState == 1
    KitTitle(stringResource(R.string.rig3d_tab))
    val statusText = stringResource(when (status) {
        3 -> R.string.rig3d_status_auto
        2 -> R.string.rig3d_status_compatible
        1 -> R.string.rig3d_status_other
        else -> R.string.rig3d_status_none
    })
    Text(statusText, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
        modifier = Modifier.padding(bottom = 6.dp).testTag("rig3d.status"))
    if (working) {
        val pct = (store.autoRigProgress * 100f).toInt().coerceIn(0, 100)
        val label = stringResource(R.string.rig3d_working, pct)
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
        Spacer(Modifier.height(6.dp))
        LinearProgressIndicator(
            progress = { store.autoRigProgress },
            modifier = Modifier.fillMaxWidth().semantics {
                contentDescription = label
                progressBarRangeInfo = ProgressBarRangeInfo(store.autoRigProgress, 0f..1f)
            },
            color = AureaColors.Accent,
            trackColor = AureaColors.Chip,
        )
        Spacer(Modifier.height(6.dp))
        Box(Modifier.testTag("rig3d.cancel")) { KitChip(stringResource(R.string.rig3d_cancel), on = false) { store.cancelAutoRig() } }
        return
    }
    if (status == 0) {
        Box(Modifier.testTag("rig3d.autorig")) {
            ActionCard(stringResource(R.string.rig3d_autorig), stringResource(R.string.rig3d_autorig_desc)) { store.startAutoRig() }
        }
        Spacer(Modifier.height(6.dp))
        KitHint(stringResource(R.string.rig3d_tpose_hint))
        return
    }
    // Rig do arquivo: "Corrigir" abre a lista dos ossos canônicos (tocar = escolher a junta).
    var assignList by rememberSaveable(store.primary) { mutableStateOf(status == 1) }
    var assigning by rememberSaveable(store.primary) { mutableIntStateOf(-1) }
    Box(Modifier.testTag("rig3d.fix")) {
        ActionCard(stringResource(R.string.rig3d_fix), stringResource(R.string.rig3d_fix_desc)) {
            if (status == 3) store.setRigFix(true) else assignList = !assignList
        }
    }
    if (status >= 2) {
        Spacer(Modifier.height(6.dp))
        Box(Modifier.testTag("rig3d.animate")) {
            ActionCard(stringResource(R.string.rig3d_animate), stringResource(R.string.rig3d_animate_desc)) { store.animateBones() }
        }
    }
    val joints = store.humanoidJoints
    if (status != 3 && assignList && joints != null) {
        // Lista dos ossos canônicos: tocar escolhe a junta do arquivo.
        KitTitle(stringResource(R.string.rig3d_bones))
        KitHint(stringResource(R.string.rig3d_fix_assign_hint))
        ChipRow {
            HumanoidBoneNames.forEachIndexed { b, res ->
                val node = joints.getOrNull(b * HUMANOID_JOINT_FLOATS)?.toInt() ?: -1
                KitChip(stringResource(res), on = node >= 0) { assigning = b }
            }
        }
    } else if (status >= 2 && joints != null) {
        KitTitle(stringResource(R.string.rig3d_bones))
        ChipRow {
            HumanoidBoneNames.forEachIndexed { b, res ->
                val node = joints.getOrNull(b * HUMANOID_JOINT_FLOATS)?.toInt() ?: -1
                if (node >= 0) Box(Modifier.testTag("rig3d.bone.$b")) {
                    KitChip(stringResource(res), on = store.rigPoseMode && store.poseBone == node) { store.animateBones(node) }
                }
            }
        }
    }
    val ik = store.humanoidIk
    if (status >= 2 && ik != null) {
        KitTitle(stringResource(R.string.rig3d_ik))
        ChipRow {
            for (limb in 0 until 4) {
                val on = ik.getOrNull(limb * HUMANOID_IK_FLOATS) ?: -1f
                if (on < 0f) continue
                Box(Modifier.testTag("rig3d.ik.$limb")) {
                    KitChip(stringResource(LimbNames[limb]), on = on > .5f) { store.setHumanoidIk(limb, on <= .5f) }
                }
            }
        }
        val anyOn = (0 until 4).any { (ik.getOrNull(it * HUMANOID_IK_FLOATS) ?: 0f) > .5f }
        if (anyOn) {
            KitHint(stringResource(R.string.rig3d_ik_hint))
            ChipRow {
                for (limb in 0 until 4) {
                    if ((ik.getOrNull(limb * HUMANOID_IK_FLOATS) ?: 0f) <= .5f) continue
                    val keyed = (ik.getOrNull(limb * HUMANOID_IK_FLOATS + 5) ?: 0f) > .5f
                    KitChip("◆ " + stringResource(LimbNames[limb]), on = keyed) { store.toggleHumanoidIkKey(limb) }
                }
            }
        }
    }
    if (assigning >= 0) AssignBoneDialog(env, assigning) { assigning = -1 }
    // Motion Capture (BETA) logo abaixo, no mesmo painel (MocapSection.kt).
    // Também com esqueleto do arquivo não humanoide (status 1): a seção pede o mapeamento em "Corrigir esqueleto".
    if (status >= 1) MocapSection(env)
}

/** Escolher a junta do arquivo para o osso canônico [bone] (rig importado). */
@Composable
private fun AssignBoneDialog(env: PanelEnv, bone: Int, onDismiss: () -> Unit) {
    val store = env.store
    val bones = store.poseBones ?: floatArrayOf()
    val nodes = remember(bones) { (0 until bones.size / com.aurea.aurea.editor.BONE_FLOATS).map { bones[it * com.aurea.aurea.editor.BONE_FLOATS].toInt() } }
    val current = store.humanoidJoints?.getOrNull(bone * HUMANOID_JOINT_FLOATS)?.toInt() ?: -1
    val title = stringResource(R.string.rig3d_choose_node, stringResource(HumanoidBoneNames[bone.coerceIn(0, 16)]))
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            LazyColumn(Modifier.heightIn(max = 360.dp)) {
                item {
                    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                        KitChip(stringResource(R.string.rig3d_none_node), on = current < 0) { store.assignHumanoidBone(bone, -1); onDismiss() }
                    }
                }
                items(nodes) { node ->
                    val name = remember(node) { store.modelNodeName(node) }
                    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                        KitChip(name.ifBlank { "#$node" }, on = node == current) { store.assignHumanoidBone(bone, node); onDismiss() }
                        Spacer(Modifier.width(4.dp))
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.rig3d_cancel)) } },
    )
}
