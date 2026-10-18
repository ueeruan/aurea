package com.aurea.aurea.editor

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.PackageManager
import android.media.MediaRecorder
import android.net.Uri
import android.os.Build
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay
import java.io.File

// =============================================================================
//  GRAVAR VOZ (Adicionar → Áudio → "Gravar voz").
//
//  A narração é gravada pelo microfone em AAC/.m4a e entra na timeline como
//  camada de áudio pelo mesmo `importAudio` de um arquivo escolhido — desfazer,
//  waveform e export não sabem que ela veio do microfone. Gravar é recurso da
//  plataforma (MediaRecorder aqui, AVAudioRecorder no iOS: VoiceRecorder.swift);
//  o motor só recebe o arquivo pronto.
//
//  O painel fica embaixo e o palco continua à vista: com "Tocar o vídeo
//  enquanto grava", a timeline toca a partir do cabeçote e a pessoa narra
//  olhando o vídeo. A camada entra no quadro onde a gravação começou.
//
//  Interrupções: app fora da tela (ligação, HOME) para a gravação e GUARDA o
//  que foi gravado; a camada entra quando o app volta (o motor fica suspenso
//  em segundo plano). Cancelar descarta o arquivo.
// =============================================================================

private const val TAG = "AureaVoz"

/**
 * Uma tomada: MediaRecorder em AAC dentro de MPEG-4 (.m4a), mono, 44,1 kHz,
 * 128 kbps. O motor abre pelo extrator de mídia do Android, como qualquer .m4a.
 */
internal class VoiceTakeRecorder(
    private val context: Context,
    val file: File,
    private val onFailure: () -> Unit,
) {
    private var recorder: MediaRecorder? = null

    fun start(): Boolean {
        val r = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) MediaRecorder(context)
        else @Suppress("DEPRECATION") MediaRecorder()
        return try {
            r.setAudioSource(MediaRecorder.AudioSource.MIC)
            r.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
            r.setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            r.setAudioChannels(1)
            r.setAudioSamplingRate(44_100)
            r.setAudioEncodingBitRate(128_000)
            r.setOutputFile(file.absolutePath)
            // O microfone sumiu no meio (outro app tomou, erro do servidor de mídia):
            // para e guarda o que já foi gravado.
            r.setOnErrorListener { _, what, extra ->
                Log.w(TAG, "erro do gravador: $what/$extra")
                onFailure()
            }
            r.prepare()
            r.start()
            recorder = r
            true
        } catch (failure: Exception) {
            // Sem permissão, microfone ocupado por outro app ou codec recusado.
            Log.w(TAG, "microfone indisponivel: ${failure.javaClass.simpleName}")
            runCatching { r.release() }
            file.delete()
            false
        }
    }

    /** Pico desde a última leitura (0..32767). */
    fun amplitude(): Int = try { recorder?.maxAmplitude ?: 0 } catch (_: IllegalStateException) { 0 }

    /** Para e fecha o .m4a. Falso = nada aproveitável (e o arquivo já saiu). */
    fun stop(): Boolean {
        val r = recorder ?: return false
        recorder = null
        // `stop` lança se nenhum quadro válido chegou (gravação curta demais).
        val ok = try { r.stop(); true } catch (_: RuntimeException) { false }
        runCatching { r.release() }
        if (!ok || file.length() == 0L) {
            file.delete()
            return false
        }
        return true
    }

    fun discard() {
        val r = recorder
        recorder = null
        if (r != null) {
            try { r.stop() } catch (_: RuntimeException) { }
            runCatching { r.release() }
        }
        file.delete()
    }
}

internal enum class VoicePhase { Idle, Countdown, Recording }

/**
 * O estado de uma sessão do painel: a tomada em curso, o relógio, o medidor
 * e a tomada que espera o app voltar para entrar na timeline.
 */
