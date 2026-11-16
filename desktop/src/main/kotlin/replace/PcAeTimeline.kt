package com.aurea.aurea.editor

import android.graphics.Paint
import android.graphics.RectF
import android.util.LongSparseArray
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.PointerIcon
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.isCtrlPressed
import androidx.compose.ui.input.pointer.isPrimaryPressed
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.isShiftPressed
import androidx.compose.ui.input.pointer.pointerHoverIcon
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.timeline.ClipColors
import com.aurea.aurea.editor.timeline.ClipGeometry
import com.aurea.aurea.editor.timeline.Keyframes
import com.aurea.aurea.editor.timeline.MarkerColors
import com.aurea.aurea.editor.timeline.MiddleEllipsis
import com.aurea.aurea.editor.timeline.OmegaRuler
import com.aurea.aurea.editor.timeline.RowCache
import com.aurea.aurea.editor.timeline.RowModel
import com.aurea.aurea.editor.timeline.Snap
import com.aurea.aurea.editor.timeline.ThumbStrip
import com.aurea.aurea.editor.timeline.Thumbs
import com.aurea.aurea.editor.timeline.TimeAxis
import com.aurea.aurea.editor.timeline.Timecode
import com.aurea.aurea.editor.timeline.TimelineMetrics
import com.aurea.aurea.editor.timeline.TimelineZoomRequests
import com.aurea.aurea.editor.timeline.WaveGrid
import com.aurea.aurea.editor.timeline.WaveSource
import com.aurea.aurea.editor.timeline.WaveStrip
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.MenuIconCell
import com.aurea.aurea.ui.platform.platformCanvas
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.AureaTimeline
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.CupertinoIconsFont
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import java.awt.Cursor
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

// =============================================================================
//  A timeline do Aurea PC no jeito do After Effects, com a cara do Aurea.
//
//   ┌ esquerda: camadas (pílulas do Aurea) ───────┬ direita: régua e tempo ────────┐
//   │ ▮ 👁 ● 🔒 ▸ ▣ 1 Nome   tímida fx MB aj 3D Pai │ ▕▓▓▓ cartão do Aurea ▓▓▓▏      │
//   │     ▾ Transformar                           │                                │
//   │       ⏱ Posição   960, 540     ◀ ◆ ▶        │     ◆          ◆               │
//   └─────────────────────────────────────────────┴────────────────────────────────┘
//
//  Estrutura e mouse do AE: uma linha por camada, o 0 à esquerda, a área de
//  trabalho no alto da régua, o cabeçote andando. O desenho é o da timeline do
//  Aurea (TimelinePainter, ramo Omega): cartão na cor do tipo, zebra, tira de
//  miniaturas, onda, realce, etiqueta, rótulo, losangos, aba do tempo e tiques.
//  As peças vêm dos mesmos tokens e ajudantes (AureaTimeline, ClipColors,
//  ThumbStrip, WaveStrip, OmegaRuler, TimelineMetrics); a edição vai ao motor
//  ao soltar (um desfazer por gesto).
// =============================================================================

/** Uma propriedade do Transformar: as trilhas (eixos) e como mostrar o valor. */
private class AeProp(val label: Int, val props: IntArray, val factor: Float, val unit: String, val step: Float, val read: (LayerDetail) -> FloatArray)

private val AE_PROPS = listOf(
    AeProp(R.string.fx_ancora, intArrayOf(9, 10), 1f, "", 1f) { floatArrayOf(it.anchor.getOrElse(0) { 0f }, it.anchor.getOrElse(1) { 0f }) },
    AeProp(R.string.fx_posicao, intArrayOf(0, 1), 1f, "", 1f) { floatArrayOf(it.position.getOrElse(0) { 0f }, it.position.getOrElse(1) { 0f }) },
    AeProp(R.string.fx_escala, intArrayOf(3, 4), 100f, "%", 0.5f) { floatArrayOf(it.scale.getOrElse(0) { 1f }, it.scale.getOrElse(1) { 1f }) },
    AeProp(R.string.fx_rotacao, intArrayOf(8), 1f, "°", 0.5f) { floatArrayOf(it.rotation.getOrElse(2) { 0f }) },
    AeProp(R.string.fx_opacidade, intArrayOf(12), 100f, "%", 0.5f) { floatArrayOf(it.opacity) },
)
/** O Transformar de uma camada 3D (como no AE): Z na âncora, posição e escala; giro X, Y e Z separados. */
private val AE_PROPS_3D = listOf(
    AeProp(R.string.fx_ancora, intArrayOf(9, 10, 11), 1f, "", 1f) {
        floatArrayOf(it.anchor.getOrElse(0) { 0f }, it.anchor.getOrElse(1) { 0f }, it.anchor.getOrElse(2) { 0f })
    },
    AeProp(R.string.fx_posicao, intArrayOf(0, 1, 2), 1f, "", 1f) {
        floatArrayOf(it.position.getOrElse(0) { 0f }, it.position.getOrElse(1) { 0f }, it.position.getOrElse(2) { 0f })
    },
    AeProp(R.string.fx_escala, intArrayOf(3, 4, 5), 100f, "%", 0.5f) {
        floatArrayOf(it.scale.getOrElse(0) { 1f }, it.scale.getOrElse(1) { 1f }, it.scale.getOrElse(2) { 1f })
    },
    AeProp(R.string.edt_rotation_x, intArrayOf(6), 1f, "°", 0.5f) { floatArrayOf(it.rotation.getOrElse(0) { 0f }) },
    AeProp(R.string.edt_rotation_y, intArrayOf(7), 1f, "°", 0.5f) { floatArrayOf(it.rotation.getOrElse(1) { 0f }) },
    AeProp(R.string.edt_rotation_z, intArrayOf(8), 1f, "°", 0.5f) { floatArrayOf(it.rotation.getOrElse(2) { 0f }) },
    AeProp(R.string.fx_opacidade, intArrayOf(12), 100f, "%", 0.5f) { floatArrayOf(it.opacity) },
)
private val AE_TRANSFORM_PROPS = (AE_PROPS + AE_PROPS_3D).flatMap { it.props.asIterable() }.distinct().toIntArray()

/** Os grupos de propriedades de uma camada aberta, na ordem do AE. */
private enum class AeGroup { Text, Masks, Effects, Transform, Material, Audio }

/** O que uma linha de propriedade (fora do Transformar) mostra. */
private sealed interface AeItem
/** Um efeito da camada: nome, ligar/desligar; abre os parâmetros animados embaixo. */
private class AeFxItem(val effect: com.aurea.aurea.engine.LayerEffect, val twirl: Boolean) : AeItem
/** Um parâmetro animado de um efeito. */
private class AeFxParamItem(val effectId: Int, val typeId: Int, val index: Int) : AeItem
/** Uma máscara: nome, modo e inverter; abre caminho, suavizar, opacidade e expandir. */
private class AeMaskItem(val maskId: Int, val number: Int) : AeItem
/** Propriedade da máscara: −1 = caminho; 0 suavizar, 1 expandir, 2 opacidade (os índices do motor). */
private class AeMaskPropItem(val maskId: Int, val param: Int) : AeItem
private data object AeSourceTextItem : AeItem
/** Opções de material: 0 aceita luzes, 1 projeta sombra, 2 recebe sombra. */
private class AeMaterialItem(val which: Int) : AeItem
private data object AeAudioItem : AeItem

private val AE_TEXT_TRACKS = intArrayOf(
    com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE, com.aurea.aurea.engine.TrackProperty.TEXT_ANIM_PARAM,
    com.aurea.aurea.engine.TrackProperty.TEXT_TRACKING, com.aurea.aurea.engine.TrackProperty.TEXT_PATH_FIRST_MARGIN,
    com.aurea.aurea.engine.TrackProperty.TEXT_PATH_LAST_MARGIN,
)

/** As chaves que a trilha de cada grupo junta. */
private fun groupMatch(g: AeGroup): (KeyframeRow) -> Boolean = when (g) {
    AeGroup.Transform -> { k -> k.effectIndex < 0 && k.property in AE_TRANSFORM_PROPS }
    AeGroup.Text -> { k -> k.property in AE_TEXT_TRACKS }
    AeGroup.Masks -> { k -> k.property == com.aurea.aurea.engine.TrackProperty.MASK_PARAM || k.property == com.aurea.aurea.engine.TrackProperty.MASK_PATH }
    AeGroup.Effects -> { k -> k.property == com.aurea.aurea.engine.TrackProperty.EFFECT_PARAM || k.property == com.aurea.aurea.engine.TrackProperty.MESH_WARP }
    AeGroup.Material -> { k -> k.property == com.aurea.aurea.engine.TrackProperty.MATERIAL_PARAM }
    AeGroup.Audio -> { k -> k.property == com.aurea.aurea.engine.TrackProperty.AUDIO_VOLUME }
}

/** O nome do grupo (os textos do Aurea). */
@Composable
private fun groupLabel(g: AeGroup): String = stringResource(when (g) {
    AeGroup.Transform -> R.string.fx_name_transform
    AeGroup.Text -> R.string.panel_texto
    AeGroup.Masks -> R.string.panel_mascaras
    AeGroup.Effects -> R.string.panel_efeitos
    AeGroup.Material -> R.string.pc_tl_material
    AeGroup.Audio -> R.string.sh_add_tab_audio
})

/** Duplo clique no grupo: o painel do Aurea dele (na janela flutuante; Efeitos ficam à direita). */
private fun openGroupPanel(store: EditorStore, ui: EditorUi, id: Long, g: AeGroup) {
    if (store.primary != id || store.selection.size != 1) store.select(id)
    val panel = when (g) {
        AeGroup.Transform -> com.aurea.aurea.editor.panels.EditorPanel.Transform
        AeGroup.Text -> com.aurea.aurea.editor.panels.EditorPanel.Text
        AeGroup.Masks -> com.aurea.aurea.editor.panels.EditorPanel.Mask
        AeGroup.Effects -> com.aurea.aurea.editor.panels.EditorPanel.Effects
        AeGroup.Material -> com.aurea.aurea.editor.panels.EditorPanel.Element3D
        AeGroup.Audio -> com.aurea.aurea.editor.panels.EditorPanel.Audio
    }
    pcOpenPanel(store, ui, panel)
}

/** "Animar ▸" do texto: os animadores do Aurea (Transformar · Animadores) na janela flutuante. */
private fun openTextAnimators(store: EditorStore, ui: EditorUi, id: Long) {
    if (store.primary != id || store.selection.size != 1) store.select(id)
    com.aurea.aurea.editor.panels.PanelRequests.transformTab = com.aurea.aurea.editor.panels.TransformTab.Animadores
    pcOpenPanel(store, ui, com.aurea.aurea.editor.panels.EditorPanel.Transform)
}

/** Medidas (dp). As do cartão e dos losangos vêm do [TimelineMetrics] da Omega. */
private object AeDims {
    const val RULER = 46f          // régua: área de trabalho, tiques, aba do tempo
    const val WORK_TOP = 2f        // faixa da área de trabalho
    const val WORK_H = 6f
    const val TICK_TOP = 10f       // tiques pendurados daqui (segundo 18, meio 12, quadro 5)
    const val TAB_TOP = 28f        // aba do tempo atual (18 de altura)
    const val ROW = 28f            // camada comum (cartão de 23)
    const val ROW_VIDEO = 54f      // vídeo: miniaturas e a onda embaixo
    const val ROW_AUDIO = 40f      // áudio: a onda ocupa o cartão
    const val GROUP = 22f          // "Transformar"
    const val PROP = 24f           // uma propriedade (a trilha do Aurea tem 24)
    const val CARD_PAD = 2.5f      // vão acima e abaixo do cartão
    const val LEFT_PAD = 36f       // o quadro 0 não encosta na borda (a aba do tempo cabe inteira)
    const val EDGE = 6f            // alcance das pontas (aparar)
    const val HANDLE = 8f          // alças de aparar desenhadas na camada escolhida
    const val SNAP = 8f            // ímã
    const val VIDEO_WAVE = 0.42f   // parte de baixo do cartão de vídeo com a onda
    const val OUTLINE_MIN = 340f
    const val OUTLINE_MAX = 900f
}

/** Uma linha da timeline: camada, o grupo Transformar ou uma propriedade (topo e altura em px). */
private class AeLine(
    val kind: Int,
    val layer: LayerRow,
    val row: RowModel,
    val number: Int,
    val prop: AeProp?,
    val top: Float,
    val height: Float,
    /** Instantes com keyframe (quadros da timeline) e as chaves de cada um, nesta linha. */
    val instants: IntArray,
    val keysAt: Array<List<KeyframeRow>>,
    /** Grupo (linha de grupo) ou o grupo a que a propriedade pertence. */
    val group: AeGroup? = null,
    /** O que a propriedade mostra fora do Transformar (nulo = [prop]). */
    val item: AeItem? = null,
    /** As chaves desta linha (nulo = a camada inteira). */
    val match: ((KeyframeRow) -> Boolean)? = null,
    /** Chave estável (composição e o estado aberto/fechado). */
    val key: String = "",
    /** Recuo: 0 propriedade, 1 sub-propriedade (parâmetro de efeito, propriedade da máscara). */
    val depth: Int = 0,
    /** Grupo ou item aberto (a seta). */
    val open: Boolean = false,
) {
    companion object {
        const val LAYER = 0
        const val GROUP = 1
        const val PROP = 2
    }
}

private class AeLayout(val lines: List<AeLine>, val contentHeight: Float) {
    /** Linha no y do conteúdo (−1 = nenhuma). */
    fun indexAt(y: Float): Int {
        if (y < 0f || lines.isEmpty()) return -1
        var lo = 0
        var hi = lines.lastIndex
        while (lo <= hi) {
            val mid = (lo + hi) ushr 1
            val l = lines[mid]
            when {
                y < l.top -> hi = mid - 1
                y >= l.top + l.height -> lo = mid + 1
                else -> return mid
            }
        }
        return -1
    }
}

/** Estado de apresentação: zoom, rolagem e as prévias dos gestos. Nada aqui é do projeto. */
@Stable
private class AeView {
    /** Zoom em dp por segundo (0 = ainda não enquadrado). */
    var pps by mutableFloatStateOf(0f)
    /** Quadro na borda esquerda das trilhas. */
    var origin by mutableDoubleStateOf(0.0)
    var scrollY by mutableFloatStateOf(0f)
    var width by mutableIntStateOf(0)
    var height by mutableIntStateOf(0)
    var redraw by mutableIntStateOf(0)
    var cursor by mutableIntStateOf(Cursor.DEFAULT_CURSOR)
    var dragging = false
    // Mover: camadas e deslocamento.
    var moveIds by mutableStateOf(LongArray(0))
    var moveDelta by mutableIntStateOf(0)
    // Aparar: camada, ponta e o quadro novo.
    var trimId by mutableLongStateOf(0L)
    var trimIsStart by mutableStateOf(false)
    var trimFrame by mutableIntStateOf(Snap.NONE)
    // Losango na mão: camada, instante de origem e deslocamento.
    var keyLayer by mutableLongStateOf(0L)
    var keyFrom by mutableIntStateOf(Snap.NONE)
    var keyDelta by mutableIntStateOf(0)
    /** Fio do ímã (quadro). */
    var guide by mutableIntStateOf(Snap.NONE)
    var workDraft by mutableStateOf<Pair<Int, Int>?>(null)
    /** Reordenar pela coluna: camada segurada e o y do conteúdo sob o mouse. */
    var reorderId by mutableLongStateOf(0L)
    var reorderY by mutableFloatStateOf(Float.NaN)

    fun isMoving(id: Long): Boolean {
        val ids = moveIds
        for (i in ids.indices) if (ids[i] == id) return true
        return false
    }

    fun ppf(density: Float, fps: Float): Float = TimeAxis.pxPerFrame(max(pps, 0.01f), density, fps)
}

/** O menu do botão direito: da camada (ou do vazio), dos keyframes escolhidos ou da régua ([frame] = o quadro clicado). */
private class AeMenu(val x: Float, val y: Float, val layer: Long?, val kind: Int = LAYER, val frame: Int = 0) {
    companion object {
        const val LAYER = 0
        const val KEYS = 1
        const val RULER = 2
        /** Lote M: a linha de uma máscara (`frame` leva o id dela). */
        const val MASK = 3
    }
}

private fun clampPps(pps: Float): Float = if (pps.isFinite()) pps.coerceIn(0.2f, 4000f) else 100f

/** Onde ficam os losangos de uma linha (o mesmo y para pintar e para o mouse). */
private fun keyCenterY(line: AeLine, lineTop: Float, d: Float): Float {
    if (line.kind != AeLine.LAYER) return lineTop + line.height / 2f
    val cardTop = lineTop + AeDims.CARD_PAD * d
    val cardH = line.height - 2f * AeDims.CARD_PAD * d
    return if (line.row.type == LayerType.Video) cardTop + cardH * (1f - AeDims.VIDEO_WAVE) / 2f else cardTop + cardH / 2f
}

/** Instantes das trilhas [props] (Transformar) de uma camada, em quadros da timeline. */
private fun laneKeys(l: LayerRow, keys: List<KeyframeRow>, props: IntArray): Pair<IntArray, Array<List<KeyframeRow>>> =
    laneKeys(l, keys) { it.effectIndex < 0 && it.property in props }

/** Instantes das chaves que [match] aceita, em quadros da timeline (as do mesmo instante juntas). */
private fun laneKeys(l: LayerRow, keys: List<KeyframeRow>, match: (KeyframeRow) -> Boolean): Pair<IntArray, Array<List<KeyframeRow>>> {
    val mine = keys.filter { it.timelineVisible && match(it) }.sortedBy { it.time }
    val times = ArrayList<Int>()
    val groups = ArrayList<List<KeyframeRow>>()
    var from = 0
    while (from < mine.size) {
        var to = from + 1
        while (to < mine.size && mine[to].time == mine[from].time) to++
        times.add(Keyframes.toTimeline(mine[from].time, l.startFrame, l.offsetFrames))
        groups.add(mine.subList(from, to))
        from = to
    }
    return times.toIntArray() to groups.toTypedArray()
}

