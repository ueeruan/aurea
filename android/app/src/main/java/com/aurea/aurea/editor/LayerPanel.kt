package com.aurea.aurea.editor

import android.app.Application
import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.GenericShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.GroupSpeedBody
import com.aurea.aurea.editor.panels.PanelRequests
import com.aurea.aurea.editor.panels.TransformTab
import com.aurea.aurea.editor.panels.VolumePopoutBody
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.LayerType
import kotlinx.coroutines.launch

// =============================================================================
// Omega painel (área 04): O PAINEL DA CAMADA (`fragment_element_edit` da
// referência, 200 dp por cima da timeline). Substitui a doca de fichas.
//
//   y=15  Fileira 1 · 38 · 5 colunas  [Velocidade] [Cortar início|Dividir|Cortar fim] [Áudio]
//                                      (cabeçote fora do clipe: [◀ par ▶] no lugar do trio)
//   y=63  Fileira 2 · 57 · 3 colunas  [Cor e preenchimento] [Borda e sombra] [Mesclagem e opacidade]
//   y=126 Fileira 3 · 57 · 3–4 col.   [Mover e transformar] [Editar…] [Presets] [Efeitos]
//
// Vaga invisível guarda o espaço (como o INVISIBLE da referência). Sem a
// fileira 2 inteira (áudio, câmera, luz, nulo) a fileira 3 vira botões ALTOS.
// As regras de tempo e de áudio vêm do motor (`Engine::query_layer_panel`); a
// grade por tipo é a conta pura [layerPanelSpec] (par do iOS, mesmos ids).
// =============================================================================

/** Bits de `Engine::query_layer_panel` (EngineOmegaLayer.cpp; iguais no iOS). */
internal object LayerPanelBits {
    const val TRIM_START = 1
    const val SPLIT = 2
    const val TRIM_END = 4
    const val EXTEND_START = 8
    const val EXTEND_END = 16
    const val MOVE_START = 32
    const val MOVE_END = 64
    const val RETIME_EXTEND_START = 128
    const val RETIME_SHORTEN_START = 256
    const val RETIME_SHORTEN_END = 512
    const val RETIME_EXTEND_END = 1024
    const val LOCKED = 2048
    const val HAS_AUDIO = 4096
    const val GAIN_KEYS = 8192
    const val MISSING_MEDIA = 16384
    const val SPEED_BAKEABLE = 32768
}

/** Um botão da grade. O id é `layer.panel.<tag>` nas duas plataformas. */
internal enum class LayerSlot(val tag: String) {
    ColorFill("colorFill"),
    BorderShadow("borderShadow"),
    BlendOpacity("blendOpacity"),
    MoveTransform("moveTransform"),
    Edit("edit"),
    Presets("presets"),
    Effects("effects"),
}

/** O que a vaga "Editar…" é em cada tipo (uma por vez, como na referência). */
internal enum class LayerEdit(val glyph: Char, @StringRes val label: Int) {
    Shape(ShellGlyph.SliderHorizontalBelowRectangle, R.string.sh_dock_edit_shape),
    /** Imagem e vídeo: na referência são forma com mídia ("Editar forma"); no AUREA, o recorte (Máscara). */
    MediaShape(ShellGlyph.SliderHorizontalBelowRectangle, R.string.sh_dock_edit_shape),
    Points(CupertinoGlyph.PencilOutline, R.string.om_lp_edit_points),
    Drawing(CupertinoGlyph.Scribble, R.string.om_lp_edit_drawing),
    Text(CupertinoGlyph.Textformat, R.string.sh_dock_edit_text),
    Group(CupertinoGlyph.ArrowDownRightSquare, R.string.om_lp_edit_group),
    Camera(CupertinoGlyph.CameraFill, R.string.om_lp_camera_options),
    Light(CupertinoGlyph.Lightbulb, R.string.om_lp_light_options),
    Particles(CupertinoGlyph.Bolt, R.string.om_lp_edit_particles),
    Material(CupertinoGlyph.Cube, R.string.panel_material_ambiente),
}

/** Para onde a Velocidade leva: o painel completo (mídia), o embutido (grupo) ou o constante (sem fonte). */
internal enum class SpeedRoute { Panel, Group, Constant }

/** O que o painel precisa da camada (data class: recompõe só quando muda). */
internal data class PanelLayer(
    val id: Long,
    val kind: Int,
    val locked: Boolean,
    val start: Int,
    val end: Int,
    val adjustment: Boolean = false,
    val vector: Boolean = false,
    /** Vetor de desenho à mão (área 06 liga quando o tipo existir). */
    val drawing: Boolean = false,
    val text3D: Boolean = false,
    val effectCount: Int = 0,
)

/**
 * A grade de um tipo. [row2] tem 3 vagas e [row3] 2 a 4 (nula = invisível,
 * guarda o espaço). [tallRow3]: a fileira 2 inteira sumiu e a 3 ocupa as duas
 * alturas (colapso "a"). [speedArrow]: a seta ▾ da Velocidade (só no grupo).
 */
