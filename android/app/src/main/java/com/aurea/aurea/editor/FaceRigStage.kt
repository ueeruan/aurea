package com.aurea.aurea.editor

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin

/**
 * RIG 2D DE ROSTO no palco (aurea.rig.face). Os controles (virar, acenar,
 * olhos, boca...) são parâmetros comuns do cartão, com keyframe. "Ajustar
 * pontos" liga as alças sobre a camada: centro e canto do rosto, de cada olho
 * e da boca, o queixo e o topo do cabelo. Arrastar uma alça move o ponto (o
 * gesto inteiro é UM passo de desfazer). O motor guarda e resolve
 * (Engine::query_face_rig e família) em fração da caixa da camada.
 */
internal object FaceRigStage {
    const val HANDLES = 10
    val TYPE: Int get() = effectTypeId("aurea.rig.face")
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)
    var editing by mutableStateOf(false)
    /** Alça escolhida (face_rig::Handle); −1 = nenhuma. */
    var selected by mutableIntStateOf(-1)

    fun open(layerId: Long, effectId: Int) {
        if (layer != layerId || effect != effectId) { selected = -1; editing = false }
        layer = layerId
        effect = effectId
    }

    fun close(effectId: Int) {
        if (effect != effectId) return
        effect = -1
        editing = false
        selected = -1
    }
}

/** Nomes das alças (ordem de face_rig::Handle) — acessibilidade e dica. */
internal val FaceRigHandleNames = intArrayOf(
    R.string.face_rig_h_face, R.string.face_rig_h_face_size, R.string.face_rig_h_eye_left,
    R.string.face_rig_h_eye_left_size, R.string.face_rig_h_eye_right, R.string.face_rig_h_eye_right_size,
    R.string.face_rig_h_mouth, R.string.face_rig_h_mouth_size, R.string.face_rig_h_neck, R.string.face_rig_h_hair,
)

/** Alcance do toque numa alça. */
private val FaceHandleReach = 32.dp
private val FaceInk = Color(0xFFFFFFFF)
private val EyeInk = Color(0xFFFFD60A)
private val MouthInk = Color(0xFFFF6FA8)
private val NeckInk = Color(0xFF4FC3FF)

/** Modo "Ajustar pontos" ligado na camada escolhida? */
internal fun faceRigActive(store: EditorStore): Boolean {
    if (!FaceRigStage.editing || FaceRigStage.effect < 0 || store.selection.size != 1 || store.primary != FaceRigStage.layer) return false
    val d = store.detail ?: return false
    return d.id == FaceRigStage.layer && !d.locked
}

/**
 * Põe o Rig 2D de rosto na camada escolhida (ou abre o que ela já tem). O motor
 * acha os pontos ao pôr o efeito (mesmo passo de desfazer). Mesmo portão do
 * AutoRig (AureaHyper).
 */
internal fun addFaceRig(store: EditorStore): Boolean {
    store.primary ?: return false
    if (!store.requireHyperSubscription()) return false
    val type = FaceRigStage.TYPE
    if (store.effects.any { it.typeId == type }) store.focusExistingEffect(type) else store.addEffectAndFocus(type)
    return true
}

private fun faceCorners(store: EditorStore): FloatArray? {
    val d = store.detail ?: return null
    val c = FloatArray(8)
    return if (LayerGeometry.corners(d, c)) c else null
}

private fun faceUvToComp(c: FloatArray, u: Float, v: Float, out: FloatArray) {
    out[0] = c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])
    out[1] = c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])
}

private fun faceCompToUv(c: FloatArray, x: Float, y: Float, out: FloatArray): Boolean {
    val ax = c[2] - c[0]; val ay = c[3] - c[1]
    val bx = c[6] - c[0]; val by = c[7] - c[1]
    val det = ax * by - ay * bx
    if (kotlin.math.abs(det) < 1e-6f) return false
    val px = x - c[0]; val py = y - c[1]
    out[0] = (px * by - py * bx) / det
    out[1] = (ax * py - ay * px) / det
    return true
}

