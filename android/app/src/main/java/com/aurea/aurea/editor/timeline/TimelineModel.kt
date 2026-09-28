package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.theme.LayerType
import kotlin.math.max
import kotlin.math.min

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
    /**
     * LINHA MAGNÉTICA: os cortes desta camada andam como faixa de montagem de
     * vídeo. A timeline usa isto para decidir se o arrasto reordena a fita.
     */
    val magnetic: Boolean,
    val animated: Boolean,
    val name: String,
    /** Etiqueta de cor (0 = nenhuma; i = `ShellColors.LabelPalette[i - 1]`). */
    val label: Int,
    /** Instantes com keyframe (qualquer trilha), em frames da TIMELINE, ordenados e sem repetição. */
    val instants: IntArray,
    /** Keyframes de cada instante (todas as trilhas que têm marca ali), paralelo a [instants]. */
    val keysAt: Array<List<KeyframeRow>>,
    val track: TimelineTrack? = null,
    /** A LINHA da timeline (`LayerRow.trackId`); 0 = sem linha (projeto antigo: a camada é a linha dela). */
    val line: Int = 0,
    /**
     * FILEIRA COMPARTILHADA: os trechos da MESMA linha que dividem esta fileira,
     * lado a lado, em ordem de tempo. Null = fileira de um trecho só (ela mesma).
     * Os campos desta fileira são só o resumo dela (pílula, altura); quem se
     * desenha, se toca e se edita é cada trecho de [segments].
     */
    val shared: Array<RowModel>? = null,
) {
    /** Os trechos desenhados e tocados nesta fileira: os da linha, ou só ela. */
    val segments: Array<RowModel> = shared ?: arrayOf(this)

    /** O trecho [id] desta fileira (null = não mora aqui). */
    fun segment(id: Long): RowModel? {
        for (s in segments) if (s.id == id) return s
        return null
    }

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
                row.line == l.trackId && l.nameEquals(nameUtf8)
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
        magnetic = l.magnetic,
        animated = l.animated || keys.isNotEmpty(),
        name = name,
        label = l.label,
        instants = times.toIntArray(),
        keysAt = groups.toTypedArray(),
        line = l.trackId,
    )
}

/**
 * Fileiras por LINHA (`trackId`): trechos da mesma linha dividem UMA fileira,
 * lado a lado no tempo — os dois pedaços de um split ficam onde estavam, não
 * em duas fileiras. A fileira fica na posição do trecho MAIS ALTO da linha na
 * ordem de desenho (estável: é a ordem que o motor manda). Camada sem par
 * (linha só dela, ou linha 0 de projeto antigo) volta como estava: o MESMO
 * objeto, e a lista inteira volta a mesma quando ninguém divide linha.
 *
 * Trechos da mesma linha que se SOBREPÕEM no tempo (duplicar no cabeçote,
 * mover solto por cima do vizinho) não se escondem um atrás do outro: o que
 * não cabe desce para uma fileira logo abaixo, da mesma linha.
 */
internal fun sharedRows(rows: List<RowModel>): List<RowModel> {
    val byLine = HashMap<Int, ArrayList<RowModel>>()
    var anyShared = false
    for (r in rows) {
        if (r.line == 0 || r.track != null) continue
        val group = byLine.getOrPut(r.line) { ArrayList(2) }
        group.add(r)
        if (group.size >= 2) anyShared = true
    }
    if (!anyShared) return rows
    val out = ArrayList<RowModel>(rows.size)
    for (r in rows) {
        val group = if (r.line == 0 || r.track != null) null else byLine[r.line]
        if (group == null || group.size == 1) {
            out.add(r)
            continue
        }
        // Vazio = a linha já saiu inteira com o trecho mais alto dela.
        if (group.isEmpty()) continue
        val ordered = group.sortedWith(SEGMENT_ORDER)
        group.clear()
        val packed = ArrayList<ArrayList<RowModel>>(1)
        val ends = ArrayList<Int>(1)
        for (s in ordered) {
            var k = 0
            while (k < packed.size && ends[k] > s.start) k++
            if (k == packed.size) {
                packed.add(ArrayList(ordered.size))
                ends.add(s.end)
            } else {
                ends[k] = max(ends[k], s.end)
            }
            packed[k].add(s)
        }
        for (segs in packed) out.add(if (segs.size == 1) segs[0] else sharedRow(r.line, segs.toTypedArray()))
    }
    return out
}

private val SEGMENT_ORDER = compareBy<RowModel>({ it.start }, { it.end }, { it.id })

/** O resumo de uma fileira compartilhada: a pílula acende se algum trecho aparece e trava se todos travam. */
private fun sharedRow(line: Int, segs: Array<RowModel>): RowModel {
    val head = segs[0]
    var start = head.start
    var end = head.end
    var visible = false
    var locked = true
    var magnetic = false
    var animated = false
    for (s in segs) {
        start = min(start, s.start)
        end = max(end, s.end)
        visible = visible || s.visible
        locked = locked && s.locked
        magnetic = magnetic || s.magnetic
        animated = animated || s.animated
    }
    return RowModel(
        id = head.id, type = head.type, start = start, end = end, offset = 0,
        visible = visible, locked = locked, magnetic = magnetic, animated = animated,
        name = head.name, label = head.label, instants = IntArray(0), keysAt = emptyArray(),
        line = line, shared = segs,
    )
}

