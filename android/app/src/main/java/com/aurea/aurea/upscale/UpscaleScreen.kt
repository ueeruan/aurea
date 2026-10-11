package com.aurea.aurea.upscale

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.min

// =============================================================================
//  Upscale com IA — ferramenta própria, fora da aba Exportar.
//  A pessoa entra, escolhe um vídeo da galeria, o fator (2× ou 4×) e toca em
//  Melhorar: o store cria um projeto com o tamanho do vídeo, importa e exporta
//  com a rede neural (EditorStore.upscaleVideo). O progresso é o do Exporter;
//  o resultado vai para Filmes/Aurea, com Abrir e Compartilhar.
// =============================================================================

@Composable
fun UpscaleScreen(store: EditorStore) {
    val exporter = store.exporter
    val st = exporter.state
    val stage = store.upscaleStage
    val context = LocalContext.current
    var picked by rememberSaveable { mutableStateOf<String?>(null) }
    var factor by rememberSaveable { mutableIntStateOf(2) }
    val busy = stage == EditorStore.UpscaleStage.Preparing || exporter.busy
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) { picked = uri.toString(); exporter.reset(); store.upscaleReset() }
    }
    val muted = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted))

    Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
        Row(Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).testTag("upscale.voltar").tocavel(enabled = !busy, onClick = { store.closeUpscale() }),
                contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, if (!busy) AureaColors.Text else AureaColors.Disabled)
            }
            Text(stringResource(R.string.upscale_title), style = AureaType.TitleMedium, modifier = Modifier.padding(start = 4.dp))
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 18.dp)) {
            when {
                st.phase == ExportPhase.Running || st.phase == ExportPhase.Publishing -> {
                    Spacer(Modifier.height(24.dp))
                    Text(stringResource(R.string.upscale_processing), style = AureaType.TitleLarge)
                    Spacer(Modifier.height(8.dp))
                    Text(stringResource(R.string.upscale_time_note), style = muted)
                    Spacer(Modifier.height(18.dp))
                    LinearProgressIndicator(progress = st.fraction, modifier = Modifier.fillMaxWidth().testTag("upscale.progresso"),
                        color = AureaColors.Accent, trackColor = AureaColors.Surface)
                    Spacer(Modifier.height(10.dp))
                    val percent = (st.fraction * 100).toInt()
                    Text(
                        if (st.framesTotal > 0) "$percent% · ${st.framesDone}/${st.framesTotal}" +
                            (if (st.etaSeconds > 0) " · ${formatEta(st.etaSeconds)}" else "") else "$percent%",
                        style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
                    )
                    if (st.notice.isNotBlank()) { Spacer(Modifier.height(8.dp)); Text(st.notice, style = muted) }
                    Spacer(Modifier.height(24.dp))
                    UpscaleButton(stringResource(R.string.common_cancel), "upscale.cancelar", primary = false) { exporter.cancel() }
                }
                st.phase == ExportPhase.Done -> {
                    Spacer(Modifier.height(24.dp))
                    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
                        CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 44.dp, AureaColors.Success)
                        Spacer(Modifier.height(10.dp))
                        Text(stringResource(R.string.upscale_done), style = AureaType.TitleLarge)
                        Spacer(Modifier.height(6.dp))
                        Text(st.message, style = muted, textAlign = TextAlign.Center)
                    }
                    Spacer(Modifier.height(24.dp))
                    val noViewer = stringResource(R.string.editor_nenhum_app_abre_video)
                    UpscaleButton(stringResource(R.string.upscale_open), "upscale.abrir") {
                        val uri = st.outputUri ?: return@UpscaleButton
                        val view = Intent(Intent.ACTION_VIEW).setDataAndType(uri, "video/mp4").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        runCatching { context.startActivity(view) }.onFailure { store.showToast(noViewer) }
                    }
                    Spacer(Modifier.height(10.dp))
                    UpscaleButton(stringResource(R.string.upscale_share), "upscale.compartilhar", primary = false) {
                        exporter.shareIntent()?.let { runCatching { context.startActivity(Intent.createChooser(it, null)) } }
                    }
                    Spacer(Modifier.height(10.dp))
                    UpscaleButton(stringResource(R.string.upscale_again), "upscale.outro", primary = false) {
                        picked = null; exporter.reset(); store.upscaleReset()
                    }
                }
                else -> {
                    Spacer(Modifier.height(16.dp))
                    Text(stringResource(R.string.upscale_hint), style = muted)
                    Spacer(Modifier.height(16.dp))
                    Box(
                        Modifier.fillMaxWidth().heightIn(min = 120.dp).clip(RoundedCornerShape(14.dp)).background(AureaColors.Surface)
                            .testTag("upscale.escolher")
                            .tocavel(enabled = !busy) { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly)) }
                            .padding(18.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            CupertinoIcon(CupertinoGlyph.PhotoOnRectangle, 30.dp, AureaColors.Accent)
                            Spacer(Modifier.height(8.dp))
                            Text(picked?.let { store.upscaleSourceName ?: stringResource(R.string.upscale_pick) } ?: stringResource(R.string.upscale_pick),
                                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
                                textAlign = TextAlign.Center)
                        }
                    }
                    Spacer(Modifier.height(18.dp))
                    Text(stringResource(R.string.upscale_factor), style = muted)
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (f in intArrayOf(2, 4)) FactorChip("${f}×", "upscale.fator.$f", factor == f, enabled = !busy) { factor = f }
                    }
                    val size = store.upscaleSourceSize
                    if (size != null) {
                        Spacer(Modifier.height(8.dp))
                        Text(stringResource(R.string.upscale_result, size.first, size.second, size.first * factor, size.second * factor), style = muted)
                    }
                    if (stage == EditorStore.UpscaleStage.Preparing) {
                        Spacer(Modifier.height(18.dp))
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth(), color = AureaColors.Accent, trackColor = AureaColors.Surface)
                        Spacer(Modifier.height(8.dp))
                        Text(stringResource(R.string.upscale_preparing), style = muted)
                    }
                    if (st.phase == ExportPhase.Failed || st.phase == ExportPhase.Cancelled || stage == EditorStore.UpscaleStage.Failed) {
                        Spacer(Modifier.height(14.dp))
                        Text(
                            st.message.ifBlank { stringResource(R.string.import_failed) },
                            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = if (st.phase == ExportPhase.Cancelled) AureaColors.Warning else AureaColors.Danger)),
                            modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Surface).padding(14.dp).testTag("upscale.erro"),
                        )
                    }
                    Spacer(Modifier.height(24.dp))
                    Text(stringResource(R.string.upscale_time_note), style = muted)
                    Spacer(Modifier.height(14.dp))
                    UpscaleButton(stringResource(R.string.upscale_start), "upscale.iniciar", enabled = picked != null && !busy) {
                        picked?.let { store.upscaleVideo(Uri.parse(it), factor) }
                    }
                }
            }
            Spacer(Modifier.height(24.dp))
        }
    }
}

private fun formatEta(seconds: Int): String {
    val m = seconds / 60; val s = seconds % 60
    return if (m > 0) "${m}m ${s}s" else "${s}s"
}

@Composable
private fun FactorChip(label: String, tag: String, on: Boolean, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.Accent.copy(alpha = 0.22f) else AureaColors.Chip)
            .testTag(tag).semantics { role = Role.Button; contentDescription = label; selected = on }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick).padding(horizontal = 18.dp, vertical = 10.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600,
            color = if (on) AureaColors.Text else AureaColors.Muted)))
    }
}

@Composable
private fun UpscaleButton(label: String, tag: String, primary: Boolean = true, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(12.dp))
            .background(when { !enabled -> AureaColors.Surface; primary -> AureaColors.Accent; else -> AureaColors.Chip })
            .testTag(tag).semantics { role = Role.Button; contentDescription = label }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600,
            color = when { !enabled -> AureaColors.Disabled; primary -> AureaColors.OnAccent; else -> AureaColors.Text })))
    }
}
