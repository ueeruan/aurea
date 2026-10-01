package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.CurveRailIcon
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

@Composable
internal fun ClipEditPanel(env: PanelEnv) {
    val store = env.store
    val row = store.layers.firstOrNull { it.id == store.primary } ?: return
    var mode by remember(row.id) { mutableStateOf(2) }
    var step by remember(row.id) { mutableStateOf("1") }
    var previous by remember(row.id) { mutableStateOf(0L) }
    var next by remember(row.id) { mutableStateOf(0L) }
    val before = store.layers.filter { it.id != row.id && !it.locked && it.endFrame == row.startFrame }
    val after = store.layers.filter { it.id != row.id && !it.locked && it.startFrame == row.endFrame }
    val left = before.firstOrNull { it.id == previous }?.id ?: before.singleOrNull()?.id ?: 0L
    val right = after.firstOrNull { it.id == next }?.id ?: after.singleOrNull()?.id ?: 0L
    val needsLeft = mode == 3 || mode == 5
    val needsRight = mode == 4 || mode == 5
    val frames = step.toIntOrNull()?.takeIf { it in 1..3600 }
    val enabled = !row.locked && frames != null && (!needsLeft || left != 0L) && (!needsRight || right != 0L)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(row.name, color = AureaColors.Text, fontSize = 15.sp)
        Text(stringResource(R.string.edt_clip_range, row.startFrame, row.endFrame, row.durationFrames), color = AureaColors.Muted, fontSize = 12.sp)
        listOf(2 to "Slip", 3 to stringResource(R.string.edt_clip_roll_start), 4 to stringResource(R.string.edt_clip_roll_end), 5 to "Slide").chunked(2).forEach { choices ->
            Row(Modifier.fillMaxWidth()) {
                choices.forEach { (value, name) ->
                    TextButton(onClick = { mode = value }, modifier = Modifier.weight(1f).height(48.dp)) {
                        Text(if (mode == value) "✓ $name" else name, color = if (mode == value) AureaColors.Accent else AureaColors.Text)
                    }
                }
            }
        }
        Text(stringResource(when(mode) {
            2 -> R.string.edt_clip_slip_desc
            3, 4 -> R.string.edt_clip_roll_desc
            else -> R.string.edt_clip_slide_desc
        }), color = AureaColors.Muted, fontSize = 13.sp)
        if (needsLeft) ClipNeighbour(stringResource(R.string.edt_clip_previous), before.map { it.id to it.name }, left) { previous = it }
        if (needsRight) ClipNeighbour(stringResource(R.string.edt_clip_next), after.map { it.id to it.name }, right) { next = it }
        OutlinedTextField(value = step, onValueChange = { value -> if (value.length <= 4 && value.all(Char::isDigit)) step = value },
            label = { Text(stringResource(R.string.edt_clip_step)) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            listOf(-1 to stringResource(R.string.edt_clip_back), 1 to stringResource(R.string.edt_clip_forward)).forEach { (sign, title) ->
                val editLabel = stringResource(R.string.edt_clip_edit_desc, title)
                TextButton(onClick = { store.editClipTime(mode, sign * (frames ?: 1), left, right) }, enabled = enabled,
                    modifier = Modifier.weight(1f).height(48.dp).semantics { contentDescription = editLabel }) {
                    Text("${if(sign < 0) "−" else "+"}${frames ?: 0} · $title")
                }
            }
        }
        if (!enabled) Text(stringResource(if(row.locked) R.string.edt_clip_unlock else R.string.edt_clip_pick), color = AureaColors.Muted, fontSize = 12.sp)
    }
}

@Composable
private fun ClipNeighbour(title: String, items: List<Pair<Long, String>>, selected: Long, select: (Long) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth().height(48.dp)) {
            Text("$title: ${items.firstOrNull { it.first == selected }?.second ?: stringResource(R.string.edt_clip_choose)}")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            items.forEach { (id, name) -> DropdownMenuItem(text = { Text(name) }, onClick = { select(id); expanded = false }) }
        }
    }
}

/** Duração da camada em "m:ss". */
private fun clock(frames: Int, fps: Float): String {
    val s = if (fps > 0f) (frames / fps).roundToInt() else 0
    return "${s / 60}:${(s % 60).toString().padStart(2, '0')}"
}