@Stable
private class VoiceSession(
    private val context: Context,
    private val store: EditorStore,
    private val close: () -> Unit,
) {
    var phase by mutableStateOf(VoicePhase.Idle)
        private set
    var countdown by mutableIntStateOf(3)
    var elapsedMs by mutableLongStateOf(0L)
        private set
    var level by mutableFloatStateOf(0f)
        private set

    private var take: VoiceTakeRecorder? = null
    private var playAlong = true
    private var startFrame = 0
    private var startedAt = 0L
    /** Tomada parada com o app fora da tela: entra quando ele volta. */
    private var pending: PendingTake? = null

    private class PendingTake(val file: File, val frame: Int, val interrupted: Boolean)

    private fun text(id: Int) = AppText.get(context, id)

    /** Gravar: com a contagem, 3-2-1 antes; o quadro de início é o cabeçote de agora. */
    fun arm(withCountdown: Boolean, playVideo: Boolean) {
        if (phase != VoicePhase.Idle || pending != null) return
        store.pause()
        startFrame = store.playhead
        playAlong = playVideo
        if (withCountdown) {
            countdown = 3
            phase = VoicePhase.Countdown
        } else {
            begin()
        }
    }

    fun begin() {
        val recorder = VoiceTakeRecorder(context, store.newVoiceRecordingFile()) {
            finish(keep = true, importNow = true, interrupted = true)
        }
        if (!recorder.start()) {
            phase = VoicePhase.Idle
            store.showToast(text(R.string.voice_start_failed))
            return
        }
        take = recorder
        startedAt = SystemClock.elapsedRealtime()
        elapsedMs = 0L
        level = 0f
        phase = VoicePhase.Recording
        // Narrar olhando o vídeo: a timeline toca a partir do cabeçote.
        if (playAlong) store.play()
    }

    fun tick() {
        val t = take ?: return
        elapsedMs = SystemClock.elapsedRealtime() - startedAt
        level = VoiceTake.smooth(level, VoiceTake.levelFromAmplitude(t.amplitude()))
    }

    /**
     * Fim da tomada. `keep` = salvar; `importNow` = pôr na timeline já (falso
     * com o app fora da tela: espera [resumed]). A contagem só volta ao início.
     */
    fun finish(keep: Boolean, importNow: Boolean, interrupted: Boolean = false) {
        if (phase == VoicePhase.Countdown) {
            phase = VoicePhase.Idle
            return
        }
        val t = take ?: return
        take = null
        val duration = SystemClock.elapsedRealtime() - startedAt
        phase = VoicePhase.Idle
        level = 0f
        if (playAlong) store.pause()
        if (!keep) {
            t.discard()
            store.showToast(text(R.string.voice_discarded))
            return
        }
        if (duration < VoiceTake.MIN_DURATION_MS) {
            t.discard()
            store.showToast(text(R.string.voice_too_short))
            return
        }
        if (!t.stop()) {
            store.showToast(text(R.string.voice_save_failed))
            return
        }
        if (importNow) {
            store.importVoiceRecording(t.file, startFrame, interrupted)
            close()
        } else {
            pending = PendingTake(t.file, startFrame, interrupted)
        }
    }

    /** O app voltou: a tomada guardada na saída entra na timeline. */
    fun resumed() {
        val p = pending ?: return
        pending = null
        store.importVoiceRecording(p.file, p.frame, p.interrupted)
        close()
    }

    /** O painel saiu da tela: o que foi gravado não se perde. */
    fun dispose() {
        if (take != null) finish(keep = true, importNow = false)
        val p = pending ?: return
        pending = null
        store.importVoiceRecording(p.file, p.frame, p.interrupted)
    }
}

private tailrec fun Context.hostActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.hostActivity()
    else -> null
}

/**
 * O painel de gravação, por cima da timeline (o palco fica à vista). Um véu
 * leve segura os toques no resto do editor enquanto ele está aberto.
 */