// =============================================================================
@Composable
internal fun PcAeTimeline(store: EditorStore, ui: EditorUi, modifier: Modifier) {
    val density = LocalDensity.current
    val d = density.density
    val measurer = rememberTextMeasurer(cacheSize = 64)
    val painter = remember(d, measurer) { AePainter(d, measurer) }
    val view = remember { AeView() }
    val outlineThumbs = remember { ThumbStrip() }
    val rowCache = remember { RowCache() }
    val expanded = remember { mutableStateMapOf<Long, Boolean>() }
    /** Grupos e itens abertos ("camada/grupo", "camada/fx/efeito", "camada/mask/máscara"). */
    val groupOpen = remember { mutableStateMapOf<String, Boolean>() }
    /** Vídeos com som (lido do motor uma vez por mudança de camadas). */
    val audioOf = remember(store.layers) { HashMap<Long, Boolean>() }
    var search by remember { mutableStateOf("") }
    var outlineW by remember { mutableFloatStateOf(520f) }
    var menu by remember { mutableStateOf<AeMenu?>(null) }
    val rulerPx = AeDims.RULER * d
    val leftPad = AeDims.LEFT_PAD * d

    // Linhas: a ordem do store (a camada da frente em cima, como no AE); as tímidas
    // saem com "Ocultar camadas tímidas"; a busca filtra pelo nome.
    val rows = rowCache.build(store.layers, store.keyframes)
    // Efeitos e máscaras: o motor publica os da camada escolhida (as outras mostram só o grupo).
    val primary = store.primary
    val fxList = store.effects.filter { com.aurea.aurea.editor.panels.fxShownInEffectList(it) }
    val maskState = store.masks?.takeIf { it.layer == primary }
    val layout = run {
        val lines = ArrayList<AeLine>(store.layers.size + 8)
        var y = 0f
        val query = search.trim()
        store.layers.forEachIndexed { i, l ->
            if (store.hideShy && l.shy) return@forEachIndexed
            if (query.isNotEmpty() && !l.name.contains(query, ignoreCase = true)) return@forEachIndexed
            val r = rows.getOrNull(i) ?: return@forEachIndexed
            val h = when (r.type) {
                LayerType.Video -> AeDims.ROW_VIDEO
                LayerType.Audio -> AeDims.ROW_AUDIO
                else -> AeDims.ROW
            } * d
            lines += AeLine(AeLine.LAYER, l, r, i + 1, null, y, h, r.instants, r.keysAt)
            y += h
            if (expanded[l.id] == true) {
                val keys = store.keyframes[l.id].orEmpty()
                val number = i + 1
                val isPrimary = l.id == primary
                val threeD = l.isThreeD || l.kind in 8..10
                // Um grupo (a seta e a trilha que junta as chaves dele) e, aberto, as linhas de [children].
                fun group(g: AeGroup, defaultOpen: Boolean, children: () -> Unit) {
                    val key = "${l.id}/${g.name}"
                    val open = groupOpen[key] ?: defaultOpen
                    val match = groupMatch(g)
                    val (gi, gk) = laneKeys(l, keys, match)
                    lines += AeLine(AeLine.GROUP, l, r, number, null, y, AeDims.GROUP * d, gi, gk, group = g, match = match, key = key, open = open)
                    y += AeDims.GROUP * d
                    if (open) children()
                }
                fun prop(g: AeGroup, key: String, item: AeItem?, p: AeProp?, match: (KeyframeRow) -> Boolean, depth: Int = 0, open: Boolean = false) {
                    val (pi, pk) = laneKeys(l, keys, match)
                    lines += AeLine(AeLine.PROP, l, r, number, p, y, AeDims.PROP * d, pi, pk,
                        group = g, item = item, match = match, key = key, depth = depth, open = open)
                    y += AeDims.PROP * d
                }
                // Texto: o texto-fonte (as linhas dependem da camada escolhida: o motor só fala dela).
                if (r.type == LayerType.Text) group(AeGroup.Text, true) {
                    if (isPrimary) prop(AeGroup.Text, "${l.id}/text/source", AeSourceTextItem, null, { it.property == com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE })
                }
                // Máscaras: uma linha por máscara; aberta, caminho, suavizar, opacidade e expandir.
                val masks = maskState?.masks.orEmpty()
                if (if (isPrimary) masks.isNotEmpty() else l.maskCount > 0) group(AeGroup.Masks, true) {
                    if (isPrimary) masks.forEachIndexed { n, m ->
                        val mk = "${l.id}/mask/${m.id}"
                        val open = groupOpen[mk] ?: false
                        val ofMask: (KeyframeRow) -> Boolean = { k -> k.effectIndex == m.id && (k.property == com.aurea.aurea.engine.TrackProperty.MASK_PARAM || k.property == com.aurea.aurea.engine.TrackProperty.MASK_PATH) }
                        prop(AeGroup.Masks, mk, AeMaskItem(m.id, n + 1), null, ofMask, open = open)
                        if (open) {
                            prop(AeGroup.Masks, "$mk/path", AeMaskPropItem(m.id, -1), null,
                                { k -> k.effectIndex == m.id && k.property == com.aurea.aurea.engine.TrackProperty.MASK_PATH }, depth = 1)
                            for (param in intArrayOf(0, 2, 1)) {
                                prop(AeGroup.Masks, "$mk/$param", AeMaskPropItem(m.id, param), null,
                                    { k -> k.effectIndex == m.id && k.property == com.aurea.aurea.engine.TrackProperty.MASK_PARAM && k.paramIndex == param }, depth = 1)
                            }
                        }
                    }
                }
                // Efeitos: um por linha (ligar/desligar, clique abre nos Controles de efeitos); os parâmetros animados embaixo.
                if (if (isPrimary) fxList.isNotEmpty() else l.effectCount > 0) group(AeGroup.Effects, true) {
                    if (isPrimary) fxList.forEach { e ->
                        val ek = "${l.id}/fx/${e.effectId}"
                        val ofFx: (KeyframeRow) -> Boolean = { k -> k.effectIndex == e.effectId && (k.property == com.aurea.aurea.engine.TrackProperty.EFFECT_PARAM || k.property == com.aurea.aurea.engine.TrackProperty.MESH_WARP) }
                        val animated = store.effectParams[e.effectId].orEmpty().filter { p ->
                            val s = com.aurea.aurea.editor.panels.ParamSlot.of(p)
                            !s.hidden && s.animatable && keys.any { k -> k.property == com.aurea.aurea.engine.TrackProperty.EFFECT_PARAM && k.effectIndex == e.effectId && k.paramIndex / 4 == p.index }
                        }
                        val open = groupOpen[ek] ?: true
                        prop(AeGroup.Effects, ek, AeFxItem(e, animated.isNotEmpty()), null, ofFx, open = open)
                        if (open) animated.forEach { p ->
                            prop(AeGroup.Effects, "$ek/${p.index}", AeFxParamItem(e.effectId, e.typeId, p.index), null,
                                { k -> k.property == com.aurea.aurea.engine.TrackProperty.EFFECT_PARAM && k.effectIndex == e.effectId && k.paramIndex / 4 == p.index }, depth = 1)
                        }
                    }
                }
                // Transformar (o áudio não tem, como no AE); camada 3D com Z e o giro em X, Y e Z.
                if (r.type != LayerType.Audio) group(AeGroup.Transform, true) {
                    (if (threeD) AE_PROPS_3D else AE_PROPS).forEach { p ->
                        prop(AeGroup.Transform, "${l.id}/tf/${p.props.first()}/${p.props.size}", null, p, { k -> k.effectIndex < 0 && k.property in p.props })
                    }
                }
                // Opções de material (camada 3D e objeto 3D): luzes e sombras, quando o motor tem.
                if (threeD || r.type == LayerType.Model3D) {
                    val material = if (!isPrimary) emptyList() else buildList {
                        if (store.layerAcceptsLights >= 0) add(0)
                        if (store.modelShadows != null) { add(1); add(2) }
                    }
                    if (!isPrimary || material.isNotEmpty()) group(AeGroup.Material, false) {
                        material.forEach { w -> prop(AeGroup.Material, "${l.id}/mat/$w", AeMaterialItem(w), null, { false }) }
                    }
                }
                // Áudio: os níveis (volume) com cronômetro.
                val hasAudio = when (r.type) {
                    LayerType.Audio -> true
                    LayerType.Video -> if (isPrimary) store.detail?.hasAudio == true else audioOf.getOrPut(l.id) { store.layerDetailOf(l.id)?.hasAudio == true }
                    else -> false
                }
                if (hasAudio) group(AeGroup.Audio, true) {
                    if (isPrimary) prop(AeGroup.Audio, "${l.id}/audio", AeAudioItem, null, { it.property == com.aurea.aurea.engine.TrackProperty.AUDIO_VOLUME })
                }
            }
        }
        AeLayout(lines, y)
    }
    val layoutNow = rememberUpdatedState(layout)
    fun maxScroll(): Float = max(0f, layoutNow.value.contentHeight - (view.height - rulerPx) + 24f * d)
    fun toggleTransform(id: Long) { expanded[id] = expanded[id] != true }

    // Zoom pedido de fora (=/−, botões, Alt/Ctrl + roda): o cabeçote fica no lugar se está à vista.
    LaunchedEffect(Unit) {
        TimelineZoomRequests.flow.collect { f ->
            val fps = TimeAxis.safeFps(store.project.fps)
            val before = view.ppf(d, fps)
            val head = store.playhead.toDouble()
            val hx = leftPad + ((head - view.origin) * before).toFloat()
            val anchor = if (hx in 0f..view.width.toFloat()) head else view.origin + (view.width / 2f - leftPad) / before
            val ax = leftPad + ((anchor - view.origin) * before).toFloat()
            view.pps = clampPps(view.pps * f)
            view.origin = max(0.0, anchor - (ax - leftPad) / view.ppf(d, fps))
        }
    }
    // Enquadra a composição na largura ao abrir o projeto.
    LaunchedEffect(store.project.path) {
        view.pps = 0f
        view.scrollY = 0f
        view.origin = 0.0
        snapshotFlow { Triple(view.width, store.project.durationFrames, view.pps) }.collect { (w, dur, pps) ->
            if (w > 0 && dur > 0 && pps <= 0f) {
                val seconds = dur / TimeAxis.safeFps(store.project.fps)
                view.pps = clampPps((w - leftPad - 32f * d) / d / max(seconds, 0.5f))
                view.origin = 0.0
            }
        }
    }
    // Tocando (ou indo a um ponto fora da vista): vira a página como o AE.
    LaunchedEffect(Unit) {
        snapshotFlow { store.playhead }.collect { p ->
            if (view.dragging || view.width <= 0 || view.pps <= 0f) return@collect
            val ppf = view.ppf(d, TimeAxis.safeFps(store.project.fps))
            val x = leftPad + ((p - view.origin) * ppf).toFloat()
            if (x < leftPad * 0.5f || x > view.width - 12f * d) {
                view.origin = max(0.0, p - 0.05 * (view.width - leftPad) / ppf)
            }
        }
    }

    androidx.compose.foundation.layout.Box(
        modifier.background(AureaColors.EditorCanvas).pointerInput(Unit) {
            // Roda: rola as linhas (as duas colunas juntas); Shift (ou roda lateral): o tempo;
            // Ctrl: zoom (Alt + roda já é zoom na janela). Botão direito: o menu do AE.
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent(PointerEventPass.Initial)
                    if (ev.type == PointerEventType.Scroll) {
                        if (ev.changes.any { it.isConsumed }) continue
                        val c = ev.changes.firstOrNull() ?: continue
                        val dy = c.scrollDelta.y
                        val dx = c.scrollDelta.x
                        val mods = ev.keyboardModifiers
                        val fps = TimeAxis.safeFps(store.project.fps)
                        when {
                            mods.isCtrlPressed -> if (dy != 0f) TimelineZoomRequests.request(if (dy < 0f) 1.25f else 1f / 1.25f)
                            mods.isShiftPressed || dx != 0f -> {
                                val amount = if (mods.isShiftPressed) dy else dx
                                view.origin = max(0.0, view.origin + amount * 60f * d / view.ppf(d, fps))
                            }
                            else -> view.scrollY = (view.scrollY + dy * 36f * d).coerceIn(0f, maxScroll())
                        }
                        ev.changes.forEach { it.consume() }
                    } else if (ev.type == PointerEventType.Press && ev.buttons.isSecondaryPressed) {
                        val p = ev.changes.firstOrNull()?.position ?: continue
                        ev.changes.forEach { it.consume() }
                        val lay = layoutNow.value
                        val line = if (p.y > rulerPx) lay.lines.getOrNull(lay.indexAt(p.y - rulerPx + view.scrollY)) else null
                        val id = line?.layer?.id
                        if (id != null && id !in store.selection) store.select(id)
                        // Nas trilhas (à direita da divisória): um losango abre os assistentes de keyframe;
                        // a régua, a área de trabalho.
                        val tx = p.x - (outlineW + 5f) * d
                        if (tx >= 0f) {
                            val ppf = view.ppf(d, TimeAxis.safeFps(store.project.fps))
                            if (line != null && id != null) {
                                val ki = keyAt(line, Offset(tx, p.y), rulerPx + line.top - view.scrollY, view.origin, ppf, leftPad, d)
                                if (ki >= 0) {
                                    val group = line.keysAt[ki]
                                    // Um losango fora da seleção vira a seleção; dentro dela, o menu age em todos.
                                    if (store.keySelection?.containsAnyOn(id, group) != true) store.tapTimelineKey(id, group.first(), group)
                                    menu = AeMenu(p.x, p.y, id, AeMenu.KEYS)
                                    continue
                                }
                            } else if (p.y <= rulerPx) {
                                val frame = (view.origin + (tx - leftPad) / ppf).roundToInt().coerceAtLeast(0)
                                menu = AeMenu(p.x, p.y, null, AeMenu.RULER, frame)
                                continue
                            }
                        }
                        // Lote M: a linha de uma máscara tem o menu dela (RotoBezier, travar, suavizar, nulos).
                        val maskItem = line?.item as? AeMaskItem
                        menu = if (id != null && maskItem != null) AeMenu(p.x, p.y, id, AeMenu.MASK, maskItem.maskId) else AeMenu(p.x, p.y, id)
                    }
                }
            }
        },
    ) {
        Row(Modifier.fillMaxSize()) {
            // ---- Esquerda: as camadas e o Transformar ---------------------------------------
            Column(Modifier.width(outlineW.dp).fillMaxHeight()) {
                AeOutlineHeader(store, search) { search = it; view.scrollY = 0f }
                AeOutline(store, ui, view, layout, outlineThumbs, rulerPx, expanded, groupOpen, ::toggleTransform)
            }
            // Divisória arrastável entre a coluna das camadas e as trilhas.
            Box(
                Modifier.width(5.dp).fillMaxHeight().background(AureaColors.Hairline.copy(alpha = 0.5f))
                    .pointerHoverIcon(PointerIcon(Cursor(Cursor.E_RESIZE_CURSOR)))
                    .pointerInput(Unit) {
                        detectHorizontalDragGestures { change, dx ->
                            change.consume()
                            outlineW = (outlineW + dx / d).coerceIn(AeDims.OUTLINE_MIN, AeDims.OUTLINE_MAX)
                        }
                    },
            )
            // ---- Direita: régua, cartões e losangos (um Canvas, um gesto) -------------------
            AeTracks(store, view, painter, layout, layoutNow, rulerPx, leftPad, ::toggleTransform, Modifier.weight(1f).fillMaxHeight())
        }
        menu?.let { m ->
            AeContextMenu(store, ui, m, expanded, ::toggleTransform) { menu = null }
        }
    }
}

// =============================================================================
// Trilhas (direita)
// =============================================================================

@Composable
private fun AeTracks(
    store: EditorStore, view: AeView, painter: AePainter, layout: AeLayout,
    layoutNow: androidx.compose.runtime.State<AeLayout>, rulerPx: Float, leftPad: Float,
    onToggleTransform: (Long) -> Unit, modifier: Modifier,
) {
    val d = LocalDensity.current.density
    val toggleNow = rememberUpdatedState(onToggleTransform)
    Canvas(
        modifier.clipToBounds()
            .onSizeChanged { view.width = it.width; view.height = it.height }
            .pointerHoverIcon(PointerIcon(Cursor(view.cursor)))
            .pointerInput(Unit) {
                // Cursor de aparar sobre as pontas e as alças da área de trabalho.
                awaitPointerEventScope {
                    while (true) {
                        val ev = awaitPointerEvent()
                        if (ev.type != PointerEventType.Move && ev.type != PointerEventType.Enter) continue
                        if (view.dragging) continue
                        val p = ev.changes.firstOrNull()?.position ?: continue
                        view.cursor = hoverCursor(store, view, layoutNow.value, p, rulerPx, leftPad, d)
                    }
                }
            }
            .pointerInput(Unit) {
                var lastAt = 0L
                var lastPos = Offset.Zero
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    if (!currentEvent.buttons.isPrimaryPressed) return@awaitEachGesture
                    val mods = currentEvent.keyboardModifiers
                    val double = down.uptimeMillis - lastAt < 400L && (down.position - lastPos).getDistance() < 6f * d
                    lastAt = if (double) 0L else down.uptimeMillis
                    lastPos = down.position
                    view.dragging = true
                    try {
                        trackGesture(store, view, layoutNow.value, down, double, mods.isCtrlPressed || mods.isShiftPressed,
                            rulerPx, leftPad, d, viewConfiguration.touchSlop) { toggleNow.value(it) }
                    } finally {
                        view.dragging = false
                        view.guide = Snap.NONE
                    }
                }
            },
    ) {
        painter.draw(this, store, view, layout, rulerPx, leftPad)
    }
}

/** Segue o botão até soltar; [onDrag] a cada passo depois do slop. Devolve se arrastou. */
private suspend fun AwaitPointerEventScope.followDrag(down: PointerInputChange, slop: Float, onDrag: (Offset) -> Unit): Boolean {
    var dragging = false
    while (true) {
        val ev = awaitPointerEvent()
        val c = ev.changes.firstOrNull { it.id == down.id } ?: break
        if (!c.pressed) {
            c.consume()
            break
        }
        if (!dragging && (c.position - down.position).getDistance() < slop) continue
        dragging = true
        c.consume()
        onDrag(c.position)
    }
    return dragging
}

/** Alvos do ímã: cabeçote (à parte), marcadores, 0 e as bordas das camadas fora de [skip]. */
private fun snapTargets(store: EditorStore, skip: (Long) -> Boolean, extra: IntArray = IntArray(0)): IntArray {
    val mk = store.markers.frames
    val buf = IntArray(mk.size + store.layers.size * 2 + extra.size + 1)
    var n = 0
    buf[n++] = 0
    for (f in mk) buf[n++] = f
    for (l in store.layers) if (!skip(l.id)) {
        buf[n++] = l.startFrame
        buf[n++] = l.endFrame
    }
    for (f in extra) buf[n++] = f
    return Snap.sortedDistinct(buf, n)
}