/**
 * TEMPO E VELOCIDADE [A] (`showSpeedSheet`): "1,00x · duração", o que acontece
 * com a barra, a régua entre a tartaruga e a lebre (escala logarítmica: meio
 * risco para 0,5x vale o mesmo que para 2x), os atalhos e os interruptores.
 * Velocidade e Reverso são do motor (vídeo, miniatura e som seguem a mesma
 * conta de tempo); o som acompanha a velocidade junto com o tom.
 */
@Composable
internal fun SpeedPanel(env: PanelEnv) {
    val store = env.store
    val kind by remember(store) { derivedStateOf { store.detail?.kind ?: 0 } }
    val frames by remember(store) { derivedStateOf { store.detail?.let { it.endFrame - it.startFrame } ?: 0 } }
    val speed by remember(store) { derivedStateOf { store.detail?.speed ?: 1f } }
    val reversed by remember(store) { derivedStateOf { store.detail?.reversed ?: false } }
    val frameBlend by remember(store) { derivedStateOf { store.detail?.frameBlendMode ?: 0 } }
    val animated by remember(store) { derivedStateOf { store.detail?.speedAnimated ?: false } }
    val keyHere by remember(store) {
        derivedStateOf {
            val d = store.detail
            d != null && store.keyframes[d.id].orEmpty().any { it.property == TrackProperty.SPEED && it.time == d.localPlayhead }
        }
    }
    val media = kind == LayerType.Video.kind || kind == LayerType.Audio.kind
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 24.dp)) {
        if (!media) {
            Text(
                stringResource(R.string.panel_velocidade_vale_video_audio_nas_outras),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        if (speed == 0f && !animated) {
            Text(stringResource(R.string.edt_freeze_frame, clock(frames, store.project.fps)), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(R.string.panel_este_trecho_quadro_parado_apare_bordas),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        Text("${speedLabel(speed)} · ${clock(frames, store.project.fps)}", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
        Spacer(Modifier.height(10.dp))
        // O que a mudança de velocidade faz com a barra: o início fica onde está
        // e o fim acompanha (é o que o motor faz — nada de escolha falsa aqui).
        // Com keyframes a barra não muda de tamanho: a velocidade varia dentro dela.
        Text(
            stringResource(if (animated) R.string.panel_velocidade_keyframes_duracao_fica else R.string.panel_inicio_camada_fica_lugar_fim_acompanha),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(12.dp))
        // ◇ na etiqueta: grava/apaga o keyframe de velocidade no cabeçote (a
        // curva aparece nas trilhas e no gráfico como qualquer propriedade).
        PropertyCustomRow(
            stringResource(R.string.editor_velocidade),
            selected = animated,
            onSelect = { store.toggleSpeedKeyframe() },
            keyframe = when { keyHere -> KeyframeLook.KeyHere; animated -> KeyframeLook.Animated; else -> KeyframeLook.None },
        ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Tortoise, 20.dp, AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            // Régua em log2 × 100: arrastar a mesma distância dobra ou divide.
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { log2Speed(store.detail?.speed ?: 1f) },
                    unitsPerDp = 1f,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { log2Speed(store.detail?.speed ?: 1f) },
                        unitsPerDp = { 1f },
                        min = -332f,
                        max = 332f,
                        onStart = { store.beginGesture("velocidade") },
                        onValue = { v -> store.setLayerSpeed(snapSpeed(2f.pow(v / 100f))) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.Hare, 20.dp, AureaColors.Muted)
            Spacer(Modifier.width(8.dp))
            ValueBox(speedLabel(speed), width = 64.dp, onTap = null)
        }
        }
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0.25f to "0,25x", 0.5f to "0,5x", 1f to "1x", 2f to "2x", 4f to "4x").forEach { (value, label) ->
                val on = kotlin.math.abs(speed - value) < 0.005f
                Box(
                    Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(onClick = { store.setLayerSpeed(value) })
                        .padding(horizontal = 12.dp, vertical = 6.dp),
                ) {
                    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        if (kind == LayerType.Video.kind) {
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_passar_tras_frente), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                AureaToggle(checked = reversed, onCheckedChange = { store.setLayerReversed(it) })
            }
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_quadros_camera_lenta), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf(0 to stringResource(R.string.panel_repetir_quadro), 1 to stringResource(R.string.panel_misturar), 2 to stringResource(R.string.panel_movimento_suave)).forEach { (m, label) ->
                        val on = frameBlend == m
                        Box(
                            Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                                .tocavel(onClick = { store.setFrameBlend(m) }).padding(horizontal = 10.dp, vertical = 6.dp),
                        ) {
                            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                        }
                    }
                }
            }
        }
    }
}