@Composable
internal fun VoiceRecorderOverlay(store: EditorStore, onClose: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    val closeNow by rememberUpdatedState(onClose)
    val session = remember(store) { VoiceSession(context, store) { closeNow() } }
    fun micGranted() = context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    var granted by remember { mutableStateOf(micGranted()) }
    var blocked by remember { mutableStateOf(false) }
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        granted = ok
        // Negado sem "mostrar justificativa" = o sistema não pergunta mais: só pelas configurações.
        blocked = !ok && context.hostActivity()?.shouldShowRequestPermissionRationale(Manifest.permission.RECORD_AUDIO) == false
    }
    var withCountdown by rememberSaveable { mutableStateOf(false) }
    var playVideo by rememberSaveable { mutableStateOf(true) }

    DisposableEffect(owner, session) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                // Fora da tela (HOME, ligação atendida): para e guarda.
                Lifecycle.Event.ON_STOP -> session.finish(keep = true, importNow = false, interrupted = true)
                Lifecycle.Event.ON_START -> {
                    granted = micGranted()
                    if (granted) blocked = false
                    session.resumed()
                }
                else -> Unit
            }
        }
        owner.lifecycle.addObserver(observer)
        onDispose {
            owner.lifecycle.removeObserver(observer)
            session.dispose()
        }
    }
    LaunchedEffect(session.phase) {
        when (session.phase) {
            VoicePhase.Countdown -> {
                for (n in 3 downTo 1) {
                    session.countdown = n
                    delay(1000L)
                }
                if (session.phase == VoicePhase.Countdown) session.begin()
            }
            VoicePhase.Recording -> {
                // Relógio e medidor a 20 Hz; a troca de fase cancela este laço.
                while (true) {
                    session.tick()
                    delay(50L)
                }
            }
            VoicePhase.Idle -> Unit
        }
    }
    BackHandler {
        when (session.phase) {
            VoicePhase.Recording -> session.finish(keep = true, importNow = true)
            VoicePhase.Countdown -> session.finish(keep = false, importNow = false)
            VoicePhase.Idle -> onClose()
        }
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
        val panelMax = maxHeight * 0.72f
        Box(
            Modifier
                .fillMaxSize()
                .background(Color.Black.copy(alpha = 0.22f))
                .pointerInput(Unit) {
                    awaitEachGesture { awaitFirstDown(requireUnconsumed = false).consume() }
                },
        )
        Column(
            Modifier
                .align(Alignment.BottomCenter)
                // No tablet/paisagem o painel não estica a tela toda.
                .widthIn(max = 560.dp)
                .fillMaxWidth()
                .heightIn(max = panelMax)
                .clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                .background(AureaColors.EditorPanelHigh)
                .border(1.dp, AureaColors.Hairline, RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .testTag("voice.panel"),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Row(Modifier.fillMaxWidth().heightIn(min = 36.dp), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(CupertinoGlyph.MicFill, 18.dp, AureaColors.Danger)
                Spacer(Modifier.width(8.dp))
                Text(
                    stringResource(R.string.voice_title),
                    modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W600)),
                )
            }
            if (!granted) {
                VoicePermission(blocked, onAllow = { permission.launch(Manifest.permission.RECORD_AUDIO) }, onSettings = {
                    runCatching {
                        context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
                    }
                }, onCancel = onClose)
            } else {
                VoiceControls(
                    session = session,
                    busy = store.busyMessage != null,
                    withCountdown = withCountdown,
                    onCountdown = { withCountdown = it },
                    playVideo = playVideo,
                    onPlayVideo = { playVideo = it },
                    onClose = onClose,
                )
            }
        }
    }
}

@Composable
private fun VoiceControls(
    session: VoiceSession,
    busy: Boolean,
    withCountdown: Boolean,
    onCountdown: (Boolean) -> Unit,
    playVideo: Boolean,
    onPlayVideo: (Boolean) -> Unit,
    onClose: () -> Unit,
) {
    val phase = session.phase
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted, textAlign = TextAlign.Center))
    // Relógio (ou o número da contagem) bem grande: dá para ler com o aparelho longe.
    val elapsedLabel = stringResource(R.string.voice_elapsed)
    if (phase == VoicePhase.Countdown) {
        val startsIn = stringResource(R.string.voice_countdown_value, session.countdown)
        Text(
            session.countdown.toString(),
            modifier = Modifier.padding(top = 4.dp).semantics { contentDescription = startsIn }.testTag("voice.countdownValue"),
            style = AureaType.Base.merge(TextStyle(fontSize = 44.sp, fontWeight = FontWeight.W700, color = AureaColors.Danger)),
        )
    } else {
        val time = VoiceTake.elapsedLabel(session.elapsedMs)
        Text(
            time,
            modifier = Modifier.padding(top = 4.dp).semantics { contentDescription = "$elapsedLabel $time" }.testTag("voice.timer"),
            style = AureaType.Base.merge(TextStyle(fontSize = 44.sp, fontWeight = FontWeight.W600,
                color = if (phase == VoicePhase.Recording) AureaColors.Text else AureaColors.Muted)),
        )
    }
    Text(
        stringResource(if (phase == VoicePhase.Recording) R.string.voice_recording else R.string.voice_hint),
        modifier = Modifier.padding(horizontal = 8.dp),
        style = if (phase == VoicePhase.Recording) muted.merge(TextStyle(color = AureaColors.Danger, fontWeight = FontWeight.W600)) else muted,
    )
    Spacer(Modifier.height(10.dp))
    VoiceMeter(session.level)
    Spacer(Modifier.height(14.dp))
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            val discardLabel = stringResource(if (phase == VoicePhase.Idle) R.string.common_cancel else R.string.voice_cancel_take)
            Box(
                Modifier
                    .heightIn(min = 48.dp)
                    .widthIn(min = 96.dp)
                    .clip(RoundedCornerShape(24.dp))
                    .background(AureaColors.Chip)
                    .semantics { contentDescription = discardLabel }
                    .tocavel {
                        if (phase == VoicePhase.Idle) onClose() else session.finish(keep = false, importNow = false)
                    }
                    .padding(horizontal = 16.dp)
                    .testTag("voice.cancel"),
                contentAlignment = Alignment.Center,
            ) {
                Text(stringResource(R.string.common_cancel), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
            }
        }
        RecordButton(
            recording = phase == VoicePhase.Recording,
            enabled = !busy || phase != VoicePhase.Idle,
        ) {
            when (phase) {
                VoicePhase.Idle -> session.arm(withCountdown, playVideo)
                VoicePhase.Countdown -> session.finish(keep = false, importNow = false)
                VoicePhase.Recording -> session.finish(keep = true, importNow = true)
            }
        }
        Spacer(Modifier.weight(1f))
    }
    if (phase == VoicePhase.Idle) {
        Spacer(Modifier.height(10.dp))
        VoiceToggleRow(stringResource(R.string.voice_countdown), withCountdown, "voice.countdown", onCountdown)
        VoiceToggleRow(stringResource(R.string.voice_play_along), playVideo, "voice.playAlong", onPlayVideo)
        if (playVideo) Text(stringResource(R.string.voice_headphones), modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
            style = muted.merge(TextStyle(textAlign = TextAlign.Start)))
    }
}

