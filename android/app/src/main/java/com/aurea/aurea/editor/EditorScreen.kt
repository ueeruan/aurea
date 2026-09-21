package com.aurea.aurea.editor

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.ui.AureaColors
import com.aurea.aurea.ui.layerKindName
import kotlin.math.roundToInt

/**
 * O editor, no fluxo do Alight Motion: preview grande em cima, barra de
 * reprodução, timeline, ferramentas e o painel da camada selecionada.
 *
 * SUPERFÍCIES INDEPENDENTES: o `SurfaceView` recebe o vídeo direto do motor
 * (Vulkan → swapchain), o Compose desenha a interface. Nenhum elemento desta
 * tela processa frame, e nenhum bitmap de vídeo entra no heap gerenciado.
 */
@Composable
fun EditorScreen(viewModel: EditorViewModel) {
    val ui = viewModel.ui

    val pickVideo = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) viewModel.importVideo(uri)
    }
    val importVideo = {
        pickVideo.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(AureaColors.Background)
            // Edge-to-edge: a interface não fica sob a barra de status nem sob
            // a barra de gestos.
            .systemBarsPadding(),
    ) {
        TopBar(viewModel)

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
                                val frame = holder.surfaceFrame
                                viewModel.attachSurface(
                                    holder.surface,
                                    frame.width().coerceAtLeast(1),
                                    frame.height().coerceAtLeast(1),
                                )
                            }

                            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                                // Rotação e split-screen: o swapchain é recriado no tamanho novo.
                                viewModel.resizeSurface(width, height)
                            }

                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                viewModel.detachSurface()
                            }
                        })
                    }
                },
                modifier = Modifier.fillMaxSize(),
            )

            if (!ui.engineReady) {
                Overlay("Preparando o motor gráfico…")
            } else if (ui.importing) {
                Overlay("Importando vídeo…")
            } else if (ui.layers.isEmpty()) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Overlay("Projeto vazio")
                    Spacer(Modifier.height(10.dp))
                    Text(
                        text = "Importar vídeo",
                        color = AureaColors.Background,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier
                            .clip(RoundedCornerShape(10.dp))
                            .background(AureaColors.Accent)
                            .clickable { importVideo() }
                            .padding(horizontal = 18.dp, vertical = 10.dp),
                    )
                }
            }

            if (ui.hudVisible) {
                PerfHud(viewModel.perf, viewModel.uiFps, Modifier.align(Alignment.TopStart))
            }
        }

        PlayerBar(viewModel)

        TimelinePanel(
            state = ui,
            onScrub = { frame -> viewModel.seekTo(frame) },
            onScrubStart = { frame -> viewModel.scrubStart(frame) },
            onScrubMove = { frame -> viewModel.scrubTo(frame) },
            onScrubEnd = { viewModel.scrubEnd() },
            onSelectLayer = { id, additive -> viewModel.select(id, additive) },
            onToggleVisibility = { id, visible -> viewModel.toggleLayerVisibility(id, visible) },
            onTrimLayer = { id, start, end -> viewModel.moveLayerInTime(id, start, end) },
            onBeginGesture = { label -> viewModel.beginGesture(label) },
            onEndGesture = { viewModel.endGesture() },
            onReorder = { id, index -> viewModel.reorderLayer(id, index) },
            modifier = Modifier.height(if (ui.layers.isEmpty()) 110.dp else 170.dp),
        )

        LayerToolbar(viewModel, importVideo)

        InspectorPanel(viewModel)
    }

    ui.errorMessage?.let { message ->
        ErrorDialog(message = message, onDismiss = { viewModel.dismissError() })
    }
}

@Composable
private fun Overlay(text: String) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.SurfaceHighest)
            .padding(horizontal = 20.dp, vertical = 14.dp),
    ) {
        Text(text = text, style = MaterialTheme.typography.bodyMedium, color = AureaColors.OnSurfaceMuted)
    }
}

