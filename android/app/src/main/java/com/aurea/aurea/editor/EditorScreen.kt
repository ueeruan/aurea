package com.aurea.aurea.editor

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.Redo
import androidx.compose.material.icons.filled.Undo
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.aurea.aurea.ui.AureaColors
import com.aurea.aurea.ui.layerKindName

/**
 * O editor.
 *
 * A ORGANIZAÇÃO SEGUE O FLUXO DO ALIGHT MOTION, porque é o que o usuário já
 * conhece: preview grande em cima ocupando a maior parte da tela, timeline
 * logo abaixo com as camadas empilhadas verticalmente e o tempo correndo na
 * horizontal, e uma barra de ferramentas entre os dois. O usuário que já usou
 * um editor de celular encontra tudo onde espera.
 *
 * SUPERFÍCIES INDEPENDENTES: o `SurfaceView` desenha o vídeo; o Compose desenha
 * a interface por cima. São duas superfícies de verdade — é por isso que o
 * preview roda a 60 fps mesmo quando a timeline rola, e é por isso que nenhum
 * bitmap de vídeo entra no heap gerenciado.
 *
 * Nenhum elemento desta tela processa frame.
 */
@Composable
fun EditorScreen(viewModel: EditorViewModel) {
    val ui = viewModel.ui
    val context = LocalContext.current

    var surfaceView by remember { mutableStateOf<SurfaceView?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(AureaColors.Background),
    ) {
        TopBar(viewModel)

        // ---------------------------------------------------------------------
        // Preview
        //
        // A proporção da superfície segue a da composição: um preview quadrado
        // numa composição 16:9 mostraria barras pretas e desperdiçaria metade da
        // área útil no aparelho.
        // ---------------------------------------------------------------------
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .padding(horizontal = 8.dp),
            contentAlignment = Alignment.Center,
        ) {
            AndroidView(
                factory = { ctx ->
                    SurfaceView(ctx).also { view ->
                        view.holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(holder: SurfaceHolder) {
                                val metrics = ctx.resources.displayMetrics
                                val refresh = ctx.display?.refreshRate ?: 60f
                                viewModel.attachSurface(
                                    holder.surface,
                                    view.width.coerceAtLeast(1),
                                    view.height.coerceAtLeast(1),
                                    refresh,
                                )
                                @Suppress("UNUSED_EXPRESSION") metrics
                            }

                            override fun surfaceChanged(
                                holder: SurfaceHolder, format: Int, width: Int, height: Int,
                            ) {
                                // Rotação e split-screen passam por aqui. Rotear
                                // para o motor é o que evita o preview esticar.
                                viewModel.resizeSurface(width, height)
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                viewModel.detachSurface()
                            }
                        })
                        surfaceView = view
                    }
                },
                modifier = Modifier.fillMaxSize(),
            )

            // Sobreposição: só aparece quando o motor NÃO está pronto. Uma tela
            // preta sem explicação faria o usuário achar que o app travou.
            if (!ui.engineReady) {
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(AureaColors.SurfaceHighest)
                        .padding(20.dp),
                ) {
                    Text(
                        text = "Preparando o motor gráfico…",
                        style = MaterialTheme.typography.bodyMedium,
                        color = AureaColors.OnSurfaceMuted,
                    )
                }
            }
        }

        PlayerBar(viewModel)

        // ---------------------------------------------------------------------
        // Timeline
        // ---------------------------------------------------------------------
        TimelinePanel(
            state = ui,
            onScrub = { frame -> viewModel.seekTo(frame) },
            onSelectLayer = { id, additive -> viewModel.select(id, additive) },
            onToggleVisibility = { id, visible -> viewModel.toggleLayerVisibility(id, visible) },
            onTrimLayer = { id, start, end -> viewModel.moveLayerInTime(id, start, end) },
            onBeginGesture = { label -> viewModel.beginGesture(label) },
            onEndGesture = { viewModel.endGesture() },
            onReorder = { id, index -> viewModel.reorderLayer(id, index) },
            modifier = Modifier.height(if (ui.layers.isEmpty()) 132.dp else 260.dp),
        )

        LayerToolbar(viewModel)

        InspectorPanel(viewModel)
    }

    ui.errorMessage?.let { message ->
        ErrorDialog(message = message, onDismiss = { viewModel.dismissError() })
    }
}

