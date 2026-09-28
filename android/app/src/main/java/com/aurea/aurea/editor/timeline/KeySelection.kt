package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import kotlin.math.max

/**
 * Um keyframe identificado pela TRILHA (propriedade, efeito, componente) e
 * pelo tempo LOCAL da camada — sem valor nem curva: é a chave estável que o
 * motor entende em `edit_keyframe_selection` (4 inteiros por keyframe).
 */
data class KeyRef(val property: Int, val effect: Int, val param: Int, val time: Int) {
    fun sameTrack(k: KeyframeRow) = k.property == property && k.effectIndex == effect && k.paramIndex == param
    fun matches(k: KeyframeRow) = sameTrack(k) && k.time == time

    companion object {
        fun of(k: KeyframeRow) = KeyRef(k.property, k.effectIndex, k.paramIndex, k.time)
    }
}

/**
 * Seleção de keyframes da TIMELINE: UMA camada e um conjunto de keyframes de
 * QUALQUER trilha dela (Posição X, Escala, parâmetro de efeito...). Pura (sem
 * motor nem Compose) para os testes de JVM; o store guarda uma instância e
 * troca por outra a cada mudança.
 *
 * Contrato igual no iOS (`TimelineKeySelection` em TimelineModel.swift):
 * - tocar num losango de trilha alterna só aquele keyframe;
 * - tocar num losango do RESUMO alterna o grupo inteiro daquele instante
 *   (todas as trilhas com marca ali): se já estava todo escolhido, sai todo;
 * - mover desloca todas as referências pelo MESMO delta (o motor já aceitou);
 * - se alguma referência deixou de existir nos keyframes relidos, a seleção
 *   inteira é descartada (desfazer, apagar a camada, outra superfície editou).
 */