@Composable
private fun TopBar(viewModel: EditorViewModel) {
    val ui = viewModel.ui
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(AureaColors.Surface)
            .padding(horizontal = 4.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = { viewModel.closeProject() }) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Voltar", tint = AureaColors.OnSurface)
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = ui.projectTitle + if (ui.dirty) " •" else "",
                style = MaterialTheme.typography.bodyLarge,
                color = AureaColors.OnSurface,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
            )
            Text(
                text = previewSummary(ui),
                style = MaterialTheme.typography.labelSmall,
                color = AureaColors.OnSurfaceFaint,
                maxLines = 1,
            )
        }
        // Painel DEV: métricas reais do motor por cima do preview.
        Text(
            text = "DEV",
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = if (ui.hudVisible) AureaColors.Background else AureaColors.OnSurfaceMuted,
            modifier = Modifier
                .clip(RoundedCornerShape(6.dp))
                .background(if (ui.hudVisible) AureaColors.Warning else AureaColors.SurfaceHigh)
                .clickable { viewModel.toggleHud() }
                .padding(horizontal = 8.dp, vertical = 4.dp),
        )
        IconButton(onClick = { viewModel.undo() }, enabled = ui.canUndo) {
            Icon(Icons.AutoMirrored.Filled.Undo, "Desfazer", tint = if (ui.canUndo) AureaColors.OnSurface else AureaColors.OnSurfaceFaint)
        }
        IconButton(onClick = { viewModel.redo() }, enabled = ui.canRedo) {
            Icon(Icons.AutoMirrored.Filled.Redo, "Refazer", tint = if (ui.canRedo) AureaColors.OnSurface else AureaColors.OnSurfaceFaint)
        }
        IconButton(onClick = { viewModel.saveProject() }) {
            Icon(Icons.Filled.Save, "Salvar", tint = AureaColors.OnSurface)
        }
    }
}

private fun previewSummary(ui: EditorUiState): String {
    val comp = if (ui.compWidth > 0) "${ui.compWidth}×${ui.compHeight} @ ${"%.2f".format(ui.fps)}" else "—"
    val res = if (ui.previewWidth > 0) "${ui.previewWidth}×${ui.previewHeight}" else "—"
    val fps = if (ui.currentFps > 0f) "%.0f fps".format(ui.currentFps) else "—"
    return "$comp · preview $res ${ui.previewScaleLabel} · $fps"
}

/**
 * Reprodução: passo para trás, play/pause, passo para a frente, contador e a
 * qualidade do preview — à mão, porque é a decisão que o usuário toma quando
 * o preview engasga.
 */
@Composable
private fun PlayerBar(viewModel: EditorViewModel) {
    val ui = viewModel.ui
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = { viewModel.stepFrames(-1) }) {
            Icon(Icons.Filled.SkipPrevious, "Frame anterior", tint = AureaColors.OnSurface)
        }
        IconButton(onClick = { viewModel.togglePlayback() }) {
            Icon(
                if (ui.playing) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                contentDescription = if (ui.playing) "Pausar" else "Reproduzir",
                tint = AureaColors.Accent,
                modifier = Modifier.size(30.dp),
            )
        }
        IconButton(onClick = { viewModel.stepFrames(1) }) {
            Icon(Icons.Filled.SkipNext, "Próximo frame", tint = AureaColors.OnSurface)
        }
        Column(modifier = Modifier.width(84.dp)) {
            Text("${ui.playheadFrame}", style = MaterialTheme.typography.labelMedium, color = AureaColors.OnSurface)
            Text("de ${ui.totalFrames}", style = MaterialTheme.typography.labelSmall, color = AureaColors.OnSurfaceFaint)
        }
        Spacer(Modifier.weight(1f))
        val options = listOf(
            Triple("AUTO", 1, 1),
            Triple("FULL", 1, 1),
            Triple("1/2", 1, 2),
            Triple("1/4", 1, 4),
            Triple("1/8", 1, 8),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
            options.forEach { (label, num, den) ->
                val selected = ui.previewScaleLabel == label
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelSmall,
                    color = if (selected) AureaColors.Background else AureaColors.OnSurfaceMuted,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (selected) AureaColors.Accent else AureaColors.SurfaceHigh)
                        .clickable { viewModel.setPreviewScale(label, num, den) }
                        .padding(horizontal = 6.dp, vertical = 4.dp),
                )
            }
        }
    }
}