private fun log2Speed(s: Float): Float = (kotlin.math.ln(s.coerceIn(0.05f, 16f)) / kotlin.math.ln(2f)) * 100f

/** Encosta nos valores redondos (0,25 · 0,5 · 1 · 2 · 4) quando passa perto. */
private fun snapSpeed(s: Float): Float {
    for (v in floatArrayOf(0.25f, 0.5f, 1f, 2f, 4f)) if (kotlin.math.abs(s - v) / v < 0.03f) return v
    return (s * 100f).roundToInt() / 100f
}

private fun speedLabel(s: Float): String = "${com.aurea.aurea.ui.ds.numeroPtBr(s, 2)}x"

/**
 * SOM [A] (`audio_sheet.dart`): "Som" com o nível em dB, Mudo, Solo, Volume
 * (animável, com ◇), Ganho, Balanço e fades de igual potência — tudo ligado ao
 * mixer do motor (o mesmo que exporta).
 */
@Composable
internal fun AudioPanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 24.dp)) {
        if (detail == null || !detail.hasAudio) {
            Text(
                if (detail?.kind == LayerType.Video.kind) stringResource(R.string.panel_este_video_nao_tem_trilha_som) else stringResource(R.string.panel_esta_camada_nao_tem_audio),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        val level = detail.audioVolume * detail.audioGain
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Speaker2, 18.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.panel_som), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
            Spacer(Modifier.weight(1f))
            Text(
                if (detail.audioMuted || level <= 0f) stringResource(R.string.panel_mudo).lowercase() else db(level),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)),
            )
        }
        Spacer(Modifier.height(10.dp))
        AudioToggle(stringResource(R.string.panel_mudo), detail.audioMuted) { store.setAudioMuted(it) }
        AudioToggle(stringResource(R.string.panel_solo), detail.audioSolo) { store.setAudioSolo(it) }
        val keyLook = when {
            store.keyframes[detail.id].orEmpty().any { it.property == TrackProperty.AUDIO_VOLUME && it.time == detail.localPlayhead } -> KeyframeLook.KeyHere
            detail.volumeAnimated -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
        val volumeKeys = listOf(com.aurea.aurea.engine.TrackKey(TrackProperty.AUDIO_VOLUME))
        AudioRuler(
            label = stringResource(R.string.panel_volume), keyframe = keyLook, onKeyframe = { store.toggleVolumeKeyframe() },
            expression = store.expressionLook(volumeKeys), onExpression = { store.openExpression("Volume", volumeKeys, 100f, "%") },
            value = { store.detail?.audioVolume?.times(100f) ?: 100f }, text = "${(detail.audioVolume * 100f).roundToInt()}%",
            unitsPerDp = 0.5f, min = 0f, max = 200f, gesture = "volume", store = store,
        ) { store.setAudioVolume(it / 100f) }
        AudioRuler(
            label = stringResource(R.string.panel_reforco), value = { dbValue(store.detail?.audioGain ?: 1f) }, text = db(detail.audioGain),
            unitsPerDp = 0.1f, min = -24f, max = 12f, gesture = "reforço", store = store,
        ) { store.setAudioGain(if (it <= -24f) 0f else 10f.pow(it / 20f)) }
        AudioRuler(
            label = stringResource(R.string.panel_esquerda_direita), value = { (store.detail?.audioPan ?: 0f) * 100f }, text = pan(detail.audioPan),
            unitsPerDp = 0.5f, min = -100f, max = 100f, gesture = "balanço", store = store,
        ) { store.setAudioPan(it / 100f) }
        val maxFade = max(0f, (detail.endFrame - detail.startFrame) / fps / 2f)
        AudioRuler(
            label = stringResource(R.string.panel_entrada_suave), value = { (store.detail?.audioFadeIn ?: 0) / fps }, text = secs(detail.audioFadeIn / fps),
            unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = stringResource(R.string.panel_entrada_suave_1fd1), store = store,
        ) { store.setAudioFade(true, (it * fps).roundToInt()) }
        AudioRuler(
            label = stringResource(R.string.panel_saida_suave), value = { (store.detail?.audioFadeOut ?: 0) / fps }, text = secs(detail.audioFadeOut / fps),
            unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = stringResource(R.string.panel_saida_suave_9ac9), store = store,
        ) { store.setAudioFade(false, (it * fps).roundToInt()) }
        Spacer(Modifier.height(6.dp))
        Text(
            stringResource(R.string.panel_volume_sobe_desce_forma_natural_sem),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 14.85.sp, color = AureaColors.Muted)),
        )
        if (detail.kind == LayerType.Video.kind) {
            Spacer(Modifier.height(12.dp))
            Row(
                Modifier.fillMaxWidth().height(44.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                    .tocavel { store.extractAudio(detail.id) }.padding(horizontal = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.MusicNote2, 16.dp, AureaColors.Accent)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.panel_extrair_audio_camada), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            }
        }
    }
}

