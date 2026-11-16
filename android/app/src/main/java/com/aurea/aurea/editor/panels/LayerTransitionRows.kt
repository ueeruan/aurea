package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/** Os tipos do motor (`Layer::transitionIn/Out`, `Engine::set_transition`), na ordem. */
private val TransitionNames = intArrayOf(
    R.string.ae_transition_none, R.string.ae_transition_dissolve, R.string.ae_transition_slide_up,
    R.string.ae_transition_slide_left, R.string.ae_transition_zoom, R.string.ae_transition_spin,
)

/** As durações oferecidas, em segundos (o motor prende em [1 quadro, metade do trecho]). */
private val TransitionSeconds = floatArrayOf(0.25f, 0.5f, 1f, 2f)

/**
 * TRANSIÇÕES DA CAMADA (entrada e saída), no painel de aparência: o motor já as
 * desenhava (e o modo Edição as usa nos cortes), mas só o corte tinha controle.
 * Fechado = uma linha com o resumo; aberto = tipo e duração de cada ponta.
 */
@Composable
internal fun LayerTransitionRows(store: EditorStore) {
    val detail = store.detail ?: return
    if (detail.kind == LayerType.Audio.kind) return
    val id = detail.id
    var open by rememberSaveable { mutableStateOf(false) }
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val title = stringResource(R.string.ae_transitions)
    val inName = stringResource(TransitionNames[detail.transitionIn.coerceIn(0, 5)])
    val outName = stringResource(TransitionNames[detail.transitionOut.coerceIn(0, 5)])
    fun secondsOf(frames: Int) = numeroPtBr(frames / fps, 2) + " s"
    val summary = buildString {
        append(stringResource(R.string.ae_transition_in)).append(" · ").append(inName)
        if (detail.transitionIn != 0) append(" ").append(secondsOf(detail.transitionInFrames))
        append("   ")
        append(stringResource(R.string.ae_transition_out)).append(" · ").append(outName)
        if (detail.transitionOut != 0) append(" ").append(secondsOf(detail.transitionOutFrames))
    }
    Column(Modifier.fillMaxWidth().padding(horizontal = 10.dp)) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(44.dp)
                .testTag("layer.transitions")
                .semantics {
                    contentDescription = title
                    stateDescription = summary
                }
                .tocavel { open = !open },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 16.dp, AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
            Spacer(Modifier.width(10.dp))
            Text(
                summary,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
        if (open) {
            TransitionEnd(store, id, out = false, type = detail.transitionIn, frames = detail.transitionInFrames, fps = fps,
                locked = detail.locked)
            TransitionEnd(store, id, out = true, type = detail.transitionOut, frames = detail.transitionOutFrames, fps = fps,
                locked = detail.locked)
            KitHint(stringResource(R.string.ae_transition_hint))
            Spacer(Modifier.height(6.dp))
        }
    }
}

@Composable
private fun TransitionEnd(store: EditorStore, id: Long, out: Boolean, type: Int, frames: Int, fps: Float, locked: Boolean) {
    val tag = if (out) "layer.transition.out" else "layer.transition.in"
    val label = stringResource(if (out) R.string.ae_transition_out else R.string.ae_transition_in)
    val half = (0.5f * fps).roundToInt().coerceAtLeast(1)
    Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    ChipRow {
        TransitionNames.forEachIndexed { t, res ->
            Box(Modifier.testTag("$tag.type.$t")) {
                KitChip(stringResource(res), on = type == t) {
                    if (!locked) store.setLayerTransition(id, out, t, if (frames > 0) frames else half)
                }
            }
        }
    }
    if (type != 0) {
        ChipRow {
            Text(
                stringResource(R.string.ae_transition_duration),
                modifier = Modifier.padding(end = 6.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
            TransitionSeconds.forEach { s ->
                val f = (s * fps).roundToInt().coerceAtLeast(1)
                Box(Modifier.testTag("$tag.seconds.$s")) {
                    KitChip(numeroPtBr(s, 2) + " s", on = frames == f) {
                        if (!locked) store.setLayerTransition(id, out, type, f)
                    }
                }
            }
        }
    }
}