@Composable
private fun LayerToolbar(viewModel: EditorViewModel, importVideo: () -> Unit) {
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
        IconButton(onClick = importVideo, enabled = ui.engineReady) {
            Icon(Icons.Filled.Add, "Importar vídeo", tint = AureaColors.Accent)
        }
        IconButton(onClick = { selected?.let { viewModel.splitLayerAtPlayhead(it) } }, enabled = hasSelection) {
            Icon(Icons.Filled.ContentCut, "Dividir no playhead",
                tint = if (hasSelection) AureaColors.OnSurface else AureaColors.OnSurfaceFaint)
        }
        IconButton(onClick = { selected?.let { viewModel.duplicateLayer(it) } }, enabled = hasSelection) {
            Icon(Icons.Filled.ContentCopy, "Duplicar",
                tint = if (hasSelection) AureaColors.OnSurface else AureaColors.OnSurfaceFaint)
        }
        IconButton(onClick = { selected?.let { viewModel.deleteLayer(it) } }, enabled = hasSelection) {
            Icon(Icons.Filled.Delete, "Remover",
                tint = if (hasSelection) AureaColors.Danger else AureaColors.OnSurfaceFaint)
        }
        Spacer(Modifier.weight(1f))
        Text(
            text = if (hasSelection) layerKindName(ui.layers.firstOrNull { it.id == selected }?.kind ?: 0)
            else "nenhuma selecionada",
            style = MaterialTheme.typography.labelSmall,
            color = AureaColors.OnSurfaceFaint,
            modifier = Modifier.padding(end = 8.dp),
        )
    }
}

// =============================================================================
// Painel da camada: efeitos
// =============================================================================
@Composable
private fun InspectorPanel(viewModel: EditorViewModel) {
    val ui = viewModel.ui
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(max = 260.dp)
            .background(AureaColors.Surface)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 12.dp, vertical = 8.dp),
    ) {
        if (ui.selectedIds.isEmpty()) {
            Text(
                text = if (ui.layers.isEmpty()) "Importe um vídeo para começar."
                else "Selecione uma camada na timeline para aplicar efeitos.",
                style = MaterialTheme.typography.bodySmall,
                color = AureaColors.OnSurfaceFaint,
            )
            return@Column
        }

        // Catálogo: um toque adiciona o efeito no fim da pilha.
        Row(
            modifier = Modifier.horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            ui.effectCatalog.forEach { entry ->
                Text(
                    text = "+ ${entry.name}",
                    style = MaterialTheme.typography.labelMedium,
                    color = AureaColors.OnSurface,
                    maxLines = 1,
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .background(AureaColors.SurfaceHigh)
                        .clickable { viewModel.addEffect(entry.typeId) }
                        .padding(horizontal = 10.dp, vertical = 6.dp),
                )
            }
        }

        Spacer(Modifier.height(8.dp))

        if (ui.layerEffects.isEmpty()) {
            Text("Sem efeitos nesta camada.", style = MaterialTheme.typography.bodySmall, color = AureaColors.OnSurfaceFaint)
        }

        ui.layerEffects.forEach { effect ->
            val open = effect.effectId == ui.openEffectId
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 2.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (open) AureaColors.SurfaceHighest else AureaColors.SurfaceHigh)
                    .clickable { viewModel.openEffect(effect.effectId) }
                    .padding(start = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = effect.name.ifEmpty { "Efeito desconhecido" },
                    style = MaterialTheme.typography.labelLarge,
                    color = if (effect.enabled) AureaColors.OnSurface else AureaColors.OnSurfaceFaint,
                    modifier = Modifier.weight(1f),
                )
                IconButton(onClick = { viewModel.setEffectEnabled(effect.effectId, !effect.enabled) }) {
                    Icon(
                        if (effect.enabled) Icons.Filled.Visibility else Icons.Filled.VisibilityOff,
                        contentDescription = if (effect.enabled) "Desligar" else "Ligar",
                        tint = AureaColors.OnSurfaceMuted,
                    )
                }
                IconButton(onClick = { viewModel.removeEffect(effect.effectId) }) {
                    Icon(Icons.Filled.Close, "Remover efeito", tint = AureaColors.OnSurfaceMuted)
                }
            }
            if (open) {
                Column(modifier = Modifier.padding(start = 8.dp, end = 4.dp, bottom = 6.dp)) {
                    ui.effectParams.filter { !it.hidden }.forEach { param ->
                        ParamEditor(param, viewModel)
                    }
                }
            }
        }
    }
}

