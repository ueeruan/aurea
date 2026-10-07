package com.aurea.aurea.editor

import com.aurea.aurea.editor.panels.EditorPanel

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.hypot

/**
 * FANTOCHE no palco (aurea.distort.puppet, o Puppet Pin do AE). "Editar pinos"
 * no cartão do efeito liga o modo: a malha deformada (clara) e os pinos
 * (pontos amarelos; o escolhido cheio) aparecem sobre a camada. Tocar na
 * camada = pino novo; arrastar um pino = move ao vivo (auto-key do palco no
 * cabeçote; o arrasto inteiro é UM passo de desfazer); segurar = apaga;
 * "Concluir" sai. Mesmo padrão da Malha de deformação (MeshWarpStage.kt): o
 * motor guarda e resolve (Engine::query_puppet e família) em fração da caixa
 * da camada; aqui só dedo e desenho, pelos cantos da camada.
 */
internal object PuppetStage {
    const val PIN_FLOATS = 4
    val TYPE: Int get() = effectTypeId("aurea.distort.puppet")
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)
    var editing by mutableStateOf(false)
    /** Pino escolhido (índice do motor); −1 = nenhum. */
    var selected by mutableIntStateOf(-1)
    /** A doca (o antigo Rig) pediu o modo de pinos nesta camada assim que o cartão abrir. */
    var pendingEdit by mutableLongStateOf(0L)

    fun open(layerId: Long, effectId: Int) {
        if (layer != layerId || effect != effectId) { selected = -1; editing = false }
        layer = layerId
        effect = effectId
        if (pendingEdit == layerId) { editing = true; pendingEdit = 0L }
    }

    fun close(effectId: Int) {
        if (effect != effectId) return
        effect = -1
        editing = false
        selected = -1
    }
}

/** Modo de pinos ligado na camada escolhida? */
internal fun puppetActive(store: EditorStore): Boolean {
    if (!PuppetStage.editing || PuppetStage.effect < 0 || store.selection.size != 1 || store.primary != PuppetStage.layer) return false
    val d = store.detail ?: return false
    return d.id == PuppetStage.layer && !d.locked
}

/**
 * A ferramenta Fantoche (a entrada "Rig" da doca): abre o cartão do efeito na
 * camada escolhida — cria o efeito se ainda não tem — já no modo de pinos.
 */
internal fun openPuppetTool(store: EditorStore, ui: EditorUi) {
    val layer = store.primary ?: return
    val type = PuppetStage.TYPE
    PuppetStage.pendingEdit = layer
    if (store.effects.any { it.typeId == type }) store.focusExistingEffect(type) else store.addEffectAndFocus(type)
    openPanel(store, ui, EditorPanel.Effects)
}

private fun puppetCorners(store: EditorStore): FloatArray? {
    val d = store.detail ?: return null
    val c = FloatArray(8)
    return if (LayerGeometry.corners(d, c)) c else null
}

private fun uvToComp(c: FloatArray, u: Float, v: Float, out: FloatArray) {
    out[0] = c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])
    out[1] = c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])
}

private fun compToUv(c: FloatArray, x: Float, y: Float, out: FloatArray): Boolean {
    val ax = c[2] - c[0]; val ay = c[3] - c[1]
    val bx = c[6] - c[0]; val by = c[7] - c[1]
    val det = ax * by - ay * bx
    if (kotlin.math.abs(det) < 1e-6f) return false
    val px = x - c[0]; val py = y - c[1]
    out[0] = (px * by - py * bx) / det
    out[1] = (ax * py - ay * px) / det
    return true
}

private val PinYellow = Color(0xFFFFD60A)
private val MeshInk = Color(0x8CFFFFFF)

internal fun DrawScope.drawPuppetOverlay(store: EditorStore, m: StageMapper) {
    store.rigRevision; store.playhead   // editar ou andar o cabeçote redesenha
    val c = puppetCorners(store) ?: return
    val p = FloatArray(2)
    fun screen(u: Float, v: Float): Offset {
        uvToComp(c, u, v, p)
        return Offset(m.sx(p[0]), m.sy(p[1]))
    }
    // Malha deformada: triângulos em traço fino e claro.
    val mesh = store.puppetMesh(PuppetStage.layer, PuppetStage.effect)
    if (mesh.size >= 6) {
        val path = Path()
        var i = 0
        while (i + 5 < mesh.size) {
            val a = screen(mesh[i], mesh[i + 1]); val b = screen(mesh[i + 2], mesh[i + 3]); val d = screen(mesh[i + 4], mesh[i + 5])
            path.moveTo(a.x, a.y); path.lineTo(b.x, b.y); path.lineTo(d.x, d.y); path.close()
            i += 6
        }
        drawPath(path, MeshInk, style = Stroke(1.dp.toPx()))
    }
    val under = ShellColors.OutlineUnder
    val pins = store.puppetPins(PuppetStage.layer, PuppetStage.effect)
    var k = 0
    while (k + PuppetStage.PIN_FLOATS <= pins.size) {
        val index = pins[k].toInt()
        val o = screen(pins[k + 1], pins[k + 2])
        val chosen = index == PuppetStage.selected
        val r = 7.dp.toPx()
        drawCircle(under, r + 2.dp.toPx(), o)
        if (chosen) drawCircle(PinYellow, r, o)
        else drawCircle(PinYellow, r - 1.dp.toPx(), o, style = Stroke(2.5.dp.toPx()))
        // Key do pino no cabeçote: anel externo.
        if (pins[k + 3] > 0.5f) drawCircle(PinYellow, r + 5.dp.toPx(), o, style = Stroke(1.5.dp.toPx()))
        k += PuppetStage.PIN_FLOATS
    }
}

