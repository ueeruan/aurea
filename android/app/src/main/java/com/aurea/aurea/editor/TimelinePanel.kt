package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.ui.AureaColors
import com.aurea.aurea.ui.layerColor
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * A timeline.
 *
 * ORGANIZAÇÃO, igual à do Alight Motion: a régua de tempo corre na horizontal,
 * as camadas ficam empilhadas na vertical, e a lista de nomes ocupa uma coluna
 * fixa à esquerda. A ORDEM VERTICAL É A ORDEM DE COMPOSIÇÃO — o que está em
 * cima aparece na frente —, e é o que o usuário vê no preview.
 *
 * A coluna de nomes NÃO rola na horizontal junto com as barras: ela é fixa, e
 * as barras deslizam por baixo. Sem isso, arrastar para ver o fim do projeto
 * levaria os nomes embora e o usuário perderia a referência de qual barra é
 * qual.
 *
 * Como no Alight Motion, arrastar a BORDA de uma barra faz o corte (trim) e
 * arrastar o MEIO move a camada no tempo. São gestos distintos porque as
 * intenções são distintas, e misturá-los num só produziria corte acidental ao
 * tentar mover.
 */
@Composable
fun TimelinePanel(
    state: EditorUiState,
    onScrub: (Int) -> Unit,
    onSelectLayer: (Long, Boolean) -> Unit,
    onToggleVisibility: (Long, Boolean) -> Unit,
    onTrimLayer: (Long, Int, Int) -> Unit,
    onBeginGesture: (String) -> Unit,
    onEndGesture: () -> Unit,
    onReorder: (Long, Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val rowHeight = 40.dp
    val rulerHeight = 22.dp
    val nameColumnWidth = 132.dp
    val pixelsPerFrame = state.timelineZoom

    val horizontalScroll = rememberScrollState()
    val listState = rememberLazyListState()

    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(AureaColors.Background),
    ) {
        // ---------------------------------------------------------------------
        // Cabeçalho: régua de tempo.
        // ---------------------------------------------------------------------
        Row(modifier = Modifier.fillMaxWidth()) {
            // Canto superior esquerdo: contador de frames. Ocupa o lugar do
            // cabeçalho da coluna de nomes, então tem que ter a mesma largura.
            Box(
                modifier = Modifier
                    .width(nameColumnWidth)
                    .height(rulerHeight)
                    .background(AureaColors.Surface)
                    .padding(horizontal = 6.dp),
                contentAlignment = Alignment.CenterStart,
            ) {
                Text(
                    text = "${state.playheadFrame}/${state.totalFrames}",
                    style = MaterialTheme.typography.labelSmall,
                    color = AureaColors.OnSurfaceMuted,
                )
            }

            TimeRuler(
                totalFrames = state.totalFrames,
                fps = state.fps,
                pixelsPerFrame = pixelsPerFrame,
                playheadFrame = state.playheadFrame,
                scrollState = horizontalScroll,
                rulerHeight = rulerHeight,
                onScrub = onScrub,
            )
        }

        // ---------------------------------------------------------------------
        // Corpo: nomes de um lado, barras do outro.
        // ---------------------------------------------------------------------
        if (state.layers.isEmpty()) {
            EmptyTimeline(message = "Nenhuma camada. Use + para adicionar vídeo ou texto.")
            return@Column
        }

        Row(modifier = Modifier.fillMaxSize()) {
            // Coluna de nomes. Rola na vertical junto com as barras (mesma lista),
            // mas não na horizontal.
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .width(nameColumnWidth)
                    .fillMaxHeight()
                    .background(AureaColors.Surface),
            ) {
                items(state.layers, key = { it.id }) { layer ->
                    LayerNameRow(
                        layer = layer,
                        selected = state.selectedIds.contains(layer.id),
                        height = rowHeight,
                        onSelect = { additive -> onSelectLayer(layer.id, additive) },
                        onToggleVisibility = { onToggleVisibility(layer.id, !layer.visible) },
                    )
                }
            }

            // Barras. Rola na horizontal com a régua e na vertical com os nomes:
            // as duas listas compartilham o estado vertical, então nunca
            // desalinham.
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .horizontalScroll(horizontalScroll),
            ) {
                items(state.layers, key = { it.id }) { layer ->
                    LayerBarRow(
                        layer = layer,
                        selected = state.selectedIds.contains(layer.id),
                        height = rowHeight,
                        pixelsPerFrame = pixelsPerFrame,
                        totalFrames = state.totalFrames,
                        onSelect = { additive -> onSelectLayer(layer.id, additive) },
                        onTrim = { start, end -> onTrimLayer(layer.id, start, end) },
                        onBeginGesture = { onBeginGesture("mover camada") },
                        onEndGesture = onEndGesture,
                    )
                }
            }
        }
    }
}