internal data class LayerPanelSpec(
    val row2: List<LayerSlot?>,
    val row3: List<LayerSlot?>,
    val edit: LayerEdit?,
    val tallRow3: Boolean,
    val speedArrow: Boolean,
    val speed: SpeedRoute,
)

/** A grade por tipo (P0-4): conta pura, testada na JVM; gêmea de `LayerPanelSpec.of` no iOS. */
internal fun layerPanelSpec(l: PanelLayer): LayerPanelSpec {
    val type = LayerType.of(l.kind)
    val c = LayerSlot.ColorFill
    val b = LayerSlot.BorderShadow
    val m = LayerSlot.BlendOpacity
    val std = listOf(LayerSlot.MoveTransform, LayerSlot.Edit, LayerSlot.Presets, LayerSlot.Effects)
    if (l.adjustment || type == LayerType.Adjustment) {
        // Camada de ajuste: só a mistura e os efeitos que ela aplica embaixo.
        return LayerPanelSpec(listOf(null, null, m), listOf(null, null, null, LayerSlot.Effects), null, false, false, SpeedRoute.Constant)
    }
    return when (type) {
        LayerType.Shape -> LayerPanelSpec(
            listOf(c, b, m), std,
            when { l.drawing -> LayerEdit.Drawing; l.vector -> LayerEdit.Points; else -> LayerEdit.Shape },
            false, false, SpeedRoute.Constant,
        )
        LayerType.Text -> LayerPanelSpec(listOf(c, b, m), std, LayerEdit.Text, false, false, SpeedRoute.Constant)
        LayerType.Model3D -> LayerPanelSpec(
            listOf(c, null, m), std, if (l.text3D) LayerEdit.Text else LayerEdit.Material, false, false, SpeedRoute.Constant,
        )
        // Imagem e vídeo são forma com mídia na referência (evidência REA, seção 8):
        // "Editar forma". O preenchimento de mídia é da área 05 (até lá a vaga fica vazia).
        LayerType.Image -> LayerPanelSpec(listOf(null, b, m), std, LayerEdit.MediaShape, false, false, SpeedRoute.Constant)
        LayerType.Video -> LayerPanelSpec(listOf(null, b, m), std, LayerEdit.MediaShape, false, false, SpeedRoute.Panel)
        LayerType.Group -> LayerPanelSpec(listOf(null, b, m), std, LayerEdit.Group, false, true, SpeedRoute.Group)
        LayerType.Particles -> LayerPanelSpec(listOf(null, null, m), std, LayerEdit.Particles, false, false, SpeedRoute.Constant)
        // Áudio: os efeitos de áudio na largura toda e altos (colapso "c").
        LayerType.Audio -> LayerPanelSpec(listOf(null, null, null), listOf(LayerSlot.Effects), null, true, false, SpeedRoute.Panel)
        LayerType.Camera -> LayerPanelSpec(
            listOf(null, null, null), listOf(LayerSlot.MoveTransform, LayerSlot.Edit, LayerSlot.Presets, null),
            LayerEdit.Camera, true, false, SpeedRoute.Constant,
        )
        LayerType.Light -> LayerPanelSpec(
            listOf(null, null, null),
            listOf(LayerSlot.MoveTransform, LayerSlot.Edit, null, if (l.effectCount > 0) LayerSlot.Effects else null),
            LayerEdit.Light, true, false, SpeedRoute.Constant,
        )
        // Nulo: a vaga de edição sai da cadeia (colapso "b"): Mover | Presets.
        LayerType.Null -> LayerPanelSpec(
            listOf(null, null, null), listOf(LayerSlot.MoveTransform, LayerSlot.Presets), null, true, false, SpeedRoute.Constant,
        )
        LayerType.Adjustment -> LayerPanelSpec(listOf(null, null, m), listOf(null, null, null, LayerSlot.Effects), null, false, false, SpeedRoute.Constant)
    }
}

/**
 * O botão Áudio: habilitado com áudio (`hasAnyAudio` do motor: todo vídeo,
 * todo áudio, grupo com som) ou com mais de um keyframe de volume.
 */
internal fun layerAudioEnabled(bits: Int): Boolean =
    bits and (LayerPanelBits.HAS_AUDIO or LayerPanelBits.GAIN_KEYS) != 0

/** Estado do painel visto de fora (a timeline mostra as alças com a velocidade do grupo aberta). */
internal object LayerPanelState {
    /** A velocidade embutida do grupo está aberta (alças de velocidade na timeline, área 03). */
    var speedPopout by mutableStateOf(false)
    /** Camadas cuja mídia ausente já foi avisada nesta sessão (o aviso sai uma vez). */
    val warnedMissing = mutableSetOf<Long>()
    /** A última camada que entrou com animação (voltar de um painel não anima de novo). */
    var lastShown = 0L
}

private enum class LayerPopout { None, Speed, Volume }