private suspend fun AwaitPointerEventScope.trackGesture(
    store: EditorStore, view: AeView, lay: AeLayout, down: PointerInputChange, double: Boolean, additive: Boolean,
    rulerPx: Float, leftPad: Float, d: Float, slop: Float, onToggleTransform: (Long) -> Unit,
) {
    val fps = TimeAxis.safeFps(store.project.fps)
    val ppf = view.ppf(d, fps)
    fun frameAt(x: Float): Double = view.origin + (x - leftPad) / ppf
    fun xOf(f: Int): Float = leftPad + ((f - view.origin) * ppf).toFloat()
    val pos = down.position
    val tol = (AeDims.SNAP * d / ppf).toDouble()
    val lastFrame = max(0, store.navigationEnd.coerceAtLeast(1) - 1)

    // ---- Régua ------------------------------------------------------------------
    if (pos.y < rulerPx) {
        val duration = max(1, store.project.durationFrames)
        val area = store.workArea ?: (0 to duration)
        if (pos.y <= (AeDims.WORK_TOP + AeDims.WORK_H + 3f) * d) {
            val xa = xOf(area.first)
            val xb = xOf(area.second)
            val near = AeDims.EDGE * d
            val mode = when {
                abs(pos.x - xa) <= near -> 0
                abs(pos.x - xb) <= near -> 1
                pos.x in xa..xb -> 2
                else -> -1
            }
            if (mode >= 0) {
                val f0 = frameAt(pos.x)
                view.cursor = if (mode == 2) Cursor.MOVE_CURSOR else Cursor.E_RESIZE_CURSOR
                followDrag(down, 0f) { p ->
                    val df = (frameAt(p.x) - f0).roundToInt()
                    view.workDraft = when (mode) {
                        0 -> (area.first + df).coerceIn(0, area.second - 1) to area.second
                        1 -> area.first to (area.second + df).coerceAtLeast(area.first + 1)
                        else -> {
                            val s = max(0, area.first + df)
                            s to s + (area.second - area.first)
                        }
                    }
                }
                view.workDraft?.let { store.setWorkArea(it.first, it.second) }
                view.workDraft = null
                return
            }
        }
        // Duplo clique num marcador: o editor dele.
        if (double) {
            val near = store.markers.frames.minByOrNull { abs(xOf(it) - pos.x) }
            if (near != null && abs(xOf(near) - pos.x) <= 6f * d) {
                store.openMarkerEditor(near)
                return
            }
        }
        // O cabeçote vai até o mouse e segue o arrasto.
        if (store.playing) store.pause()
        fun seekAt(x: Float) = store.seek(frameAt(x).roundToInt().coerceIn(0, lastFrame))
        seekAt(pos.x)
        followDrag(down, 0f) { seekAt(it.x) }
        return
    }

    // ---- Linhas -----------------------------------------------------------------
    val idx = lay.indexAt(pos.y - rulerPx + view.scrollY)
    val line = lay.lines.getOrNull(idx)
    if (line == null) {
        if (!additive) store.clearSelection()
        return
    }
    val l = line.layer
    val r = line.row
    val id = l.id
    val lineTop = rulerPx + line.top - view.scrollY

    // Losango: na camada escolhida e nas trilhas do Transformar.
    val keyIdx = if (line.kind != AeLine.LAYER || id in store.selection) keyAt(line, pos, lineTop, view.origin, ppf, leftPad, d) else -1
    if (keyIdx >= 0) {
        val keys = line.keysAt[keyIdx]
        val at = line.instants[keyIdx]
        if (id !in store.selection) store.select(id)
        if (additive) {
            // Ctrl/Shift: soma ou tira o losango da seleção de keyframes (como no AE), sem arrastar.
            store.toggleTimelineKeys(id, keys)
            return
        }
        // Já faz parte de uma seleção maior: o arrasto leva todos; senão, só ele fica escolhido.
        val inSelection = store.keySelection?.let { it.size > keys.size && it.containsAnyOn(id, keys) } == true
        if (!inSelection) store.tapTimelineKey(id, keys.first(), keys)
        // O texto-fonte é uma trilha virtual (chaves congeladas): escolhe, não arrasta.
        val virtual = keys.any { it.property == com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE }
        if (double && !virtual) {
            PcCurvePopup.open = true
            return
        }
        if (l.locked || virtual) return
        val targets = snapTargets(store, { it == id }, line.instants).filter { it != at }.toIntArray()
        view.keyLayer = id
        view.keyFrom = at
        view.keyDelta = 0
        val moved = followDrag(down, slop) { p ->
            val desired = at + (p.x - pos.x) / ppf.toDouble()
            var nf = desired.roundToInt()
            view.guide = Snap.NONE
            if (store.snapping) {
                val sn = Snap.nearest(targets, desired, store.playhead, tol)
                if (sn != Snap.NONE) {
                    nf = sn
                    view.guide = sn
                }
            }
            // O keyframe não vai para antes do quadro 0 da camada (tempo local negativo).
            nf = max(nf, max(0, r.start - r.offset))
            view.keyDelta = nf - at
        }
        val delta = view.keyDelta
        view.keyLayer = 0L
        view.keyFrom = Snap.NONE
        view.keyDelta = 0
        if (moved && delta != 0) {
            if (inSelection) {
                store.prepareTimelineKeyDrag()
                store.shiftTimelineKeys(delta)
            } else {
                store.moveKeyframesTogether(id, keys, delta)
                val landed = keys.map { it.copy(time = it.time + delta) }
                store.tapTimelineKey(id, landed.first(), landed)
            }
        } else if (!moved && inSelection) {
            // Clique simples num já escolhido: fica só ele.
            store.tapTimelineKey(id, keys.first(), keys)
        }
        return
    }
    // Fora de um losango: a seleção de keyframes sai (Delete volta a ser da camada).
    if (store.keySelection != null) store.clearKeySelection()
    if (line.kind != AeLine.LAYER) {
        // Trilha de propriedade: clicar no vazio escolhe a camada.
        if (id !in store.selection) store.select(id)
        return
    }

    // Cartão da camada: pontas aparam, o corpo move.
    val cardTop = lineTop + AeDims.CARD_PAD * d
    val cardBottom = lineTop + line.height - AeDims.CARD_PAD * d
    val x0 = xOf(r.start)
    val x1 = xOf(r.end)
    val edge = AeDims.EDGE * d
    val inside = pos.y in cardTop..cardBottom && pos.x >= x0 - edge && pos.x <= x1 + edge
    if (!inside) {
        if (!additive) store.clearSelection()
        return
    }
    if (additive) {
        // Ctrl/Shift: soma ou tira da seleção (como no AE), sem arrastar.
        store.select(id, additive = true)
        return
    }
    val wasSelected = id in store.selection
    if (!wasSelected) store.select(id)
    if (double) {
        onToggleTransform(id)
        return
    }
    if (l.locked) return
    val dS = abs(pos.x - x0)
    val dE = abs(pos.x - x1)
    val onStart = dS <= edge && dS <= dE
    val onEnd = !onStart && dE <= edge
    if (onStart || onEnd) {
        val base = if (onStart) r.start else r.end
        val targets = snapTargets(store, { it == id })
        view.trimId = id
        view.trimIsStart = onStart
        view.trimFrame = Snap.NONE
        val moved = followDrag(down, slop) { p ->
            if (store.playing) store.pause()
            val desired = base + (p.x - pos.x) / ppf.toDouble()
            var nf = desired.roundToInt()
            view.guide = Snap.NONE
            if (store.snapping) {
                val sn = Snap.nearest(targets, desired, store.playhead, tol)
                if (sn != Snap.NONE) {
                    nf = sn
                    view.guide = sn
                }
            }
            view.trimFrame = if (onStart) nf.coerceIn(0, r.end - 1) else nf.coerceAtLeast(r.start + 1)
        }
        val f = view.trimFrame
        view.trimId = 0L
        view.trimFrame = Snap.NONE
        if (moved && f != Snap.NONE && f != base) {
            if (onStart) store.trimStart(id, f) else store.trimEnd(id, f)
        }
        return
    }
    // Mover: as escolhidas (destravadas) andam juntas; o ímã pega a ponta mais perto.
    val ids = store.layers.filter { it.id in store.selection && !it.locked }.map { it.id }.ifEmpty { listOf(id) }
    val idArr = ids.toLongArray()
    val minStart = store.layers.filter { it.id in ids }.minOfOrNull { it.startFrame } ?: r.start
    val targets = snapTargets(store, { it in ids })
    val out = IntArray(2)
    val moved = followDrag(down, slop) { p ->
        if (store.playing) store.pause()
        val desired = (r.start + (p.x - pos.x) / ppf).roundToInt()
        var newStart = desired
        view.guide = Snap.NONE
        if (store.snapping) {
            Snap.span(targets, desired, r.end - r.start, store.playhead, tol, out)
            newStart = out[0]
            view.guide = out[1]
        }
        view.moveIds = idArr
        view.moveDelta = max(newStart - r.start, -minStart)
    }
    val delta = view.moveDelta
    view.moveIds = LongArray(0)
    view.moveDelta = 0
    if (moved && delta != 0) {
        store.beginGesture("move")
        store.moveLayers(ids, delta)
        store.endGesture()
    } else if (!moved && wasSelected && store.selection.size > 1) {
        // Clique simples numa já escolhida: fica só ela (como no AE).
        store.select(id)
    }
}

/** Índice do losango sob o mouse nesta linha (−1 = nenhum). */
private fun keyAt(line: AeLine, p: Offset, lineTop: Float, view: Double, ppf: Float, cx: Float, d: Float): Int {
    if (line.instants.isEmpty()) return -1
    if (abs(p.y - keyCenterY(line, lineTop, d)) > 10f * d) return -1
    val i = Keyframes.nearestIndex(line.instants, TimeAxis.frameAt(p.x, view, ppf, cx))
    if (i < 0) return -1
    return if (abs(TimeAxis.xOf(line.instants[i].toDouble(), view, ppf, cx) - p.x) <= 6f * d) i else -1
}

private fun hoverCursor(store: EditorStore, view: AeView, lay: AeLayout, p: Offset, rulerPx: Float, leftPad: Float, d: Float): Int {
    val fps = TimeAxis.safeFps(store.project.fps)
    val ppf = view.ppf(d, fps)
    fun xOf(f: Int): Float = leftPad + ((f - view.origin) * ppf).toFloat()
    val edge = AeDims.EDGE * d
    if (p.y < rulerPx) {
        if (p.y > (AeDims.WORK_TOP + AeDims.WORK_H + 3f) * d) return Cursor.DEFAULT_CURSOR
        val area = store.workArea ?: (0 to max(1, store.project.durationFrames))
        return if (abs(p.x - xOf(area.first)) <= edge || abs(p.x - xOf(area.second)) <= edge) Cursor.E_RESIZE_CURSOR else Cursor.DEFAULT_CURSOR
    }
    val line = lay.lines.getOrNull(lay.indexAt(p.y - rulerPx + view.scrollY)) ?: return Cursor.DEFAULT_CURSOR
    if (line.kind != AeLine.LAYER || line.layer.locked) return Cursor.DEFAULT_CURSOR
    val lineTop = rulerPx + line.top - view.scrollY
    if (p.y < lineTop + AeDims.CARD_PAD * d || p.y > lineTop + line.height - AeDims.CARD_PAD * d) return Cursor.DEFAULT_CURSOR
    val x0 = xOf(line.row.start)
    val x1 = xOf(line.row.end)
    return if (abs(p.x - x0) <= edge || abs(p.x - x1) <= edge) Cursor.E_RESIZE_CURSOR else Cursor.DEFAULT_CURSOR
}

// =============================================================================
// Pintor (o desenho do Aurea, organizado como o AE)
// =============================================================================

private const val AE_WAVE_MAX = 4096
private const val AE_THUMB_QUERIES = 6
private const val AE_THUMB_MAX_PX = 512
private const val AE_ZEBRA_MAX = 4096

private class AePainter(private val d: Float, private val measurer: TextMeasurer) {
    private val m = TimelineMetrics(d, 1f, referenceTrial = true)
    private val thumbs = ThumbStrip()
    private val waves = WaveStrip(AE_WAVE_MAX)
    private val waveLines = FloatArray(AE_WAVE_MAX * 4)
    private val wavePaint = Paint().apply { isAntiAlias = false; strokeCap = Paint.Cap.BUTT }
    private var waveStore: EditorStore? = null
    private val waveSource = WaveSource { layer, start, fpb, count, out ->
        waveStore?.queryWaveform(layer, start, fpb, count, out) ?: 0
    }
    /** Ganho da onda (pico^0,7): a fala baixa aparece sem estourar a alta (decupar de ouvido). */
    private val gain = FloatArray(256) { (it / 255.0).pow(0.7).toFloat() }
    private val barClip = android.graphics.Path()
    private val rectF = RectF()
    private val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val diamonds = HashMap<Int, Path>()
    private var groupBuf = IntArray(256)
    private val triangle = Path()
    private val markerPath = Path()
    private val tabPath = Path()
    private val ticks = arrayOf(Path(), Path(), Path())
    private val tickStroke = Stroke(m.omegaTick)
    private val selStroke = Stroke(m.selStroke)
    private val keyStroke = Stroke(m.omegaKeyStroke, join = StrokeJoin.Round)
    private val keyAltStroke = Stroke(m.omegaKeyAltStroke, join = StrokeJoin.Round)
    private val keyRingStroke = Stroke(m.omegaKeyRing, join = StrokeJoin.Round)
    private val tc = IntArray(4)
    private var playheadFrame = Snap.NONE

