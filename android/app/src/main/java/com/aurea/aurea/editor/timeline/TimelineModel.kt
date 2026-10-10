package com.aurea.aurea.editor.timeline

import android.content.Context
import androidx.annotation.StringRes
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
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

    // The overview diamond represents every key at this instant. Focused rows
    // already contain only their property; never leave invisible peers behind.
    fun keysForDrag(index: Int): List<KeyframeRow> = keysAt[index]

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
    // The shared flag includes an animation key exactly at the clip's end.
    // Never reapply the render interval's exclusive end to editable anchors.
    val keys = all.filter { it.timelineVisible }.sortedBy { it.time }
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

/**
 * Arrasto VERTICAL de UM trecho (como no Alight Motion: só ele anda, nunca a
 * linha inteira). Diz onde o trecho cai com o dedo em [y] (conteúdo: a partir
 * do topo da 1ª fileira, com a rolagem): ENTRE duas fileiras (fileira própria
 * ali — o traço de inserção) ou DENTRO de uma linha magnética/compartilhada
 * (faixa do meio da fileira, só se o trecho couber no tempo sem sobrepor).
 * O motor (`move_layer_to_row`) decide a pilha; aqui sai só o pedido dele:
 * âncora + modo (0 = acima da fileira da âncora, âncora 0 = no fundo; 1 = entrar
 * na linha da âncora). O iOS tem o mesmo cálculo em `TimelineRowDrop`.
 */
internal object RowDrop {
    const val NONE = 0
    const val INSERT = 1
    const val JOIN = 2

    /** [lineY] = y (conteúdo) do traço de inserção; [row] = fileira acesa ao entrar numa linha. */
    data class Target(val kind: Int, val anchor: Long, val mode: Int, val lineY: Float, val row: Int) {
        companion object { val NONE = Target(RowDrop.NONE, 0L, 0, Float.NaN, -1) }
    }

    /** Faixa do meio da fileira que conta como "dentro da linha" (o resto é entre fileiras). */
    private const val JOIN_EDGE = 0.25f

    fun target(rows: List<RowModel>, tops: FloatArray, y: Float, moving: RowModel, source: Int): Target {
        val n = rows.size
        if (n == 0 || tops.size != n + 1 || source !in 0 until n || !y.isFinite()) return Target.NONE
        val keys = timelineGroupKeys(rows)
        val t = when {
            y < tops[0] -> 0
            y >= tops[n] -> n - 1
            else -> {
                var i = 0
                while (i < n - 1 && y >= tops[i + 1]) i++
                i
            }
        }
        var gs = t
        while (gs > 0 && keys[gs - 1] == keys[t]) gs--
        var ge = t + 1
        while (ge < n && keys[ge] == keys[t]) ge++
        var ss = source
        while (ss > 0 && keys[ss - 1] == keys[source]) ss--
        var se = source + 1
        while (se < n && keys[se] == keys[source]) se++
        val line = moving.line
        // Quem mais mora na linha do trecho (em qualquer fileira dela).
        var sharesLine = false
        if (line != 0) for (r in rows) if (r.track == null && r.line == line) for (s in r.segments) if (s.id != moving.id) sharesLine = true

        // DENTRO de uma linha: faixa do meio de uma fileira de camada cuja linha
        // é magnética ou dividida, que não seja a do próprio trecho.
        val row = rows[t]
        val h = tops[t + 1] - tops[t]
        val frac = if (h > 0f) (y - tops[t]) / h else 0.5f
        val middle = frac >= JOIN_EDGE && frac <= 1f - JOIN_EDGE
        // No meio da própria fileira (ou de outra fileira da mesma linha): fica onde está.
        if (middle && (t == source || (line != 0 && row.track == null && row.line == line))) return Target.NONE
        if (row.track == null && row.line != 0 && row.line != line && middle) {
            var magnetic = false
            var count = 0
            var fits = true
            var anchor = 0L
            for (r in rows) {
                if (r.track != null || r.line != row.line) continue
                for (s in r.segments) {
                    if (s.id == moving.id) continue
                    count++
                    if (anchor == 0L) anchor = s.id
                    if (s.magnetic) magnetic = true
                    if (s.start < moving.end && moving.start < s.end) fits = false
                }
            }
            if ((magnetic || count >= 2) && fits && anchor != 0L) return Target(JOIN, anchor, 1, Float.NaN, t)
        }
        // ENTRE fileiras: metade de cima do grupo = acima dele; de baixo = abaixo.
        val mid = (tops[gs] + tops[ge]) / 2f
        val gap = if (y < mid) gs else ge
        // Soltar colado no próprio grupo, sozinho na linha: nada muda.
        if (!sharesLine && (gap == ss || gap == se)) return Target.NONE
        var anchor = 0L
        if (gap < n) {
            var e = gap + 1
            while (e < n && keys[e] == keys[gap]) e++
            loop@ for (k in gap until e) {
                if (rows[k].track != null) continue
                for (s in rows[k].segments) if (s.id != moving.id) { anchor = s.id; break@loop }
            }
            if (anchor == 0L) return Target.NONE
        }
        return Target(INSERT, anchor, 0, tops[gap], -1)
    }
}

