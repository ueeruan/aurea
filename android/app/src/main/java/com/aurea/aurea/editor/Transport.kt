package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import kotlin.math.abs
import kotlin.math.roundToInt

// =============================================================================
// Barra de reprodução A.01 (46 dp)
// =============================================================================

/**
 * ↶ ↷ · [|◀ ▶ ▶|] centrado · copiar/colar · olho · tela cheia. Botões dos
 * lados com `clamp((W − 132)/6, 30, 40)` (num 320 os seis em 40 estouravam).
 * Enquanto um dedo manipula algo no palco, a barra vira a de informações.
 */
@Composable
internal fun TransportBar(store: EditorStore, ui: EditorUi) {
    if (ui.manipulating) {
        InfoBar(store)
        return
    }
    BoxWithConstraints(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.Transport)
            .background(AureaColors.EditorTopBar),
    ) {
        val side = ((maxWidth.value - 132f) / 6f).coerceIn(30f, 40f).dp
        val canUndo by remember { derivedStateOf { store.project.canUndo } }
        val canRedo by remember { derivedStateOf { store.project.canRedo } }
        // |◀ ▶| andam por KEYFRAME quando a camada escolhida tem marcas.
        val hasMarks by remember {
            derivedStateOf { store.primary?.let { !store.keyframes[it].isNullOrEmpty() } ?: false }
        }
        Row(Modifier.fillMaxHeight(), verticalAlignment = Alignment.CenterVertically) {
            ChromeButton(CupertinoGlyph.ArrowUturnLeft, "Desfazer", onClick = if (canUndo) ({ store.undo() }) else null, width = side, height = ShellDims.Transport)
            ChromeButton(CupertinoGlyph.ArrowUturnRight, "Refazer", onClick = if (canRedo) ({ store.redo() }) else null, width = side, height = ShellDims.Transport)
            Row(Modifier.weight(1f), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                ChromeButton(
                    CupertinoGlyph.BackwardEnd,
                    if (hasMarks) "Keyframe anterior · segure para o início" else "Um quadro atrás · segure para o início",
                    onClick = { if (!store.stepToKeyframe(-1)) store.step(-1) },
                    height = ShellDims.Transport,
                    onLongClick = { store.seek(0) },
                )
                PlayButton(store)
                ChromeButton(
                    CupertinoGlyph.ForwardEnd,
                    if (hasMarks) "Próximo keyframe · segure para o fim" else "Um quadro à frente · segure para o fim",
                    onClick = { if (!store.stepToKeyframe(1)) store.step(1) },
                    height = ShellDims.Transport,
                    onLongClick = { store.seek(store.project.durationFrames) },
                )
            }
            ChromeButton(CupertinoGlyph.DocOnClipboard, "Copiar e colar", onClick = { openSheet(store, ui, ShellSheet.CopyPaste) }, width = side, height = ShellDims.Transport)
            ChromeButton(CupertinoGlyph.Eye, "Opções de visualização", onClick = { store.comingSoon("Opções de visualização") }, width = side, height = ShellDims.Transport)
            ChromeButton(
                if (ui.fullscreen) CupertinoGlyph.FullscreenExit else CupertinoGlyph.Fullscreen,
                if (ui.fullscreen) "Sair da tela cheia" else "Tela cheia",
                onClick = { ui.fullscreen = !ui.fullscreen },
                width = side,
                height = ShellDims.Transport,
            )
        }
    }
}

/**
 * Play/pausa (ícone 26, alvo 52). Com repetição ligada o play fica em
 * destaque e ganha o selinho do laço — a A.01 usava `acao`, sem contraste
 * sobre o cromo (bug 27).
 */
@Composable
private fun PlayButton(store: EditorStore) {
    val playing = store.playing
    val loop = store.looping
    Box(contentAlignment = Alignment.Center) {
        ChromeButton(
            if (playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill,
            when {
                loop -> "Repetição ligada · segure para desligar"
                playing -> "Pausar"
                else -> "Reproduzir · segure para repetir"
            },
            onClick = { store.togglePlayback() },
            size = 26.dp,
            width = 52.dp,
            height = ShellDims.Transport,
            tint = if (loop) AureaColors.Accent else AureaColors.Text,
            onLongClick = { store.setLoop(!store.looping) },
        )
        if (loop) {
            CupertinoIcon(
                CupertinoGlyph.Repeat,
                11.dp,
                AureaColors.Accent,
                Modifier.align(Alignment.BottomEnd).padding(end = 8.dp, bottom = 8.dp),
            )
        }
    }
}

/**
 * A barra de informações: o número que está mudando fica onde o olho já
 * está (X, Y, escala, rotação da camada sob o dedo).
 */
@Composable
private fun InfoBar(store: EditorStore) {
    val d = store.detail
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.Transport)
            .background(AureaColors.EditorTopBar)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (d == null) return@Row
        InfoPair("X", d.position[0].roundToInt().toString())
        InfoPair("Y", d.position[1].roundToInt().toString())
        InfoPair("Escala", "${(d.scale[0] * 100f).roundToInt()}%")
        InfoPair("Rotação", "${"%.1f".format(java.util.Locale.ROOT, d.rotation[2]).replace('.', ',')}°")
    }
}

