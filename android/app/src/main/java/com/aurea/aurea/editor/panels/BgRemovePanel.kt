package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay

// =============================================================================
//  "Remover fundo" (0.4.5, aurea.key.background_remove). O recorte por quadro
//  é do motor (EngineBgRemove.cpp: cache salvo com o projeto, estável no tempo);
//  aqui ficam a análise do clipe (progresso, cancelar), o estado deste quadro
//  e os avisos. Os controles de refino são as linhas normais do efeito.
//  Par iOS: BgRemoveCardTools (RotoPaintStage.swift).
// =============================================================================

internal const val BG_REMOVE_KEY = "aurea.key.background_remove"
internal val BG_REMOVE_TYPE_ID: Int by lazy { effectTypeId(BG_REMOVE_KEY) }
/** Índice do parâmetro "Modelo" (0 Automático, 1 Pessoa) no motor. */
private const val BG_MODEL_PARAM = 10

/** O aviso de falha pelo motivo do motor (DepthMapService::Failure). */
internal fun bgRemoveFailedText(reason: Long): Int = when (reason) {
    2L, 4L -> R.string.bg_remove_failed_model
    3L -> R.string.bg_remove_failed_decode
    else -> R.string.bg_remove_failed_frames
}

@Composable
private fun BgChip(label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .heightIn(min = 44.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.Text.copy(alpha = 0.18f) else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
    }
}

/** No topo do cartão do Remover fundo: análise do clipe, progresso e estado. */
@Composable
internal fun BgRemoveControls(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    val video = store.layers.firstOrNull { it.id == layer }?.let { LayerType.of(it.kind) == LayerType.Video } == true
    // [analisados, total, analisando, falhou, quadro pronto, prontos no clipe, quadros do clipe, modelo, válido]
    var status by remember(layer, effectId) { mutableStateOf(LongArray(9)) }
    LaunchedEffect(layer, effectId) {
        while (true) {
            val next = store.engineForStress.bgRemoveStatus(layer, effectId)
            // Render ocupado (válido = 0): fica o último estado, sem piscar.
            if (next[8] != 0L) status = next
            val busy = status[2] != 0L || status[4] == 0L
            delay(if (busy) 250 else 800)
        }
    }
    val muted = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Muted))
    val running = status[2] != 0L
    val clipFrames = status[6]
    val readyFrames = status[5]
    val requestedPerson = store.effectParams[effectId]?.firstOrNull { it.index == BG_MODEL_PARAM }?.value?.firstOrNull()?.toInt() == 1
    Column(Modifier.fillMaxWidth().padding(bottom = 6.dp)) {
        Text(stringResource(R.string.bg_remove_note), style = muted)
        if (status[8] != 0L) {
            val ready = status[4] != 0L
            Text(
                stringResource(if (ready) R.string.bg_remove_frame_ready else R.string.bg_remove_frame_pending),
                Modifier.padding(top = 6.dp).testTag("fx.bg.frame").semantics { liveRegion = LiveRegionMode.Polite },
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, fontWeight = FontWeight.W600,
                    color = if (ready) AureaColors.Success else AureaColors.Text)),
            )
            if (video && clipFrames > 1) {
                Text(
                    if (readyFrames >= clipFrames) stringResource(R.string.bg_remove_clip_done)
                    else stringResource(R.string.bg_remove_clip_status, readyFrames.toInt(), clipFrames.toInt()),
                    Modifier.padding(top = 2.dp).testTag("fx.bg.clip"), style = muted,
                )
            }
            if (requestedPerson && status[7] == 0L) Text(
                stringResource(R.string.bg_remove_person_unavailable),
                Modifier.padding(top = 2.dp).testTag("fx.bg.person"), style = muted,
            )
            if (status[3] != 0L && !running) Text(
                stringResource(bgRemoveFailedText(status[3])),
                Modifier.padding(top = 2.dp).testTag("fx.bg.failed"),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, color = AureaColors.Danger)),
            )
        }
        if (video) {
            val failedText = stringResource(R.string.bg_remove_failed)
            Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                val analyzeLabel = if (clipFrames > 1) stringResource(R.string.bg_remove_analyze_count, clipFrames.toInt())
                                   else stringResource(R.string.bg_remove_analyze)
                if (running) {
                    BgChip(stringResource(R.string.bg_remove_cancel), "fx.bg.cancel", true) {
                        store.engineForStress.bgRemoveCancel()
                        status = status.copyOf().also { it[2] = 0L }
                    }
                } else if (readyFrames < clipFrames || clipFrames <= 1) {
                    BgChip(analyzeLabel, "fx.bg.analyze", false) {
                        if (store.playing) store.pause()
                        if (store.engineForStress.bgRemoveAnalyze(layer, effectId))
                            status = status.copyOf().also { it[2] = 1L; it[0] = 0L; it[1] = maxOf(1L, clipFrames) }
                        else store.showToast(failedText)
                    }
                }
            }
            if (running && status[1] > 0) {
                val done = status[0].coerceIn(0L, status[1])
                val fraction = done.toFloat() / status[1].toFloat()
                val label = stringResource(R.string.bg_remove_progress, done.toInt(), status[1].toInt())
                LinearProgressIndicator(
                    progress = { fraction },
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp).testTag("fx.bg.progressBar").semantics {
                        contentDescription = label
                        progressBarRangeInfo = ProgressBarRangeInfo(fraction, 0f..1f)
                    },
                    color = AureaColors.Keyframe,
                    trackColor = AureaColors.Chip,
                )
                Text(label, Modifier.padding(top = 4.dp).testTag("fx.bg.progress"), style = muted)
            }
        }
    }
}