/**
 * Uma trilha real do motor, uma seção (property -1) sem keyframes sintéticos,
 * ou — com [group] — a TRILHA DE GRUPO de uma propriedade de vários eixos
 * (Posição X/Y/Z, Escala, Rotação, Âncora...): property/param são os do 1º
 * eixo do grupo e a trilha junta os keyframes de todos os eixos. É só vista e
 * gesto sobre as trilhas por eixo (o motor e o store continuam por eixo).
 */
internal data class TimelineTrack(val property: Int, val effect: Int = -1, val param: Int = 0, val group: Boolean = false)

/**
 * O grupo de eixos de uma trilha (a trilha-base dele, com `group`), ou null
 * quando a propriedade é de um componente só. Transform: 0–2 Posição, 3–5
 * Escala, 6–8 Rotação, 9–11 Âncora, 13–14 Inclinação; peça 3D (42): 0–2
 * Posição, 3–5 Rotação, 6–8 Escala.
 */
internal fun trackGroup(t: TimelineTrack): TimelineTrack? = when {
    t.group -> t
    t.property in 0..11 -> TimelineTrack(t.property / 3 * 3, t.effect, t.param, group = true)
    t.property == 13 || t.property == 14 -> TimelineTrack(13, t.effect, t.param, group = true)
    t.property == 42 && t.param in 0..8 -> TimelineTrack(42, t.effect, t.param / 3 * 3, group = true)
    else -> null
}

/** Um grupo de eixos aberto (▾) numa camada: as trilhas por eixo aparecem embaixo dele. */
internal data class LaneGroupKey(val layer: Long, val track: TimelineTrack)

/**
 * Nomes das trilhas no idioma do app. O controlador entrega o Application
 * ([TimelineController]); sem ele (testes de JVM) os nomes saem em inglês.
 */
internal object LaneNames {
    @Volatile var app: Context? = null
    private val english = mapOf(
        R.string.tl_position to "Position", R.string.tl_scale to "Scale", R.string.tl_rotation to "Rotation",
        R.string.tl_anchor to "Anchor", R.string.tl_skew to "Skew", R.string.tl_opacity to "Opacity",
        R.string.tl_part to "Part %1\$d · %2\$s", R.string.tl_time_remap to "Time remap", R.string.tl_effect to "Effect",
        R.string.tl_audio to "Audio · %1\$d", R.string.tl_text_anim to "Text animation %1\$d · %2\$d",
        R.string.tl_mask to "Mask %1\$d · %2\$s", R.string.tl_feather to "Feather", R.string.tl_expansion to "Expansion",
        R.string.tl_vector to "Vector · %1\$d", R.string.tl_shape to "Shape · %1\$d", R.string.tl_particles to "Particles · %1\$d",
        R.string.tl_speed to "Speed", R.string.tl_animator to "Animator %1\$d · %2\$d", R.string.tl_material to "Material %1\$d · %2\$s",
        R.string.tl_alpha to "Alpha", R.string.tl_metallic to "Metallic", R.string.tl_roughness to "Roughness",
        R.string.tl_transform to "Transform", R.string.tl_3d to "3D · %1\$d",
    )
    fun get(@StringRes id: Int, vararg args: Any): String {
        val context = app
        return if (context != null) AppText.get(context, id, *args) else String.format(english.getValue(id), *args)
    }
}

private val AXIS_BASES = listOf(R.string.tl_position, R.string.tl_scale, R.string.tl_rotation, R.string.tl_anchor)
private val AXIS_LETTERS = listOf("X", "Y", "Z")

/** Eixo da transformação (0..14): "Posição X", ..., "Opacidade", "Inclinação X/Y". */
private fun axisName(property: Int): String? = when (property) {
    in 0..11 -> LaneNames.get(AXIS_BASES[property / 3]) + " " + AXIS_LETTERS[property % 3]
    12 -> LaneNames.get(R.string.tl_opacity)
    13, 14 -> LaneNames.get(R.string.tl_skew) + " " + AXIS_LETTERS[property - 13]
    else -> null
}