/**
 * Régua de tempo.
 *
 * O passo entre marcações é escolhido para que elas fiquem a pelo menos 56 dp
 * uma da outra: com zoom afastado, marcar cada segundo produziria um borrão de
 * traços; com zoom aproximado, marcar de 10 em 10 segundos deixaria o usuário
 * sem referência. O passo é calculado, não fixo.
 */
@Composable
private fun TimeRuler(
    totalFrames: Int,
    fps: Float,
    pixelsPerFrame: Float,
    playheadFrame: Int,
    scrollState: androidx.compose.foundation.ScrollState,
    rulerHeight: androidx.compose.ui.unit.Dp,
    onScrub: (Int) -> Unit,
) {
    val minSpacingPx = 56f
    val frameStep = remember(pixelsPerFrame, fps) {
        val candidates = listOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600)
        candidates.firstOrNull { it * pixelsPerFrame >= minSpacingPx } ?: 3600
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(rulerHeight)
            .horizontalScroll(scrollState)
            .pointerInput(totalFrames, pixelsPerFrame) {
                detectTapGestures { offset ->
                    // Toque na régua = posicionar o playhead. É o gesto de
                    // scrubbing mais direto, e não precisa de arrasto.
                    onScrub((offset.x / pixelsPerFrame).roundToInt().coerceIn(0, totalFrames))
                }
            },
    ) {
        Canvas(
            modifier = Modifier
                .width((totalFrames * pixelsPerFrame + 240f).dp.coerceAtLeast(1.dp))
                .fillMaxHeight(),
        ) {
            val heightPx = size.height
            val widthPx = size.width

            // Traços da régua.
            var frame = 0
            while (frame <= totalFrames) {
                val x = frame * pixelsPerFrame
                if (x > widthPx) break
                val isMajor = (frame % (frameStep * 5)) == 0
                drawLine(
                    color = if (isMajor) AureaColors.OnSurfaceFaint else AureaColors.Outline,
                    start = Offset(x, if (isMajor) heightPx * 0.35f else heightPx * 0.6f),
                    end = Offset(x, heightPx),
                    strokeWidth = if (isMajor) 1.5f else 1f,
                )
                frame += frameStep
            }

            // Playhead. Desenhado por último para ficar por cima dos traços.
            val playheadX = playheadFrame * pixelsPerFrame
            drawLine(
                color = AureaColors.Playhead,
                start = Offset(playheadX, 0f),
                end = Offset(playheadX, heightPx),
                strokeWidth = 2f,
            )
            drawCircle(
                color = AureaColors.Playhead,
                radius = heightPx * 0.28f,
                center = Offset(playheadX, heightPx * 0.28f),
            )
        }
    }
}

/** Uma linha da coluna de nomes: tipo, nome, visibilidade. */
@Composable
private fun LayerNameRow(
    layer: LayerRow,
    selected: Boolean,
    height: androidx.compose.ui.unit.Dp,
    onSelect: (Boolean) -> Unit,
    onToggleVisibility: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(height)
            .background(if (selected) AureaColors.Selection else Color.Transparent)
            .clickable(onClick = { onSelect(false) })
            .padding(start = 8.dp, end = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Faixa de cor por tipo: identifica a camada num relance, sem ler o nome.
        Box(
            modifier = Modifier
                .width(3.dp)
                .height(height * 0.55f)
                .clip(RoundedCornerShape(2.dp))
                .background(layerColor(layer.kind)),
        )

        Spacer(Modifier.width(7.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = layer.name,
                style = MaterialTheme.typography.bodySmall,
                color = if (layer.visible) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                maxLines = 1,
            )
            if (layer.hasParent) {
                Text(
                    text = "vinculada",
                    style = MaterialTheme.typography.labelSmall,
                    color = AureaColors.OnSurfaceFaint,
                )
            }
        }

        if (layer.locked) {
            Icon(
                Icons.Filled.Lock,
                contentDescription = "Travada",
                tint = AureaColors.OnSurfaceFaint,
                modifier = Modifier
                    .padding(end = 2.dp)
                    .size(15.dp),
            )
        }

        Icon(
            imageVector = if (layer.visible) Icons.Filled.Visibility else Icons.Filled.VisibilityOff,
            contentDescription = if (layer.visible) "Ocultar" else "Mostrar",
            tint = if (layer.visible) AureaColors.OnSurfaceMuted else AureaColors.OnSurfaceFaint,
            modifier = Modifier
                .size(30.dp)
                .clickable(onClick = onToggleVisibility)
                .padding(6.dp),
        )
    }
}

/**
 * A barra de uma camada na timeline.
 *
 * POSIÇÃO E DURAÇÃO vêm do motor, em frames, não em pixels: a UI converte. Se a
 * UI guardasse pixels, mudar o zoom exigiria reescrever o modelo — e o zoom é
 * uma decisão de visualização, não do projeto.
 *
 * OS HANDLES de corte aparecem só na camada selecionada. Mostrá-los em todas
 * deixaria a timeline coberta de alças e o toque no meio da barra (que é o
 * gesto de MOVER) competiria com as bordas.
 */