@Composable
private fun ParamEditor(param: EffectParam, viewModel: EditorViewModel) {
    when (param.type) {
        ParamType.FLOAT, ParamType.ANGLE, ParamType.INT -> ScalarSlider(
            label = param.label,
            unit = param.unit,
            value = param.value[0],
            min = param.min,
            max = param.max,
            integer = param.type == ParamType.INT,
            viewModel = viewModel,
        ) { viewModel.setEffectParam(param.index, it) }

        ParamType.BOOL -> Row(verticalAlignment = Alignment.CenterVertically) {
            Text(param.label, style = MaterialTheme.typography.bodySmall, color = AureaColors.OnSurfaceMuted,
                modifier = Modifier.weight(1f))
            Switch(checked = param.value[0] >= 0.5f, onCheckedChange = {
                viewModel.setEffectParam(param.index, if (it) 1f else 0f)
            })
        }

        ParamType.ENUM -> Column {
            Text(param.label, style = MaterialTheme.typography.bodySmall, color = AureaColors.OnSurfaceMuted)
            Row(
                modifier = Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                param.enumLabels.forEachIndexed { i, label ->
                    val selected = param.value[0].roundToInt() == i
                    Text(
                        text = label,
                        style = MaterialTheme.typography.labelSmall,
                        color = if (selected) AureaColors.Background else AureaColors.OnSurfaceMuted,
                        modifier = Modifier
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (selected) AureaColors.Accent else AureaColors.SurfaceHighest)
                            .clickable { viewModel.setEffectParam(param.index, i.toFloat()) }
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                    )
                }
            }
        }

        ParamType.POINT2D, ParamType.POINT3D -> {
            val comps = if (param.type == ParamType.POINT2D) 2 else 3
            val names = listOf("X", "Y", "Z")
            for (c in 0 until comps) {
                ScalarSlider(
                    label = "${param.label} ${names[c]}",
                    unit = param.unit,
                    value = param.value[c],
                    min = param.min,
                    max = param.max,
                    integer = false,
                    viewModel = viewModel,
                ) { v ->
                    val next = param.value.copyOf()
                    next[c] = v
                    viewModel.setEffectVector(param.index, next)
                }
            }
        }

        ParamType.COLOR -> {
            val names = listOf("R", "G", "B")
            for (c in 0 until 3) {
                ScalarSlider(
                    label = "${param.label} ${names[c]}",
                    unit = "",
                    value = param.value[c],
                    min = 0f,
                    max = 1f,
                    integer = false,
                    viewModel = viewModel,
                ) { v ->
                    val next = param.value.copyOf()
                    next[c] = v
                    viewModel.setEffectVector(param.index, next)
                }
            }
        }

        else -> Text(
            text = "${param.label}: editor ainda não disponível",
            style = MaterialTheme.typography.bodySmall,
            color = AureaColors.OnSurfaceFaint,
        )
    }
}

/**
 * Slider de um componente. Um arrasto inteiro é UM passo de desfazer: o grupo
 * abre no primeiro movimento e fecha ao soltar.
 */