/** Eixos de uma PARTE de forma 3D: posição, rotação, escala (nessa ordem). */
private fun partAxisName(param: Int): String? {
    val base = listOf(R.string.tl_position, R.string.tl_rotation, R.string.tl_scale).getOrNull(param / 3) ?: return null
    return LaneNames.get(base) + " " + AXIS_LETTERS[param % 3]
}

private fun trackName(track: TimelineTrack, effects: List<Pair<Int, String>>): String {
    if (track.group) return when (track.property) {
        0 -> LaneNames.get(R.string.tl_position)
        3 -> LaneNames.get(R.string.tl_scale)
        6 -> LaneNames.get(R.string.tl_rotation)
        9 -> LaneNames.get(R.string.tl_anchor)
        13 -> LaneNames.get(R.string.tl_skew)
        42 -> LaneNames.get(R.string.tl_part, track.effect + 1,
            listOf(R.string.tl_position, R.string.tl_rotation, R.string.tl_scale).getOrNull(track.param / 3)?.let { LaneNames.get(it) } ?: track.param.toString())
        else -> LaneNames.get(R.string.tl_3d, track.property)
    }
    return axisName(track.property) ?: when (track.property) {
        30 -> LaneNames.get(R.string.tl_time_remap)
        31 -> (effects.firstOrNull { it.first == track.effect }?.second ?: LaneNames.get(R.string.tl_effect)) + " · ${track.param + 1}"
        32 -> LaneNames.get(R.string.tl_audio, track.param + 1)
        33 -> LaneNames.get(R.string.tl_text_anim, track.effect + 1, track.param + 1)
        43 -> LaneNames.get(R.string.tl_mask, track.effect + 1,
            listOf(R.string.tl_feather, R.string.tl_expansion, R.string.tl_opacity).getOrNull(track.param)?.let { LaneNames.get(it) } ?: track.param.toString())
        34 -> LaneNames.get(R.string.tl_vector, track.param + 1)
        35 -> LaneNames.get(R.string.tl_shape, track.param + 1)
        36 -> LaneNames.get(R.string.tl_particles, track.param + 1)
        39 -> LaneNames.get(R.string.tl_speed)
        40 -> LaneNames.get(R.string.tl_animator, track.effect + 1, track.param + 1)
        37 -> LaneNames.get(R.string.tl_material, track.effect + 1,
            when (track.param) {
                0 -> "R"; 1 -> "G"; 2 -> "B"
                3 -> LaneNames.get(R.string.tl_alpha); 4 -> LaneNames.get(R.string.tl_metallic); 5 -> LaneNames.get(R.string.tl_roughness)
                else -> track.param.toString()
            })
        42 -> LaneNames.get(R.string.tl_part, track.effect + 1, partAxisName(track.param) ?: track.param.toString())
        else -> LaneNames.get(R.string.tl_3d, track.property)
    }
}

/** Compatível com a versão de UMA camada aberta (testes e chamadas antigas). */
internal fun expandedRows(base: List<RowModel>, expanded: Long?, keys: Map<Long, List<KeyframeRow>>, effects: List<Pair<Int, String>>): List<RowModel> =
    if (expanded == null) base else expandedRows(base, setOf(expanded), emptySet(), keys) { effects }

/**
 * Trilhas abertas de VÁRIAS camadas de uma vez (cada uma com o seu ▸/▾). Uma
 * propriedade de vários eixos com 2+ eixos animados vira UMA trilha de grupo
 * (um losango por instante, a união dos eixos); o grupo aberto em [openGroups]
 * mostra as trilhas por eixo logo abaixo dele. Um eixo sozinho continua sendo
 * a trilha dele.
 */