private fun handleInk(handle: Int): Color = when (handle) {
    0, 1 -> FaceInk
    2, 3, 4, 5 -> EyeInk
    6, 7 -> MouthInk
    else -> NeckInk
}

internal fun DrawScope.drawFaceRigOverlay(store: EditorStore, m: StageMapper) {
    store.rigRevision; store.playhead   // editar ou andar o cabeçote redesenha
    val c = faceCorners(store) ?: return
    val h = store.faceRigHandles(FaceRigStage.layer, FaceRigStage.effect)
    if (h.size < FaceRigStage.HANDLES * 2) return
    val p = FloatArray(2)
    fun screen(u: Float, v: Float): Offset {
        faceUvToComp(c, u, v, p)
        return Offset(m.sx(p[0]), m.sy(p[1]))
    }
    val under = ShellColors.OutlineUnder
    // Elipses do rosto, dos olhos e da boca (centro + raios, em fração da camada).
    for (part in intArrayOf(0, 2, 4, 6)) {
        val cu = h[part * 2]; val cv = h[part * 2 + 1]
        val ru = h[(part + 1) * 2] - cu; val rv = h[(part + 1) * 2 + 1] - cv
        val path = Path()
        for (i in 0..48) {
            val t = (i / 48f) * 2f * Math.PI.toFloat()
            val o = screen(cu + ru * cos(t), cv + rv * sin(t))
            if (i == 0) path.moveTo(o.x, o.y) else path.lineTo(o.x, o.y)
        }
        drawPath(path, under, style = Stroke(3.dp.toPx()))
        drawPath(path, handleInk(part), style = Stroke(1.5.dp.toPx()))
    }
    for (i in 0 until FaceRigStage.HANDLES) {
        val o = screen(h[i * 2], h[i * 2 + 1])
        val edge = i == 1 || i == 3 || i == 5 || i == 7
        val r = (if (edge) 5 else 7).dp.toPx()
        drawCircle(under, r + 2.dp.toPx(), o)
        if (i == FaceRigStage.selected || !edge) drawCircle(handleInk(i), r, o)
        else drawCircle(handleInk(i), r - 1.dp.toPx(), o, style = Stroke(2.dp.toPx()))
    }
}

/** Alça sob o dedo (a mais perto), ou −1. */
private fun pickHandle(h: FloatArray, c: FloatArray, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val p = FloatArray(2)
    var best = -1
    var bestD = reach
    for (i in 0 until FaceRigStage.HANDLES) {
        faceUvToComp(c, h[i * 2], h[i * 2 + 1], p)
        val d = hypot(x - m.sx(p[0]), y - m.sy(p[1]))
        if (d < bestD) { bestD = d; best = i }
    }
    return best
}

/** Gesto no modo "Ajustar pontos". Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.faceRigGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val layer = FaceRigStage.layer
    val effect = FaceRigStage.effect
    val c = faceCorners(store)
    val h = if (m.valid && c != null) store.faceRigHandles(layer, effect) else FloatArray(0)
    val hit = if (c != null && h.size >= FaceRigStage.HANDLES * 2)
        pickHandle(h, c, m, down.position.x, down.position.y, FaceHandleReach.toPx()) else -1
    val slop = 6.dp.toPx()
    down.consume()
    var moved = false
    var sent = false
    val start = FloatArray(2)
    val uv = FloatArray(2)
    if (hit >= 0 && c != null) faceCompToUv(c, m.cx(down.position.x), m.cy(down.position.y), start)
    while (true) {
        val e = awaitPointerEvent()
        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
        ch.consume()
        if (!ch.pressed) break
        if (!moved && hypot(ch.position.x - down.position.x, ch.position.y - down.position.y) < slop) continue
        moved = true
        if (hit < 0 || c == null || !m.valid) continue
        if (!faceCompToUv(c, m.cx(ch.position.x), m.cy(ch.position.y), uv)) continue
        // A alça anda o quanto o dedo andou (sem pular para debaixo dele).
        store.faceRigMoveHandle(layer, effect, hit, h[hit * 2] + uv[0] - start[0], h[hit * 2 + 1] + uv[1] - start[1], sent)
        sent = true
    }
    FaceRigStage.selected = hit
    if (sent) store.rigGestureEnd()
}

/**
 * Ferramentas do Rig 2D de rosto no cartão do efeito: o estado dos pontos
 * achados, "Ajustar pontos"/"Concluir" e "Detectar rosto de novo". Fechar o
 * cartão sai do modo de pontos.
 */