data class KeySelection(
    val layer: Long,
    val keys: Set<KeyRef> = emptySet(),
    /**
     * Keyframes escolhidos em OUTRAS camadas (veio do app antigo: no modo
     * "Selecionar" tocar nos losangos de outra fileira soma à seleção). [layer]
     * continua a principal (Copiar/Colar/Duplicar agem só nela); mover e
     * excluir valem para todas.
     */
    val others: Map<Long, Set<KeyRef>> = emptyMap(),
) {
    val size: Int get() = keys.size + others.values.sumOf { it.size }
    fun isEmpty() = size == 0

    /** Há keyframes escolhidos em mais de uma camada? */
    val crossLayer: Boolean get() = others.isNotEmpty()

    /** As referências de uma camada (vazio se ela não tem nada escolhido). */
    fun on(layer: Long): Set<KeyRef> = if (layer == this.layer) keys else others[layer].orEmpty()

    /** Camadas com algum keyframe escolhido (a principal primeiro). */
    fun layers(): List<Long> {
        val out = ArrayList<Long>(1 + others.size)
        if (keys.isNotEmpty()) out.add(layer)
        for ((id, refs) in others) if (refs.isNotEmpty()) out.add(id)
        return out
    }

    fun containsAnyOn(layer: Long, group: List<KeyframeRow>): Boolean {
        val refs = on(layer)
        return refs.isNotEmpty() && group.any { KeyRef.of(it) in refs }
    }

    /** Alterna o grupo na camada dada: na principal ou numa das outras. */
    fun toggleGroupOn(layer: Long, group: List<KeyframeRow>): KeySelection {
        if (layer == this.layer) return toggleGroup(group)
        if (group.isEmpty()) return this
        val refs = group.mapTo(LinkedHashSet()) { KeyRef.of(it) }
        val current = others[layer].orEmpty()
        val next = if (current.containsAll(refs)) current - refs else current + refs
        return copy(others = if (next.isEmpty()) others - layer else others + (layer to next))
    }

    /**
     * Quanto a seleção inteira pode andar para cada camada ficar dentro do
     * próprio clipe: [clip] dá (início, fim, deslocamento interno) de cada
     * camada. Devolve (mínimo ≤ 0, máximo ≥ 0) — o mais restritivo de todas.
     */
    fun shiftLimits(clip: (Long) -> IntArray?): IntArray {
        var lo = Int.MIN_VALUE
        var hi = Int.MAX_VALUE
        for (id in layers()) {
            val c = clip(id) ?: continue
            val refs = on(id)
            val first = Keyframes.toTimeline(refs.minOf { it.time }, c[0], c[2])
            val last = Keyframes.toTimeline(refs.maxOf { it.time }, c[0], c[2])
            lo = max(lo, minOf(0, c[0] - first))
            hi = minOf(hi, max(0, c[1] - last))
        }
        return intArrayOf(if (lo == Int.MIN_VALUE) 0 else lo, if (hi == Int.MAX_VALUE) 0 else hi)
    }

    /** Todas as camadas continuam válidas (cada referência existe nos keyframes relidos)? */
    fun validatedAll(current: (Long) -> List<KeyframeRow>?): KeySelection? {
        for (id in layers()) {
            val rows = current(id) ?: return null
            for (ref in on(id)) if (rows.none { ref.matches(it) }) return null
        }
        return this
    }

    /** Empacotado para o motor, só das referências de uma camada. */
    fun references(layer: Long): LongArray = pack(on(layer))

    operator fun contains(key: KeyframeRow) = KeyRef.of(key) in keys
    fun containsAny(group: List<KeyframeRow>) = group.any { KeyRef.of(it) in keys }

    fun toggle(key: KeyframeRow): KeySelection = toggleGroup(listOf(key))

    /** Alterna um grupo: entra tudo o que falta; se já estava todo escolhido, sai todo. */
    fun toggleGroup(group: List<KeyframeRow>): KeySelection {
        if (group.isEmpty()) return this
        val refs = group.mapTo(LinkedHashSet()) { KeyRef.of(it) }
        return if (keys.containsAll(refs)) copy(keys = keys - refs) else copy(keys = keys + refs)
    }

    /** Todas as referências andam `delta` frames (depois que o motor aceitou o mesmo delta). */
    fun shifted(delta: Int): KeySelection =
        if (delta == 0) this else copy(
            keys = keys.mapTo(LinkedHashSet()) { it.copy(time = it.time + delta) },
            others = others.mapValues { (_, refs) -> refs.mapTo(LinkedHashSet()) { it.copy(time = it.time + delta) } },
        )

    /** Continua valendo só se TODA referência ainda existe; senão null (a seleção some inteira). */
    fun validated(current: List<KeyframeRow>): KeySelection? {
        if (keys.isEmpty()) return this
        for (ref in keys) if (current.none { ref.matches(it) }) return null
        return this
    }

    /** Os keyframes escolhidos, na ordem dos keyframes relidos. */
    fun rows(current: List<KeyframeRow>): List<KeyframeRow> = current.filter { KeyRef.of(it) in keys }

    fun minTime(): Int = keys.minOfOrNull { it.time } ?: 0
    fun maxTime(): Int = keys.maxOfOrNull { it.time } ?: 0

    /** Empacotado para o motor: propriedade, efeito (−1 = nenhum), componente, tempo local. */
    fun references(): LongArray = pack(keys)

    private fun pack(refs: Set<KeyRef>): LongArray {
        val out = LongArray(refs.size * 4)
        var i = 0
        for (ref in refs) {
            out[i++] = ref.property.toLong()
            out[i++] = ref.effect.toLong()
            out[i++] = ref.param.toLong()
            out[i++] = ref.time.toLong()
        }
        return out
    }

    /**
     * Delta do "Duplicar": a cópia começa 1 frame depois do ÚLTIMO escolhido
     * (o motor cola ancorado no mais cedo). Null se estoura o Int do motor.
     */
    fun duplicateDelta(): Int? {
        if (keys.isEmpty()) return null
        val delta = maxTime().toLong() + 1L - minTime().toLong()
        val last = maxTime().toLong() + delta
        return if (last > Int.MAX_VALUE) null else delta.toInt()
    }

    companion object {
        fun single(layer: Long, key: KeyframeRow) = KeySelection(layer, setOf(KeyRef.of(key)))

        /** "Todos": todos os keyframes que a timeline mostra da camada (respeita o foco de trilhas). */
        fun all(layer: Long, keys: List<KeyframeRow>, focus: List<com.aurea.aurea.engine.TrackKey>? = null): KeySelection {
            val visible = if (focus == null) keys else focusedKeys(keys, focus)
            return KeySelection(layer, visible.mapTo(LinkedHashSet()) { KeyRef.of(it) })
        }
    }
}