internal fun expandedRows(
    base: List<RowModel>,
    expanded: Set<Long>,
    openGroups: Set<LaneGroupKey>,
    keys: Map<Long, List<KeyframeRow>>,
    effects: (Long) -> List<Pair<Int, String>>,
): List<RowModel> {
    if (expanded.isEmpty()) return base
    return base.flatMap { row ->
        // Numa fileira compartilhada, as trilhas abertas são do TRECHO aberto
        // (tempo e keyframes dele) e entram logo abaixo da fileira.
        val owner = row.segments.firstOrNull { it.id in expanded }
        if (owner == null) listOf(row) else {
            val fx = effects(owner.id)
            val all = keys[owner.id].orEmpty()
            val tracks = all.groupBy { TimelineTrack(it.property, it.effectIndex, it.paramIndex) }
            val lanes = arrayListOf(row)
            fun lane(track: TimelineTrack, name: String, values: List<KeyframeRow> = emptyList()) {
                val groups = values.groupBy { it.time }.toSortedMap()
                lanes += RowModel(owner.id, owner.type, owner.start, owner.end, owner.offset, owner.visible, owner.locked,
                    owner.magnetic, values.isNotEmpty(), name, owner.label,
                    groups.keys.map { Keyframes.toTimeline(it, owner.start, owner.offset) }.toIntArray(), groups.values.toTypedArray(), track)
            }
            lane(TimelineTrack(-1), "  " + LaneNames.get(R.string.tl_transform))
            fx.forEach { (id, name) -> lane(TimelineTrack(31, id, -1), "  $name") }
            val ordered = tracks.keys.sortedWith(compareBy({ it.property }, { it.effect }, { it.param }))
            // Os eixos animados de cada grupo (só 2+ vira trilha de grupo).
            val members = ordered.groupBy { trackGroup(it) }
            val emitted = HashSet<TimelineTrack>()
            for (track in ordered) {
                val group = trackGroup(track)
                val axes = if (group != null) members[group].orEmpty() else emptyList()
                if (group == null || axes.size < 2) {
                    lane(track, "  " + trackName(track, fx), tracks[track].orEmpty())
                    continue
                }
                if (!emitted.add(group)) continue
                lane(group, "  " + trackName(group, fx), axes.flatMap { tracks[it].orEmpty() })
                if (LaneGroupKey(owner.id, group) in openGroups) {
                    for (axis in axes) lane(axis, "      " + trackName(axis, fx), tracks[axis].orEmpty())
                }
            }
            lanes
        }
    }
}

/**
 * Altura de uma trilha de propriedade: baixa (16 dp), para caberem muitas sob
 * as camadas. O dedo não perde o alvo: um toque que não pega nada na trilha
 * sob ele tenta a vizinha (ver [LaneTouch]), o que dá ≥ 32 dp a cada losango.
 */
internal const val LANE_HEIGHT_DP = 16f

/**
 * Folga vertical de toque das trilhas baixas: a ordem das fileiras a tentar
 * para um dedo em [y] (coordenada de conteúdo) sobre a fileira [index] — ela
 * mesma, depois a trilha vizinha cujo centro está a até [reach] px do dedo
 * (a mais perto primeiro). Só trilhas de propriedade entram como vizinhas.
 */
internal object LaneTouch {
    fun order(isLane: (Int) -> Boolean, tops: FloatArray, index: Int, y: Float, reach: Float): IntArray {
        if (index < 0 || index + 1 >= tops.size) return intArrayOf(index)
        val near = ArrayList<Pair<Int, Float>>(2)
        for (j in intArrayOf(index - 1, index + 1)) {
            if (j < 0 || j + 1 >= tops.size || !isLane(j)) continue
            val d = kotlin.math.abs((tops[j] + tops[j + 1]) / 2f - y)
            if (d <= reach) near.add(j to d)
        }
        near.sortBy { it.second }
        return IntArray(1 + near.size) { if (it == 0) index else near[it - 1].first }
    }
}

/**
 * Seleção por RETÂNGULO (modo "Selecionar"): os keyframes cujos losangos têm
 * o centro dentro de [frameLo, frameHi] × [yLo, yHi] (tempo da timeline e y de
 * conteúdo), por camada — no resumo da camada, todos do instante; numa
 * trilha, os dela. [tops] tem os topos das fileiras (`rows.size + 1`);
 * [keyCy] dá o centro dos losangos medido do topo da fileira; [keysVisible]
 * diz se o trecho mostra losangos (os escondidos não entram).
 */
internal object BoxSelect {
    fun pick(
        rows: List<RowModel>,
        tops: FloatArray,
        keyCy: (RowModel) -> Float,
        frameLo: Double,
        frameHi: Double,
        yLo: Float,
        yHi: Float,
        keysVisible: (RowModel) -> Boolean,
    ): Map<Long, List<KeyframeRow>> {
        val out = LinkedHashMap<Long, ArrayList<KeyframeRow>>()
        val lo = kotlin.math.ceil(min(frameLo, frameHi) - 1e-9)
        val hi = kotlin.math.floor(max(frameLo, frameHi) + 1e-9)
        val top = min(yLo, yHi)
        val bottom = max(yLo, yHi)
        for (i in rows.indices) {
            if (i >= tops.size) break
            val row = rows[i]
            val cy = tops[i] + keyCy(row)
            if (cy < top || cy > bottom) continue
            for (s in row.segments) {
                if (!keysVisible(s)) continue
                for (k in s.instants.indices) {
                    val t = s.instants[k]
                    if (t < lo || t > hi) continue
                    val list = out.getOrPut(s.id) { ArrayList() }
                    for (key in s.keysAt[k]) if (list.none { KeyRef.of(it) == KeyRef.of(key) }) list.add(key)
                }
            }
        }
        return out
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
    return if (row.track != null) LANE_HEIGHT_DP * density
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