@Composable
internal fun FaceRigCardTools(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    DisposableEffect(layer, effectId) {
        FaceRigStage.open(layer, effectId)
        onDispose { if (FaceRigStage.layer == layer) FaceRigStage.close(effectId) }
    }
    val editing = FaceRigStage.editing && FaceRigStage.effect == effectId && FaceRigStage.layer == layer
    val revision = store.rigRevision
    val placed = remember(layer, effectId, revision, store.curveRevision) {
        store.faceRigHandles(layer, effectId).getOrNull(FaceRigStage.HANDLES * 2)?.toInt() ?: -1
    }
    val locked = store.detail?.locked == true
    val muted = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted))
    // Rig 2D v2: no "Controle do rig" (o nulo das partes separadas) os controles
    // movem todas as partes; pontos e separar ficam na imagem original.
    if (store.detail?.kind == 6) {
        Text(stringResource(R.string.face_rig_control_hint), style = muted,
            modifier = Modifier.padding(bottom = 6.dp).testTag("fx.faceRig.control"))
        return
    }
    Column(Modifier.padding(bottom = 6.dp)) {
        val status = when (placed) {
            2 -> R.string.face_rig_found
            1 -> R.string.face_rig_found_eyes
            0 -> R.string.face_rig_not_found
            else -> null
        }
        if (status != null) Text(stringResource(status), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
            color = if (placed == 0) AureaColors.Text else AureaColors.Muted)), modifier = Modifier.testTag("fx.faceRig.status"))
        Text(stringResource(R.string.face_rig_hint), style = muted, modifier = Modifier.padding(top = 2.dp))
        val adjust = stringResource(if (editing) R.string.puppet_done else R.string.face_rig_adjust)
        val detect = stringResource(R.string.face_rig_detect)
        Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            com.aurea.aurea.editor.panels.KitChip(adjust, on = editing,
                modifier = Modifier.testTag("fx.faceRig.adjust").semantics { contentDescription = adjust }) {
                if (locked) return@KitChip
                if (FaceRigStage.effect != effectId || FaceRigStage.layer != layer) FaceRigStage.open(layer, effectId)
                FaceRigStage.editing = !editing
                if (!FaceRigStage.editing) FaceRigStage.selected = -1
            }
            com.aurea.aurea.editor.panels.KitChip(detect, on = false,
                modifier = Modifier.testTag("fx.faceRig.detect").semantics { contentDescription = detect }) {
                if (!locked) store.faceRigAutoPlace(layer, effectId)
            }
        }
        // v2: cada parte numa camada (cabelo, rosto, olhos, boca, óculos...), com o
        // que fica atrás preenchido e o nulo "Controle do rig". Portão AureaHyper.
        val split = stringResource(R.string.face_rig_split)
        val splitHint = stringResource(R.string.face_rig_split_hint)
        com.aurea.aurea.editor.panels.KitChip(split, on = false,
            modifier = Modifier.padding(top = 6.dp).testTag("fx.faceRig.split")
                .semantics { contentDescription = "$split. $splitHint" }) {
            if (locked || store.busyMessage != null) return@KitChip
            if (!store.requireHyperSubscription()) return@KitChip
            FaceRigStage.editing = false
            store.faceRigSplitParts(layer, effectId)
        }
        Text(splitHint, style = muted, modifier = Modifier.padding(top = 2.dp))
        if (editing) {
            val chosen = FaceRigStage.selected
            if (chosen in FaceRigHandleNames.indices) Text(
                stringResource(R.string.face_rig_handle_a11y, stringResource(FaceRigHandleNames[chosen])),
                style = muted, modifier = Modifier.padding(top = 4.dp),
            )
        }
    }
}