private fun dbValue(linear: Float): Float = if (linear <= 0f) -24f else (20f * log10(linear)).coerceIn(-24f, 12f)

private fun db(linear: Float): String {
    if (linear <= 0f) return "−∞ dB"
    val v = 20f * log10(linear)
    val r = (v * 10f).roundToInt() / 10f
    return (if (r > 0f) "+" else if (r < 0f) "−" else "") + "%.1f".format(abs(r)).replace('.', ',') + " dB"
}

@Composable
private fun pan(p: Float): String {
    val v = (p * 100f).roundToInt()
    return when {
        v == 0 -> stringResource(R.string.panel_centro)
        v < 0 -> stringResource(R.string.edt_pan_left, -v)
        else -> stringResource(R.string.edt_pan_right, v)
    }
}

private fun secs(s: Float): String = "%.2f s".format(s).replace('.', ',')

@Composable
private fun AudioToggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

/** Linha com régua arrastável (um gesto = um passo de desfazer) e o valor à direita. */
@Composable
private fun AudioRuler(
    label: String,
    value: () -> Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    gesture: String,
    store: com.aurea.aurea.state.EditorStore,
    keyframe: KeyframeLook = KeyframeLook.None,
    onKeyframe: (() -> Unit)? = null,
    expression: com.aurea.aurea.engine.ExpressionLook = com.aurea.aurea.engine.ExpressionLook.None,
    onExpression: (() -> Unit)? = null,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(
        label, selected = keyframe != KeyframeLook.None, onSelect = { onKeyframe?.invoke() }, keyframe = keyframe,
        expression = expression, onExpression = onExpression,
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}

/**
 * REMAPEAR TEMPO em lista (inspirado no Node Video, com o tema do Aurea): três
 * linhas e o Ao contrário, nada de gráfico de valor.
 *  1. Manter o tom do áudio — liga/desliga (o som segue a curva sem mudar a nota).
 *  2. Remapear tempo — o MOMENTO DA FONTE no cabeçote, em timecode. Arrastar o
 *     valor para os lados (ajuste fino: 1 quadro a cada 4 dp) ou tocar para
 *     digitar grava a chave ali: "neste instante da timeline mostra este
 *     instante do vídeo". O ◇ do rótulo marca/tira a chave; a faixa fina embaixo
 *     mostra as chaves e o cabeçote (tocar numa chave vai até ela e a escolhe; a
 *     curva ao lado abre o editor de curva NORMAL do Aurea para aquele trecho).
 *  3. Interpolação do tempo — Desligado / Mistura de quadros / Optical flow.
 * O dado é a MESMA curva da camada: prévia, exportação e som seguem ela.
 */
@Composable
internal fun TimeRemapEffectEditor(env: PanelEnv, effectId: Int) {
    val store = env.store
    val q by remember(store) { derivedStateOf { store.timeRemap } }
    val local by remember(store) { derivedStateOf { store.detail?.localPlayhead ?: 0 } }
    val seconds by remember(store, effectId) { derivedStateOf { store.paramOf(effectId, 0)?.value?.getOrNull(0) ?: 0f } }
    val keepPitch by remember(store) { derivedStateOf { store.detail?.keepPitch ?: false } }
    val frameBlend by remember(store) { derivedStateOf { store.detail?.frameBlendMode ?: 0 } }
    val reversed by remember(store) { derivedStateOf { store.detail?.remapReversed ?: false } }
    val isVideo by remember(store) { derivedStateOf { store.detail?.kind == LayerType.Video.kind } }
    val selectedKey by remember(store) {
        derivedStateOf { store.selectedKeyframe?.second?.takeIf { it.property == TrackProperty.TIME_REMAP }?.time }
    }
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted))
    val rowText = AureaType.Base.merge(TextStyle(fontSize = 13.sp))
    Column(Modifier.fillMaxWidth()) {
        // 1) Manter o tom do áudio.
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.remap_manter_tom), modifier = Modifier.weight(1f), style = rowText)
            AureaToggle(checked = keepPitch, onCheckedChange = { store.setKeepPitch(it) })
        }
        val data = q
        if (data == null || data.size < 5) {
            Text(stringResource(R.string.remap_ligue_efeito), style = muted)
            return@Column
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        val lo = data[1].toInt()
        val hi = data[2].toInt()
        val lastFrame = max(1f, if (data[3] > 0f) data[3] else data[2])
        val keyTimes = (0 until data[0].toInt()).mapNotNull { i -> data.getOrNull(5 + i * 7)?.toInt() }
        val keyHere = keyTimes.indexOf(local)
        val inside = local in lo..hi
        // 2) Remapear tempo: ◇ no rótulo, timecode arrastável/digitável e a curva.
        var drag by remember { mutableStateOf<Float?>(null) }
        var dragLocal by remember(store.primary, effectId) { mutableStateOf<Int?>(null) }
        val shownFrames = drag ?: (seconds * fps)
        val title = stringResource(R.string.remap_linha_tempo)
        val curveDesc = stringResource(R.string.remap_editar_curva)
        PropertyCustomRow(
            label = title,
            selected = keyHere >= 0,
            onSelect = {
                if (inside) {
                    if (keyHere >= 0) store.remapRemove(keyHere) else store.remapInsert(local.toLong())
                }
            },
            keyframe = if (keyHere >= 0) KeyframeLook.KeyHere else KeyframeLook.Animated,
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.weight(1f).height(48.dp).valueDrag(
                        enabled = inside,
                        start = { store.paramOf(effectId, 0)?.value?.getOrNull(0)?.times(fps) ?: 0f },
                        unitsPerDp = { 0.25f },
                        min = 0f,
                        max = lastFrame,
                        onStart = { dragLocal = local; store.beginGesture("tempo do vídeo") },
                        onValue = { f -> drag = f.roundToInt().toFloat(); store.setRemapTime(effectId, f.roundToInt() / fps, dragLocal ?: local) },
                        onEnd = { store.endGesture(); drag = null; dragLocal = null },
                    ),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    ValueBox(
                        timecode(shownFrames.roundToInt(), fps),
                        width = 112.dp,
                        enabled = inside,
                        onTap = {
                            env.openKeypad(
                                KeypadRequest(title, (shownFrames / fps), "s", 0f, lastFrame / fps, 2) { s ->
                                    store.setRemapTime(effectId, s.coerceIn(0f, lastFrame / fps))
                                },
                            )
                        },
                    )
                }
                Box(
                    Modifier.size(44.dp).semantics { contentDescription = curveDesc }
                        .tocavel(enabled = keyTimes.size >= 2) { openRemapCurve(env, local) },
                    contentAlignment = Alignment.Center,
                ) {
                    CurveRailIcon(enabled = keyTimes.size >= 2, animated = selectedKey != null || keyHere >= 0)
                }
            }
        }
        RemapKeyStrip(
            keys = keyTimes,
            lo = lo,
            hi = hi,
            playhead = local,
            selected = selectedKey,
            description = stringResource(R.string.remap_faixa_chaves),
            onKey = { t -> store.selectRemapKey(t) },
            onScrub = { t -> store.detail?.let { d -> store.seek(d.timelineFrame(t.coerceIn(lo, max(lo, hi - 1)))) } },
        )
        if (!inside) Text(stringResource(R.string.remap_fora_clipe), style = muted)
        Spacer(Modifier.height(6.dp))
        // 3) Interpolação do tempo (quadros entre os da fonte: prévia e exportação).
        if (isVideo) {
            Text(stringResource(R.string.remap_interpolacao), style = rowText)
            Spacer(Modifier.height(6.dp))
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(0 to R.string.remap_interp_off, 1 to R.string.remap_interp_blend, 2 to R.string.remap_interp_flow).forEach { (m, label) ->
                    RemapChip(stringResource(label), on = frameBlend == m) { store.setFrameBlend(m) }
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        // Ao contrário: espelha a curva (duas vezes volta ao que era).
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.remap_ao_contrario), modifier = Modifier.weight(1f), style = rowText)
            AureaToggle(checked = reversed, onCheckedChange = { store.reverseRemap() })
        }
        Text(stringResource(R.string.remap_dica_lista), style = muted)
    }
}