@Composable
private fun ScalarSlider(
    label: String,
    unit: String,
    value: Float,
    min: Float,
    max: Float,
    integer: Boolean,
    viewModel: EditorViewModel,
    onChange: (Float) -> Unit,
) {
    var dragging by remember { mutableStateOf(false) }
    var local by remember { mutableStateOf(value) }
    val shown = if (dragging) local else value
    val lo = if (max > min) min else 0f
    val hi = if (max > min) max else 1f
    Column {
        Row {
            Text(label, style = MaterialTheme.typography.bodySmall, color = AureaColors.OnSurfaceMuted,
                modifier = Modifier.weight(1f))
            Text(
                text = (if (integer) "${shown.roundToInt()}" else "%.2f".format(shown)) + if (unit.isNotEmpty()) " $unit" else "",
                style = MaterialTheme.typography.labelSmall,
                color = AureaColors.OnSurface,
            )
        }
        Slider(
            value = shown.coerceIn(lo, hi),
            valueRange = lo..hi,
            onValueChange = { v ->
                if (!dragging) {
                    dragging = true
                    viewModel.beginGesture(label)
                }
                local = if (integer) v.roundToInt().toFloat() else v
                onChange(local)
            },
            onValueChangeFinished = {
                if (dragging) viewModel.endGesture()
                dragging = false
            },
            colors = SliderDefaults.colors(thumbColor = AureaColors.Accent, activeTrackColor = AureaColors.Accent),
            modifier = Modifier.height(28.dp),
        )
    }
}

// =============================================================================
// Painel DEV
// =============================================================================
@Composable
private fun PerfHud(p: PerfStats, uiFps: Float, modifier: Modifier) {
    fun mb(bytes: Long) = "%.0f MB".format(bytes / (1024.0 * 1024.0))
    fun ms(v: Float) = "%.1f".format(v)
    val scale = "${if (p.renderAuto) "AUTO " else ""}${p.renderScaleNum}/${p.renderScaleDen}"
    val lines = listOf(
        "Preview ${"%.1f".format(p.previewFps)} fps · UI ${"%.0f".format(uiFps)} fps · orçamento ${ms(p.frameBudgetMs)} ms",
        "CPU ${ms(p.cpuFrameMs)} · GPU ${ms(p.gpuFrameMs)} ms${if (!p.gpuTimers) " (sem timer)" else ""}",
        "Decode ${ms(p.decodeMs)} · Cor ${ms(p.colorConvMs)} · Efeitos ${ms(p.effectsMs)}",
        "  blur ${ms(p.blurMs)} · glow ${ms(p.glowMs)} · Composite ${ms(p.compositeMs)}",
        "Saída ${ms(p.outputMs)} · Acquire ${ms(p.acquireMs)} · Present ${ms(p.presentMs)}",
        "Drop ${p.droppedFrames} (recente ${p.droppedRecent}) · Escala $scale ${p.previewWidth}×${p.previewHeight}",
        "Cache dec ${p.decodedCacheFrames} fr ${mb(p.decodedCacheBytes)} · RAM ${mb(p.ramBytes)} · GPU ${mb(p.gpuMemoryBytes)}",
        "Passes ${p.passesExecuted} (+${p.passesCulled} cortados) · Tex ${p.physicalTextures} (alias ${p.aliasedTextures})",
        "Pipelines ${p.pipelinesTotal} (+${p.pipelineCompilesLive} ao vivo) · Tex criadas ${p.texturesCreated}",
        "Decoder ${p.decoder.ifEmpty { "—" }} ${if (p.hardwareDecoder) "HW" else "SW"} ${if (p.zeroCopy) "zero-copy" else "CPU"}",
        "Seeks ${p.seeks} · coalescidos ${p.coalesced} · seek ${ms(p.lastSeekMs)} ms · atrasados ${p.staleFrames}",
        "GPU ${p.gpuName} · térmico ${p.thermal} · layers ${p.layersRendered}",
    )
    Column(
        modifier = modifier
            .padding(6.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(Color(0xB0000000))
            .padding(6.dp),
    ) {
        lines.forEach {
            Text(it, color = Color(0xFFB8F5C8), fontFamily = FontFamily.Monospace, fontSize = 9.sp, lineHeight = 11.sp)
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