@Composable
private fun androidx.compose.foundation.layout.RowScope.InfoPair(label: String, value: String) {
    Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 10.5.sp, color = ShellColors.White40)))
        Text(
            value,
            maxLines = 1,
            overflow = TextOverflow.Clip,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)).merge(AureaType.Tabular),
        )
    }
}

// =============================================================================
// Barra de tempo da tela cheia (44 dp)
// =============================================================================

/**
 * Na tela cheia a timeline some; sem esta barra não havia como andar pelo
 * vídeo. Tocar pula para o ponto, arrastar percorre (scrub), e se estava
 * tocando volta a tocar ao soltar.
 */
@Composable
internal fun FullscreenTimeBar(store: EditorStore) {
    val haptic = LocalHapticFeedback.current
    var dragging by remember { mutableStateOf(false) }
    val style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600)).merge(AureaType.Tabular)
    Row(
        Modifier
            .fillMaxWidth()
            .height(ShellDims.FullscreenTimeBar)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(ShellTime.tenths(store.playhead, store.project.fps), style = style)
        Spacer(Modifier.width(12.dp))
        Canvas(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .pointerInput(store) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        fun frameAt(x: Float): Int {
                            val total = store.project.durationFrames
                            val f = if (size.width > 0) (x / size.width).coerceIn(0f, 1f) else 0f
                            return (f * (total - 1).coerceAtLeast(0)).roundToInt()
                        }
                        store.seek(frameAt(down.position.x))
                        val slop = viewConfiguration.touchSlop
                        var scrub = false
                        var wasPlaying = false
                        var travelled = 0f
                        while (true) {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!c.pressed) break
                            travelled += abs(c.positionChange().x)
                            if (!scrub && travelled > slop) {
                                scrub = true
                                wasPlaying = store.playing
                                if (wasPlaying) store.pause()
                                dragging = true
                                haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                store.scrubStart(frameAt(c.position.x))
                            } else if (scrub) {
                                store.scrubTo(frameAt(c.position.x))
                            }
                            c.consume()
                        }
                        if (scrub) {
                            store.scrubEnd()
                            dragging = false
                            if (wasPlaying) store.play()
                        }
                    }
                },
        ) {
            val total = store.project.durationFrames
            val f = if (total > 1) (store.playhead.toFloat() / (total - 1)).coerceIn(0f, 1f) else 0f
            val y = size.height / 2
            val thick = (if (dragging) 5.dp else 3.dp).toPx()
            val x = size.width * f
            drawLine(ShellColors.Track22, Offset(0f, y), Offset(size.width, y), thick, StrokeCap.Round)
            drawLine(AureaColors.Accent, Offset(0f, y), Offset(x, y), thick, StrokeCap.Round)
            drawCircle(AureaColors.Text, (if (dragging) 9.dp else 7.dp).toPx(), Offset(x, y))
        }
        Spacer(Modifier.width(12.dp))
        Text(ShellTime.tenths(store.project.durationFrames, store.project.fps), style = style.merge(TextStyle(color = AureaColors.Muted)))
    }
}

// =============================================================================
// Folha "Copiar e colar"
// =============================================================================

@Composable
internal fun CopyPasteSheet(store: EditorStore, onDismiss: () -> Unit) {
    val primary = store.primary
    val count = store.layers.size
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    ShellMenuSheet(onDismiss) {
        MenuSection("Copiar e colar")
        MenuItemRow(CupertinoGlyph.DocOnDoc, "Copiar camada", if (primary != null) act { store.comingSoon("Copiar camada") } else null)
        MenuItemRow(CupertinoGlyph.DocOnClipboard, "Colar camada no cabeçote", act { store.comingSoon("Colar camada") })
        MenuItemRow(
            CupertinoGlyph.PlusSquareOnSquare,
            "Duplicar camada",
            if (primary != null) act { store.duplicateLayers(listOf(primary)) } else null,
        )
        MenuItemRow(CupertinoGlyph.CheckmarkSquare, "Selecionar todas as camadas", if (count >= 2) act { store.selectAll() } else null)
        MenuItemRow(CupertinoGlyph.Square, "Limpar seleção", act { store.clearSelection() })
        MenuSection("Estilo e efeitos")
        MenuItemRow(CupertinoGlyph.Paintbrush, "Copiar estilo", if (primary != null) act { store.comingSoon("Copiar estilo") } else null)
        MenuItemRow(ShellGlyph.PaintbrushFill, "Colar estilo…", if (primary != null) act { store.comingSoon("Colar estilo") } else null)
        MenuItemRow(CupertinoGlyph.Sparkles, "Copiar efeitos", if (primary != null) act { store.comingSoon("Copiar efeitos") } else null)
        MenuItemRow(CupertinoGlyph.WandStars, "Colar efeitos", if (primary != null) act { store.comingSoon("Colar efeitos") } else null)
    }
}