/** Timecode da fonte, H:MM:SS:QQ (quadros na taxa do projeto). */
internal fun timecode(frames: Int, fps: Float): String {
    val rate = max(1, fps.roundToInt())
    val f = max(0, frames)
    val totalSec = f / rate
    return String.format(java.util.Locale.ROOT, "%d:%02d:%02d:%02d", totalSec / 3600, (totalSec / 60) % 60, totalSec % 60, f % rate)
}

/**
 * Editor de curva NORMAL do Aurea para o trecho do remapeamento: a chave
 * escolhida na faixa, senão a que abre o trecho sob o cabeçote.
 */
private fun openRemapCurve(env: PanelEnv, local: Int) {
    val store = env.store
    val layer = store.primary ?: return
    val keys = store.keyframes[layer].orEmpty().filter { it.property == TrackProperty.TIME_REMAP }.sortedBy { it.time }
    if (keys.size < 2) return
    val chosen = store.selectedKeyframe?.takeIf { it.first == layer && it.second.property == TrackProperty.TIME_REMAP }?.second
    val key = chosen?.let { c -> keys.firstOrNull { it.time == c.time } }
        ?: keys.lastOrNull { it.time <= local }?.takeIf { it != keys.last() }
        ?: keys[keys.size - 2].takeIf { local >= keys.last().time }
        ?: keys.first()
    store.selectKeyframe(layer, key)
    env.onOpenPanel(EditorPanel.Curve)
}