    // --- Texto (os estilos da timeline do Aurea) --------------------------------------
    private val labelStyle = TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W500, platformStyle = PlatformTextStyle(includeFontPadding = false))
    private val tickLabelStyle = TextStyle(color = AureaTimeline.TickMajor, fontSize = 9.sp, fontWeight = FontWeight.W500,
        fontFeatureSettings = "tnum", platformStyle = PlatformTextStyle(includeFontPadding = false))
    private val digitStyle = TextStyle(color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.W700, letterSpacing = 0.3.sp,
        fontFeatureSettings = "tnum", platformStyle = PlatformTextStyle(includeFontPadding = false))
    private val balloonStyle = TextStyle(color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.W700,
        fontFeatureSettings = "tnum", platformStyle = PlatformTextStyle(includeFontPadding = false))
    private val markerNameStyle = TextStyle(color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.W600,
        platformStyle = PlatformTextStyle(includeFontPadding = false))

    private class NameLayout(val name: String, val bucket: Int, val layout: TextLayoutResult)
    private val names = LongSparseArray<NameLayout>()
    private val tickLabels = HashMap<Int, TextLayoutResult>()
    private val markerNames = HashMap<Pair<String, Int>, TextLayoutResult>()
    private val glyphs = HashMap<Int, TextLayoutResult>()
    private val digits = arrayOfNulls<TextLayoutResult>(11)
    private var digitWidth = 0f
    private var colonWidth = 0f
    private var digitBaseline = 0f
    private var balloonFrame = Snap.NONE
    private var balloonLayout: TextLayoutResult? = null

    fun draw(scope: DrawScope, store: EditorStore, v: AeView, lay: AeLayout, rulerH: Float, cx: Float) = with(scope) {
        val w = size.width
        val h = size.height
        val fps = TimeAxis.safeFps(store.project.fps)
        val ppf = v.ppf(d, fps)
        val view = v.origin
        waveStore = store
        v.redraw   // lido: as miniaturas que não couberam neste quadro pedem o próximo
        thumbs.beginFrame(AE_THUMB_QUERIES)
        playheadFrame = store.playhead
        val duration = store.project.durationFrames
        val cache = store.thumbnails
        val generation = store.thumbnailGeneration
        val selection = store.selection
        val multi = selection.size > 1

        drawRect(AureaColors.EditorCanvas)
        // ---- Linhas -------------------------------------------------------------------
        clipRect(top = rulerH) {
            val endX = TimeAxis.xOf(duration.toDouble(), view, ppf, cx)
            if (endX < w) drawRect(OUT_VEIL, Offset(max(endX, 0f), rulerH), Size(w - max(endX, 0f), h - rulerH))
            val scroll = v.scrollY
            var i = max(0, lay.indexAt(scroll))
            while (i < lay.lines.size) {
                val line = lay.lines[i]
                val top = rulerH + line.top - scroll
                if (top > h) break
                i++
                if (top + line.height < rulerH) continue
                val selected = line.layer.id in selection
                when (line.kind) {
                    AeLine.LAYER -> {
                        if (selected) drawRect(SELECTED_ROW, Offset(0f, top), Size(w, line.height))
                        drawLayerCard(store, v, line, top, w, view, ppf, cx, fps, selected, multi, cache, generation)
                    }
                    else -> drawLane(store, v, line, top, w, view, ppf, cx, fps)
                }
                drawRect(ROW_LINE, Offset(0f, top + line.height - 1f), Size(w, 1f))
            }
            // Fio do ímã e o destino do reordenar.
            val g = v.guide
            if (g != Snap.NONE) {
                val gx = TimeAxis.xOf(g.toDouble(), view, ppf, cx)
                drawRect(AureaColors.Accent, Offset(gx - m.guide / 2f, rulerH), Size(m.guide, h - rulerH))
            }
            val ry = v.reorderY
            if (v.reorderId != 0L && !ry.isNaN()) {
                val target = lay.lines.getOrNull(lay.indexAt(ry))
                if (target != null) {
                    val y = rulerH + target.top - scroll + if (ry > target.top + target.height / 2f) target.height else 0f
                    drawRect(AureaColors.Accent, Offset(0f, y - m.reorderLine / 2f), Size(w, m.reorderLine))
                }
            }
        }
        // ---- Régua --------------------------------------------------------------------
        drawRuler(store, v, w, view, ppf, cx, fps, rulerH)
        if (thumbs.starved) v.redraw++
        // ---- Cabeçote: fio de 1,5 dp interrompido na aba ------------------------------
        val smooth = store.playheadSmooth
        val headF = if (store.playing && !smooth.isNaN() && abs(smooth - store.playhead) < 1.5) smooth else store.playhead.toDouble()
        val px = TimeAxis.xOf(headF, view, ppf, cx)
        if (px >= -2f && px <= w + 2f) {
            val color = if (v.moveIds.isNotEmpty()) AureaColors.Playhead.copy(alpha = 0.5f) else AureaColors.Playhead
            val half = m.playhead / 2f
            val tabTop = AeDims.TAB_TOP * d
            val tickTop = AeDims.TICK_TOP * d
            drawRect(color, Offset(px - half, tickTop), Size(m.playhead, tabTop - tickTop + d))
            val resume = tabTop + m.omegaTabHeight - d
            drawRect(color, Offset(px - half, resume), Size(m.playhead, max(0f, h - resume)))
        }
        drawTimeTab(px, store.playhead, fps, w)
    }

    // --- Cartão da camada (o omegaClip do Aurea) -----------------------------------------
    private fun DrawScope.drawLayerCard(
        store: EditorStore, v: AeView, line: AeLine, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        selected: Boolean, multi: Boolean, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int,
    ) {
        val r = line.row
        var s = r.start
        var e = r.end
        var shift = 0
        val moving = v.isMoving(r.id)
        if (moving) {
            s += v.moveDelta
            e += v.moveDelta
            shift = v.moveDelta
        }
        if (v.trimId == r.id && v.trimFrame != Snap.NONE) {
            if (v.trimIsStart) s = v.trimFrame else e = v.trimFrame
        }
        val cardTop = top + AeDims.CARD_PAD * d
        val cardH = line.height - 2f * AeDims.CARD_PAD * d
        val bottom = cardTop + cardH
        val x0 = TimeAxis.xOf(s.toDouble(), view, ppf, cx)
        val x1 = TimeAxis.xOf(e.toDouble(), view, ppf, cx)
        val pad = m.barRadius * 2f
        if (x1 >= -pad && x0 <= w + pad) {
            val left = max(x0, -pad)
            val right = min(x1, w + pad)
            val radius = ClipGeometry.cornerRadius(right - left, cardH, m.barRadius)
            // Sombra: leve sempre; mais forte e espalhada com o clipe levantado.
            if (moving) {
                val layer = AureaTimeline.ClipLiftShadow.copy(alpha = AureaTimeline.ClipLiftShadow.alpha / 3f)
                for (k in 1..3) {
                    val g = m.omegaLiftShadow * k / 3f
                    drawRoundRect(layer, Offset(left - g / 2f, cardTop - g / 4f + m.omegaShadow * 2f), Size(right - left + g, cardH + g / 2f), CornerRadius(radius + g / 2f))
                }
            } else {
                drawRoundRect(AureaTimeline.ClipShadow, Offset(left, cardTop + m.omegaShadow), Size(right - left, cardH), CornerRadius(radius))
            }
            val canvas = platformCanvas
            rectF.set(left, cardTop, right, bottom)
            barClip.reset()
            barClip.addRoundRect(rectF, radius, radius, android.graphics.Path.Direction.CW)
            canvas.save()
            canvas.clipPath(barClip)
            // 1. Cor sólida do tipo (ClipColors.solid), 2. zebra de quadros.
            drawRect(Color(ClipColors.solid(r.type.color.toArgb())), Offset(left, cardTop), Size(right - left, cardH))
            drawZebra(s, cardTop, cardH, left, right, view, ppf, cx)
            // 3. Tira de miniaturas (vídeo e imagem), presa à mídia: anda junto no mover.
            bitmapPaint.alpha = 255
            if (r.hasThumbs) drawThumbs(canvas, r, cardTop, cardH, x0, x1, w, view, ppf, cx, fps, cache, generation, r.start - r.offset + shift)
            // 4. Onda: no áudio o cartão inteiro; no vídeo a faixa de baixo, sobre fundo escuro.
            val visL = max(x0, 0f)
            val visR = min(x1, w)
            when (r.type) {
                LayerType.Audio -> drawWave(r, cardTop + 2f * d, bottom - 2f * d, visL, visR, view, ppf, cx, shift, null, AureaTimeline.ClipWave)
                LayerType.Video -> drawWave(r, bottom - cardH * AeDims.VIDEO_WAVE, bottom, visL, visR, view, ppf, cx, shift, WAVE_BACKING, AureaTimeline.GlyphAudio)
                else -> {}
            }
            // Realce sutil no topo do cartão (brilho e fio de 1 dp).
            val sheenH = (if (r.type == LayerType.Video) cardH * (1f - AeDims.VIDEO_WAVE) else cardH) * AureaTimeline.ClipSheenDepth
            drawRect(Brush.verticalGradient(listOf(AureaTimeline.ClipSheen, Color.Transparent), startY = cardTop, endY = cardTop + sheenH),
                Offset(left, cardTop), Size(right - left, sheenH))
            drawRect(AureaTimeline.ClipSheenLine, Offset(left, cardTop), Size(right - left, m.omegaSheenLine))
            // 5. Triângulo da etiqueta no canto de cima à esquerda.
            val tag = ShellColors.LabelPalette.getOrNull(r.label - 1)
            if (tag != null) {
                val leg = min(cardH, m.bar) / 2f
                triangle.reset()
                triangle.moveTo(x0, cardTop)
                triangle.lineTo(x0 + leg, cardTop)
                triangle.lineTo(x0, cardTop + leg)
                triangle.close()
                drawPath(triangle, tag)
            }
            // 6. Rótulo de 11 sp; segue a borda visível (o nome não some com o clipe longo rolado).
            val labelCy = if (r.type == LayerType.Video) cardTop + cardH * (1f - AeDims.VIDEO_WAVE) / 2f else cardTop + cardH / 2f
            drawLabel(r, labelCy, max(x0, 0f), x1)
            // 7. Marcas da camada.
            drawLayerMarkers(store.layerMarkers, r.id, cardTop, cardH, left, right, view, ppf, cx)
            // Camada oculta: o cartão apagado.
            if (!r.visible) drawRect(AureaColors.EditorCanvas.copy(alpha = 0.55f), Offset(left, cardTop), Size(right - left, cardH))
            canvas.restore()
            // Escolhida: o contorno branco de 2 dp do Aurea e as alças de aparar nas duas pontas.
            if (selected) {
                clipRect(left, cardTop, right, bottom) {
                    val stroke = ClipGeometry.strokeWidth(right - left, cardH, m.selStroke)
                    if (right - left <= stroke) {
                        drawRect(AureaTimeline.ClipSelected, Offset(left, cardTop), Size(right - left, cardH))
                    } else {
                        drawRoundRect(AureaTimeline.ClipSelected, Offset(left + stroke / 2f, cardTop + stroke / 2f),
                            Size(right - left - stroke, cardH - stroke),
                            CornerRadius(ClipGeometry.cornerRadius(right - left - stroke, cardH - stroke, m.barRadius - stroke / 2f)),
                            style = selStroke)
                    }
                    if (!r.locked && x1 - x0 >= 3f * AeDims.HANDLE * d) {
                        drawTrimHandle(x0, cardTop, cardH, start = true)
                        drawTrimHandle(x1 - AeDims.HANDLE * d, cardTop, cardH, start = false)
                    }
                }
            }
            if (r.locked) drawGlyph(CupertinoGlyph.LockFill, 9f, Color.White.copy(alpha = 0.75f), min(x1, w) - 9f * d, cardTop + 7f * d)
        }
        // Losangos: em edição na camada escolhida (sozinha); os outros pequenos e a 160/255.
        val editing = selected && !multi
        drawKeys(store, v, line, keyCenterY(line, top, d), w, view, ppf, cx, fps, editing, m.omegaKeyEdit, m.omegaKeyAlt)
    }

    /** Alça de aparar: pílula branca dentro da ponta, chevron escuro (o traço do Aurea). */
    private fun DrawScope.drawTrimHandle(left: Float, top: Float, height: Float, start: Boolean) {
        val hw = AeDims.HANDLE * d
        drawRoundRect(AureaTimeline.TrimGrip.copy(alpha = 0.92f), Offset(left, top), Size(hw, height), CornerRadius(min(3f * d, hw / 2f)))
        drawGlyph(if (start) CupertinoGlyph.ChevronLeft else CupertinoGlyph.ChevronRight, 8f, Color.Black.copy(alpha = 0.8f), left + hw / 2f, top + height / 2f)
    }

    /** Trilha do Transformar: faixa da duração da camada, a linha entre as chaves e os losangos. */
    private fun DrawScope.drawLane(store: EditorStore, v: AeView, line: AeLine, top: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float) {
        drawRect(LANE_BG, Offset(0f, top), Size(w, line.height))
        val r = line.row
        var s = r.start
        var e = r.end
        if (v.isMoving(r.id)) {
            s += v.moveDelta
            e += v.moveDelta
        }
        val x0 = TimeAxis.xOf(s.toDouble(), view, ppf, cx)
        val x1 = TimeAxis.xOf(e.toDouble(), view, ppf, cx)
        if (x1 > 0f && x0 < w) drawRect(Color(ClipColors.solid(r.type.color.toArgb())).copy(alpha = 0.16f), Offset(max(x0, 0f), top + 2f * d), Size(min(x1, w) - max(x0, 0f), line.height - 4f * d))
        val cy = top + line.height / 2f
        if (line.kind == AeLine.PROP && line.instants.size >= 2) {
            val a = TimeAxis.xOf(line.instants.first().toDouble(), view, ppf, cx)
            val b = TimeAxis.xOf(line.instants.last().toDouble(), view, ppf, cx)
            drawLine(AureaColors.Accent.copy(alpha = 0.5f), Offset(a, cy), Offset(b, cy), 1.5f * d)
        }
        val editing = line.kind == AeLine.PROP
        drawKeys(store, v, line, cy, w, view, ppf, cx, fps, editing, m.omegaKeyAlt * 1.25f, m.omegaKeyAlt)
    }

    /** Zebra de quadros: faixas brancas de meio quadro, uma sim e uma não (dobra até ter 4 dp). */
    private fun DrawScope.drawZebra(startF: Int, top: Float, height: Float, left: Float, right: Float, view: Double, ppf: Float, cx: Float) {
        if (!(ppf > 0f)) return
        var band = 0.5
        while (band * ppf < m.omegaZebraMin && band < 1e7) band *= 2.0
        val bandPx = (band * ppf).toFloat()
        val fromK = max(0L, floor((TimeAxis.frameAt(left, view, ppf, cx) - startF) / band).toLong())
        val toK = ceil((TimeAxis.frameAt(right, view, ppf, cx) - startF) / band).toLong()
        var k = fromK + (fromK and 1L)
        var count = 0
        while (k <= toK && count < AE_ZEBRA_MAX) {
            val xa = TimeAxis.xOf(startF + k * band, view, ppf, cx)
            drawRect(AureaTimeline.ClipZebra, Offset(xa, top), Size(bandPx, height))
            k += 2
            count++
        }
    }

    /** Tira de miniaturas: ladrilhos com índice ABSOLUTO a partir do instante 0 da mídia ([originFrame]). */
    private fun drawThumbs(
        canvas: android.graphics.Canvas, r: RowModel, top: Float, height: Float, x0: Float, x1: Float, w: Float,
        view: Double, ppf: Float, cx: Float, fps: Float, cache: com.aurea.aurea.state.ThumbnailCache, generation: Int, originFrame: Int,
    ) {
        val heightPx = height.roundToInt().coerceIn(1, AE_THUMB_MAX_PX)
        val tile = heightPx * thumbs.aspect(cache, r.id)
        val origin = TimeAxis.xOf(originFrame.toDouble(), view, ppf, cx)
        val visL = max(x0, 0f)
        val visR = min(x1, w)
        if (visR <= visL || tile <= 0f) return
        val image = r.type == LayerType.Image
        var i = max(0, floor((visL - origin) / tile).toInt())
        val iEnd = floor((visR - origin) / tile).toInt()
        while (i <= iEnd) {
            val bucket = if (image) -1 else Thumbs.bucketOf(i * tile / ppf.toDouble(), fps)
            val frame = if (image) r.start else Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), r.start, r.offset)
            val bmp = thumbs.get(cache, r.id, bucket, frame, heightPx, generation)
            if (bmp != null) {
                val l = origin + i * tile
                rectF.set(l, top, l + tile, top + height)
                canvas.drawBitmap(bmp, null, rectF, bitmapPaint)
            }
            i++
        }
    }

    /**
     * Onda do som em grade fixa do tempo ([WaveStrip]: pergunta ao motor só quando a
     * vista sai da janela guardada ou o degrau de zoom muda). [shift] = prévia do mover.
     */
    private fun DrawScope.drawWave(
        r: RowModel, areaTop: Float, areaBottom: Float, visL: Float, visR: Float,
        view: Double, ppf: Float, cx: Float, shift: Int, backing: Color?, color: Color,
    ) {
        val store = waveStore ?: return
        if (visR <= visL || ppf <= 0f) return
        val fpb = WaveGrid.framesPerBucket(max(1f, d * 1.5f), ppf)
        val first = WaveGrid.bucketAt(TimeAxis.frameAt(visL, view, ppf, cx) - shift, fpb)
        val last = WaveGrid.bucketAt(TimeAxis.frameAt(visR, view, ppf, cx) - shift, fpb)
        val e = waves.get(waveSource, r.id, store.layers, store.thumbnailGeneration, fpb, first, last) ?: return
        if (backing != null) drawRect(backing, Offset(visL, areaTop), Size(visR - visL, areaBottom - areaTop))
        val mid = (areaTop + areaBottom) / 2f
        val half = (areaBottom - areaTop) / 2f - d
        drawRect(color.copy(alpha = 0.25f), Offset(visL, mid - 0.5f), Size(visR - visL, 1f))
        var k = 0
        var b = first
        while (b <= last && k + 4 <= waveLines.size) {
            val amp = gain[e.at(b)] * half
            val x = TimeAxis.xOf((b + 0.5) * fpb + shift, view, ppf, cx)
            b++
            if (amp < 0.5f || x < visL || x > visR) continue
            waveLines[k++] = x
            waveLines[k++] = mid - amp
            waveLines[k++] = x
            waveLines[k++] = mid + amp
        }
        if (k == 0) return
        wavePaint.color = color.toArgb()
        wavePaint.strokeWidth = max(1f, m.omegaWave)
        platformCanvas.drawLines(waveLines, 0, k, wavePaint)
    }

    /** Rótulo do clipe: 14 dp para dentro, reticências no meio, sombra curta (legível sobre as miniaturas). */
    private fun DrawScope.drawLabel(r: RowModel, cy: Float, x0: Float, x1: Float) {
        if (r.name.isEmpty()) return
        val avail = (x1 - x0) - 2f * m.omegaLabelInset
        if (avail < 8f * d) return
        val step = 12f * d
        val bucket = (avail / step).toInt()
        if (bucket <= 0) return
        var cached = names.get(r.id)
        if (cached == null || cached.bucket != bucket || cached.name != r.name) {
            val limit = bucket * step
            fun measure(text: String) = measurer.measure(text, labelStyle, softWrap = false, maxLines = 1)
            val fitted = MiddleEllipsis.fit(r.name) { measure(it).size.width <= limit }
            if (fitted.isEmpty()) return
            if (names.size() > 256) names.clear()
            cached = NameLayout(r.name, bucket, measure(fitted))
            names.put(r.id, cached)
        }
        val layout = cached.layout
        drawText(layout, color = AureaTimeline.ClipLabel, topLeft = Offset(x0 + m.omegaLabelInset, cy - layout.size.height / 2f),
            shadow = Shadow(AureaTimeline.ClipLabelShadow, Offset(0f, m.omegaShadow), m.omegaLabelShadow))
    }

    /** Marcas da camada dentro do clipe: fio, triângulo no topo e a duração como faixa na base. */
    private fun DrawScope.drawLayerMarkers(
        mk: EditorStore.LayerMarkers, layer: Long, top: Float, height: Float, left: Float, right: Float, view: Double, ppf: Float, cx: Float,
    ) {
        if (mk.size == 0) return
        val s = 3.5f * d
        for (i in 0 until mk.size) {
            if (mk.layers[i] != layer) continue
            val x = TimeAxis.xOf(mk.frames[i].toDouble(), view, ppf, cx)
            if (x < left - s || x > right + s) continue
            val color = Color(MarkerColors.display(mk.colors[i]))
            if (mk.durations[i] > 0) {
                val xe = min(TimeAxis.xOf((mk.frames[i] + mk.durations[i]).toDouble(), view, ppf, cx), right)
                if (xe > x) drawRect(color.copy(alpha = 0.55f), Offset(x, top + height - 3f * d), Size(xe - x, 3f * d))
            }
            drawLine(color, Offset(x, top), Offset(x, top + height), strokeWidth = 1.5f * d)
            markerPath.reset()
            markerPath.moveTo(x - s, top)
            markerPath.lineTo(x + s, top)
            markerPath.lineTo(x, top + s * 1.4f)
            markerPath.close()
            drawPath(markerPath, color)
        }
    }

    // --- Losangos (os da Omega: 2:3 arredondados) -----------------------------------------
    private fun diamond(h: Float): Path {
        val key = (h * 4f).roundToInt()
        return diamonds.getOrPut(key) {
            val hh = h / 2f
            val hw = h / 3f
            val px = floatArrayOf(0f, hw, 0f, -hw)
            val py = floatArrayOf(-hh, 0f, hh, 0f)
            val k = 0.22f
            Path().apply {
                for (i in 0 until 4) {
                    val p = (i + 3) % 4
                    val q = (i + 1) % 4
                    val inX = px[i] + (px[p] - px[i]) * k
                    val inY = py[i] + (py[p] - py[i]) * k
                    val outX = px[i] + (px[q] - px[i]) * k
                    val outY = py[i] + (py[q] - py[i]) * k
                    if (i == 0) moveTo(inX, inY) else lineTo(inX, inY)
                    quadraticTo(px[i], py[i], outX, outY)
                }
                close()
            }
        }
    }

    private fun DrawScope.altKey(x: Float, cy: Float, h: Float) {
        val path = diamond(h)
        translate(x, cy) {
            drawPath(path, AureaTimeline.KeyAlt)
            drawPath(path, AureaTimeline.KeyAltStroke, style = keyAltStroke)
        }
    }

    private fun DrawScope.editKey(x: Float, cy: Float, h: Float, highlighted: Boolean, dragging: Boolean) {
        if (dragging) {
            translate(x, cy) { drawPath(diamond(h * 1.15f), AureaTimeline.KeyFill, style = keyAltStroke) }
            return
        }
        if (highlighted) translate(x, cy) { drawPath(diamond(h), AureaColors.Accent, style = keyRingStroke) }
        val path = diamond(if (highlighted) h * 0.85f else h)
        translate(x, cy) {
            drawPath(path, AureaTimeline.KeyFill)
            drawPath(path, AureaTimeline.KeyStroke, style = keyStroke)
        }
    }

    private fun DrawScope.keyPill(xa: Float, xb: Float, cy: Float, h: Float, editing: Boolean) {
        val ph = h * 2f / 3f
        val wPill = max(xb - xa, ph)
        val left = (xa + xb) / 2f - wPill / 2f
        val corner = CornerRadius(ph / 2f)
        drawRoundRect(if (editing) AureaTimeline.KeyFill else AureaTimeline.KeyAlt, Offset(left, cy - ph / 2f), Size(wPill, ph), corner)
        drawRoundRect(if (editing) AureaTimeline.KeyStroke else AureaTimeline.KeyAltStroke, Offset(left, cy - ph / 2f), Size(wPill, ph), corner,
            style = if (editing) keyStroke else keyAltStroke)
    }

    private fun DrawScope.drawKeys(
        store: EditorStore, v: AeView, line: AeLine, cy: Float, w: Float, view: Double, ppf: Float, cx: Float, fps: Float,
        editing: Boolean, editH: Float, altH: Float,
    ) {
        val inst = line.instants
        if (inst.isEmpty()) return
        val r = line.row
        val shift = if (v.isMoving(r.id)) v.moveDelta else 0
        val sel = store.selectedKeyframe
        val selFrame = if (sel != null && sel.first == r.id && (line.match?.invoke(sel.second) ?: true))
            Keyframes.toTimeline(sel.second.time, r.start, r.offset) else Snap.NONE
        val dragFrom = if (v.keyLayer == r.id) v.keyFrom else Snap.NONE
        // Os escolhidos (Ctrl/Shift + clique): o anel de destaque em cada um.
        val chosen = store.keySelection?.on(r.id).orEmpty()
        val viewK = view - shift
        val need = 2 * ((w + 2f * m.keyTouchHalf) / m.keyMergeGap).toInt() + 8
        if (groupBuf.size < need) groupBuf = IntArray(need)
        val groups = Keyframes.visibleGroups(inst, viewK, ppf, cx, w, m.keyTouchHalf, m.keyMergeGap, groupBuf)
        for (g in 0 until groups) {
            val i = groupBuf[2 * g]
            val j = groupBuf[2 * g + 1]
            val xa = TimeAxis.xOf(inst[i].toDouble(), viewK, ppf, cx)
            if (i == j) {
                val f = inst[i]
                if (f == dragFrom) continue
                val picked = chosen.isNotEmpty() && line.keysAt.getOrNull(i)?.any { com.aurea.aurea.editor.timeline.KeyRef.of(it) in chosen } == true
                if (editing || picked) editKey(xa, cy, if (editing) editH else altH * 1.25f, f == selFrame || f == playheadFrame || picked, false) else altKey(xa, cy, altH)
            } else {
                keyPill(xa, TimeAxis.xOf(inst[j].toDouble(), viewK, ppf, cx), cy, if (editing) editH else altH, editing)
            }
        }
        if (dragFrom != Snap.NONE && java.util.Arrays.binarySearch(inst, dragFrom) >= 0) {
            val nf = dragFrom + v.keyDelta
            val x = TimeAxis.xOf(nf.toDouble(), viewK, ppf, cx)
            editKey(x, cy, if (editing) editH else altH, false, true)
            drawBalloon(x, cy - editH / 2f, nf, fps)
        }
    }

    /** Balão de tempo do losango na mão (`MM:SS:FF` 10 sp sobre preto .82). */
    private fun DrawScope.drawBalloon(x: Float, keyTop: Float, frame: Int, fps: Float) {
        if (balloonFrame != frame || balloonLayout == null) {
            balloonFrame = frame
            balloonLayout = measurer.measure(Timecode.format(frame, fps), balloonStyle)
        }
        val layout = balloonLayout ?: return
        val bw = layout.size.width + m.balloonPadH * 2f
        val bh = layout.size.height + m.balloonPadV * 2f
        val left = x - bw / 2f
        val top = max(AeDims.RULER * d, keyTop - m.balloonGap - bh)
        drawRoundRect(BALLOON, Offset(left, top), Size(bw, bh), CornerRadius(m.balloonRadius))
        drawText(layout, topLeft = Offset(left + m.balloonPadH, top + m.balloonPadV))
    }

    // --- Régua ------------------------------------------------------------------------------
    private fun DrawScope.drawRuler(store: EditorStore, v: AeView, w: Float, view: Double, ppf: Float, cx: Float, fps: Float, rulerH: Float) {
        drawRect(AureaColors.EditorCanvas, Offset.Zero, Size(w, rulerH))
        drawRect(AureaTimeline.OmegaHeaderBorder, Offset(0f, rulerH - 1f), Size(w, 1f))
        val tickTop = AeDims.TICK_TOP * d
        // Área de trabalho (AE): faixa no alto, alças nas pontas, véu fora dela.
        val duration = max(1, store.project.durationFrames)
        val area = v.workDraft ?: store.workArea
        val (a0, a1) = area ?: (0 to duration)
        val xa = TimeAxis.xOf(a0.toDouble(), view, ppf, cx)
        val xb = TimeAxis.xOf(a1.toDouble(), view, ppf, cx)
        val bandTop = AeDims.WORK_TOP * d
        val bandH = AeDims.WORK_H * d
        if (area != null) {
            if (xa > 0f) drawRect(WORK_VEIL, Offset(0f, 0f), Size(min(xa, w), rulerH))
            if (xb < w) drawRect(WORK_VEIL, Offset(max(xb, 0f), 0f), Size(w - max(xb, 0f), rulerH))
        }
        if (xb > 0f && xa < w) {
            drawRoundRect(if (area != null) WORK_FILL else WORK_IDLE, Offset(max(xa, 0f), bandTop), Size(min(xb, w) - max(xa, 0f), bandH), CornerRadius(bandH / 2f))
            val hw = 3f * d
            val hh = bandH + 4f * d
            val handle = if (area != null) WORK_HANDLE else WORK_IDLE_HANDLE
            drawRoundRect(handle, Offset(xa, bandTop - 2f * d), Size(hw, hh), CornerRadius(hw / 2f))
            drawRoundRect(handle, Offset(xb - hw, bandTop - 2f * d), Size(hw, hh), CornerRadius(hw / 2f))
        }
        // Tiques da Omega (segundo 18, meio 12, quadro 5), pendurados de [tickTop].
        for (p in ticks) p.reset()
        val t0 = TimeAxis.frameAt(0f, view, ppf, cx)
        val t1 = TimeAxis.frameAt(w, view, ppf, cx)
        if (t1 >= 0.0) {
            val fFirst = max(0L, floor(t0).toLong())
            val fLast = ceil(t1).toLong()
            val pps = max(v.pps, 0.01f)
            val step = OmegaRuler.frameStep(pps, fps)
            val secStep = OmegaRuler.secondStep(pps)
            val sec0 = max(0L, floor(fFirst / fps.toDouble()).toLong())
            val sec1 = ceil(fLast / fps.toDouble()).toLong()
            fun tick(path: Path, frame: Long, heightDp: Float) {
                val x = TimeAxis.xOf(frame.toDouble(), view, ppf, cx)
                if (x < -1f || x > w + 1f) return
                path.moveTo(x, tickTop)
                path.lineTo(x, tickTop + heightDp * d)
            }
            var s = (sec0 / secStep) * secStep
            while (s <= sec1) {
                tick(ticks[OmegaRuler.SECOND], OmegaRuler.frameOf(s.toDouble(), fps), OmegaRuler.heightDp(OmegaRuler.SECOND))
                s += secStep
            }
            if (secStep == 1 && pps / 2f >= OmegaRuler.MIN_TICK_DP) {
                for (k in sec0..sec1) tick(ticks[OmegaRuler.HALF], OmegaRuler.frameOf(k + 0.5, fps), OmegaRuler.heightDp(OmegaRuler.HALF))
            }
            if (secStep == 1 && step < fps / 2f) {
                for (sec in sec0..sec1) OmegaRuler.frameTicks(sec, fps, pps) { f ->
                    if (f in fFirst..fLast) tick(ticks[OmegaRuler.FRAME], f, OmegaRuler.heightDp(OmegaRuler.FRAME))
                }
            }
            for (p in ticks) drawPath(p, AureaTimeline.OmegaTick, style = tickStroke)
            // Rótulos dos segundos (m:ss), espaçados para não se tocarem.
            var labelStep = LABEL_STEPS.firstOrNull { it * pps >= 64f } ?: LABEL_STEPS.last()
            if (labelStep < 1) labelStep = 1
            var ls = (sec0 / labelStep) * labelStep
            while (ls <= sec1) {
                val x = TimeAxis.xOf(OmegaRuler.frameOf(ls.toDouble(), fps).toDouble(), view, ppf, cx)
                if (x >= -40f * d && x <= w) {
                    val key = ls.toInt()
                    val layout = tickLabels[key] ?: measurer.measure(Timecode.rulerLabel(key), tickLabelStyle).also {
                        if (tickLabels.size > 128) tickLabels.clear()
                        tickLabels[key] = it
                    }
                    drawText(layout, topLeft = Offset(x + 3f * d, tickTop))
                }
                ls += labelStep
            }
        }
        // Prévia já calculada (faixa azul na base da régua).
        val buffer = 2f * d
        for (range in store.previewBufferRanges) {
            val left = TimeAxis.xOf(range.startFrame.toDouble(), view, ppf, cx).coerceAtLeast(0f)
            val right = TimeAxis.xOf(range.endFrame.toDouble(), view, ppf, cx).coerceAtMost(w)
            if (right > left) drawRect(PREVIEW_BUFFER, Offset(left, rulerH - buffer - 1f), Size(right - left, buffer))
        }
        // Marcadores da composição: triângulo na cor da marca, fio e o nome à direita.
        val mk = store.markers
        val half = 4f * d
        for (i in 0 until mk.size) {
            val x = TimeAxis.xOf(mk.frames[i].toDouble(), view, ppf, cx)
            val label = mk.labels.getOrNull(i).orEmpty()
            if (label.isNotEmpty()) {
                val nextX = if (i + 1 < mk.size) TimeAxis.xOf(mk.frames[i + 1].toDouble(), view, ppf, cx) else w
                val left = x + half + 3f * d
                val avail = min(nextX - half, w) - left - 2f * d
                if (avail >= 18f * d && left < w) drawMarkerName(label, left, avail, tickTop)
            }
            if (x < -half || x > w + half) continue
            val color = Color(MarkerColors.display(mk.colors[i]))
            val s = if (mk.kinds[i] == 1) half * 0.7f else half
            markerPath.reset()
            markerPath.moveTo(x - s, tickTop)
            markerPath.lineTo(x + s, tickTop)
            markerPath.lineTo(x, tickTop + s * 1.4f)
            markerPath.close()
            drawPath(markerPath, color)
            drawLine(color, Offset(x, tickTop + s * 1.4f), Offset(x, rulerH), strokeWidth = d)
        }
    }

    private fun DrawScope.drawMarkerName(label: String, left: Float, avail: Float, top: Float) {
        val step = 12f * d
        val bucket = max(1, (avail / step).toInt())
        val key = label to bucket
        val layout = markerNames[key] ?: measurer.measure(
            text = label, style = markerNameStyle, overflow = TextOverflow.Ellipsis, softWrap = false, maxLines = 1,
            constraints = Constraints(maxWidth = max(1, (bucket * step).toInt())),
        ).also { if (markerNames.size > 256) markerNames.clear(); markerNames[key] = it }
        drawText(layout, alpha = 0.85f, topLeft = Offset(left, top))
    }

    /**
     * Aba do tempo atual (a da Omega): 70 × 18 dp no cabeçote, canto de cima à
     * esquerda chanfrado, `MM:SS:FF` tabular no destaque. Presa dentro da régua.
     */
    private fun DrawScope.drawTimeTab(px: Float, frame: Int, fps: Float, w: Float) {
        if (digits[0] == null) {
            for (dgt in 0..9) digits[dgt] = measurer.measure(dgt.toString(), digitStyle)
            digits[10] = measurer.measure(":", digitStyle)
            digitWidth = (0..9).maxOf { digits[it]!!.size.width }.toFloat()
            colonWidth = digits[10]!!.size.width.toFloat()
            digitBaseline = digits[0]!!.firstBaseline
        }
        Timecode.split(frame, fps, tc)
        val ffDigits = if (fps > 100f) 3 else 2
        var hourDigits = 0
        var hv = tc[0]
        while (hv > 0) {
            hourDigits++
            hv /= 10
        }
        val colons = if (hourDigits > 0) 3 else 2
        val total = (hourDigits + 4 + ffDigits) * digitWidth + colons * colonWidth
        val tabW = max(m.omegaTabMinWidth, total + 2f * m.omegaTabPad)
        if (px < -tabW || px > w + tabW) return
        val cx = (px).coerceIn(tabW / 2f, max(tabW / 2f, w - tabW / 2f))
        val left = cx - tabW / 2f
        val right = cx + tabW / 2f
        val top = AeDims.TAB_TOP * d
        val bottom = top + m.omegaTabHeight
        val r = m.omegaTabRadius
        val ch = m.omegaTabChamfer
        tabPath.reset()
        tabPath.moveTo(left + ch, top)
        tabPath.lineTo(right - r, top)
        tabPath.quadraticTo(right, top, right, top + r)
        tabPath.lineTo(right, bottom - r)
        tabPath.quadraticTo(right, bottom, right - r, bottom)
        tabPath.lineTo(left + r, bottom)
        tabPath.quadraticTo(left, bottom, left, bottom - r)
        tabPath.lineTo(left, top + ch)
        tabPath.close()
        drawPath(tabPath, AureaTimeline.OmegaTimeTab)
        val textTop = top + (m.omegaTabHeight - digits[0]!!.size.height) / 2f
        var x = cx - total / 2f
        if (hourDigits > 0) {
            x = drawNumber(tc[0], hourDigits, x, textTop)
            x = drawColon(x, textTop)
        }
        x = drawNumber(tc[1], 2, x, textTop)
        x = drawColon(x, textTop)
        x = drawNumber(tc[2], 2, x, textTop)
        x = drawColon(x, textTop)
        drawNumber(tc[3], ffDigits, x, textTop)
    }

    private fun DrawScope.drawNumber(value: Int, count: Int, x0: Float, top: Float): Float {
        var x = x0
        var div = 1
        repeat(count - 1) { div *= 10 }
        while (div > 0) {
            val layout = digits[(value / div) % 10]!!
            drawText(layout, color = AureaColors.Accent, topLeft = Offset(x + (digitWidth - layout.size.width) / 2f, top))
            x += digitWidth
            div /= 10
        }
        return x
    }

    private fun DrawScope.drawColon(x: Float, top: Float): Float {
        drawText(digits[10]!!, color = AureaColors.Accent, topLeft = Offset(x, top))
        return x + colonWidth
    }

    /** Glifo CupertinoIcons (no estilo do app) centrado em (cx, cy). */
    private fun DrawScope.drawGlyph(glyph: Char, sizeDp: Float, tint: Color, cx: Float, cy: Float) {
        val shown = AureaIconStyle.glyph(glyph)
        val key = shown.code * 64 + sizeDp.roundToInt()
        val layout = glyphs.getOrPut(key) {
            val fontSize = sizeDp.sp
            measurer.measure(shown.toString(), TextStyle(fontFamily = CupertinoIconsFont, fontSize = fontSize, lineHeight = fontSize,
                platformStyle = PlatformTextStyle(includeFontPadding = false),
                lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both)))
        }
        drawText(layout, color = tint, topLeft = Offset(cx - layout.size.width / 2f, cy - layout.size.height / 2f))
    }

    private companion object {
        val PREVIEW_BUFFER = Color(0xFF4DA3FF)
        val WORK_FILL = Color(0xB3FFC94D)
        val WORK_HANDLE = Color(0xFFFFC94D)
        val WORK_IDLE = Color(0x2EFFFFFF)
        val WORK_IDLE_HANDLE = Color(0x66FFFFFF)
        val WORK_VEIL = Color(0x40000000)
        val OUT_VEIL = Color(0x33000000)
        val SELECTED_ROW = Color(0x0FFFFFFF)
        val ROW_LINE = Color(0x0DFFFFFF)
        val LANE_BG = Color(0x1A000000)
        val WAVE_BACKING = Color(0x99101418)
        val BALLOON = Color.Black.copy(alpha = 0.82f)
        val LABEL_STEPS = longArrayOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600)
    }
}