/** A camada como o painel a vê (ou nula, se ela sumiu). */
private fun panelLayerOf(store: EditorStore, layerId: Long): PanelLayer? {
    val d = store.detail?.takeIf { it.id == layerId }
    return store.layers.firstOrNull { it.id == layerId }?.let {
        PanelLayer(
            id = it.id,
            kind = it.kind,
            locked = it.locked,
            start = it.startFrame,
            end = it.endFrame,
            adjustment = it.adjustment,
            vector = d != null && store.isVectorLayer,
            // Omega conteúdo (área 06): a camada de desenho (VectorData::drawing).
            drawing = d != null && store.isDrawingLayer,
            text3D = store.text3d != null,
            effectCount = it.effectCount,
        )
    }
}

/** Altura do conteúdo do painel: 200 dp em 1× (cresce com a fonte, como a zona). */
private const val PANEL_BASE = 200f

@Composable
internal fun LayerPanel(store: EditorStore, ui: EditorUi, layerId: Long) {
    val layer by remember(layerId) { derivedStateOf { panelLayerOf(store, layerId) } }
    val l = layer ?: return
    val spec = remember(l) { layerPanelSpec(l) }
    // Os bits dependem do cabeçote, das bordas, da trava e das chaves de volume.
    // O cabeçote é lido DENTRO do derivedStateOf: tocando, o painel só recompõe
    // quando o trio vira par (ou o contrário), não a cada quadro.
    val keyCount = store.keyframes[layerId]?.size ?: 0
    val audioFlags = store.detail?.takeIf { it.id == layerId }?.audioFlags ?: 0
    val bits by remember(layerId, l.start, l.end, l.locked, keyCount, audioFlags) {
        derivedStateOf { store.layerPanelBits(layerId) }
    }
    var popout by remember(layerId) { mutableStateOf(LayerPopout.None) }
    val reveal = remember(layerId) { Animatable(0f) }
    val scope = rememberCoroutineScope()
    fun openPopout(p: LayerPopout) {
        if (store.playing) store.pause()
        popout = p
        scope.launch {
            reveal.snapTo(0f)
            reveal.animateTo(1f, tween(200, easing = FastOutSlowInEasing))
        }
    }
    fun closePopout() {
        scope.launch {
            reveal.animateTo(0f, tween(200, easing = FastOutSlowInEasing))
            popout = LayerPopout.None
        }
    }
    LaunchedEffect(popout) { LayerPanelState.speedPopout = popout == LayerPopout.Speed }
    DisposableEffect(Unit) { onDispose { LayerPanelState.speedPopout = false } }
    // Voltar fecha o painel embutido antes de sair do painel da camada.
    BackHandler(enabled = popout != LayerPopout.None) { closePopout() }
    MissingMediaNotice(layerId, bits)

    // P1-10: o painel sobe 100 dp em 100 ms ao escolher a camada (voltar de um
    // painel de propriedade não anima: ele já estava embaixo).
    val enter = remember(layerId) { Animatable(if (LayerPanelState.lastShown == layerId) 0f else 100f) }
    LaunchedEffect(layerId) {
        LayerPanelState.lastShown = layerId
        enter.animateTo(0f, tween(100, easing = FastOutSlowInEasing))
    }

    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .clipToBounds()
            .graphicsLayer { translationY = enter.value * density }
            .background(AureaColors.LayerPanel)
            .testTag("layer.panel"),
    ) {
        val k = (maxHeight.value / PANEL_BASE).coerceIn(0.6f, 2f)
        Column(Modifier.fillMaxSize()) {
            Spacer(Modifier.height((15f * k).dp))
            LayerTimeRow(store, l, spec, bits, height = (38f * k).dp,
                onSpeed = {
                    when (spec.speed) {
                        SpeedRoute.Group -> if (popout == LayerPopout.Speed) closePopout() else openPopout(LayerPopout.Speed)
                        else -> openPanel(store, ui, EditorPanel.Speed)
                    }
                },
                onAudio = { if (popout == LayerPopout.Volume) closePopout() else openPopout(LayerPopout.Volume) },
            )
            Spacer(Modifier.height((10f * k).dp))
            if (spec.tallRow3) {
                GridRow(store, ui, l, spec, spec.row3, height = (120f * k).dp, tall = true)
            } else {
                GridRow(store, ui, l, spec, spec.row2, height = (57f * k).dp, tall = false)
                Spacer(Modifier.height((6f * k).dp))
                GridRow(store, ui, l, spec, spec.row3, height = (57f * k).dp, tall = false)
            }
        }
        if (popout != LayerPopout.None) {
            val fromEnd = popout == LayerPopout.Volume
            Box(
                Modifier
                    .fillMaxSize()
                    .graphicsLayer {
                        clip = true
                        shape = revealShape(reveal.value, fromEnd)
                    }
                    // O painel embutido segura o toque: nada passa para a grade embaixo.
                    .pointerInput(Unit) { awaitPointerEventScope { while (true) awaitPointerEvent() } },
            ) {
                when (popout) {
                    LayerPopout.Speed -> GroupSpeedPopout(store, l, bits, headHeight = (15f + 38f) * k, onClose = { closePopout() })
                    LayerPopout.Volume -> VolumePopout(store, ui, headHeight = (15f + 38f) * k, onClose = { closePopout() })
                    LayerPopout.None -> Unit
                }
            }
        }
    }
}

