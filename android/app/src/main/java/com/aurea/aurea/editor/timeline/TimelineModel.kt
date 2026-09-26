package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackKey
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
    val track: TimelineTrack? = null,
) {
    /** Vídeo e imagem têm miniatura no motor; o resto é só a cor. */
    val hasThumbs: Boolean get() = track == null && (type == LayerType.Video || type == LayerType.Image)

    fun toLocal(timelineFrame: Int) = Keyframes.toLocal(timelineFrame, start, offset)

    fun selectedFrame(key: KeyframeRow): Int {
        val frame = Keyframes.toTimeline(key.time, start, offset)
        val index = instants.binarySearch(frame)
        return if (index >= 0 && keysAt[index].any { it.property == key.property &&
            it.effectIndex == key.effectIndex && it.paramIndex == key.paramIndex }) frame else Snap.NONE
    }

    fun keysForDrag(index: Int, focused: Boolean): List<KeyframeRow> =
        if (focused || track != null) keysAt[index] else keysAt[index].take(1)

    fun dragInstants(keys: List<KeyframeRow>): IntArray = instants.filterIndexed { i, _ ->
        keysAt[i].any { candidate -> keys.any { it.property == candidate.property &&
            it.effectIndex == candidate.effectIndex && it.paramIndex == candidate.paramIndex } }
    }.toIntArray()
}

internal fun focusedKeys(keys: List<KeyframeRow>, focus: List<TrackKey>): List<KeyframeRow> =
    keys.filter { key -> focus.any { it.property == key.property && it.effectIndex == key.effectIndex && it.paramIndex == key.paramIndex } }

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

internal fun buildRow(l: LayerRow, all: List<KeyframeRow>, name: String = l.name): RowModel {
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

/** A real engine track, or a property section (property -1) without synthetic keys. */
internal data class TimelineTrack(val property: Int, val effect: Int = -1, val param: Int = 0)
internal fun expandedRows(base: List<RowModel>, expanded: Long?, keys: Map<Long, List<KeyframeRow>>, effects: List<Pair<Int, String>>): List<RowModel> {
    if (expanded == null) return base
    return base.flatMap { row ->
        if (row.id != expanded) listOf(row) else {
            val all = keys[row.id].orEmpty()
            val tracks = all.groupBy { TimelineTrack(it.property, it.effectIndex, it.paramIndex) }
            val lanes = arrayListOf(row)
            fun lane(track: TimelineTrack, name: String, values: List<KeyframeRow> = emptyList()) {
                val groups = values.groupBy { it.time }.toSortedMap()
                lanes += RowModel(row.id, row.type, row.start, row.end, row.offset, row.visible, row.locked,
                    values.isNotEmpty(), "  $name", row.label,
                    groups.keys.map { Keyframes.toTimeline(it, row.start, row.offset) }.toIntArray(), groups.values.toTypedArray(), track)
            }
            lane(TimelineTrack(-1), "Transform")
            effects.forEach { (id, name) -> lane(TimelineTrack(31, id, -1), name) }
            tracks.entries.sortedWith(compareBy({ it.key.property }, { it.key.effect }, { it.key.param })).forEach { (track, values) ->
                val names = listOf("Position X", "Position Y", "Position Z", "Scale X", "Scale Y", "Scale Z", "Rotation X", "Rotation Y", "Rotation Z", "Anchor X", "Anchor Y", "Anchor Z", "Opacity", "Skew X", "Skew Y")
                val name = names.getOrNull(track.property) ?: when (track.property) {
                    30 -> "Time remap"
                    31 -> (effects.firstOrNull { it.first == track.effect }?.second ?: "Effect") + " · ${track.param + 1}"
                    32 -> "Audio · ${track.param + 1}"
                    33 -> "Text animation ${track.effect + 1} · ${track.param + 1}"
                    34 -> "Vector · ${track.param + 1}"
                    35 -> "Shape · ${track.param + 1}"
                    36 -> "Particles · ${track.param + 1}"
                    37 -> "Material ${track.effect + 1} · ${listOf("R", "G", "B", "Alpha", "Metallic", "Roughness").getOrNull(track.param) ?: track.param}"
                    else -> "3D · ${track.property}"
                }
                lane(track, name, values)
            }
            lanes
        }
    }
}

/** Shared by paint, hit testing, scroll bounds and layer-reorder geometry. */
internal fun timelineRowHeight(row: RowModel, layerHeight: Float, density: Float): Float =
    if (row.track == null) layerHeight else 28f * density
internal fun timelineRowTop(rows: List<RowModel>, index: Int, layerHeight: Float, density: Float): Float {
    var top = 0f
    for (i in 0 until index.coerceIn(0, rows.size)) top += timelineRowHeight(rows[i], layerHeight, density)
    return top
}
internal fun timelineRowIndex(rows: List<RowModel>, y: Float, layerHeight: Float, density: Float): Int {
    if (y < 0f) return -1
    var bottom = 0f
    for (i in rows.indices) {
        bottom += timelineRowHeight(rows[i], layerHeight, density)
        if (y < bottom) return i
    }
    return rows.size
}