/**
 * Barra superior: voltar, título e desfazer/refazer.
 *
 * O título mostra um ponto quando há alteração não salva. É o sinal mais barato
 * de "você vai perder isso se sair" — e o mais fácil de ignorar, por isso o
 * app também guarda o journal de autosave.
 */
@Composable
private fun TopBar(viewModel: EditorViewModel) {
    val ui = viewModel.ui

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(AureaColors.Surface)
            .padding(horizontal = 4.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = { viewModel.closeProject() }) {
            Icon(
                Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = "Voltar",
                tint = AureaColors.OnSurface,
            )
        }

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = ui.projectTitle + if (ui.dirty) " •" else "",
                style = MaterialTheme.typography.bodyLarge,
                color = AureaColors.OnSurface,
                fontWeight = FontWeight.Medium,
            )
            Text(
                text = previewSummary(ui),
                style = MaterialTheme.typography.labelSmall,
                color = AureaColors.OnSurfaceFaint,
            )
        }

        IconButton(onClick = { viewModel.undo() }, enabled = ui.canUndo) {
            Icon(
                Icons.Filled.Undo,
                contentDescription = "Desfazer",
                tint = if (ui.canUndo) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
            )
        }
        IconButton(onClick = { viewModel.redo() }, enabled = ui.canRedo) {
            Icon(
                Icons.Filled.Redo,
                contentDescription = "Refazer",
                tint = if (ui.canRedo) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
            )
        }
    }
}

private fun previewSummary(ui: EditorUiState): String {
    val res = if (ui.previewWidth > 0) "${ui.previewWidth}×${ui.previewHeight}" else "—"
    val fps = if (ui.currentFps > 0f) "%.0f fps".format(ui.currentFps) else "—"
    return "$res · $fps · preview ${ui.previewScaleLabel}"
}

/**
 * Barra de reprodução: play/pause, contador de frames e seletor de qualidade
 * do preview.
 *
 * O seletor de qualidade fica AQUI, ao lado do play, e não escondido num menu.
 * É a decisão que o usuário precisa tomar quando o preview engasga, e no
 * Alight Motion ela também está à mão; escondê-la faria o usuário achar que o
 * app é lento em vez de descobrir que pode reduzir.
 */
@Composable
private fun PlayerBar(viewModel: EditorViewModel) {
    val ui = viewModel.ui

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = { viewModel.togglePlayback() }) {
            Icon(
                if (ui.playing) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                contentDescription = if (ui.playing) "Pausar" else "Reproduzir",
                tint = AureaColors.Accent,
                modifier = Modifier.size(30.dp),
            )
        }

        Column(modifier = Modifier.width(96.dp)) {
            Text(
                text = "frame ${ui.playheadFrame}",
                style = MaterialTheme.typography.labelMedium,
                color = AureaColors.OnSurface,
            )
            Text(
                text = "de ${ui.totalFrames}",
                style = MaterialTheme.typography.labelSmall,
                color = AureaColors.OnSurfaceFaint,
            )
        }

        Spacer(Modifier.weight(1f))

        // Seletor de escala do preview. AUTO deixa o controlador adaptativo
        // decidir — é o padrão e o que a maioria deve usar.
        val options = listOf(
            Triple("AUTO", 1, 1),
            Triple("FULL", 1, 1),
            Triple("1/2", 1, 2),
            Triple("1/4", 1, 4),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            options.forEach { (label, num, den) ->
                val selected = ui.previewScaleLabel == label
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelSmall,
                    color = if (selected) AureaColors.Background else AureaColors.OnSurfaceMuted,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (selected) AureaColors.Accent else AureaColors.SurfaceHigh)
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                        .clickable { viewModel.setPreviewScale(label, num, den) },
                )
            }
        }
    }
}

/**
 * Barra de ferramentas das camadas.
 *
 * As quatro ações que o usuário faz o tempo todo: adicionar, dividir, duplicar
 * e remover. Ficam sempre visíveis porque escondê-las num menu custaria dois
 * toques em cada corte — e edição é feita de centenas de cortes.
 *
 * A ação de dividir usa o playhead: é o gesto padrão de edição (posicionar e
 * cortar), e é assim no Alight Motion.
 */