// =============================================================================
// Coluna das camadas (esquerda)
// =============================================================================

/** As larguras das colunas do AE (cabeçalho e linhas usam as mesmas), em dp. */
private object AeCols {
    val tag = 3.dp
    val sw = 22.dp
    val twirl = 16.dp
    val thumb = 30.dp
    val number = 24.dp
    val parent = 104.dp
}

/** Tempo atual (como o do AE), a busca e os ícones das colunas. Mesma altura da régua. */
@Composable
private fun AeOutlineHeader(store: EditorStore, search: String, onSearch: (String) -> Unit) {
    Column(Modifier.fillMaxWidth().height(AeDims.RULER.dp)) {
        Row(Modifier.fillMaxWidth().height(28.dp).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            AeClock(store)
            Spacer(Modifier.width(12.dp))
            AeSearchField(search, stringResource(R.string.pc_search), onSearch)
        }
        Row(Modifier.fillMaxWidth().height(18.dp), verticalAlignment = Alignment.CenterVertically) {
            Spacer(Modifier.width(AeCols.tag))
            for (g in listOf(CupertinoGlyph.Eye, CupertinoGlyph.CircleFill, CupertinoGlyph.LockFill)) {
                Box(Modifier.width(AeCols.sw), contentAlignment = Alignment.Center) { CupertinoIcon(g, 10.dp, AureaColors.Subtle) }
            }
            Spacer(Modifier.width(AeCols.twirl + AeCols.thumb))
            Text("#", color = AureaColors.Subtle, fontSize = 10.sp, modifier = Modifier.width(AeCols.number), textAlign = TextAlign.Center)
            Text(stringResource(R.string.pc_col_name), color = AureaColors.Subtle, fontSize = 10.sp, modifier = Modifier.weight(1f), maxLines = 1)
            Box(Modifier.width(AeCols.sw), contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.PersonCropCircle, 10.dp, AureaColors.Subtle) }
            Box(Modifier.width(AeCols.sw), contentAlignment = Alignment.Center) { AeQualityGlyph(0, AureaColors.Subtle, 10.dp) }
            for (g in listOf(CupertinoGlyph.Sparkles, CupertinoGlyph.MotionBlur, CupertinoGlyph.SliderHorizontal3, CupertinoGlyph.Cube)) {
                Box(Modifier.width(AeCols.sw), contentAlignment = Alignment.Center) { CupertinoIcon(g, 10.dp, AureaColors.Subtle) }
            }
            Text(stringResource(R.string.pc_col_parent), color = AureaColors.Subtle, fontSize = 10.sp,
                modifier = Modifier.width(AeCols.parent).padding(start = 8.dp), maxLines = 1)
            Spacer(Modifier.width(6.dp))
        }
    }
}

/** Relógio do tempo atual: isolado para só ele recompor a cada quadro tocando. */
@Composable
private fun AeClock(store: EditorStore) {
    val fps = TimeAxis.safeFps(store.project.fps)
    Column {
        Text(Timecode.format(store.playhead, fps), color = AureaColors.Accent, fontSize = 15.sp, fontWeight = FontWeight.SemiBold,
            lineHeight = 15.sp, style = TextStyle(fontFeatureSettings = "tnum"))
        Text(String.format(java.util.Locale.ROOT, "%05d (%.2f fps)", store.playhead, fps), color = AureaColors.Subtle, fontSize = 8.sp, lineHeight = 8.sp)
    }
}

@Composable
private fun AeSearchField(value: String, hint: String, onChange: (String) -> Unit) {
    Box(Modifier.width(170.dp).height(20.dp).background(AureaColors.Chip, RoundedCornerShape(10.dp)).padding(horizontal = 8.dp), contentAlignment = Alignment.CenterStart) {
        if (value.isEmpty()) Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.Search, 10.dp, AureaColors.Subtle)
            Spacer(Modifier.width(4.dp))
            Text(hint, color = AureaColors.Subtle, fontSize = 11.sp)
        }
        BasicTextField(value, onChange, singleLine = true, textStyle = TextStyle(color = AureaColors.Text, fontSize = 11.sp),
            cursorBrush = SolidColor(AureaColors.Accent), modifier = Modifier.fillMaxWidth())
    }
}

@Composable
private fun AeOutline(
    store: EditorStore, ui: EditorUi, view: AeView, layout: AeLayout, thumbs: ThumbStrip, rulerPx: Float,
    expanded: MutableMap<Long, Boolean>, groupOpen: MutableMap<String, Boolean>, onToggleTransform: (Long) -> Unit,
) {
    val density = LocalDensity.current
    // Só as linhas à vista são compostas; a faixa muda só quando outra linha entra ou sai.
    val visible by remember(layout) {
        derivedStateOf {
            val top = view.scrollY
            val bottom = top + max(0f, view.height - rulerPx)
            val a = max(0, layout.indexAt(top))
            var b = a
            while (b + 1 < layout.lines.size && layout.lines[b + 1].top < bottom) b++
            a..b
        }
    }
    Box(Modifier.fillMaxSize().clipToBounds()) {
        for (i in visible) {
            val line = layout.lines.getOrNull(i) ?: continue
            if (line.kind != AeLine.LAYER) continue
            androidx.compose.runtime.key(line.layer.id) {
                AeLayerOutline(store, view, line, layout, expanded[line.layer.id] == true, thumbs,
                    Modifier.offset { IntOffset(0, (line.top - view.scrollY).roundToInt()) }.fillMaxWidth()
                        .height(with(density) { line.height.toDp() }),
                ) { onToggleTransform(line.layer.id) }
            }
        }
        // Grupos e propriedades à vista (um detalhe lido por camada aberta).
        val details = remember(store.playhead, store.layers, store.keyframes, store.detail) { HashMap<Long, LayerDetail?>() }
        for (i in visible) {
            val line = layout.lines.getOrNull(i) ?: continue
            if (line.kind == AeLine.LAYER) continue
            val id = line.layer.id
            val modifier = Modifier.offset { IntOffset(0, (line.top - view.scrollY).roundToInt()) }.fillMaxWidth()
                .height(with(density) { line.height.toDp() })
            val toggle = { groupOpen[line.key] = !line.open }
            androidx.compose.runtime.key(line.key) {
                if (line.kind == AeLine.GROUP) {
                    AeGroupHeader(store, ui, line, modifier, toggle)
                } else {
                    val d = details.getOrPut(id) { store.layerDetailOf(id) }
                    AePropLine(store, ui, line, d, modifier, toggle)
                }
            }
        }
        // Destino do reordenar.
        val ry = view.reorderY
        if (view.reorderId != 0L && !ry.isNaN()) {
            val target = layout.lines.getOrNull(layout.indexAt(ry))
            if (target != null) {
                val y = target.top + (if (ry > target.top + target.height / 2f) target.height else 0f) - view.scrollY
                Box(Modifier.offset { IntOffset(0, (y - density.density).roundToInt()) }.fillMaxWidth().height(2.dp).background(AureaColors.Accent))
            }
        }
    }
}

/**
 * Uma camada na coluna: a pílula do cabeçalho do Aurea (fundo e fio da Omega,
 * ponta direita redonda, faixa da etiqueta), com os interruptores do AE.
 * Clique escolhe (Ctrl/Shift soma); arrastar para cima/baixo reordena.
 */
@Composable
private fun AeLayerOutline(
    store: EditorStore, view: AeView, line: AeLine, layout: AeLayout, open: Boolean, thumbs: ThumbStrip,
    modifier: Modifier, onTwirl: () -> Unit,
) {
    val l = line.layer
    val r = line.row
    val id = l.id
    val selected = id in store.selection
    val lineNow = rememberUpdatedState(line)
    val layoutNow = rememberUpdatedState(layout)
    val pill = RoundedCornerShape(topEnd = 14.dp, bottomEnd = 14.dp)
    Box(modifier.padding(top = 1.dp, bottom = 1.dp, end = 6.dp)) {
        Row(
            Modifier.fillMaxSize().clip(pill).background(AureaTimeline.OmegaHeader)
                .background(if (selected) AureaColors.Accent.copy(alpha = 0.22f) else Color.Transparent)
                .border(if (selected) 1.dp else 0.5.dp, if (selected) AureaColors.Accent else AureaTimeline.OmegaHeaderBorder, pill)
                .pointerInput(id) {
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = true)
                        if (!currentEvent.buttons.isPrimaryPressed) return@awaitEachGesture
                        val mods = currentEvent.keyboardModifiers
                        val startY = lineNow.value.top
                        var dragging = false
                        while (true) {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!c.pressed) {
                                c.consume()
                                break
                            }
                            if (!dragging && abs(c.position.y - down.position.y) < viewConfiguration.touchSlop) continue
                            dragging = true
                            c.consume()
                            view.reorderId = id
                            view.reorderY = startY + c.position.y
                        }
                        if (dragging) {
                            val lay = layoutNow.value
                            val target = lay.lines.getOrNull(lay.indexAt(view.reorderY))?.layer
                            view.reorderId = 0L
                            view.reorderY = Float.NaN
                            if (target != null && target.id != id) {
                                val to = store.layers.indexOfFirst { it.id == target.id }
                                if (to >= 0) store.reorderLayer(id, to)
                            }
                        } else if (mods.isCtrlPressed || mods.isShiftPressed) {
                            store.select(id, additive = true)
                        } else {
                            if (store.keySelection != null) store.clearKeySelection()
                            store.select(id)
                        }
                    }
                },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // Faixa da etiqueta (2 dp à vista no Aurea; aqui 3).
            Box(Modifier.width(AeCols.tag).fillMaxHeight().background(ShellColors.LabelPalette.getOrNull(l.label - 1) ?: Color.Transparent))
            AeSwitch(if (l.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, l.visible,
                stringResource(if (l.visible) R.string.editor_ocultar_camada else R.string.editor_mostrar_camada)) { store.setLayerVisible(id, !l.visible) }
            AeSwitch(CupertinoGlyph.CircleFill, l.solo, stringResource(R.string.editor_solo)) { store.setLayerSolo(id, !l.solo) }
            AeSwitch(CupertinoGlyph.LockFill, l.locked,
                stringResource(if (l.locked) R.string.editor_desbloquear_camada else R.string.editor_bloquear_camada)) { store.setLayerLocked(id, !l.locked) }
            Box(Modifier.width(AeCols.twirl).fillMaxHeight().tocavel(shrink = 1f, onClick = onTwirl), contentAlignment = Alignment.Center) {
                CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 9.dp, AureaTimeline.GutterIcon)
            }
            Box(Modifier.width(AeCols.thumb).fillMaxHeight(), contentAlignment = Alignment.Center) { AeLayerGlyph(store, line, thumbs) }
            Text("${line.number}", color = AureaTimeline.GutterIcon, fontSize = 11.sp, modifier = Modifier.width(AeCols.number),
                textAlign = TextAlign.Center, style = TextStyle(fontFeatureSettings = "tnum"))
            Text(l.name, color = if (selected) AureaColors.Text else AureaTimeline.GutterEye.copy(alpha = if (l.visible) 0.85f else 0.45f),
                fontSize = 12.sp, fontWeight = FontWeight.W500, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            AeSwitch(CupertinoGlyph.PersonCropCircle, l.shy, stringResource(R.string.ae_layer_shy)) { store.setLayerShy(id, !l.shy) }
            AeQualitySwitch(store, l, enabled = r.type != LayerType.Audio)
            AeSwitch(CupertinoGlyph.Sparkles, l.effectCount > 0, stringResource(R.string.pc_tl_fx)) {
                if (store.primary != id || store.selection.size != 1) store.select(id)
                PcLibraryState.effects = true
            }
            val visual = r.type != LayerType.Audio
            AeMotionBlurSwitch(store, id, visual)
            AeSwitch(CupertinoGlyph.SliderHorizontal3, l.adjustment, stringResource(R.string.editor_camada_ajuste), enabled = visual) {
                store.setLayerAdjustment(id, !l.adjustment)
            }
            val intrinsic = l.kind in 8..10
            AeSwitch(CupertinoGlyph.Cube, l.isThreeD || intrinsic, stringResource(R.string.sh_add_tab_3d), enabled = visual && !intrinsic && !l.locked) {
                if (store.primary != id || store.selection.size != 1) store.select(id)
                store.changeLayer3D(!l.isThreeD)
            }
            AeParentPicker(store, l)
            Spacer(Modifier.width(6.dp))
        }
    }
}

/**
 * Desfoque de movimento da camada: o estado só vem do detalhe (o da principal já
 * está no store; o das outras é lido quando as camadas mudam). Isolado para o
 * detalhe que muda tocando não recompor a linha inteira.
 */
@Composable
private fun AeMotionBlurSwitch(store: EditorStore, id: Long, enabled: Boolean) {
    val other = remember(store.layers, store.primary, id) { if (store.primary == id) null else store.layerDetailOf(id)?.motionBlur == true }
    val on = other ?: (store.detail?.motionBlur == true)
    AeSwitch(CupertinoGlyph.MotionBlur, on, stringResource(R.string.editor_desfoque_movimento), enabled = enabled) {
        if (!on && !store.compMotionBlur) store.setCompositionMotionBlur(true)
        store.setLayerMotionBlur(id, !on)
    }
}

/** Um interruptor do AE com os glifos do Aurea: aceso = branco, apagado = tom muted. */
@Composable
private fun AeSwitch(glyph: Char, on: Boolean, description: String, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.width(AeCols.sw).fillMaxHeight()
            .then(if (enabled) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier)
            .semantics { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 12.dp,
            if (on) AureaTimeline.GutterEye.copy(alpha = 0.92f) else AureaTimeline.GutterIcon.copy(alpha = if (enabled) 0.32f else 0.14f))
    }
}

