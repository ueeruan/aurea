package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
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
    val media = kind == LayerType.Video.kind || kind == LayerType.Audio.kind
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 24.dp)) {
        if (!media) {
            Text(
                "A velocidade vale para vídeo e áudio. Nas outras camadas, aproxime ou afaste os keyframes para animar mais rápido ou mais devagar.",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        if (speed == 0f) {
            Text("Quadro congelado · ${clock(frames, store.project.fps)}", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
            Spacer(Modifier.height(8.dp))
            Text(
                "Este trecho é um quadro parado. Apare as bordas na timeline para mudar quanto tempo ele fica na tela.",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.2.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        Text("${speedLabel(speed)} · ${clock(frames, store.project.fps)}", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
        Spacer(Modifier.height(10.dp))
        // O que a mudança de velocidade faz com a barra: o início fica onde está
        // e o fim acompanha (é o que o motor faz — nada de escolha falsa aqui).
        Text(
            "O início da camada fica no lugar; o fim acompanha a nova velocidade.",
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
        )
        Spacer(Modifier.height(12.dp))
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
        Spacer(Modifier.height(14.dp))
        val remap by remember(store) { derivedStateOf { store.detail?.timeRemap ?: false } }
        Text("Acelerar e desacelerar no tempo", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.W700, color = AureaColors.Muted)))
        Spacer(Modifier.height(6.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(-1 to "Sem rampa", 1 to "Suave", 2 to "Lento no meio", 3 to "Acelerar", 4 to "Desacelerar").forEach { (preset, label) ->
                val on = (preset == -1 && !remap)
                Box(
                    Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(onClick = { store.applySpeedRamp(preset) })
                        .padding(horizontal = 12.dp, vertical = 6.dp),
                ) {
                    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                }
            }
        }
        if (remap) {
            Spacer(Modifier.height(8.dp))
            TimeRemapGraph(store)
            Spacer(Modifier.height(6.dp))
            Text(
                "Com a rampa ligada, som e vídeo seguem a mesma mudança de velocidade.",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
        }
        Spacer(Modifier.height(8.dp))
        if (kind == LayerType.Video.kind) {
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Passar de trás para frente", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                AureaToggle(checked = reversed, onCheckedChange = { store.setLayerReversed(it) })
            }
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Quadros na câmera lenta", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf(0 to "Repetir quadro", 1 to "Misturar", 2 to "Movimento suave").forEach { (m, label) ->
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
                if (detail?.kind == LayerType.Video.kind) "Este vídeo não tem trilha de som." else "Esta camada não tem áudio.",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
            return@Column
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        val level = detail.audioVolume * detail.audioGain
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Speaker2, 18.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text("Som", style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)))
            Spacer(Modifier.weight(1f))
            Text(
                if (detail.audioMuted || level <= 0f) "mudo" else db(level),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)),
            )
        }
        Spacer(Modifier.height(10.dp))
        AudioToggle("Mudo", detail.audioMuted) { store.setAudioMuted(it) }
        AudioToggle("Solo", detail.audioSolo) { store.setAudioSolo(it) }
        val keyLook = when {
            store.keyframes[detail.id].orEmpty().any { it.property == TrackProperty.AUDIO_VOLUME && it.time == detail.localPlayhead } -> KeyframeLook.KeyHere
            detail.volumeAnimated -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
        val volumeKeys = listOf(com.aurea.aurea.engine.TrackKey(TrackProperty.AUDIO_VOLUME))
        AudioRuler(
            label = "Volume", keyframe = keyLook, onKeyframe = { store.toggleVolumeKeyframe() },
            expression = store.expressionLook(volumeKeys), onExpression = { store.openExpression("Volume", volumeKeys, 100f, "%") },
            value = { store.detail?.audioVolume?.times(100f) ?: 100f }, text = "${(detail.audioVolume * 100f).roundToInt()}%",
            unitsPerDp = 0.5f, min = 0f, max = 200f, gesture = "volume", store = store,
        ) { store.setAudioVolume(it / 100f) }
        AudioRuler(
            label = "Reforço", value = { dbValue(store.detail?.audioGain ?: 1f) }, text = db(detail.audioGain),
            unitsPerDp = 0.1f, min = -24f, max = 12f, gesture = "reforço", store = store,
        ) { store.setAudioGain(if (it <= -24f) 0f else 10f.pow(it / 20f)) }
        AudioRuler(
            label = "Esquerda e direita", value = { (store.detail?.audioPan ?: 0f) * 100f }, text = pan(detail.audioPan),
            unitsPerDp = 0.5f, min = -100f, max = 100f, gesture = "balanço", store = store,
        ) { store.setAudioPan(it / 100f) }
        val maxFade = max(0f, (detail.endFrame - detail.startFrame) / fps / 2f)
        AudioRuler(
            label = "Entrada suave", value = { (store.detail?.audioFadeIn ?: 0) / fps }, text = secs(detail.audioFadeIn / fps),
            unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = "entrada suave", store = store,
        ) { store.setAudioFade(true, (it * fps).roundToInt()) }
        AudioRuler(
            label = "Saída suave", value = { (store.detail?.audioFadeOut ?: 0) / fps }, text = secs(detail.audioFadeOut / fps),
            unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = "saída suave", store = store,
        ) { store.setAudioFade(false, (it * fps).roundToInt()) }
        Spacer(Modifier.height(6.dp))
        Text(
            "O volume sobe e desce de forma natural, sem buraco no meio do som.",
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
                Text("Extrair o áudio para uma camada", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
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

private fun pan(p: Float): String {
    val v = (p * 100f).roundToInt()
    return when {
        v == 0 -> "Centro"
        v < 0 -> "E ${-v}"
        else -> "D $v"
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