/** O recorte que cresce do canto (de cima à esquerda; [fromEnd] de cima à direita). */
private fun revealShape(progress: Float, fromEnd: Boolean) = GenericShape { size, layoutDirection ->
    val p = progress.coerceIn(0f, 1f)
    val w = size.width * p
    val h = size.height * p
    // "Do fim" segue o sentido de leitura: em árabe o Áudio está à esquerda.
    val endSide = fromEnd != (layoutDirection == androidx.compose.ui.unit.LayoutDirection.Rtl)
    val left = if (endSide) size.width - w else 0f
    addRect(Rect(left, 0f, left + w, h))
}

// =============================================================================
// Fileira 1: Velocidade · trio (ou par) · Áudio
// =============================================================================

@Composable
private fun LayerTimeRow(
    store: EditorStore,
    l: PanelLayer,
    spec: LayerPanelSpec,
    bits: Int,
    height: Dp,
    onSpeed: () -> Unit,
    onAudio: () -> Unit,
) {
    val locked = bits and LayerPanelBits.LOCKED != 0
    Row(Modifier.fillMaxWidth().height(height), verticalAlignment = Alignment.CenterVertically) {
        // Col. 1 · Velocidade (todos os tipos; no grupo, com a seta ▾ do painel embutido).
        val speedLabel = stringResource(R.string.editor_velocidade)
        val opens = stringResource(R.string.om_lp_opens_panel)
        PanelPress(
            Modifier.weight(1f).fillMaxHeight().testTag("layer.panel.speed")
                .semantics {
                    contentDescription = speedLabel
                    if (spec.speedArrow) stateDescription = opens
                },
            onClick = { if (locked) store.toastText(R.string.editor_camada_bloqueada_desbloqueie_editar) else onSpeed() },
        ) {
            CupertinoIcon(CupertinoGlyph.Speedometer, 22.dp, AureaColors.Text)
            if (spec.speedArrow) {
                CupertinoIcon(
                    CupertinoGlyph.ArrowtriangleDownFill, 8.dp, AureaColors.Text,
                    Modifier.align(Alignment.BottomEnd).padding(end = 5.dp, bottom = 3.dp),
                )
            }
        }
        // Cols. 2–4 · o trio com o cabeçote dentro; fora, o par Mover/Expandir.
        // A ordem do tempo fica LTR em qualquer idioma (como a timeline).
        KeepLtr {
            Row(
                Modifier.weight(3f).fillMaxHeight().padding(horizontal = 2.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(AureaColors.LayerPanelButton),
            ) {
                val before = bits and LayerPanelBits.EXTEND_START != 0
                val after = bits and LayerPanelBits.EXTEND_END != 0
                when {
                    before -> {
                        TimeButton(TimeGlyph.MoveStart, R.string.om_lp_move_start_to_playhead, "layer.panel.timeLeft", 1f) {
                            timeEdit(store, l, locked) { if (!store.moveLayerToPlayhead(l.id, alignEnd = false)) store.toastText(R.string.timeline_cut_failed) }
                        }
                        RowDivider()
                        TimeButton(TimeGlyph.ExtendStart, R.string.editor_extend_start_to_playhead, "layer.panel.timeRight", 1f) {
                            timeEdit(store, l, locked) { if (!store.extendToPlayhead(l.id, start = true)) store.toastText(R.string.timeline_cut_failed) }
                        }
                    }
                    after -> {
                        TimeButton(TimeGlyph.ExtendEnd, R.string.editor_extend_end_to_playhead, "layer.panel.timeLeft", 1f) {
                            timeEdit(store, l, locked) { if (!store.extendToPlayhead(l.id, start = false)) store.toastText(R.string.timeline_cut_failed) }
                        }
                        RowDivider()
                        TimeButton(TimeGlyph.MoveEnd, R.string.om_lp_move_end_to_playhead, "layer.panel.timeRight", 1f) {
                            timeEdit(store, l, locked) { if (!store.moveLayerToPlayhead(l.id, alignEnd = true)) store.toastText(R.string.timeline_cut_failed) }
                        }
                    }
                    else -> {
                        TimeButton(TimeGlyph.TrimStart, R.string.editor_aparar_inicio_cabecote, "timeline.cut.start", 1f) {
                            insideEdit(store, l, locked) { if (!store.trimStart(l.id, store.playhead)) store.toastText(R.string.timeline_cut_failed) }
                        }
                        RowDivider()
                        TimeButton(TimeGlyph.Split, R.string.editor_dividir_cabecote, "timeline.cut.split", 1f) {
                            insideEdit(store, l, locked) { store.splitAtPlayhead(listOf(l.id)) }
                        }
                        RowDivider()
                        TimeButton(TimeGlyph.TrimEnd, R.string.editor_aparar_fim_cabecote, "timeline.cut.end", 1f) {
                            insideEdit(store, l, locked) { if (!store.trimEnd(l.id, store.playhead)) store.toastText(R.string.timeline_cut_failed) }
                        }
                    }
                }
            }
        }
        // Col. 5 · Áudio: abre o volume embutido; sem áudio fica desabilitado.
        val enabled = layerAudioEnabled(bits)
        val muted = store.detail?.takeIf { it.id == l.id }?.audioMuted == true
        val audioLabel = stringResource(if (enabled) R.string.om_lp_audio else R.string.panel_esta_camada_nao_tem_audio)
        val mutedState = stringResource(R.string.panel_mudo)
        val tint = when {
            !enabled -> AureaColors.LayerPanelDisabled
            muted -> AureaColors.Accent
            else -> AureaColors.Text
        }
        PanelPress(
            Modifier.weight(1f).fillMaxHeight().testTag("layer.panel.audio")
                .semantics {
                    contentDescription = audioLabel
                    if (muted && enabled) stateDescription = mutedState
                    if (!enabled) disabled()
                },
            enabled = enabled,
            onClick = onAudio,
        ) {
            CupertinoIcon(if (enabled && !muted) CupertinoGlyph.Speaker2 else CupertinoGlyph.SpeakerSlash, 22.dp, tint)
            CupertinoIcon(CupertinoGlyph.ArrowtriangleDownFill, 7.dp, tint, Modifier.align(Alignment.BottomCenter).padding(bottom = 3.dp))
        }
    }
}

@Composable
private fun RowDivider() {
    Box(Modifier.width(1.dp).fillMaxHeight().background(AureaColors.LayerPanelPressed))
}

@Composable
private fun RowScope.TimeButton(glyph: TimeGlyph, @StringRes label: Int, tag: String, weight: Float, onClick: () -> Unit) {
    val text = stringResource(label)
    PanelPress(
        Modifier.weight(weight).fillMaxHeight().testTag(tag).semantics { contentDescription = text },
        haptic = true,
        onClick = onClick,
    ) {
        TimeGlyphIcon(glyph, AureaColors.Text, Modifier.size(22.dp))
    }
}

/** Trio: só com o cabeçote estritamente dentro (nas bordas o corte não teria o que tirar). */
private inline fun insideEdit(store: EditorStore, l: PanelLayer, locked: Boolean, action: () -> Unit) {
    val t = store.playhead
    when {
        locked -> store.toastText(R.string.editor_camada_bloqueada_desbloqueie_editar)
        t <= l.start || t >= l.end -> store.toastText(R.string.sh_playhead_into_layer)
        else -> {
            if (store.playing) store.pause()
            action()
        }
    }
}

/** Par: mover e esticar, com o cabeçote fora do clipe. */
private inline fun timeEdit(store: EditorStore, @Suppress("UNUSED_PARAMETER") l: PanelLayer, locked: Boolean, action: () -> Unit) {
    if (locked) { store.toastText(R.string.editor_camada_bloqueada_desbloqueie_editar); return }
    if (store.playing) store.pause()
    action()
}

// =============================================================================
// Fileiras 2 e 3: a grade de 57 dp
// =============================================================================

@Composable
private fun GridRow(
    store: EditorStore,
    ui: EditorUi,
    l: PanelLayer,
    spec: LayerPanelSpec,
    slots: List<LayerSlot?>,
    height: Dp,
    tall: Boolean,
) {
    Row(Modifier.fillMaxWidth().height(height)) {
        slots.forEach { slot ->
            if (slot == null) {
                Spacer(Modifier.weight(1f))
            } else {
                GridButton(slot, spec.edit, tall, Modifier.weight(1f).fillMaxHeight()) {
                    onSlot(store, ui, l, slot, spec.edit)
                }
            }
        }
    }
}

@Composable
private fun GridButton(slot: LayerSlot, edit: LayerEdit?, tall: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val glyph = slotGlyph(slot, edit)
    val label = stringResource(slotLabel(slot, edit))
    PanelPress(
        modifier.padding(horizontal = 2.dp).testTag("layer.panel.${slot.tag}").semantics { contentDescription = label },
        radius = 6.dp,
        haptic = true,
        onClick = onClick,
    ) {
        Column(
            Modifier.fillMaxSize().padding(horizontal = 3.dp, vertical = if (tall) 10.dp else 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            if (slot == LayerSlot.MoveTransform) {
                CupertinoIcon(CupertinoGlyph.Move, 24.dp, AureaColors.Text)
            } else {
                CupertinoIcon(glyph, 24.dp, AureaColors.Text)
            }
            Spacer(Modifier.height(4.dp))
            // 7 a 11,5 sp com redução automática (`panelbutton_autotext_*`).
            BasicText(
                label,
                maxLines = 2,
                style = AureaType.Base.merge(TextStyle(color = AureaColors.Text, textAlign = TextAlign.Center, lineHeight = 13.sp)),
                autoSize = TextAutoSize.StepBased(minFontSize = 7.sp, maxFontSize = 11.5.sp, stepSize = 0.5.sp),
                modifier = Modifier.fillMaxWidth().clearAndSetSemantics { },
            )
        }
    }
}

private fun slotGlyph(slot: LayerSlot, edit: LayerEdit?): Char = when (slot) {
    LayerSlot.ColorFill -> CupertinoGlyph.Paintbrush
    LayerSlot.BorderShadow -> CupertinoGlyph.SquareOnSquare
    LayerSlot.BlendOpacity -> CupertinoGlyph.CircleLefthalfFill
    LayerSlot.MoveTransform -> CupertinoGlyph.Move
    LayerSlot.Edit -> edit?.glyph ?: CupertinoGlyph.Pencil
    LayerSlot.Presets -> CupertinoGlyph.WandStars
    LayerSlot.Effects -> CupertinoGlyph.Sparkles
}

@StringRes
private fun slotLabel(slot: LayerSlot, edit: LayerEdit?): Int = when (slot) {
    LayerSlot.ColorFill -> R.string.sh_dock_color_fill
    LayerSlot.BorderShadow -> R.string.om_lp_border_shadow
    LayerSlot.BlendOpacity -> R.string.panel_mistura_opacidade
    LayerSlot.MoveTransform -> R.string.om_lp_move_transform
    LayerSlot.Edit -> edit?.label ?: R.string.sh_dock_edit_shape
    LayerSlot.Presets -> R.string.sh_dock_presets
    LayerSlot.Effects -> R.string.sh_dock_effects
}

/**
 * Para onde cada botão leva HOJE. Os painéis novos das áreas 05 (Cor e
 * preenchimento, Borda e sombra, Câmera) e 06 (Editar pontos, Desenho) trocam
 * o destino AQUI quando existirem; até lá abre o painel atual mais próximo.
 */
private fun onSlot(store: EditorStore, ui: EditorUi, l: PanelLayer, slot: LayerSlot, edit: LayerEdit?) {
    val type = LayerType.of(l.kind)
    when (slot) {
        // Omega aparência (área 05): Cor e preenchimento (forma e texto) e Borda e
        // sombra (todas as camadas visuais) têm painel próprio.
        LayerSlot.ColorFill -> openPanel(store, ui, when {
            type == LayerType.Model3D -> EditorPanel.Element3D
            l.vector -> EditorPanel.Vector
            else -> EditorPanel.Shape
        })
        LayerSlot.BorderShadow -> openPanel(store, ui, EditorPanel.BorderShadow)
        LayerSlot.BlendOpacity -> openPanel(store, ui, EditorPanel.Appearance)
        LayerSlot.MoveTransform -> openPanel(store, ui, EditorPanel.Transform)
        LayerSlot.Edit -> when (edit) {
            LayerEdit.Text -> store.openTextContentEditor()
            LayerEdit.Group -> store.openPrecomp(l.id)
            LayerEdit.Shape -> openPanel(store, ui, EditorPanel.ShapeEdit)
            // Omega conteúdo (área 06, P0-4/P0-5): Editar pontos e o editor de desenho.
            LayerEdit.Points -> openPanel(store, ui, EditorPanel.EditPoints)
            LayerEdit.Drawing -> openPanel(store, ui, EditorPanel.Drawing)
            LayerEdit.MediaShape -> openPanel(store, ui, EditorPanel.Mask)
            // Omega aparência (P1-6): a lente saiu de Transformar para Opções da câmera.
            LayerEdit.Camera -> openPanel(store, ui, EditorPanel.Camera)
            LayerEdit.Light, LayerEdit.Material -> openPanel(store, ui, EditorPanel.Element3D)
            LayerEdit.Particles -> openPanel(store, ui, EditorPanel.Particles)
            null -> Unit
        }
        LayerSlot.Presets -> openLayerPresets(store, ui, l)
        LayerSlot.Effects -> openPanel(store, ui, EditorPanel.Effects)
    }
}

// =============================================================================
// Presets (P1-12): o painel filtrado pelo tipo; sem nenhum, o cartão vazio
// =============================================================================

/** Pedido do cartão "ainda não há presets" (aberto por [openLayerPresets]). */
internal object LayerPresetsEmpty {
    var showing by mutableStateOf(false)
}

private fun openLayerPresets(store: EditorStore, ui: EditorUi, l: PanelLayer) {
    val lib = store.presets
    val type = LayerType.of(l.kind)
    val isText = type == LayerType.Text || l.text3D
    val kinds = if (isText) listOf(PresetKind.Text) else PresetKind.entries.filter { it != PresetKind.Text }
    val any = kinds.any { lib.entries(it).isNotEmpty() }
    if (any) openPanel(store, ui, EditorPanel.Presets) else LayerPresetsEmpty.showing = true
}

/** O cartão claro de "ainda não há presets" (`presets_empty_state_popup` da referência). */
@Composable
internal fun LayerPresetsEmptyDialog(store: EditorStore, ui: EditorUi) {
    if (!LayerPresetsEmpty.showing) return
    val close = { LayerPresetsEmpty.showing = false }
    AureaDialog(
        title = stringResource(R.string.om_presets_empty_title),
        message = stringResource(R.string.om_presets_empty_body),
        onDismiss = close,
        tag = "presets.empty",
        actions = listOf(
            DialogAction(stringResource(R.string.om_presets_empty_browse), DialogRole.Positive, tag = "presets.empty.browse") {
                store.presetsOpenKind = PresetKind.Animation
                openPanel(store, ui, EditorPanel.Presets)
            },
            DialogAction(stringResource(R.string.common_close), DialogRole.Negative, tag = "presets.empty.close"),
        ),
    ) {
        Box(Modifier.fillMaxWidth().padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.WandStars, 64.dp, AureaColors.LightAccentInk)
        }
    }
}

// =============================================================================
// P2-16: aviso de mídia ausente, uma vez por camada
// =============================================================================

@Composable
private fun MissingMediaNotice(layerId: Long, bits: Int) {
    var show by remember(layerId) { mutableStateOf(false) }
    LaunchedEffect(layerId, bits and LayerPanelBits.MISSING_MEDIA) {
        if (bits and LayerPanelBits.MISSING_MEDIA != 0 && LayerPanelState.warnedMissing.add(layerId)) show = true
    }
    if (show) {
        AureaAlert(
            title = stringResource(R.string.om_lp_missing_media_title),
            message = stringResource(R.string.om_lp_missing_media),
            confirmLabel = stringResource(R.string.common_ok),
            showCancel = false,
            onConfirm = {},
            onDismiss = { show = false },
        )
    }
}

// =============================================================================
// Painéis embutidos: a velocidade do grupo (do canto de cima à esquerda) e o
// volume (do canto de cima à direita), 200 ms (P1-7, P1-8)
// =============================================================================

@Composable
private fun GroupSpeedPopout(store: EditorStore, l: PanelLayer, bits: Int, headHeight: Float, onClose: () -> Unit) {
    val close = stringResource(R.string.om_lp_speed_close)
    Column(
        Modifier
            .fillMaxSize()
            .testTag("layer.panel.groupSpeed")
            .padding(horizontal = 4.dp)
            .padding(top = 4.dp, bottom = 6.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(AureaColors.LayerPanelButton),
    ) {
        // A "aba": a Velocidade ATIVA (destaque) com o cue ▴; tocar fecha.
        Row(Modifier.fillMaxWidth().height((headHeight - 4f).dp)) {
            PanelPress(
                Modifier.weight(1f).fillMaxHeight().testTag("layer.panel.groupSpeed.close").semantics { contentDescription = close },
                onClick = onClose,
            ) {
                CupertinoIcon(CupertinoGlyph.Speedometer, 22.dp, AureaColors.Accent)
                CupertinoIcon(
                    CupertinoGlyph.ArrowtriangleDownFill, 8.dp, AureaColors.Accent,
                    Modifier.align(Alignment.BottomEnd).padding(end = 5.dp, bottom = 3.dp).graphicsLayer { rotationZ = 180f },
                )
            }
            Spacer(Modifier.weight(4f))
        }
        Box(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .clip(RoundedCornerShape(bottomStart = 4.dp, bottomEnd = 4.dp))
                .background(AureaColors.LayerPanelBody),
        ) {
            GroupSpeedBody(store, l.id, bits)
        }
    }
}

@Composable
private fun VolumePopout(store: EditorStore, ui: EditorUi, headHeight: Float, onClose: () -> Unit) {
    val close = stringResource(R.string.om_lp_volume_close)
    Column(Modifier.fillMaxSize().testTag("layer.panel.volume")) {
        // Cabeça transparente: só o Áudio PRESSIONADO na coluna 5; tocar fecha.
        Row(Modifier.fillMaxWidth().height(headHeight.dp)) {
            Spacer(Modifier.weight(4f))
            PanelPress(
                Modifier.weight(1f).fillMaxHeight().padding(top = (headHeight - 38f).coerceAtLeast(0f).dp)
                    .background(AureaColors.LayerPanelBody, RoundedCornerShape(topStart = 4.dp, topEnd = 4.dp))
                    .testTag("layer.panel.volume.close").semantics { contentDescription = close },
                onClick = onClose,
            ) {
                CupertinoIcon(CupertinoGlyph.Speaker2, 22.dp, AureaColors.Accent)
            }
        }
        Box(Modifier.fillMaxWidth().weight(1f).background(AureaColors.LayerPanelBody)) {
            VolumePopoutBody(store, onClose, onOpenPanel = { openPanel(store, ui, it) })
        }
    }
}

// =============================================================================
// Peças
// =============================================================================

/**
 * Botão do painel: sem fundo; pressionado = `LayerPanelPressed` (A11) com o
 * raio dado. O alvo é a célula inteira (≥ 44 dp na fileira de 38 com a vizinha).
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun PanelPress(
    modifier: Modifier,
    enabled: Boolean = true,
    radius: Dp = 0.dp,
    haptic: Boolean = false,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
    content: @Composable androidx.compose.foundation.layout.BoxScope.() -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val haptics = androidx.compose.ui.platform.LocalHapticFeedback.current
    Box(
        modifier
            .clip(RoundedCornerShape(radius))
            .background(if (pressed && enabled) AureaColors.LayerPanelPressed else Color.Transparent)
            .combinedClickable(
                interactionSource = interaction,
                indication = null,
                enabled = enabled,
                role = Role.Button,
                onLongClick = onLongClick,
                onClick = {
                    if (haptic) haptics.performHapticFeedback(androidx.compose.ui.hapticfeedback.HapticFeedbackType.TextHandleMove)
                    onClick()
                },
            )
            .alpha(if (enabled) 1f else 0.6f),
        contentAlignment = Alignment.Center,
        content = content,
    )
}

/** Os desenhos da fileira de tempo (próprios, no traço dos colchetes antigos). */
internal enum class TimeGlyph { TrimStart, Split, TrimEnd, MoveStart, ExtendStart, ExtendEnd, MoveEnd }

@Composable
internal fun TimeGlyphIcon(glyph: TimeGlyph, color: Color, modifier: Modifier) {
    Canvas(modifier) { drawTimeGlyph(glyph, color) }
}

private fun DrawScope.drawTimeGlyph(glyph: TimeGlyph, color: Color) {
    val u = size.width / 24f
    // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.Line).
    val stroke = AureaIconStyle.Line * u
    val line = Stroke(width = stroke, cap = StrokeCap.Round, join = StrokeJoin.Round)
    val dashed = Stroke(width = stroke, cap = StrokeCap.Round, join = StrokeJoin.Round,
        pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u)))
    fun bracket(outer: Float, inner: Float, dash: Boolean) {
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(outer * u, 6f * u); lineTo(inner * u, 6f * u); lineTo(inner * u, 18f * u); lineTo(outer * u, 18f * u)
        }
        drawPath(path, color, style = if (dash) dashed else line)
    }
    fun playhead(x: Float) = drawLine(color, Offset(x * u, 3.5f * u), Offset(x * u, 20.5f * u), stroke, StrokeCap.Round)
    fun clip(l: Float, r: Float) = drawRoundRect(
        color, topLeft = Offset(l * u, 8f * u), size = androidx.compose.ui.geometry.Size((r - l) * u, 8f * u),
        cornerRadius = androidx.compose.ui.geometry.CornerRadius(1.5f * u), style = line,
    )
    fun arrow(from: Float, to: Float) {
        drawLine(color, Offset(from * u, 12f * u), Offset(to * u, 12f * u), stroke, StrokeCap.Round)
        val d = if (to < from) 2.5f else -2.5f
        drawLine(color, Offset(to * u, 12f * u), Offset((to + d) * u, 9.5f * u), stroke, StrokeCap.Round)
        drawLine(color, Offset(to * u, 12f * u), Offset((to + d) * u, 14.5f * u), stroke, StrokeCap.Round)
    }
    when (glyph) {
        TimeGlyph.TrimStart, TimeGlyph.Split, TimeGlyph.TrimEnd -> {
            bracket(3f, 9f, glyph == TimeGlyph.TrimStart)
            bracket(21f, 15f, glyph == TimeGlyph.TrimEnd)
            playhead(12f)
        }
        // Cabeçote à esquerda: a camada inteira anda até ele…
        TimeGlyph.MoveStart -> { playhead(3f); clip(13f, 21f); arrow(11f, 5.5f) }
        // …ou só a borda de início estica até ele (tracejado = o trecho novo).
        TimeGlyph.ExtendStart -> {
            playhead(3f); clip(13f, 21f)
            drawLine(color, Offset(5.5f * u, 8f * u), Offset(13f * u, 8f * u), stroke, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u)))
            drawLine(color, Offset(5.5f * u, 16f * u), Offset(13f * u, 16f * u), stroke, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u)))
        }
        TimeGlyph.ExtendEnd -> {
            playhead(21f); clip(3f, 11f)
            drawLine(color, Offset(11f * u, 8f * u), Offset(18.5f * u, 8f * u), stroke, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u)))
            drawLine(color, Offset(11f * u, 16f * u), Offset(18.5f * u, 16f * u), stroke, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.2f * u, 2.0f * u)))
        }
        TimeGlyph.MoveEnd -> { playhead(21f); clip(3f, 11f); arrow(13f, 18.5f) }
    }
}

/** Aviso curto no idioma escolhido no app (fora do Compose). */
internal fun EditorStore.toastText(@StringRes id: Int) = showToast(AppText.get(getApplication<Application>(), id))
