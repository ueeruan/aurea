package com.aurea.aurea.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

/**
 * SEQUÊNCIA DE IMAGENS (0.3.0): quadros numerados — os renders do Blender, do
 * After Effects ou do Cinema 4D — viram um clipe com transparência. O seletor
 * pede uma PASTA (o mais comum) ou os quadros/.zip; o motor acha o padrão
 * (render_####.png), os buracos e o formato, e esta folha mostra o que achou
 * antes de entrar na timeline. Par do iOS: `ImageSequenceSheet` (EditorView.swift).
 *
 * Devolve a ação que abre o seletor; [onPicked] roda quando algo foi escolhido
 * (o painel de adicionar fecha só então: o retorno do seletor precisa dele vivo).
 */
@Composable
internal fun rememberImageSequencePicker(store: EditorStore, asOverlay: Boolean = false, onPicked: () -> Unit = {}): () -> Unit {
    var choosing by remember { mutableStateOf(false) }
    val folder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
        if (tree != null) { store.prepareImageSequence(emptyList(), tree, asOverlay); onPicked() }
    }
    val files = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) { store.prepareImageSequence(uris, null, asOverlay); onPicked() }
    }
    if (choosing) {
        AureaActionSheet(
            title = stringResource(R.string.seq_title),
            message = stringResource(R.string.seq_hint),
            actions = listOf(
                SheetAction(stringResource(R.string.seq_pick_folder), tag = "seq.pick.folder") { folder.launch(null) },
                // EXR e TGA não têm MIME padrão em muitos provedores: o filtro é o motor.
                SheetAction(stringResource(R.string.seq_pick_files), tag = "seq.pick.files") {
                    files.launch(arrayOf("image/*", "application/zip", "application/octet-stream", "*/*"))
                },
            ),
            onDismiss = { choosing = false },
        )
    }
    return { if (store.playing) store.pause(); choosing = true }
}

/** A folha com o que a sondagem achou: taxa, transparência e cor; nada entra antes de "Importar". */
@Composable
internal fun ImageSequenceSheet(store: EditorStore, draft: EditorStore.ImageSequenceDraft) {
    val info = draft.info
    val suggested = info.optDouble("suggestedFps", 0.0).takeIf { it > 0 } ?: 24.0
    val rates = remember(suggested) { (listOf(suggested) + listOf(23.976, 24.0, 25.0, 30.0, 50.0, 60.0)).distinct() }
    var rate by remember { mutableIntStateOf(0) }
    var alpha by remember { mutableIntStateOf(0) }
    var color by remember { mutableIntStateOf(0) }
    val suggestedLabel = stringResource(R.string.seq_suggested)
    AureaModalSheet(onDismiss = { store.cancelImageSequence() }) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 6.dp).testTag("seq.sheet"),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(stringResource(R.string.seq_title), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
            Text(info.optString("pattern"), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontFamily = FontFamily.Monospace, color = AureaColors.Accent)),
                modifier = Modifier.testTag("seq.pattern"))
            Line(stringResource(R.string.seq_frames, info.optInt("frameCount"), info.optInt("first"), info.optInt("last")))
            val missing = info.optInt("missingCount")
            if (missing > 0) {
                val list = info.optJSONArray("missing")
                val shown = (0 until minOf(12, list?.length() ?: 0)).joinToString(", ") { list!!.optInt(it).toString() } +
                    if (missing > 12) "…" else ""
                Line(stringResource(R.string.seq_missing, missing, shown), AureaColors.Warning)
            }
            val transparency = stringResource(if (info.optBoolean("hasAlpha")) R.string.seq_alpha_yes else R.string.seq_alpha_no)
            Line(stringResource(R.string.seq_format, info.optInt("width"), info.optInt("height"),
                info.optString("format").uppercase(Locale.ROOT), info.optInt("bitDepth"), transparency))
            if (info.optInt("candidates") > 1) Line(stringResource(R.string.seq_candidates, info.optInt("candidates")))

            Section(stringResource(R.string.seq_rate))
            ChoiceChips(rates.mapIndexed { i, r -> fpsLabel(r) + if (i == 0) " ($suggestedLabel)" else "" }, rate, { rate = it },
                Modifier.testTag("seq.rate"))
            Section(stringResource(R.string.seq_alpha_mode))
            ChoiceChips(listOf(stringResource(R.string.seq_auto), stringResource(R.string.seq_alpha_straight),
                stringResource(R.string.seq_alpha_premultiplied), stringResource(R.string.seq_alpha_ignore)), alpha, { alpha = it })
            Section(stringResource(R.string.seq_color_mode))
            ChoiceChips(listOf(stringResource(R.string.seq_auto), stringResource(R.string.seq_color_srgb),
                stringResource(R.string.seq_color_linear)), color, { color = it })
            Text(stringResource(R.string.seq_auto_note), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))

            Spacer(Modifier.height(10.dp))
            Row(Modifier.fillMaxWidth().padding(bottom = 10.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                SheetButton(stringResource(R.string.common_cancel), primary = false, tag = "seq.cancel", modifier = Modifier.weight(1f)) {
                    store.cancelImageSequence()
                }
                SheetButton(stringResource(R.string.seq_import), primary = true, tag = "seq.import", modifier = Modifier.weight(1f)) {
                    store.importImageSequence(if (rate == 0) 0.0 else rates[rate], alpha, color)
                }
            }
        }
    }
}

private fun fpsLabel(fps: Double): String =
    if (kotlin.math.abs(fps - Math.rint(fps)) < 1e-6) fps.toInt().toString() else String.format(Locale.ROOT, "%.3f", fps).trimEnd('0')

@Composable
private fun Line(text: String, tint: androidx.compose.ui.graphics.Color = AureaColors.Text) {
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = tint)))
}

@Composable
private fun Section(text: String) {
    Text(text, modifier = Modifier.padding(top = 10.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
}

@Composable
private fun SheetButton(label: String, primary: Boolean, tag: String, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier.height(46.dp).clip(RoundedCornerShape(12.dp))
            .background(if (primary) AureaColors.Accent else AureaColors.Chip)
            .testTag(tag).tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600,
            color = if (primary) AureaColors.OnAccent else AureaColors.Text)))
    }
}