/** O quadradinho do tipo (como no cabeçalho do Aurea): miniatura, "T", ponto, nota ou o glifo. */
@Composable
private fun AeLayerGlyph(store: EditorStore, line: AeLine, thumbs: ThumbStrip) {
    val r = line.row
    val alpha = if (r.visible) 1f else 0.6f
    when (r.type) {
        LayerType.Video, LayerType.Image -> {
            val d = LocalDensity.current.density
            val heightPx = (line.height - 2f * AeDims.CARD_PAD * d).roundToInt().coerceIn(1, AE_THUMB_MAX_PX)
            val fps = TimeAxis.safeFps(store.project.fps)
            Canvas(Modifier.size(22.dp, 18.dp)) {
                val generation = store.thumbnailGeneration
                thumbs.beginFrame(2)
                val image = r.type == LayerType.Image
                val bucket = if (image) -1 else Thumbs.bucketOf(r.offset.toDouble(), fps)
                val frame = if (image) r.start else Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), r.start, r.offset)
                val bmp = thumbs.get(store.thumbnails, r.id, bucket, frame, heightPx, generation)
                if (bmp == null || bmp.width <= 0 || bmp.height <= 0) {
                    drawRoundRect(Color(ClipColors.solid(r.type.color.toArgb())), cornerRadius = CornerRadius(3f * d))
                    return@Canvas
                }
                val s = min(size.width / bmp.width, size.height / bmp.height)
                val dw = bmp.width * s
                val dh = bmp.height * s
                val dst = RectF((size.width - dw) / 2f, (size.height - dh) / 2f, (size.width + dw) / 2f, (size.height + dh) / 2f)
                platformCanvas.drawBitmap(bmp, null, dst, Paint(Paint.FILTER_BITMAP_FLAG).apply { this.alpha = (alpha * 255f).roundToInt() })
            }
        }
        LayerType.Audio -> CupertinoIcon(CupertinoGlyph.MusicNote, 11.dp, AureaTimeline.GlyphAudio.copy(alpha = alpha))
        LayerType.Text -> Text("T", color = AureaTimeline.GlyphText.copy(alpha = alpha), fontSize = 11.sp, fontWeight = FontWeight.W700)
        LayerType.Shape -> Box(Modifier.size(9.dp).background(AureaTimeline.GlyphShape.copy(alpha = alpha), CircleShape))
        else -> CupertinoIcon(r.type.glyph, 12.dp, AureaTimeline.GutterEye.copy(alpha = alpha))
    }
}

/** A coluna "Pai" do AE: o nome do pai; clicar abre a lista das camadas (menu do Aurea). */
@Composable
private fun AeParentPicker(store: EditorStore, l: LayerRow) {
    var open by remember { mutableStateOf(false) }
    val parent = store.layers.getOrNull(l.parentIndex)?.takeIf { l.parentIndex >= 0 }
    Box(
        Modifier.width(AeCols.parent).padding(start = 6.dp, end = 2.dp).height(18.dp)
            .background(Color.Black.copy(alpha = 0.22f), RoundedCornerShape(9.dp))
            .tocavel(shrink = 1f) { open = true }.padding(horizontal = 8.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        Text(parent?.name ?: stringResource(R.string.pc_none), color = AureaTimeline.GutterIcon, fontSize = 11.sp,
            maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (open) {
            val none = stringResource(R.string.pc_none)
            val entries = buildList<MenuEntry> {
                add(MenuEntry.Item(none, checked = parent == null) { store.setParent(l.id, 0L) })
                store.layers.forEachIndexed { i, o ->
                    if (o.id != l.id) add(MenuEntry.Item("${i + 1}. ${o.name}", checked = parent?.id == o.id) { store.setParent(l.id, o.id) })
                }
            }
            AnchoredMenu(expanded = true, onDismiss = { open = false }, entries = entries, width = 240.dp,
                tone = AureaSurfaceTone.Editor, tag = "pc.timeline.parent")
        }
    }
}

/**
 * O cabeçalho de um grupo (Texto, Máscaras, Efeitos, Transformar, Opções de
 * material, Áudio): a seta abre e fecha; duplo clique no nome abre o painel do
 * Aurea do grupo. Texto tem "Animar ▸"; Efeitos, o "+".
 */
@Composable
private fun AeGroupHeader(store: EditorStore, ui: EditorUi, line: AeLine, modifier: Modifier, onTwirl: () -> Unit) {
    val g = line.group ?: return
    val id = line.layer.id
    val isPrimary = store.primary == id
    // Os grupos que o motor só publica da camada escolhida: abrir escolhe a camada.
    val twirl = {
        if (!isPrimary && g != AeGroup.Transform) store.select(id)
        onTwirl()
    }
    Row(modifier.padding(start = 30.dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(16.dp).tocavel(shrink = 1f, onClick = twirl), contentAlignment = Alignment.Center) {
            CupertinoIcon(if (line.open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 9.dp, AureaTimeline.GutterIcon)
        }
        Spacer(Modifier.width(6.dp))
        Text(
            groupLabel(g), color = AureaColors.Text, fontSize = 12.sp, fontWeight = FontWeight.W500, maxLines = 1,
            modifier = Modifier.weight(1f).pointerInput(id, g) {
                detectTapGestures(
                    onTap = { twirl() },
                    onDoubleTap = { openGroupPanel(store, ui, id, g) },
                )
            },
        )
        when (g) {
            AeGroup.Text -> AeChip(stringResource(R.string.rig_animate) + " ▸", AureaColors.Accent) { openTextAnimators(store, ui, id) }
            AeGroup.Effects -> Box(
                Modifier.size(20.dp).tocavel(shrink = 1f) {
                    if (store.primary != id || store.selection.size != 1) store.select(id)
                    PcLibraryState.effects = true
                }.semantics { contentDescription = "+" },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Plus, 11.dp, AureaColors.Muted) }
            else -> {}
        }
    }
}

/** Um botãozinho de texto (fundo Chip do Aurea). */
@Composable
private fun AeChip(text: String, color: Color, onClick: () -> Unit) {
    Box(
        Modifier.height(18.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip).tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = color, fontSize = 11.sp, maxLines = 1)
    }
}

/** Uma linha de propriedade: a do Transformar ou a de um item (efeito, máscara, texto, material, áudio). */
@Composable
private fun AePropLine(store: EditorStore, ui: EditorUi, line: AeLine, d: LayerDetail?, modifier: Modifier, onTwirl: () -> Unit) {
    val l = line.layer
    when (val item = line.item) {
        null -> line.prop?.let { AePropOutline(store, l, it, d, line, modifier) }
        is AeFxItem -> AeEffectRow(store, line, item, modifier, onTwirl)
        is AeFxParamItem -> AeEffectParamRow(store, line, item, d, modifier)
        is AeMaskItem -> AeMaskRow(store, ui, line, item, modifier, onTwirl)
        is AeMaskPropItem -> AeMaskPropRow(store, line, item, d, modifier)
        AeSourceTextItem -> AeSourceTextRow(store, line, d, modifier)
        is AeMaterialItem -> AeMaterialRow(store, item, modifier)
        AeAudioItem -> AeAudioRow(store, line, d, modifier)
    }
}

/**
 * A moldura comum das propriedades: ⏱ · nome · valores · ◀ ◆ ▶ (sem
 * [onStopwatch] o cronômetro some; sem [onKey], a navegação).
 */
@Composable
private fun AePropShell(
    store: EditorStore, line: AeLine, modifier: Modifier, label: String,
    animated: Boolean, onStopwatch: (() -> Unit)?, keyHere: Boolean, onKey: (() -> Unit)?,
    extra: @Composable () -> Unit = {},
    value: @Composable () -> Unit,
) {
    Row(modifier.padding(start = (46 + 16 * line.depth).dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        if (onStopwatch != null) {
            Box(Modifier.size(20.dp).tocavel(shrink = 1f, onClick = onStopwatch), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Timer, 13.dp, if (animated) AureaColors.Accent else AureaTimeline.GutterIcon)
            }
        } else {
            Spacer(Modifier.width(20.dp))
        }
        Spacer(Modifier.width(4.dp))
        Text(label, color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.width((96 - 16 * line.depth).dp), maxLines = 1,
            overflow = TextOverflow.Ellipsis)
        value()
        Spacer(Modifier.weight(1f))
        extra()
        if (onKey != null && line.instants.size >= 2) AeCurveButton(store, line)
        if (onKey != null) AeKeyNav(store, line.instants, keyHere, onKey)
    }
}

/**
 * O símbolo de curva (o do Aurea) numa propriedade com 2+ keyframes: abre a
 * janelinha de Curvas no keyframe escolhido dela; sem um, no do cabeçote (ou
 * o anterior), como o botão do editor de gráficos do AE.
 */
@Composable
private fun AeCurveButton(store: EditorStore, line: AeLine) {
    if (line.keysAt.any { g -> g.any { it.property == com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE } }) return
    val id = line.layer.id
    val sel = store.selectedKeyframe
    val mine = sel != null && sel.first == id && line.keysAt.any { g -> g.any { k ->
        k.property == sel.second.property && k.effectIndex == sel.second.effectIndex && k.paramIndex == sel.second.paramIndex && k.time == sel.second.time
    } }
    val lit = mine && PcCurvePopup.open
    val label = stringResource(R.string.pc_curve_graph)
    Box(
        Modifier.size(20.dp).clip(RoundedCornerShape(4.dp)).background(if (lit) AureaColors.Accent.copy(alpha = 0.18f) else Color.Transparent)
            .semantics { contentDescription = label }
            .tocavel(shrink = 1f) {
                if (!mine) {
                    val head = store.playhead
                    val i = line.instants.indexOfLast { it <= head }.coerceAtLeast(0)
                    val group = line.keysAt[i]
                    if (id !in store.selection) store.select(id)
                    store.tapTimelineKey(id, group.first(), group)
                }
                PcCurvePopup.open = true
            },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.Scribble, 12.dp, if (lit) AureaColors.Accent else AureaColors.Muted)
    }
}

/** ◀ ◆ ▶: keyframe anterior, marcar/desmarcar no cabeçote, próximo. */
@Composable
private fun AeKeyNav(store: EditorStore, times: IntArray, keyHere: Boolean, onToggle: () -> Unit) {
    val head = store.playhead
    val prev = times.lastOrNull { it < head }
    val next = times.firstOrNull { it > head }
    Box(Modifier.size(20.dp).tocavel(shrink = 1f) { prev?.let(store::seek) }, contentAlignment = Alignment.Center) {
        CupertinoIcon(CupertinoGlyph.ChevronLeft, 10.dp, if (prev != null) AureaColors.Text else AureaColors.Subtle)
    }
    Box(Modifier.size(20.dp).tocavel(shrink = 1f, onClick = onToggle), contentAlignment = Alignment.Center) {
        CupertinoIcon(if (keyHere) CupertinoGlyph.SuitDiamondFill else CupertinoGlyph.SuitDiamond, 11.dp, if (keyHere) AureaColors.Keyframe else AureaColors.Muted)
    }
    Box(Modifier.size(20.dp).tocavel(shrink = 1f) { next?.let(store::seek) }, contentAlignment = Alignment.Center) {
        CupertinoIcon(CupertinoGlyph.ChevronRight, 10.dp, if (next != null) AureaColors.Text else AureaColors.Subtle)
    }
}

/** Todas as chaves de [line] (inclusive as que a timeline não desenha), para o cronômetro desligar. */
private fun lineKeys(store: EditorStore, line: AeLine): List<KeyframeRow> {
    val match = line.match ?: return emptyList()
    return store.keyframes[line.layer.id].orEmpty().filter(match)
}

/** ⏱ nome  valores (arrastar muda, clicar digita)  ◀ ◆ ▶ */
@Composable
private fun AePropOutline(store: EditorStore, l: LayerRow, prop: AeProp, d: LayerDetail?, line: AeLine, modifier: Modifier) {
    val animated = d != null && prop.props.any { d.isAnimated(it) }
    val keyHere = d != null && prop.props.any { d.hasKeyAtPlayhead(it) }
    AePropShell(
        store, line, modifier, stringResource(prop.label), animated,
        // Cronômetro: liga a animação (keyframe no cabeçote); ligado, desliga e apaga as marcas (como no AE).
        onStopwatch = {
            if (animated) store.deleteAllKeyframes(l.id, prop.props.map { TrackKey(it) })
            else store.toggleTransformKeyframe(prop.props, l.id)
        },
        keyHere = keyHere,
        onKey = { store.toggleTransformKeyframe(prop.props, l.id) },
    ) {
        if (d != null) {
            val values = prop.read(d)
            // Escala: a corrente do AE (manter proporção, ligada por padrão; a mesma do painel Transformar).
            val isScale = prop.props.firstOrNull() == com.aurea.aurea.engine.TrackProperty.SCALE_X
            if (isScale) AeScaleLink(store)
            // Os valores do começo do gesto: a proporção vem deles (o detalhe só relê no próximo quadro).
            // O arrasto do AeScrubValue guarda as funções da 1ª composição: o resto vem por estado atualizado.
            val from = remember(l.id) { FloatArray(3) }
            val valuesNow = rememberUpdatedState(values)
            val kindNow = rememberUpdatedState(d.kind)
            values.forEachIndexed { i, v ->
                AeScrubValue(v * prop.factor, prop.unit, prop.step,
                    onBegin = {
                        store.beginGesture("transform")
                        if (isScale) valuesNow.value.let { s -> for (a in 0..2) from[a] = s.getOrElse(a) { 1f } }
                    },
                    onEnd = { store.endGesture() },
                ) { shown ->
                    val target = shown / prop.factor
                    if (isScale && store.scaleAxesLinked) {
                        aeSetLinkedScale(store, l.id, kindNow.value, valuesNow.value.size == 3, i, target, from)
                    } else {
                        store.setTransform(prop.props[i], target, l.id)
                    }
                }
                if (i < values.lastIndex) Text(", ", color = AureaColors.Subtle, fontSize = 12.sp)
            }
        }
    }
}

/** A corrente da Escala (manter proporção): a mesma chave do painel Transformar do Aurea (`scaleAxesLinked`). */
@Composable
private fun AeScaleLink(store: EditorStore) {
    val on = store.scaleAxesLinked
    val label = stringResource(R.string.pc_scale_constrain)
    Box(
        Modifier.size(18.dp).clip(RoundedCornerShape(4.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.14f) else Color.Transparent)
            .semantics { contentDescription = label }
            .tocavel(shrink = 1f) { store.scaleAxesLinked = !store.scaleAxesLinked },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.Link, 11.dp, if (on) AureaColors.Accent else AureaColors.Subtle)
    }
    Spacer(Modifier.width(4.dp))
}

/**
 * Escala com a proporção presa (como o AE e o painel Transformar): o eixo
 * mexido vira [target] e os outros acompanham na mesma razão, a partir dos
 * valores do começo do gesto ([from]). X e Y (e Z) vão num comando só
 * (`setTransform2` / `setScale3`), sem o quadro intermediário com um lado
 * esticado. Algum eixo em 0: todos ficam iguais a [target].
 */
private fun aeSetLinkedScale(store: EditorStore, id: Long, kind: Int, depth: Boolean, axis: Int, target: Float, from: FloatArray) {
    if (!target.isFinite()) return
    val count = if (depth) 3 else 2
    val base = from.getOrElse(axis) { 0f }
    if ((0 until count).any { from[it] == 0f || !from[it].isFinite() } || base == 0f) {
        if (depth) store.setScale3(floatArrayOf(target, target, target), id)
        else store.setTransform2(com.aurea.aurea.engine.TrackProperty.SCALE_X, target, com.aurea.aurea.engine.TrackProperty.SCALE_Y, target, id)
        return
    }
    val k = target / base
    if (depth) {
        store.setScale3(com.aurea.aurea.engine.AureaEngine.gestureScale3D(kind, from[0], from[1], from[2], 3, k), id)
    } else {
        store.setTransform2(com.aurea.aurea.engine.TrackProperty.SCALE_X, from[0] * k, com.aurea.aurea.engine.TrackProperty.SCALE_Y, from[1] * k, id)
    }
}

// --- Efeitos -------------------------------------------------------------------------------

/** Um efeito: a seta (com parâmetros animados), o olho e o nome; clique abre nos Controles de efeitos. */
@Composable
private fun AeEffectRow(store: EditorStore, line: AeLine, item: AeFxItem, modifier: Modifier, onTwirl: () -> Unit) {
    val e = item.effect
    val name = com.aurea.aurea.editor.panels.effectDisplayName(e.typeId, e.name)
    Row(modifier.padding(start = 46.dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(20.dp).then(if (item.twirl) Modifier.tocavel(shrink = 1f, onClick = onTwirl) else Modifier), contentAlignment = Alignment.Center) {
            if (item.twirl) CupertinoIcon(if (line.open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 9.dp, AureaTimeline.GutterIcon)
        }
        Spacer(Modifier.width(4.dp))
        val eyeLabel = stringResource(if (e.enabled) R.string.fxui_a11y_disable else R.string.fxui_a11y_enable, name)
        Box(
            Modifier.size(20.dp).tocavel(shrink = 1f) { store.setEffectEnabled(e.effectId, !e.enabled) }.semantics { contentDescription = eyeLabel },
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(if (e.enabled) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 12.dp,
                if (e.enabled) AureaTimeline.GutterEye.copy(alpha = 0.92f) else AureaTimeline.GutterIcon.copy(alpha = 0.4f))
        }
        Spacer(Modifier.width(6.dp))
        Text(
            if (e.known) name else "?$name", color = if (e.enabled) AureaColors.Text else AureaColors.Subtle, fontSize = 12.sp,
            maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).tocavel(shrink = 1f, role = null) { store.focusExistingEffect(e.typeId) },
        )
    }
}

/** Um parâmetro animado de efeito: ⏱ · nome · valor · ◀ ◆ ▶ (os mesmos do painel Efeitos). */
@Composable
private fun AeEffectParamRow(store: EditorStore, line: AeLine, item: AeFxParamItem, d: LayerDetail?, modifier: Modifier) {
    val p = store.effectParams[item.effectId]?.firstOrNull { it.index == item.index } ?: return
    val slot = com.aurea.aurea.editor.panels.ParamSlot.of(p)
    val disp = com.aurea.aurea.editor.panels.paramDisplay(item.typeId, slot)
    val label = disp.labelRes?.let { stringResource(it) } ?: disp.engineLabel
    val local = d?.localPlayhead
    val keys = lineKeys(store, line)
    val keyHere = local != null && keys.any { it.time == local }
    AePropShell(
        store, line, modifier, label, animated = keys.isNotEmpty(),
        onStopwatch = { store.deleteAllKeyframes(line.layer.id, com.aurea.aurea.editor.panels.paramKeys(item.effectId, slot)) },
        keyHere = keyHere,
        onKey = { store.toggleEffectKeyframe(item.effectId, p) },
    ) {
        val comps = slot.components
        if (comps in 1..2) {
            val step = max(slot.unitsPerDp * disp.scale * 0.5f, 0.001f)
            for (c in 0 until comps) {
                AeScrubValue(disp.toDisplay(p.value.getOrElse(c) { 0f }), disp.suffix, step,
                    onBegin = { store.beginGesture("efeito") }, onEnd = { store.endGesture() },
                ) { shown -> store.setEffectParam(item.effectId, p, disp.toEngine(shown), c) }
                if (c < comps - 1) Text(", ", color = AureaColors.Subtle, fontSize = 12.sp)
            }
        } else {
            Text(p.value.take(comps.coerceAtLeast(1)).joinToString(", ") { fmt(disp.toDisplay(it)) }, color = AureaColors.Subtle, fontSize = 12.sp, maxLines = 1)
        }
    }
}

// --- Máscaras ------------------------------------------------------------------------------

/** Os modos da máscara na ordem do painel Máscara do Aurea (os ids do motor). */
private val MASK_MODES = listOf(
    0 to R.string.panel_somar, 1 to R.string.panel_subtrair, 2 to R.string.panel_intersecao,
    5 to R.string.ae_mask_lighten, 6 to R.string.ae_mask_darken, 3 to R.string.panel_diferenca, 4 to R.string.panel_desligada,
)

