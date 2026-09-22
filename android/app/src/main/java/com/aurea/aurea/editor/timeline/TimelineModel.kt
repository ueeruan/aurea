package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.ui.theme.LayerType

/**
 * Uma linha da timeline, derivada do que o store LEU do motor (não é cópia
 * editável: some e renasce a cada `modelRevision`). Existe para o pintor não
 * alocar por quadro — o nome (`LayerRow.name` cria uma String a cada leitura)
 * e os instantes de keyframe saem prontos daqui.
 */
internal class RowModel(
    val id: Long,
    val type: LayerType,
    val start: Int,
    val end: Int,
    val offset: Int,
    val visible: Boolean,
    val locked: Boolean,
    val animated: Boolean,
    val name: String,
    /** Etiqueta de cor (0 = nenhuma; i = `ShellColors.LabelPalette[i - 1]`). */
    val label: Int,
    /** Instantes com keyframe (qualquer trilha), em frames da TIMELINE, ordenados e sem repetição. */
    val instants: IntArray,
    /** Keyframes de cada instante (todas as trilhas que têm marca ali), paralelo a [instants]. */
    val keysAt: Array<List<KeyframeRow>>,
) {
    /** Vídeo e imagem têm miniatura no motor; o resto é só a cor. */
    val hasThumbs: Boolean get() = type == LayerType.Video || type == LayerType.Image

    fun toLocal(timelineFrame: Int) = Keyframes.toLocal(timelineFrame, start, offset)
}

internal fun buildRows(layers: List<LayerRow>, keyframes: Map<Long, List<KeyframeRow>>): List<RowModel> =
    List(layers.size) { i ->
        val l = layers[i]
        val keys = keyframes[l.id].orEmpty().sortedBy { it.time }
        val times = ArrayList<Int>()
        val groups = ArrayList<List<KeyframeRow>>()
        var from = 0
        while (from < keys.size) {
            var to = from + 1
            while (to < keys.size && keys[to].time == keys[from].time) to++
            times.add(Keyframes.toTimeline(keys[from].time, l.startFrame, l.offsetFrames))
            groups.add(keys.subList(from, to))
            from = to
        }
        RowModel(
            id = l.id,
            type = LayerType.of(l.kind),
            start = l.startFrame,
            end = l.endFrame,
            offset = l.offsetFrames,
            visible = l.visible,
            locked = l.locked,
            animated = l.animated || keys.isNotEmpty(),
            name = l.name,
            label = l.label,
            instants = times.toIntArray(),
            keysAt = groups.toTypedArray(),
        )
    }
