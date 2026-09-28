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
 * REMAPEAR TEMPO, versão simples: sem gráfico. Uma régua do "momento do vídeo"
 * em segundos da fonte — arrastar mostra, ao vivo na prévia, o quadro que toca
 * no cabeçote e grava a chave ali. Congelar = dois pontos iguais; ao contrário
 * = tempo diminuindo; lento/rápido = distância entre os pontos. Por chave, só a
 * suavidade em quatro palavras; e os atalhos criam os pontos pela pessoa.
 * O dado é a MESMA curva da camada (prévia, exportação e som seguem ela).
 */
@Composable
internal fun TimeRemapEffectEditor(store: EditorStore, effectId: Int) {
    val q by remember(store) { derivedStateOf { store.timeRemap } }
    val local by remember(store) { derivedStateOf { store.detail?.localPlayhead ?: 0 } }
    val seconds by remember(store, effectId) { derivedStateOf { store.paramOf(effectId, 0)?.value?.getOrNull(0) ?: 0f } }
    val ease by remember(store, effectId) { derivedStateOf { store.paramOf(effectId, 1)?.value?.getOrNull(0)?.roundToInt() ?: 0 } }
    val data = q
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted))
    val heading = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted))
    Column(Modifier.fillMaxWidth()) {
        if (data == null || data.size < 5) {
            Text(stringResource(R.string.remap_ligue_efeito), style = muted)
            return@Column
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        val lo = data[1].toInt()
        val hi = data[2].toInt()
        val maxSec = max(1f, if (data[3] > 0f) data[3] else data[2]) / fps
        val keyTimes = (0 until data[0].toInt()).mapNotNull { i -> data.getOrNull(5 + i * 7)?.toInt() }
        val keyHere = keyTimes.indexOf(local)
        val inside = local in lo..hi
        val speed = data[4]
        // Linha do tempo do vídeo: losango (marca/tira o ponto aqui) e setas entre pontos.
        PropertyCustomRow(
            label = stringResource(R.string.remap_momento_video),
            selected = keyHere >= 0,
            onSelect = {
                if (inside) {
                    if (keyHere >= 0) store.remapRemove(keyHere) else store.remapInsert(local.toLong())
                }
            },
            keyframe = if (keyHere >= 0) KeyframeLook.KeyHere else KeyframeLook.Animated,
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    when {
                        abs(speed) < 0.005f -> stringResource(R.string.remap_vel_congelado)
                        speed < 0f -> stringResource(R.string.remap_vel_reverso, com.aurea.aurea.ui.ds.numeroPtBr(-speed, 2))
                        else -> stringResource(R.string.remap_vel_aqui, com.aurea.aurea.ui.ds.numeroPtBr(speed, 2))
                    },
                    modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)),
                )
                val prev = keyTimes.lastOrNull { it < local }
                val next = keyTimes.firstOrNull { it > local }
                RemapStep(CupertinoGlyph.ChevronLeft, stringResource(R.string.remap_ponto_anterior), prev != null) {
                    prev?.let { t -> store.detail?.let { d -> store.seek(d.timelineFrame(t)) } }
                }
                RemapStep(CupertinoGlyph.ChevronRight, stringResource(R.string.remap_proximo_ponto), next != null) {
                    next?.let { t -> store.detail?.let { d -> store.seek(d.timelineFrame(t)) } }
                }
            }
        }
        var drag by remember { mutableStateOf<Float?>(null) }
        val shown = drag ?: (seconds / maxSec).coerceIn(0f, 1f)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            RemapTimeBar(
                fraction = shown,
                enabled = inside,
                modifier = Modifier.weight(1f),
                onStart = { store.beginGesture("tempo do vídeo") },
                onFraction = { f -> drag = f; store.setRemapTime(effectId, f * maxSec) },
                onEnd = { drag = null; store.endGesture() },
            )
            Spacer(Modifier.width(8.dp))
            ValueBox(secs(shown * maxSec), width = 72.dp, onTap = null)
        }
        if (!inside) Text(stringResource(R.string.remap_fora_clipe), style = muted)
        Spacer(Modifier.height(10.dp))
        // Suavidade da chave no cabeçote (o trecho que sai dela até a próxima).
        Text(stringResource(R.string.remap_suavidade), style = heading)
        Spacer(Modifier.height(6.dp))
        if (keyHere < 0) {
            Text(stringResource(R.string.remap_sem_ponto), style = muted)
        } else {
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(
                    0 to R.string.remap_ease_linear, 3 to R.string.remap_ease_in,
                    4 to R.string.remap_ease_out, 1 to R.string.remap_ease_in_out,
                ).forEach { (mode, label) ->
                    RemapChip(stringResource(label), on = ease == mode) { store.setRemapEase(effectId, mode) }
                }
            }
        }
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.remap_atalhos), style = heading)
        Spacer(Modifier.height(6.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(
                7 to R.string.remap_congelar_aqui, 2 to R.string.remap_camera_lenta, 3 to R.string.remap_acelerar,
                6 to R.string.remap_inverter, 0 to R.string.remap_normal,
            ).forEach { (preset, label) ->
                RemapChip(stringResource(label), on = false) { store.applySpeedRamp(preset) }
            }
        }
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.remap_dica), style = muted)
    }
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

@Composable
private fun RemapStep(glyph: Char, description: String, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.width(40.dp).height(40.dp).semantics { contentDescription = description }
            .tocavel(onClick = { if (enabled) onClick() }),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 18.dp, if (enabled) AureaColors.Text else AureaColors.Muted.copy(alpha = 0.4f))
    }
}

/**
 * Régua absoluta do momento do vídeo: tocar pula para ali, arrastar segue o
 * dedo (um gesto = um passo de desfazer). 0 à esquerda, fim da fonte à direita.
 */
@Composable
private fun RemapTimeBar(
    fraction: Float,
    enabled: Boolean,
    modifier: Modifier,
    onStart: () -> Unit,
    onFraction: (Float) -> Unit,
    onEnd: () -> Unit,
) {
    val accent = if (enabled) AureaColors.Accent else AureaColors.Muted
    val track = AureaColors.Chip
    Box(
        modifier.height(44.dp)
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    onStart()
                    try {
                        onFraction((down.position.x / size.width.coerceAtLeast(1)).coerceIn(0f, 1f))
                        while (true) {
                            val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            change.consume()
                            onFraction((change.position.x / size.width.coerceAtLeast(1)).coerceIn(0f, 1f))
                        }
                    } finally {
                        onEnd()
                    }
                }
            }
            .drawBehind {
                val y = size.height / 2f
                val r = 4.dp.toPx()
                drawRoundRect(track, topLeft = Offset(0f, y - r), size = Size(size.width, r * 2), cornerRadius = CornerRadius(r, r))
                val x = size.width * fraction.coerceIn(0f, 1f)
                drawRoundRect(accent, topLeft = Offset(0f, y - r), size = Size(x, r * 2), cornerRadius = CornerRadius(r, r))
                drawCircle(Color.Black.copy(alpha = 0.35f), radius = 12.dp.toPx(), center = Offset(x, y + 1.dp.toPx()))
                drawCircle(Color.White, radius = 11.dp.toPx(), center = Offset(x, y))
            },
    )
}