/** Uma máscara: a seta, o nome, o modo (menu do Aurea) e Inverter. */
@Composable
private fun AeMaskRow(store: EditorStore, ui: EditorUi, line: AeLine, item: AeMaskItem, modifier: Modifier, onTwirl: () -> Unit) {
    val m = store.masks?.find(item.maskId) ?: return
    var modes by remember { mutableStateOf(false) }
    Row(modifier.padding(start = 46.dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(20.dp).tocavel(shrink = 1f, onClick = onTwirl), contentAlignment = Alignment.Center) {
            CupertinoIcon(if (line.open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 9.dp, AureaTimeline.GutterIcon)
        }
        Spacer(Modifier.width(4.dp))
        Text(stringResource(R.string.pn_mask_n, item.number), color = AureaColors.Text, fontSize = 12.sp, maxLines = 1,
            modifier = Modifier.width(96.dp).tocavel(shrink = 1f, role = null, onClick = onTwirl))
        Box {
            val label = MASK_MODES.firstOrNull { it.first == m.op }?.second?.let { stringResource(it) } ?: "—"
            AeChip(label, AureaColors.Text) { modes = true }
            if (modes) {
                val entries = MASK_MODES.map { (op, res) ->
                    MenuEntry.Item(stringResource(res), checked = op == m.op) { store.setMaskProps(m.id, op, m.inverted, m.feather, m.expansion, m.opacity) }
                }
                AnchoredMenu(expanded = true, onDismiss = { modes = false }, entries = entries, width = 200.dp,
                    tone = AureaSurfaceTone.Editor, tag = "pc.timeline.mask.mode")
            }
        }
        Spacer(Modifier.width(6.dp))
        AeChip(stringResource(R.string.fx_inverter), if (m.inverted) AureaColors.Accent else AureaColors.Muted) {
            store.setMaskProps(m.id, m.op, !m.inverted, m.feather, m.expansion, m.opacity)
        }
        // Lote M: travada / oculta à vista e o "⋯" com o menu da máscara (o mesmo do botão direito).
        Spacer(Modifier.width(6.dp))
        if (m.locked) CupertinoIcon(CupertinoGlyph.LockFill, 10.dp, AureaColors.Muted)
        if (m.hidden) { Spacer(Modifier.width(3.dp)); CupertinoIcon(CupertinoGlyph.EyeSlash, 11.dp, AureaColors.Muted) }
        Spacer(Modifier.weight(1f))
        Box {
            var more by remember { mutableStateOf(false) }
            AeChip("⋯", AureaColors.Text) { more = true }
            if (more) {
                AnchoredMenu(expanded = true, onDismiss = { more = false }, entries = maskRowEntries(store, ui, m.id), width = 260.dp,
                    tone = AureaSurfaceTone.Editor, tag = "pc.timeline.mask.more")
            }
        }
    }
}

/** Caminho, Suavizar, Opacidade e Expandir de uma máscara (os do painel Máscara). */
@Composable
private fun AeMaskPropRow(store: EditorStore, line: AeLine, item: AeMaskPropItem, d: LayerDetail?, modifier: Modifier) {
    val m = store.masks?.find(item.maskId) ?: return
    val id = line.layer.id
    if (item.param < 0) {
        val keys = lineKeys(store, line)
        AePropShell(
            store, line, modifier, stringResource(R.string.panel_caminho), animated = m.keyCount > 0,
            onStopwatch = { if (m.keyCount > 0) store.deleteKeyframesTogether(id, keys) else store.toggleMaskKey(m.id) },
            keyHere = m.keyHere, onKey = { store.toggleMaskKey(m.id) },
        ) {}
        return
    }
    val (label, value, unit) = when (item.param) {
        0 -> Triple(stringResource(R.string.panel_suavizar), m.feather, "px")
        1 -> Triple(stringResource(R.string.panel_expandir), m.expansion, "px")
        else -> Triple(stringResource(R.string.panel_opacidade), m.opacity * 100f, "%")
    }
    val keys = lineKeys(store, line)
    val local = d?.localPlayhead
    val keyHere = local != null && keys.any { it.time == local }
    val target = TrackKey(com.aurea.aurea.engine.TrackProperty.MASK_PARAM, m.id, item.param)
    AePropShell(
        store, line, modifier, label, animated = keys.isNotEmpty(),
        onStopwatch = {
            store.timelineFocus = listOf(target)
            if (keys.isNotEmpty()) store.deleteKeyframesTogether(id, keys) else store.toggleMaskParamKey(m.id, item.param)
        },
        keyHere = keyHere,
        onKey = { store.timelineFocus = listOf(target); store.toggleMaskParamKey(m.id, item.param) },
    ) {
        AeScrubValue(value, unit, 0.5f, onBegin = { store.beginGesture("máscara") }, onEnd = { store.endGesture() }) { v ->
            store.timelineFocus = listOf(target)
            val clamped = when (item.param) {
                0 -> v.coerceIn(0f, 500f)
                1 -> v.coerceIn(-500f, 500f)
                else -> v.coerceIn(0f, 100f)
            }
            store.setMaskParam(m.id, item.param, if (item.param == 2) clamped / 100f else clamped)
        }
    }
}

// --- Texto, material e áudio ------------------------------------------------------------------

/** Texto-fonte: ⏱ grava o texto no cabeçote; clicar no texto digita; "=" abre a expressão. */
@Composable
private fun AeSourceTextRow(store: EditorStore, line: AeLine, d: LayerDetail?, modifier: Modifier) {
    val keys = lineKeys(store, line)
    val local = d?.localPlayhead
    val keyHere = local != null && keys.any { it.time == local }
    val label = stringResource(R.string.ae_anim_source_text)
    val track = remember { listOf(TrackKey(com.aurea.aurea.engine.TrackProperty.TEXT_SOURCE, 0, 0)) }
    val look = store.expressionLook(track)
    AePropShell(
        store, line, modifier, label, animated = keys.isNotEmpty(),
        onStopwatch = { store.timelineFocus = track; store.toggleTextSourceKey() },
        keyHere = keyHere,
        onKey = { store.timelineFocus = track; store.toggleTextSourceKey() },
        extra = {
            Box(Modifier.size(20.dp).tocavel(shrink = 1f) { store.openExpression(label, track) }, contentAlignment = Alignment.Center) {
                Text("=", color = when (look) {
                    com.aurea.aurea.engine.ExpressionLook.None -> AureaColors.Muted
                    com.aurea.aurea.engine.ExpressionLook.Error -> AureaColors.Danger
                    else -> AureaColors.Accent
                }, fontSize = 13.sp, fontWeight = FontWeight.W700)
            }
        },
    ) {
        val text = store.textDetail?.content.orEmpty().replace('\n', ' ')
        Text(text.ifEmpty { "—" }, color = AureaColors.Accent, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.widthIn(max = 150.dp).tocavel(shrink = 1f, role = null) { store.openTextEdit() })
    }
}

/** Aceita luzes · Projeta sombra · Recebe sombra (Ligado/Desligado, clique troca). */
@Composable
private fun AeMaterialRow(store: EditorStore, item: AeMaterialItem, modifier: Modifier) {
    val shadows = store.modelShadows
    val (label, on) = when (item.which) {
        0 -> stringResource(R.string.la_accepts_lights) to (store.layerAcceptsLights == 1)
        1 -> stringResource(R.string.pn_t3d_cast_shadow) to (shadows?.first == true)
        else -> stringResource(R.string.pn_t3d_receive_shadow) to (shadows?.second == true)
    }
    Row(modifier.padding(start = 70.dp, end = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.width(120.dp), maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(
            stringResource(if (on) R.string.common_on else R.string.common_off), color = if (on) AureaColors.Accent else AureaColors.Subtle, fontSize = 12.sp,
            modifier = Modifier.tocavel(shrink = 1f, role = null) {
                when (item.which) {
                    0 -> store.changeLayerAcceptsLights(!on)
                    1 -> shadows?.let { store.setModelShadows(!it.first, it.second) }
                    else -> shadows?.let { store.setModelShadows(it.first, !it.second) }
                }
            },
        )
    }
}

/** Níveis de áudio: o volume da camada (%), com cronômetro e keyframes. */
@Composable
private fun AeAudioRow(store: EditorStore, line: AeLine, d: LayerDetail?, modifier: Modifier) {
    val keys = lineKeys(store, line)
    val local = d?.localPlayhead
    val keyHere = local != null && keys.any { it.time == local }
    AePropShell(
        store, line, modifier, stringResource(R.string.pc_tl_audio_levels), animated = keys.isNotEmpty(),
        onStopwatch = { if (keys.isNotEmpty()) store.deleteKeyframesTogether(line.layer.id, keys) else store.toggleVolumeKeyframe() },
        keyHere = keyHere,
        onKey = { store.toggleVolumeKeyframe() },
    ) {
        if (d != null) {
            AeScrubValue(d.audioVolume * 100f, "%", 0.5f, onBegin = { store.beginGesture("volume") }, onEnd = { store.endGesture() }) { v ->
                store.setAudioVolume(v.coerceIn(0f, 200f) / 100f)
            }
        }
    }
}

/**
 * Um número do AE: arrastar para os lados muda (Shift = 10x, Ctrl = fino),
 * clicar abre para digitar (Enter grava, Esc cancela). Um gesto = um desfazer.
 */
@Composable
private fun AeScrubValue(value: Float, unit: String, step: Float, onBegin: () -> Unit, onEnd: () -> Unit, onChange: (Float) -> Unit) {
    var editing by remember { mutableStateOf(false) }
    var text by remember { mutableStateOf("") }
    if (editing) {
        val focus = remember { FocusRequester() }
        LaunchedEffect(Unit) { focus.requestFocus() }
        BasicTextField(
            value = text, onValueChange = { text = it }, singleLine = true,
            textStyle = TextStyle(color = AureaColors.Text, fontSize = 12.sp),
            cursorBrush = SolidColor(AureaColors.Accent),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = {
                text.replace(',', '.').toFloatOrNull()?.let { onBegin(); onChange(it); onEnd() }
                editing = false
            }),
            modifier = Modifier.widthIn(min = 44.dp, max = 70.dp).background(AureaColors.Chip, RoundedCornerShape(3.dp)).padding(horizontal = 3.dp)
                .focusRequester(focus)
                .onFocusChanged { if (!it.isFocused && editing) editing = false }
                .onPreviewKeyEvent { e -> if (e.key == Key.Escape) { editing = false; true } else false },
        )
        return
    }
    var live by remember { mutableFloatStateOf(value) }
    var dragging by remember { mutableStateOf(false) }
    val valueNow = rememberUpdatedState(value)
    val shown = if (dragging) live else value
    Text(
        fmt(shown) + unit, color = AureaColors.Accent, fontSize = 12.sp, style = TextStyle(fontFeatureSettings = "tnum"),
        modifier = Modifier
            .pointerHoverIcon(PointerIcon(Cursor(Cursor.W_RESIZE_CURSOR)))
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragStart = { live = valueNow.value; dragging = true; onBegin() },
                    onDragEnd = { dragging = false; onEnd() },
                    onDragCancel = { dragging = false; onEnd() },
                ) { change, dx ->
                    change.consume()
                    val mult = if (com.aurea.pc.PcKeys.shift) 10f else if (com.aurea.pc.PcKeys.ctrl) 0.1f else 1f
                    live += dx * step * mult
                    onChange(live)
                }
            }
            .tocavel(shrink = 1f, role = null) { text = fmt(valueNow.value); editing = true },
    )
}

private fun fmt(v: Float): String = if (abs(v - v.roundToInt()) < 0.005f) v.roundToInt().toString() else String.format(java.util.Locale.ROOT, "%.1f", v)

// =============================================================================
// Menu do botão direito (o do AE, no menu ancorado do Aurea)
// =============================================================================

@Composable
private fun AeContextMenu(store: EditorStore, ui: EditorUi, menu: AeMenu, expanded: Map<Long, Boolean>, onToggleTransform: (Long) -> Unit, onDismiss: () -> Unit) {
    val entries = when {
        menu.kind == AeMenu.KEYS -> keyEntries(store)
        menu.kind == AeMenu.RULER -> rulerEntries(store, menu.frame)
        menu.kind == AeMenu.MASK -> maskRowEntries(store, ui, menu.frame)
        menu.layer != null -> layerEntries(store, ui, menu.layer, expanded[menu.layer] == true, onToggleTransform, onDismiss)
        else -> emptyEntries(store)
    }
    if (entries.isEmpty()) {
        LaunchedEffect(Unit) { onDismiss() }
        return
    }
    Box(Modifier.offset { IntOffset(menu.x.roundToInt(), menu.y.roundToInt()) }.size(1.dp)) {
        AnchoredMenu(expanded = true, onDismiss = onDismiss, entries = entries, width = 260.dp,
            tone = AureaSurfaceTone.Editor, tag = "pc.timeline.menu")
    }
}

@Composable
private fun layerEntries(store: EditorStore, ui: EditorUi, id: Long, transformOpen: Boolean, onToggleTransform: (Long) -> Unit, onDismiss: () -> Unit): List<MenuEntry> {
    val row = store.layers.firstOrNull { it.id == id } ?: return emptyList()
    val ids = if (id in store.selection) store.selection.toList() else listOf(id)
    val type = LayerType.of(row.kind)
    val visual = type != LayerType.Audio
    val head = store.playhead
    val canSplit = store.layers.any { it.id in ids && head > it.startFrame && head < it.endFrame }
    val motionBlur = remember(store.layers, store.detail, id) { store.layerDetailOf(id)?.motionBlur == true }
    val out = mutableListOf<MenuEntry>()
    out += MenuEntry.Header(if (ids.size > 1) "${ids.size} × ${row.name}" else row.name)
    out += MenuEntry.Item(stringResource(R.string.editor_dividir_cabecote), glyph = CupertinoGlyph.Scissors, enabled = canSplit) { store.splitAtPlayhead(ids) }
    out += MenuEntry.Item(stringResource(R.string.editor_duplicar), glyph = CupertinoGlyph.PlusSquareOnSquare) { store.duplicateLayers(ids) }
    out += MenuEntry.Item(stringResource(R.string.pc_sc_precompose), glyph = CupertinoGlyph.RectangleStack) { store.precompose(ids) }
    out += MenuEntry.Item(stringResource(R.string.editor_copiar_camada), glyph = CupertinoGlyph.DocOnDoc) { store.copyLayers(ids) }
    out += MenuEntry.Item(stringResource(R.string.editor_colar_camada_cabecote), glyph = CupertinoGlyph.DocOnClipboard,
        enabled = store.clipboard and 1 != 0) { store.pasteLayers() }
    out += MenuEntry.Item(stringResource(if (ids.size > 1) R.string.editor_excluir_selecao else R.string.editor_excluir_camada),
        glyph = CupertinoGlyph.Trash, destructive = true) { store.deleteLayers(ids) }
    out += MenuEntry.Divider
    out += MenuEntry.Item(stringResource(if (row.visible) R.string.editor_ocultar_camada else R.string.editor_mostrar_camada),
        glyph = if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye, keepOpen = true) { store.setLayerVisible(id, !row.visible) }
    out += MenuEntry.Item(stringResource(if (row.locked) R.string.editor_desbloquear_camada else R.string.editor_bloquear_camada),
        glyph = CupertinoGlyph.LockFill, keepOpen = true) { store.setLayerLocked(id, !row.locked) }
    out += MenuEntry.Item(stringResource(R.string.editor_solo), glyph = CupertinoGlyph.Speaker2, checked = row.solo, keepOpen = true) {
        store.setLayerSolo(id, !row.solo)
    }
    out += MenuEntry.Item(stringResource(R.string.ae_layer_shy), glyph = CupertinoGlyph.PersonCropCircle, checked = row.shy, keepOpen = true) {
        store.setLayerShy(id, !row.shy)
    }
    if (visual) {
        out += MenuEntry.Item(stringResource(R.string.editor_desfoque_movimento), glyph = CupertinoGlyph.MotionBlur, checked = motionBlur, keepOpen = true) {
            if (!motionBlur && !store.compMotionBlur) store.setCompositionMotionBlur(true)
            store.setLayerMotionBlur(id, !motionBlur)
        }
        val intrinsic = row.kind in 8..10
        out += MenuEntry.Item(stringResource(R.string.sh_add_tab_3d), glyph = CupertinoGlyph.Cube, checked = row.isThreeD || intrinsic,
            enabled = !row.locked && !intrinsic, keepOpen = true) {
            if (store.primary != id || store.selection.size != 1) store.select(id)
            store.changeLayer3D(!row.isThreeD)
        }
        out += MenuEntry.Item(stringResource(R.string.editor_camada_ajuste), glyph = CupertinoGlyph.SliderHorizontal3, checked = row.adjustment, keepOpen = true) {
            store.setLayerAdjustment(id, !row.adjustment)
        }
        out += layerAeEntries(store, row)
    }
    // As ferramentas da camada (a grade do Aurea), abertas na janela flutuante.
    out += MenuEntry.Header(stringResource(R.string.pc_layer_tools))
    out += layerToolEntries(store, ui, row, ids.size > 1)
    out += MenuEntry.Divider
    out += MenuEntry.Item(stringResource(R.string.pc_marker), glyph = CupertinoGlyph.Flag) { store.toggleMarker() }
    out += MenuEntry.Item(stringResource(R.string.aet_layer_marker_edit), glyph = CupertinoGlyph.Bookmark) { store.openLayerMarkerEditor(id, store.playhead) }
    if (visual) {
        out += MenuEntry.Item(stringResource(if (store.motionSketch) R.string.aek_motion_sketch_stop else R.string.aek_motion_sketch),
            glyph = CupertinoGlyph.Scribble, checked = store.motionSketch, enabled = !row.locked) {
            if (store.primary != id || store.selection.size != 1) store.select(id)
            store.changeMotionSketch(!store.motionSketch)
        }
    }
    out += MenuEntry.Item(stringResource(R.string.aek_bake_expression), glyph = CupertinoGlyph.SuitDiamond, enabled = !row.locked) {
        if (store.primary != id || store.selection.size != 1) store.select(id)
        store.bakeExpressionKeys()
    }
    // Lote X do AE (PcAeBatchX.kt): Assistente de keyframes (áudio em keyframes) e Criar (formas/máscaras do texto).
    out += layerBatchXEntries(store, row)
    // Lote M do AE (PcAeMasks.kt): Máscara ▸ Traçado automático e Criar nulos a partir de caminhos.
    out += layerMaskRigEntries(store, row)
    out += MenuEntry.Item(stringResource(R.string.pc_tl_effects), glyph = CupertinoGlyph.Sparkles) {
        if (store.primary != id || store.selection.size != 1) store.select(id)
        PcLibraryState.effects = true
    }
    out += MenuEntry.Item(stringResource(if (transformOpen) R.string.pc_tl_hide_transform else R.string.pc_tl_show_transform), glyph = CupertinoGlyph.Rhombus) {
        onToggleTransform(id)
    }
    // Etiqueta: "sem" e as doze cores.
    val labelName = stringResource(R.string.editor_etiqueta)
    out += MenuEntry.Header(labelName)
    out += MenuEntry.IconRow(
        cells = listOf(MenuIconCell(stringResource(R.string.editor_nenhum), glyph = ShellGlyph.Nosign) { ids.forEach { store.setLayerLabel(it, 0) } }) +
            ShellColors.LabelPalette.mapIndexed { i, color -> MenuIconCell("$labelName ${i + 1}", swatch = color) { ids.forEach { store.setLayerLabel(it, i + 1) } } },
        selectedIndex = row.label,
        keepOpen = true,
    )
    return out
}

@Composable
private fun emptyEntries(store: EditorStore): List<MenuEntry> {
    val shapes = listOf(
        10 to R.string.sh_shape_square, 0 to R.string.sh_shape_circle, 1 to R.string.sh_shape_rounded,
        4 to R.string.sh_shape_triangle, 11 to R.string.editor_estrela,
    ).map { (preset, label) -> MenuEntry.Item(stringResource(label), glyph = CupertinoGlyph.Circle) { store.addShape(preset) } }
    val novo = listOf(
        MenuEntry.Item(stringResource(R.string.target_text), glyph = CupertinoGlyph.Textformat) { store.addText() },
        MenuEntry.Item(stringResource(R.string.target_shape), glyph = CupertinoGlyph.Circle, submenu = shapes),
        MenuEntry.Item(stringResource(R.string.editor_camada_ajuste), glyph = CupertinoGlyph.SliderHorizontal3) { store.addAdjustmentLayer() },
        MenuEntry.Item(stringResource(R.string.panel_camera_3d), glyph = CupertinoGlyph.CameraFill) { store.addCamera() },
        MenuEntry.Item(stringResource(R.string.scene_light_point), glyph = CupertinoGlyph.Lightbulb) { store.addLight(1) },
        MenuEntry.Item(stringResource(R.string.sh_add_null), glyph = CupertinoGlyph.SmallcircleCircle) { store.addNull(false) },
    )
    return listOf(
        MenuEntry.Item(stringResource(R.string.pc_tl_new), glyph = CupertinoGlyph.Plus, submenu = novo),
        MenuEntry.Item(stringResource(R.string.pc_import_media), glyph = CupertinoGlyph.PhotoFill) { store.assistantPanel = "media" },
        MenuEntry.Item(stringResource(R.string.editor_colar_camada_cabecote), glyph = CupertinoGlyph.DocOnClipboard,
            enabled = store.clipboard and 1 != 0) { store.pasteLayers() },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pc_sc_select_all), glyph = CupertinoGlyph.RectangleStack, enabled = store.layers.isNotEmpty()) { store.selectAll() },
        MenuEntry.Item(stringResource(R.string.pc_marker), glyph = CupertinoGlyph.Flag) { store.toggleMarker() },
        MenuEntry.Divider,
        // Guias da prévia (só na tela): grade, margens seguras e réguas.
        MenuEntry.Item(stringResource(R.string.om_vm_grid), glyph = CupertinoGlyph.Grid, checked = ViewModes.grid, keepOpen = true) { ViewModes.grid = !ViewModes.grid },
        MenuEntry.Item(stringResource(R.string.ae_guides_safe), glyph = CupertinoGlyph.Square, checked = ViewModes.safe, keepOpen = true) { ViewModes.safe = !ViewModes.safe },
        MenuEntry.Item(stringResource(R.string.ae_guides_rulers), glyph = CupertinoGlyph.LineHorizontal3, checked = ViewModes.rulers, keepOpen = true) { ViewModes.rulers = !ViewModes.rulers },
    )
}