@Composable
private fun LayerBarRow(
    layer: LayerRow,
    selected: Boolean,
    height: androidx.compose.ui.unit.Dp,
    pixelsPerFrame: Float,
    totalFrames: Int,
    onSelect: (Boolean) -> Unit,
    onTrim: (Int, Int) -> Unit,
    onBeginGesture: () -> Unit,
    onEndGesture: () -> Unit,
) {
    var start = layer.startFrame
    var end = layer.endFrame

    val handleWidthPx = 22f
    val barColor = layerColor(layer.kind)

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(height),
    ) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(layer.id, pixelsPerFrame, totalFrames) {
                    detectDragGestures(
                        onDragStart = { offset ->
                            onSelect(false)
                            // Qual gesto é decidido pela POSIÇÃO do toque: nas
                            // bordas é corte, no meio é movimento.
                            val localStart = start * pixelsPerFrame
                            val localEnd = end * pixelsPerFrame
                            val onLeft = abs(offset.x - localStart) < handleWidthPx
                            val onRight = abs(offset.x - localEnd) < handleWidthPx
                            dragMode = when {
                                onLeft -> DragMode.TrimStart
                                onRight -> DragMode.TrimEnd
                                else -> DragMode.Move
                            }
                            onBeginGesture()
                        },
                        onDragEnd = { onEndGesture() },
                        onDragCancel = { onEndGesture() },
                        onDrag = { change, dragAmount ->
                            change.consume()
                            val deltaFrames = (dragAmount.x / pixelsPerFrame).roundToInt()
                            if (deltaFrames == 0) return@detectDragGestures

                            when (dragMode) {
                                DragMode.TrimStart -> {
                                    // O início nunca passa do fim: uma camada de
                                    // duração negativa seria um estado inválido
                                    // que o motor recusa.
                                    val next = (start + deltaFrames).coerceIn(0, end - 1)
                                    start = next
                                }
                                DragMode.TrimEnd -> {
                                    val next = (end + deltaFrames).coerceIn(start + 1, totalFrames)
                                    end = next
                                }
                                DragMode.Move -> {
                                    val len = end - start
                                    val nextStart = (start + deltaFrames).coerceIn(0, totalFrames - len)
                                    start = nextStart
                                    end = nextStart + len
                                }
                            }
                            onTrim(start, end)
                        },
                    )
                }
                .pointerInput(layer.id) {
                    detectTapGestures(onLongPress = { onSelect(true) })
                },
        ) {
            val x = start * pixelsPerFrame
            val w = (end - start) * pixelsPerFrame
            val barHeight = size.height * 0.76f
            val top = (size.height - barHeight) * 0.5f

            // Barra.
            drawRoundRect(
                color = barColor,
                topLeft = Offset(x, top),
                size = Size(w.coerceAtLeast(2f), barHeight),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(5f, 5f),
            )

            // Borda de seleção. É o que diz "esta é a camada que o painel de
            // propriedades está editando".
            if (selected) {
                drawRoundRect(
                    color = AureaColors.SelectionBorder,
                    topLeft = Offset(x, top),
                    size = Size(w.coerceAtLeast(2f), barHeight),
                    cornerRadius = androidx.compose.ui.geometry.CornerRadius(5f, 5f),
                    style = androidx.compose.ui.graphics.drawscope.Stroke(width = 2f),
                )
            }

            // Alças de corte, só na selecionada.
            if (selected && w > handleWidthPx * 2.2f) {
                drawRoundRect(
                    color = AureaColors.OnSurface,
                    topLeft = Offset(x + 2f, top + barHeight * 0.25f),
                    size = Size(3f, barHeight * 0.5f),
                )
                drawRoundRect(
                    color = AureaColors.OnSurface,
                    topLeft = Offset(x + w - 5f, top + barHeight * 0.25f),
                    size = Size(3f, barHeight * 0.5f),
                )
            }

            // Faixa de keyframes: marca onde a camada tem animação. É a
            // informação que diz "mexer aqui muda ao longo do tempo" sem o
            // usuário precisar abrir o painel.
            if (layer.animated && layer.keyframeCount > 0 && w > 8f) {
                val kfY = top + barHeight - 4f
                drawRect(
                    color = AureaColors.Keyframe,
                    topLeft = Offset(x + 3f, kfY),
                    size = Size((w - 6f).coerceAtLeast(1f), 2f),
                )
            }
        }
    }
}

private enum class DragMode { Move, TrimStart, TrimEnd }

/** Variável de gesto do arrasto. Guardada fora da composição para não
 *  recompor a cada movimento do dedo — recompor a 120 Hz durante um arrasto
 *  seria trabalho puro. */
private var dragMode: DragMode = DragMode.Move

@Composable
private fun EmptyTimeline(message: String) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = message,
            style = MaterialTheme.typography.bodySmall,
            color = AureaColors.OnSurfaceFaint,
        )
    }
}