@Composable
private fun RemapChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.height(36.dp).clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/**
 * A faixa fina das chaves (tempo local do clipe, lo..hi): losangos nas chaves,
 * o escolhido aceso, e a linha do cabeçote. Tocar perto de um losango vai até
 * ele e o escolhe; tocar/arrastar no resto move o cabeçote (a prévia segue).
 */
@Composable
private fun RemapKeyStrip(
    keys: List<Int>,
    lo: Int,
    hi: Int,
    playhead: Int,
    selected: Int?,
    description: String,
    onKey: (Int) -> Unit,
    onScrub: (Int) -> Unit,
) {
    val span = max(1, hi - lo)
    val accent = AureaColors.Accent
    val keyColor = AureaColors.Text
    val track = AureaColors.Chip
    val head = AureaColors.Playhead
    val readKeys by androidx.compose.runtime.rememberUpdatedState(keys)
    val readKey by androidx.compose.runtime.rememberUpdatedState(onKey)
    val readScrub by androidx.compose.runtime.rememberUpdatedState(onScrub)
    Box(
        Modifier.fillMaxWidth().height(32.dp).padding(start = 102.dp, end = 4.dp)
            .semantics { contentDescription = description }
            .pointerInput(lo, hi) {
                val pad = 8.dp.toPx()
                fun frameAt(x: Float): Int {
                    val w = (size.width - pad * 2).coerceAtLeast(1f)
                    return lo + (((x - pad) / w).coerceIn(0f, 1f) * span).roundToInt()
                }
                fun xOf(t: Int): Float = pad + (size.width - pad * 2).coerceAtLeast(1f) * ((t - lo).toFloat() / span)
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    val hit = readKeys.minByOrNull { abs(xOf(it) - down.position.x) }
                        ?.takeIf { abs(xOf(it) - down.position.x) <= 14.dp.toPx() }
                    if (hit != null) readKey(hit) else readScrub(frameAt(down.position.x))
                    while (true) {
                        val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        change.consume()
                        if (hit == null) readScrub(frameAt(change.position.x))
                    }
                }
            }
            .drawBehind {
                val pad = 8.dp.toPx()
                val w = (size.width - pad * 2).coerceAtLeast(1f)
                fun xOf(t: Int): Float = pad + w * ((t - lo).toFloat() / span)
                val y = size.height / 2f
                drawRoundRect(track, topLeft = Offset(pad, y - 1.5.dp.toPx()), size = Size(w, 3.dp.toPx()), cornerRadius = CornerRadius(2f, 2f))
                val r = 5.dp.toPx()
                keys.forEach { t ->
                    if (t < lo - 1 || t > hi + 1) return@forEach
                    val x = xOf(t.coerceIn(lo, hi))
                    val path = androidx.compose.ui.graphics.Path().apply {
                        moveTo(x, y - r); lineTo(x + r, y); lineTo(x, y + r); lineTo(x - r, y); close()
                    }
                    drawPath(path, if (t == selected || t == playhead) accent else keyColor)
                }
                val px = xOf(playhead.coerceIn(lo, hi))
                drawLine(head, Offset(px, 2.dp.toPx()), Offset(px, size.height - 2.dp.toPx()), strokeWidth = 2.dp.toPx())
            },
    )
}