/** O botão grande: círculo vermelho para gravar, quadrado vermelho para parar. */
@Composable
private fun RecordButton(recording: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val label = stringResource(if (recording) R.string.voice_stop else R.string.voice_record)
    Box(
        Modifier
            .size(80.dp)
            .border(4.dp, if (enabled) AureaColors.Text else AureaColors.Muted, CircleShape)
            .clip(CircleShape)
            .semantics { contentDescription = label }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick)
            .testTag(if (recording) "voice.stop" else "voice.record"),
        contentAlignment = Alignment.Center,
    ) {
        if (recording) {
            Box(Modifier.size(30.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.Danger))
        } else {
            Box(Modifier.size(60.dp).clip(CircleShape).background(if (enabled) AureaColors.Danger else AureaColors.Muted))
        }
    }
}

/** Medidor de entrada: verde, amarelo perto do topo, vermelho no estouro. */
@Composable
private fun VoiceMeter(level: Float) {
    val label = stringResource(R.string.voice_level)
    val percent = (level * 100f).toInt()
    val color = when {
        level > 0.92f -> AureaColors.Danger
        level > 0.75f -> AureaColors.Warning
        else -> AureaColors.Success
    }
    Box(
        Modifier
            .fillMaxWidth()
            .height(10.dp)
            .clip(RoundedCornerShape(5.dp))
            .background(AureaColors.Chip)
            .semantics {
                contentDescription = label
                stateDescription = "$percent%"
                progressBarRangeInfo = ProgressBarRangeInfo(level.coerceIn(0f, 1f), 0f..1f)
            }
            .testTag("voice.meter"),
    ) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(level.coerceIn(0f, 1f)).background(color))
    }
}

/** Linha inteira tocável com o interruptor (o leitor de tela lê nome + ligado/desligado). */
@Composable
private fun VoiceToggleRow(label: String, checked: Boolean, tag: String, onChange: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 44.dp)
            .toggleable(value = checked, role = Role.Switch, onValueChange = onChange)
            .testTag(tag),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
        AureaToggle(checked, onChange, Modifier.clearAndSetSemantics { })
    }
}

/** Antes do pedido do sistema: por que o microfone; negado de vez, o caminho das configurações. */
@Composable
private fun VoicePermission(blocked: Boolean, onAllow: () -> Unit, onSettings: () -> Unit, onCancel: () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        CupertinoIcon(CupertinoGlyph.Mic, 36.dp, AureaColors.Accent)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.voice_permission_title), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)))
        Spacer(Modifier.height(4.dp))
        Text(
            stringResource(if (blocked) R.string.voice_permission_blocked else R.string.voice_permission_body),
            modifier = Modifier.padding(horizontal = 8.dp).testTag("voice.permission.text"),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.sp, color = AureaColors.Muted, textAlign = TextAlign.Center)),
        )
        Spacer(Modifier.height(14.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            VoicePill(stringResource(R.string.common_cancel), AureaColors.Chip, AureaColors.Text, "voice.cancel", Modifier.weight(1f), onCancel)
            if (blocked) {
                VoicePill(stringResource(R.string.voice_open_settings), AureaColors.Action, AureaColors.Text, "voice.permission.settings", Modifier.weight(1f), onSettings)
            } else {
                VoicePill(stringResource(R.string.voice_permission_allow), AureaColors.Action, AureaColors.Text, "voice.permission.allow", Modifier.weight(1f), onAllow)
            }
        }
    }
}

@Composable
private fun VoicePill(label: String, fill: Color, ink: Color, tag: String, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(24.dp))
            .background(fill)
            .semantics { contentDescription = label }
            .tocavel(onClick = onClick)
            .padding(horizontal = 14.dp)
            .testTag(tag),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, maxLines = 2, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = ink, textAlign = TextAlign.Center)))
    }
}