@Composable
private fun LayerToolbar(viewModel: EditorViewModel) {
    val ui = viewModel.ui
    val selected = ui.selectedIds.firstOrNull()
    val hasSelection = selected != null

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(AureaColors.Surface)
            .padding(horizontal = 4.dp, vertical = 2.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = { viewModel.createLayer(1, "Vídeo") }) {
            Icon(Icons.Filled.Add, "Adicionar camada", tint = AureaColors.Accent)
        }
        IconButton(onClick = { viewModel.createLayer(4, "Texto") }) {
            Text("Ab", color = AureaColors.Accent, fontWeight = FontWeight.SemiBold)
        }
        IconButton(
            onClick = { selected?.let { viewModel.splitLayerAtPlayhead(it) } },
            enabled = hasSelection,
        ) {
            Icon(
                Icons.Filled.ContentCut, "Dividir no playhead",
                tint = if (hasSelection) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
            )
        }
        IconButton(
            onClick = { selected?.let { viewModel.duplicateLayer(it) } },
            enabled = hasSelection,
        ) {
            Icon(
                Icons.Filled.ContentCopy, "Duplicar",
                tint = if (hasSelection) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
            )
        }
        IconButton(
            onClick = { selected?.let { viewModel.deleteLayer(it) } },
            enabled = hasSelection,
        ) {
            Icon(
                Icons.Filled.Delete, "Remover",
                tint = if (hasSelection) AureaColors.Danger else AureaColors.OnSurfaceFaint,
            )
        }

        Spacer(Modifier.weight(1f))

        Text(
            text = if (hasSelection) {
                layerKindName(ui.layers.firstOrNull { it.id == selected }?.kind ?: 0)
            } else {
                "nenhuma selecionada"
            },
            style = MaterialTheme.typography.labelSmall,
            color = AureaColors.OnSurfaceFaint,
            modifier = Modifier.padding(end = 8.dp),
        )
    }
}

/**
 * Painel de propriedades.
 *
 * Vazio nesta fase de propósito: mostrar sliders que não mexem em nada seria
 * pior do que mostrar nada — o usuário perderia tempo mexendo em controles
 * mortos e concluiria que o editor está quebrado. O que ainda não existe
 * aparece como texto explicando o que falta.
 */
@Composable
private fun InspectorPanel(viewModel: EditorViewModel) {
    val ui = viewModel.ui

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AureaColors.Surface)
            .padding(12.dp),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            InspectorTab.entries.forEach { tab ->
                val selected = ui.inspectorTab == tab
                val label = when (tab) {
                    InspectorTab.Properties -> "Propriedades"
                    InspectorTab.Effects -> "Efeitos"
                    InspectorTab.Keyframes -> "Keyframes"
                }
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelMedium,
                    color = if (selected) AureaColors.Background else AureaColors.OnSurfaceMuted,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (selected) AureaColors.Accent else AureaColors.SurfaceHigh)
                        .clickable { viewModel.setInspectorTab(tab) }
                        .padding(horizontal = 10.dp, vertical = 5.dp),
                )
            }
        }

        Spacer(Modifier.height(10.dp))

        val message = when {
            ui.selectedIds.isEmpty() -> "Selecione uma camada para editar as propriedades."
            ui.inspectorTab == InspectorTab.Effects ->
                "O painel de efeitos ainda não está nesta versão. O motor já compila a cadeia de efeitos; falta a interface."
            ui.inspectorTab == InspectorTab.Keyframes ->
                "O editor de curvas ainda não está nesta versão. A animação por keyframe já é avaliada pelo motor."
            else ->
                "Os controles de transformação ainda não estão nesta versão. Arraste a camada no preview para movê-la."
        }

        Text(
            text = message,
            style = MaterialTheme.typography.bodySmall,
            color = AureaColors.OnSurfaceFaint,
        )

        Spacer(Modifier.height(8.dp))

        if (ui.layers.isNotEmpty()) {
            Text(
                text = "${ui.layers.size} camada(s) · ${ui.totalFrames} frames · " +
                    "${if (ui.selectedIds.size > 1) "${ui.selectedIds.size} selecionadas" else "1 selecionada"}",
                style = MaterialTheme.typography.labelSmall,
                color = AureaColors.OnSurfaceFaint,
            )
        }
    }
}

@Composable
internal fun ErrorDialog(message: String, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Aurea") },
        text = { Text(message) },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Entendi") } },
    )
}