/**
 * As ferramentas da grade da camada do Aurea ([layerPanelSpec]: as mesmas por
 * tipo, os mesmos textos e destinos), para o menu do botão direito. Abrem na
 * janela flutuante; Efeitos fica nos Controles de efeitos (já tem a entrada dele).
 */
@Composable
private fun layerToolEntries(store: EditorStore, ui: EditorUi, row: LayerRow, multi: Boolean): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    val out = mutableListOf<MenuEntry>()
    fun select() { if (store.primary != id || store.selection.size != 1) store.select(id) }
    fun open(panel: com.aurea.aurea.editor.panels.EditorPanel) { select(); pcOpenPanel(store, ui, panel) }
    if (multi) {
        // Várias camadas: as ferramentas do lote (alinhar, distribuir, aparar no cabeçote).
        out += MenuEntry.Item(stringResource(R.string.pc_layer_tools), glyph = CupertinoGlyph.SquareGrid2x2) { PcPanelWindow.showTools(ui) }
        return out
    }
    val isPrimary = store.primary == id
    val pl = PanelLayer(
        id = id, kind = row.kind, locked = row.locked, start = row.startFrame, end = row.endFrame, adjustment = row.adjustment,
        vector = isPrimary && store.isVectorLayer, drawing = isPrimary && store.isDrawingLayer,
        text3D = isPrimary && store.text3d != null, effectCount = row.effectCount,
    )
    val spec = layerPanelSpec(pl)
    // Fileira 1 da grade: Velocidade e o som.
    out += MenuEntry.Item(stringResource(R.string.editor_velocidade), glyph = CupertinoGlyph.Speedometer, enabled = !row.locked) {
        if (spec.speed == SpeedRoute.Group) { select(); PcPanelWindow.showTools(ui) } else open(com.aurea.aurea.editor.panels.EditorPanel.Speed)
    }
    if (type == LayerType.Audio || type == LayerType.Video) {
        out += MenuEntry.Item(stringResource(R.string.panel_som), glyph = CupertinoGlyph.Speaker2) { open(com.aurea.aurea.editor.panels.EditorPanel.Audio) }
    }
    // Fileiras 2 e 3: as mesmas vagas da grade (sem Efeitos, que tem a entrada própria).
    (spec.row2 + spec.row3).filterNotNull().distinct().filter { it != LayerSlot.Effects }.forEach { slot ->
        val edit = spec.edit
        val label = when (slot) {
            LayerSlot.ColorFill -> R.string.sh_dock_color_fill
            LayerSlot.BorderShadow -> R.string.om_lp_border_shadow
            LayerSlot.BlendOpacity -> R.string.panel_mistura_opacidade
            LayerSlot.MoveTransform -> R.string.om_lp_move_transform
            LayerSlot.Edit -> edit?.label ?: R.string.sh_dock_edit_shape
            LayerSlot.Presets -> R.string.sh_dock_presets
            LayerSlot.Effects -> R.string.sh_dock_effects
            LayerSlot.Mask -> R.string.sh_dock_mask
        }
        val glyph = when (slot) {
            LayerSlot.ColorFill -> CupertinoGlyph.Paintbrush
            LayerSlot.BorderShadow -> CupertinoGlyph.SquareOnSquare
            LayerSlot.BlendOpacity -> CupertinoGlyph.CircleLefthalfFill
            LayerSlot.MoveTransform -> CupertinoGlyph.Move
            LayerSlot.Edit -> edit?.glyph ?: CupertinoGlyph.Pencil
            LayerSlot.Presets -> CupertinoGlyph.WandStars
            LayerSlot.Effects -> CupertinoGlyph.Sparkles
            LayerSlot.Mask -> ShellGlyph.SquareSplit2x2
        }
        if (slot == LayerSlot.Edit && edit == null) return@forEach
        out += MenuEntry.Item(stringResource(label), glyph = glyph) {
            when (slot) {
                LayerSlot.ColorFill -> open(when {
                    type == LayerType.Model3D -> com.aurea.aurea.editor.panels.EditorPanel.Element3D
                    pl.vector -> com.aurea.aurea.editor.panels.EditorPanel.Vector
                    else -> com.aurea.aurea.editor.panels.EditorPanel.Shape
                })
                LayerSlot.BorderShadow -> open(com.aurea.aurea.editor.panels.EditorPanel.BorderShadow)
                LayerSlot.BlendOpacity -> open(com.aurea.aurea.editor.panels.EditorPanel.Appearance)
                LayerSlot.MoveTransform -> open(com.aurea.aurea.editor.panels.EditorPanel.Transform)
                LayerSlot.Edit -> when (edit) {
                    LayerEdit.Text -> { select(); if (store.text3d != null) store.openTextContentEditor() else pcOpenPanel(store, ui, com.aurea.aurea.editor.panels.EditorPanel.Text) }
                    LayerEdit.Group -> store.openPrecomp(id)
                    LayerEdit.Shape -> open(com.aurea.aurea.editor.panels.EditorPanel.ShapeEdit)
                    LayerEdit.Points -> open(com.aurea.aurea.editor.panels.EditorPanel.EditPoints)
                    LayerEdit.Drawing -> open(com.aurea.aurea.editor.panels.EditorPanel.Drawing)
                    LayerEdit.MediaShape -> open(com.aurea.aurea.editor.panels.EditorPanel.Mask)
                    LayerEdit.Camera -> open(com.aurea.aurea.editor.panels.EditorPanel.Camera)
                    LayerEdit.Light, LayerEdit.Material -> open(com.aurea.aurea.editor.panels.EditorPanel.Element3D)
                    LayerEdit.Particles -> open(com.aurea.aurea.editor.panels.EditorPanel.Particles)
                    null -> Unit
                }
                LayerSlot.Presets -> {
                    // Como o Presets da grade: filtrado pelo tipo; sem nenhum, o cartão "ainda não há presets".
                    select()
                    val lib = store.presets
                    val isText = type == LayerType.Text || pl.text3D
                    val kinds = if (isText) listOf(com.aurea.aurea.presets.PresetKind.Text)
                    else com.aurea.aurea.presets.PresetKind.entries.filter { it != com.aurea.aurea.presets.PresetKind.Text }
                    if (kinds.any { lib.entries(it).isNotEmpty() }) pcOpenPanel(store, ui, com.aurea.aurea.editor.panels.EditorPanel.Presets) else LayerPresetsEmpty.showing = true
                }
                LayerSlot.Effects -> open(com.aurea.aurea.editor.panels.EditorPanel.Effects)
                LayerSlot.Mask -> open(com.aurea.aurea.editor.panels.EditorPanel.Mask)
            }
        }
    }
    // Animações (entrada, saída, wiggle; no texto, também os animadores de texto).
    if (type != LayerType.Audio && type != LayerType.Camera) {
        out += MenuEntry.Item(stringResource(R.string.la_animators), glyph = CupertinoGlyph.Bolt) { openTextAnimators(store, ui, id) }
    }
    // 3D: material e ambiente de uma camada 3D (o objeto 3D já chega pela grade).
    if (row.isThreeD && type != LayerType.Model3D) {
        out += MenuEntry.Item(stringResource(R.string.panel_material_ambiente), glyph = CupertinoGlyph.Cube) { open(com.aurea.aurea.editor.panels.EditorPanel.Element3D) }
    }
    out += MenuEntry.Item(stringResource(R.string.pc_layer_tools) + "…", glyph = CupertinoGlyph.SquareGrid2x2) { select(); PcPanelWindow.showTools(ui) }
    return out
}

// =============================================================================
// Qualidade, clipe do modelo, transições, assistentes de keyframe e a régua
// =============================================================================

/**
 * O interruptor Qualidade (a coluna do AE) no traço fino do Aurea: Melhor = um
 * traço liso, Rascunho = em degraus, Arame = a caixa vazada.
 */
@Composable
private fun AeQualityGlyph(quality: Int, tint: Color, size: androidx.compose.ui.unit.Dp = 12.dp) {
    Canvas(Modifier.size(size)) {
        val w = this.size.width
        val cap = androidx.compose.ui.graphics.StrokeCap.Round
        when (quality) {
            1 -> {
                val steps = Path().apply {
                    moveTo(w * 0.12f, w * 0.88f); lineTo(w * 0.12f, w * 0.62f); lineTo(w * 0.38f, w * 0.62f)
                    lineTo(w * 0.38f, w * 0.38f); lineTo(w * 0.62f, w * 0.38f); lineTo(w * 0.62f, w * 0.12f); lineTo(w * 0.88f, w * 0.12f)
                }
                drawPath(steps, tint, style = Stroke(w * 0.12f, cap = cap, join = StrokeJoin.Round))
            }
            2 -> {
                drawRect(tint, Offset(w * 0.14f, w * 0.14f), Size(w * 0.72f, w * 0.72f), style = Stroke(w * 0.1f, join = StrokeJoin.Round))
                drawLine(tint, Offset(w * 0.14f, w * 0.86f), Offset(w * 0.86f, w * 0.14f), w * 0.1f, cap = cap)
            }
            else -> drawLine(tint, Offset(w * 0.16f, w * 0.84f), Offset(w * 0.84f, w * 0.16f), w * 0.14f, cap = cap)
        }
    }
}

private val QUALITY_NAMES = intArrayOf(R.string.ae_quality_best, R.string.ae_quality_draft, R.string.ae_quality_wireframe)
private val TRANSITION_NAMES = intArrayOf(
    R.string.ae_transition_none, R.string.ae_transition_dissolve, R.string.ae_transition_slide_up,
    R.string.ae_transition_slide_left, R.string.ae_transition_zoom, R.string.ae_transition_spin,
)

/** Clique cicla Melhor → Rascunho → Arame (só a prévia; o vídeo exportado sai em Melhor). */
@Composable
private fun AeQualitySwitch(store: EditorStore, l: LayerRow, enabled: Boolean) {
    val q = l.quality.coerceIn(0, 2)
    val name = stringResource(R.string.ae_layer_quality) + ": " + stringResource(QUALITY_NAMES[q])
    Box(
        Modifier.width(AeCols.sw).fillMaxHeight()
            .then(if (enabled) Modifier.tocavel(shrink = 1f) { store.setLayerQuality(l.id, (q + 1) % 3, if (l.bicubic) 1 else 0) } else Modifier)
            .semantics { contentDescription = name },
        contentAlignment = Alignment.Center,
    ) {
        AeQualityGlyph(q, when {
            !enabled -> AureaTimeline.GutterIcon.copy(alpha = 0.14f)
            q == 0 -> AureaTimeline.GutterEye.copy(alpha = 0.92f)
            else -> AureaColors.Accent
        })
    }
}

/** Qualidade e amostragem, animação do modelo 3D e transições de entrada/saída da camada (menu do botão direito). */
@Composable
private fun layerAeEntries(store: EditorStore, row: LayerRow): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    val out = mutableListOf<MenuEntry>()
    val q = row.quality.coerceIn(0, 2)
    val sampling = if (row.bicubic) 1 else 0
    val quality = mutableListOf<MenuEntry>()
    QUALITY_NAMES.forEachIndexed { i, res ->
        quality += MenuEntry.Item(stringResource(res), checked = q == i) { store.setLayerQuality(id, i, sampling) }
    }
    quality += MenuEntry.Divider
    quality += MenuEntry.Item(stringResource(R.string.ae_sampling_bicubic), checked = row.bicubic) {
        store.setLayerQuality(id, q, if (row.bicubic) 0 else 1)
    }
    out += MenuEntry.Item(stringResource(R.string.ae_layer_quality), glyph = CupertinoGlyph.Tv, sublabel = stringResource(QUALITY_NAMES[q]), submenu = quality)
    if (type == LayerType.Model3D) {
        val revision = store.modelClipRevision
        val clips = remember(id, revision) { store.modelAnimationClips(id) }
        val current = remember(id, revision) { store.modelAnimationClip(id) }
        if (clips.isNotEmpty()) {
            val items = mutableListOf<MenuEntry>()
            items += MenuEntry.Item(stringResource(R.string.ae_model_clip_none), checked = current < 0) { store.setModelAnimationClip(id, -1) }
            clips.forEachIndexed { i, name ->
                items += MenuEntry.Item(name.ifBlank { stringResource(R.string.ae_model_clip_n, i + 1) }, checked = current == i) { store.setModelAnimationClip(id, i) }
            }
            out += MenuEntry.Item(stringResource(R.string.ae_model_clip), glyph = CupertinoGlyph.PlayRectangle, enabled = !row.locked, submenu = items)
        }
    }
    if (type != LayerType.Camera && type != LayerType.Light) {
        val detail = remember(store.layers, store.detail, id) { store.layerDetailOf(id) }
        if (detail != null) {
            val half = (0.5f * TimeAxis.safeFps(store.project.fps)).roundToInt().coerceAtLeast(1)
            val items = mutableListOf<MenuEntry>()
            for (side in 0..1) {
                val exit = side == 1
                val current = (if (exit) detail.transitionOut else detail.transitionIn).coerceIn(0, 5)
                val frames = if (exit) detail.transitionOutFrames else detail.transitionInFrames
                items += MenuEntry.Header(stringResource(if (exit) R.string.ae_transition_out else R.string.ae_transition_in))
                TRANSITION_NAMES.forEachIndexed { t, res ->
                    items += MenuEntry.Item(stringResource(res), checked = current == t) {
                        store.setLayerTransition(id, exit, t, if (frames > 0) frames else half)
                    }
                }
            }
            out += MenuEntry.Item(stringResource(R.string.ae_transitions), glyph = CupertinoGlyph.ArrowLeftRight, enabled = !row.locked, submenu = items)
        }
    }
    return out
}

/**
 * Os assistentes de keyframe do AE sobre os losangos escolhidos (clique; Ctrl/Shift
 * + clique soma): Easy Ease, inverter no tempo, escala exponencial, suavizador,
 * bézier automática/contínua, vagar no tempo, expressão em keyframes, escalar o
 * tempo, copiar, duplicar e excluir. A regra de cada um mora no motor.
 */
@Composable
private fun keyEntries(store: EditorStore): List<MenuEntry> {
    val sel = store.keySelection ?: return emptyList()
    val count = sel.size
    if (count == 0) return emptyList()
    // Bits que TODOS os escolhidos têm, lidos quando o menu abre.
    val flags = remember { store.timelineKeyFlags() }
    val auto = (flags and com.aurea.aurea.engine.AureaEngine.KEY_FLAG_AUTO) != 0
    val continuous = (flags and com.aurea.aurea.engine.AureaEngine.KEY_FLAG_CONTINUOUS) != 0
    val roving = (flags and com.aurea.aurea.engine.AureaEngine.KEY_FLAG_ROVING) != 0
    val smoother = mutableListOf<MenuEntry>()
    listOf(0.01f to "1%", 0.03f to "3%", 0.08f to "8%").forEach { (tolerance, label) ->
        smoother += MenuEntry.Item(stringResource(R.string.aek_smoother_level, label)) { store.smoothTimelineKeys(tolerance) }
    }
    val scale = mutableListOf<MenuEntry>()
    listOf(0.5 to "50%", 0.75 to "75%", 1.5 to "150%", 2.0 to "200%").forEach { (factor, label) ->
        scale += MenuEntry.Item(stringResource(R.string.aet_keys_scale_to, label)) { store.scaleTimelineKeys(factor) }
    }
    return listOf(
        MenuEntry.Header(stringResource(R.string.aek_tools) + " · $count"),
        MenuEntry.Item(stringResource(R.string.aet_easy_ease) + "  (F9)", glyph = CupertinoGlyph.SuitDiamondFill) { store.easeTimelineKeys(0) },
        MenuEntry.Item(stringResource(R.string.aet_easy_ease_in), glyph = CupertinoGlyph.SuitDiamond) { store.easeTimelineKeys(1) },
        MenuEntry.Item(stringResource(R.string.aet_easy_ease_out), glyph = CupertinoGlyph.SuitDiamond) { store.easeTimelineKeys(2) },
        MenuEntry.Item(stringResource(R.string.pc_curves) + "…", glyph = CupertinoGlyph.Waveform, enabled = store.selectedKeyframe != null) { PcCurvePopup.open = true },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.aek_time_reverse), glyph = CupertinoGlyph.ArrowLeftRight, enabled = count > 1) { store.timeReverseTimelineKeys() },
        MenuEntry.Item(stringResource(R.string.aek_exponential_scale), glyph = CupertinoGlyph.ArrowUpArrowDown, enabled = count > 1) { store.exponentialScaleTimelineKeys() },
        MenuEntry.Item(stringResource(R.string.aek_smoother), glyph = CupertinoGlyph.Scribble, enabled = count > 2, submenu = smoother),
        MenuEntry.Item(stringResource(R.string.aet_keys_scale), glyph = CupertinoGlyph.Clock, enabled = count > 1, submenu = scale),
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.aek_auto_bezier), checked = auto) {
            store.setTimelineKeyMode(if (auto) com.aurea.aurea.engine.AureaEngine.KEY_MODE_BEZIER else com.aurea.aurea.engine.AureaEngine.KEY_MODE_AUTO)
        },
        MenuEntry.Item(stringResource(R.string.aek_continuous_bezier), checked = continuous) {
            store.setTimelineKeyMode(if (continuous) com.aurea.aurea.engine.AureaEngine.KEY_MODE_BEZIER else com.aurea.aurea.engine.AureaEngine.KEY_MODE_CONTINUOUS)
        },
        MenuEntry.Item(stringResource(R.string.aek_rove), checked = roving) {
            store.setTimelineKeyMode(if (roving) com.aurea.aurea.engine.AureaEngine.KEY_MODE_ROVE_OFF else com.aurea.aurea.engine.AureaEngine.KEY_MODE_ROVE_ON)
        },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.aek_bake_expression), glyph = CupertinoGlyph.SuitDiamond) { store.bakeExpressionKeys() },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.common_copy) + "  (Ctrl+C)", glyph = CupertinoGlyph.DocOnDoc, enabled = !sel.crossLayer) { store.copyTimelineKeys() },
        MenuEntry.Item(stringResource(R.string.common_duplicate) + "  (Ctrl+D)", glyph = CupertinoGlyph.PlusSquareOnSquare, enabled = !sel.crossLayer) { store.duplicateTimelineKeys() },
        MenuEntry.Item(stringResource(R.string.common_delete) + "  (Delete)", glyph = CupertinoGlyph.Trash, destructive = true) { store.deleteTimelineKeys() },
    )
}

/** A régua: a área de trabalho no quadro clicado e Levantar/Extrair (o Lift/Extract do AE). */
@Composable
private fun rulerEntries(store: EditorStore, frame: Int): List<MenuEntry> {
    val duration = max(1, store.project.durationFrames)
    val set = store.workArea
    val area = set ?: (0 to duration)
    val out = mutableListOf<MenuEntry>()
    out += MenuEntry.Item(stringResource(R.string.aet_work_area_start_here)) { store.setWorkArea(frame, max(area.second, frame + 1)) }
    out += MenuEntry.Item(stringResource(R.string.aet_work_area_end_here), enabled = frame > 0) { store.setWorkArea(min(area.first, frame - 1).coerceAtLeast(0), frame) }
    if (set != null) {
        out += MenuEntry.Divider
        out += MenuEntry.Item(stringResource(R.string.aet_trim_to_work_area), glyph = CupertinoGlyph.Crop) { store.trimToWorkArea() }
        out += MenuEntry.Item(stringResource(R.string.aek_lift_work_area), glyph = CupertinoGlyph.ArrowUpToLine) { store.liftWorkArea(extract = false) }
        out += MenuEntry.Item(stringResource(R.string.aek_extract_work_area), glyph = CupertinoGlyph.Scissors) { store.liftWorkArea(extract = true) }
        out += MenuEntry.Item(stringResource(R.string.aet_work_area_clear), glyph = CupertinoGlyph.XmarkCircle) { store.clearWorkArea() }
    }
    return out
}