/** Pino sob o dedo (índice do motor) ou −1. */
private fun pickPin(pins: FloatArray, c: FloatArray, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val p = FloatArray(2)
    var best = reach
    var hit = -1
    var k = 0
    while (k + PuppetStage.PIN_FLOATS <= pins.size) {
        uvToComp(c, pins[k + 1], pins[k + 2], p)
        val d = hypot(x - m.sx(p[0]), y - m.sy(p[1]))
        if (d < best) { best = d; hit = pins[k].toInt() }
        k += PuppetStage.PIN_FLOATS
    }
    return hit
}

/** Gesto no modo de pinos. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.puppetGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val layer = PuppetStage.layer
    val effect = PuppetStage.effect
    val c = puppetCorners(store)
    val hit = if (m.valid && c != null) pickPin(store.puppetPins(layer, effect), c, m, down.position.x, down.position.y, 24.dp.toPx()) else -1
    val slop = 6.dp.toPx()
    down.consume()
    var moved = false
    var sent = false
    val uv = FloatArray(2)
    while (true) {
        // Segurar parado sobre um pino (0,5 s) apaga.
        val e = if (!moved && hit >= 0) withTimeoutOrNull(500L) { awaitPointerEvent() } else awaitPointerEvent()
        if (e == null) {
            store.puppetRemovePin(layer, effect, hit)
            if (PuppetStage.selected == hit) PuppetStage.selected = -1
            while (true) {   // o resto do toque não faz mais nada
                val rest = awaitPointerEvent()
                val ch = rest.changes.firstOrNull { it.id == down.id } ?: return
                ch.consume()
                if (!ch.pressed) return
            }
        }
        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
        ch.consume()
        if (!ch.pressed) break
        if (!moved && hypot(ch.position.x - down.position.x, ch.position.y - down.position.y) < slop) continue
        moved = true
        if (hit < 0 || c == null || !m.valid) continue
        if (!compToUv(c, m.cx(ch.position.x), m.cy(ch.position.y), uv)) continue
        store.puppetMovePin(layer, effect, hit, uv[0], uv[1], sent)
        sent = true
    }
    if (sent) {
        PuppetStage.selected = hit
        store.rigGestureEnd()
        return
    }
    if (moved) return
    if (hit >= 0) { PuppetStage.selected = hit; return }
    // Toque fora de pino, em cima da camada: pino novo.
    if (c == null || !m.valid || !compToUv(c, m.cx(down.position.x), m.cy(down.position.y), uv)) return
    if (uv[0] < -0.02f || uv[0] > 1.02f || uv[1] < -0.02f || uv[1] > 1.02f) { PuppetStage.selected = -1; return }
    val pin = store.puppetAddPin(layer, effect, uv[0], uv[1])
    if (pin >= 0) PuppetStage.selected = pin
}

/**
 * Ferramentas do Fantoche no cartão do efeito: "Editar pinos" liga o modo de
 * pinos no palco; "Concluir" sai. Fechar o cartão também sai.
 */
@Composable
internal fun PuppetCardTools(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    DisposableEffect(layer, effectId) {
        PuppetStage.open(layer, effectId)
        onDispose { PuppetStage.close(effectId) }
    }
    LaunchedEffect(PuppetStage.pendingEdit) { if (PuppetStage.pendingEdit == layer) PuppetStage.open(layer, effectId) }
    val editing = PuppetStage.editing && PuppetStage.effect == effectId && PuppetStage.layer == layer
    val label = stringResource(if (editing) R.string.puppet_done else R.string.puppet_edit_pins)
    Column(Modifier.padding(bottom = 6.dp)) {
        Text(
            stringResource(R.string.puppet_hint),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted)),
        )
        Box(
            Modifier
                .padding(top = 6.dp)
                .heightIn(min = 36.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(if (editing) AureaColors.Accent else AureaColors.Chip)
                .testTag("fx.puppet.edit")
                .semantics { role = Role.Button; contentDescription = label }
                .tocavel(haptic = true, onClick = {
                    if (PuppetStage.effect != effectId || PuppetStage.layer != layer) PuppetStage.open(layer, effectId)
                    PuppetStage.editing = !editing
                    if (!PuppetStage.editing) PuppetStage.selected = -1
                })
                .padding(horizontal = 12.dp, vertical = 8.dp),
        ) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600,
                color = if (editing) Color.Black else AureaColors.Text)))
        }
    }
}
