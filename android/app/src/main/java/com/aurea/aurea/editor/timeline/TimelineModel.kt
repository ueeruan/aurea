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
    List(layers.size) { i -> buildRow(layers[i], keyframes[layers[i].id].orEmpty()) }

/**
 * Linhas feitas de novo SÓ onde a camada mudou (fase 8D). Cada revisão do
 * modelo relê as camadas do motor (objetos novos); aqui a linha velha volta
 * quando tempo, estado, nome e a lista de keyframes (a MESMA lista, por
 * identidade — ver `KeyframeSnapshot`) não mudaram. Arrastar um clipe entre
 * 1000 refaz 1 linha, não 1000 (e não reordena 10.000 keyframes).
 */
internal class RowCache {
    private class Cached(
        val flags: Int,
        val kind: Int,
        val nameUtf8: ByteArray,
        val keys: List<KeyframeRow>,
        val row: RowModel,
    ) {
        fun matches(l: LayerRow, keys: List<KeyframeRow>): Boolean =
            this.keys === keys && flags == l.flags && kind == l.kind && row.id == l.id &&
                row.start == l.startFrame && row.end == l.endFrame && row.offset == l.offsetFrames &&
                l.nameEquals(nameUtf8)
    }

    private var byId = HashMap<Long, Cached>()
    private var ordered = arrayOfNulls<Cached>(0)
    private var last: List<RowModel> = emptyList()

    fun build(layers: List<LayerRow>, keyframes: Map<Long, List<KeyframeRow>>): List<RowModel> {
        // Nada mudou (o caso comum: revisão que não mexeu na timeline): a MESMA
        // lista de antes, sem alocar — e quem a observa não é invalidado.
        if (layers.size == ordered.size) {
            var same = true
            for (i in layers.indices) {
                val l = layers[i]
                val c = ordered[i]
                if (c == null || !c.matches(l, keyframes[l.id].orEmpty())) {
                    same = false
                    break
                }
            }
            if (same) return last
        }
        val next = HashMap<Long, Cached>(layers.size * 2)
        val byPos = arrayOfNulls<Cached>(layers.size)
        val out = ArrayList<RowModel>(layers.size)
        for (i in layers.indices) {
            val l = layers[i]
            val keys = keyframes[l.id].orEmpty()
            val old = byId[l.id]
            val c = if (old != null && old.matches(l, keys)) {
                old
            } else {
                val name = l.name
                Cached(l.flags, l.kind, name.toByteArray(Charsets.UTF_8), keys, buildRow(l, keys, name))
            }
            next[l.id] = c
            byPos[i] = c
            out.add(c.row)
        }
        byId = next
        ordered = byPos
        last = out
        return out
    }
}

private fun buildRow(l: LayerRow, all: List<KeyframeRow>, name: String = l.name): RowModel {
    val keys = all.sortedBy { it.time }
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
    return RowModel(
        id = l.id,
        type = LayerType.of(l.kind),
        start = l.startFrame,
        end = l.endFrame,
        offset = l.offsetFrames,
        visible = l.visible,
        locked = l.locked,
        animated = l.animated || keys.isNotEmpty(),
        name = name,
        label = l.label,
        instants = times.toIntArray(),
        keysAt = groups.toTypedArray(),
    )
}