/**
 * Chave de GRUPO de cada fileira para reordenar na vertical: fileiras seguidas
 * com a mesma chave andam juntas. Trilha de propriedade aberta anda com a
 * camada dona; as fileiras de uma linha (inclusive a de baixo, de trechos que
 * se sobrepõem) andam juntas; o resto é a própria camada.
 */
internal fun timelineGroupKeys(rows: List<RowModel>): LongArray {
    val keys = LongArray(rows.size)
    for (i in rows.indices) {
        val r = rows[i]
        keys[i] = when {
            r.track != null && i > 0 -> keys[i - 1]
            r.line != 0 -> LINE_KEY_BASE + r.line
            else -> r.id
        }
    }
    return keys
}

/** Chaves de linha longe dos ids de camada (handle empacotado, sempre positivo). */
private const val LINE_KEY_BASE = Long.MIN_VALUE

/**
 * Reordenar na vertical um GRUPO de camadas (os trechos de uma linha andam
 * juntos) com o comando de sempre, que leva UMA camada a uma posição da lista
 * (0 = topo, a da frente). O grupo vai inteiro para logo ACIMA da camada mais
 * alta do destino (subindo) ou logo ABAIXO dela (descendo) — a fileira do
 * destino fica onde a pessoa a viu. Devolve os passos (camada, posição final)
 * na ordem de aplicar; uma camada sozinha dá UM passo, o mesmo de antes.
 */
internal object RowOrder {
    fun moves(order: LongArray, block: Set<Long>, anchor: Long, up: Boolean): List<Pair<Long, Int>> {
        if (block.isEmpty() || anchor in block) return emptyList()
        val rest = ArrayList<Long>(order.size)
        val moving = ArrayList<Long>(block.size)
        for (id in order) if (id in block) moving.add(id) else rest.add(id)
        val at = rest.indexOf(anchor)
        if (at < 0 || moving.isEmpty()) return emptyList()
        val p = if (up) at else at + 1
        val target = ArrayList<Long>(order.size)
        target.addAll(rest.subList(0, p))
        target.addAll(moving)
        target.addAll(rest.subList(p, rest.size))
        val current = order.toMutableList()
        val out = ArrayList<Pair<Long, Int>>()
        fun move(id: Long, to: Int) {
            val from = current.indexOf(id)
            if (from == to) return
            current.removeAt(from)
            current.add(to, id)
            out.add(id to to)
        }
        // Subindo, de cima para baixo; descendo, de baixo para cima: cada passo
        // põe uma camada no lugar final sem tirar do lugar as que já foram.
        if (up) for (j in moving.indices) move(moving[j], p + j)
        else for (j in moving.indices.reversed()) move(moving[j], p + j)
        if (current != target) {
            // Rede de segurança: posição a posição (sempre chega na ordem pedida).
            current.clear()
            current.addAll(order.toList())
            out.clear()
            for (k in target.indices) if (current[k] != target[k]) move(target[k], k)
        }
        return out
    }
}

/** A real engine track, or a property section (property -1) without synthetic keys. */
internal data class TimelineTrack(val property: Int, val effect: Int = -1, val param: Int = 0)
internal fun expandedRows(base: List<RowModel>, expanded: Long?, keys: Map<Long, List<KeyframeRow>>, effects: List<Pair<Int, String>>): List<RowModel> {
    if (expanded == null) return base
    return base.flatMap { row ->
        // Numa fileira compartilhada, as trilhas abertas são do TRECHO aberto
        // (tempo e keyframes dele) e entram logo abaixo da fileira.
        val owner = row.segment(expanded)
        if (owner == null) listOf(row) else {
            val all = keys[owner.id].orEmpty()
            val tracks = all.groupBy { TimelineTrack(it.property, it.effectIndex, it.paramIndex) }
            val lanes = arrayListOf(row)
            fun lane(track: TimelineTrack, name: String, values: List<KeyframeRow> = emptyList()) {
                val groups = values.groupBy { it.time }.toSortedMap()
                lanes += RowModel(owner.id, owner.type, owner.start, owner.end, owner.offset, owner.visible, owner.locked,
                    owner.magnetic, values.isNotEmpty(), "  $name", owner.label,
                    groups.keys.map { Keyframes.toTimeline(it, owner.start, owner.offset) }.toIntArray(), groups.values.toTypedArray(), track)
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
/**
 * Altura de uma linha. Propriedade (uma trilha aberta) é baixa; camada é a
 * altura normal — EXCETO um trecho em LINHA MAGNÉTICA, que é a faixa de
 * montagem do vídeo: ele cresce para a forma de onda do som caber legível, que
 * é o que se olha o tempo todo ao cortar. Os vizinhos e a régua acompanham,
 * porque as posições saem de `rowOffsets`, não de um múltiplo fixo.
 */
private const val MAGNETIC_ROW_SCALE = 1.9f

internal fun timelineRowHeight(row: RowModel, layerHeight: Float, density: Float): Float {
    // Fileira compartilhada: a altura do trecho mais alto dela.
    val shared = row.shared
    if (shared != null) {
        var h = 0f
        for (s in shared) h = max(h, timelineRowHeight(s, layerHeight, density))
        return h
    }
    return if (row.track != null) 28f * density
    else if (row.magnetic && row.type == LayerType.Video) layerHeight * MAGNETIC_ROW_SCALE
    else layerHeight
}
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
