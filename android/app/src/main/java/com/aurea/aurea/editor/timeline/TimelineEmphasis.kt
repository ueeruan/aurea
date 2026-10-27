package com.aurea.aurea.editor.timeline

import java.util.Arrays

/**
 * Ênfase dos losangos na barra do clipe (spec am-ui §4.2, P1-6). Com um painel
 * editando um parâmetro (`EditorStore.timelineFocus`), os losangos DELE ficam
 * fortes e os dos outros parâmetros animados da camada aparecem esmaecidos —
 * só de referência: não se tocam nem se arrastam (o toque e o arrasto seguem
 * no parâmetro do painel, como antes). O losango sob o cabeçote acende na cor
 * de destaque. Contas puras; par do `KeyEmphasis` do iOS (TimelineMath.swift).
 */
internal object KeyEmphasis {
    /** Opacidade dos losangos dos outros parâmetros (160/255, a da referência). */
    const val DIM_ALPHA = 160f / 255f

    /** Cor de um losango forte, da maior prioridade para a menor. */
    enum class Tone { CHOSEN, PICKED, PLAYHEAD, OFF }

    /**
     * Instantes de [all] que o foco não marca: os dos outros parâmetros. Os
     * dois vêm ordenados e sem repetição (`RowModel.instants`); a saída também.
     * O instante que o foco já tem fica só no forte (nada desenhado duas vezes).
     */
    fun dimmed(all: IntArray, strong: IntArray): IntArray {
        if (all.isEmpty()) return all
        if (strong.isEmpty()) return all.copyOf()
        var n = 0
        val out = IntArray(all.size)
        for (t in all) if (Arrays.binarySearch(strong, t) < 0) out[n++] = t
        return if (n == out.size) out else out.copyOf(n)
    }

    /**
     * Losango "empilhado" (dois sobrepostos, o de baixo na cor de parcial): só
     * quando o instante tem chave em PARTE das [focusCount] trilhas do painel
     * (ex.: Posição X sem Y). Com todas marcadas é UM losango. Antes bastavam 2
     * trilhas distintas: o ◆ do Mover (X+Y) num toque aparecia como dois
     * keyframes sobrepostos — e, ao entrar no Transformar, um keyframe que se via
     * único fora dele "virava dois" (0.3.6, keys itens 1 e 3).
     */
    fun stacked(focusCount: Int, distinctKeyed: Int): Boolean = focusCount >= 2 && distinctKeyed < focusCount

    /**
     * Escolhido (âmbar, o do gráfico) > na seleção da timeline (azul com anel)
     * > sob o cabeçote (destaque) > o branco de sempre.
     */
    fun tone(chosen: Boolean, picked: Boolean, underPlayhead: Boolean): Tone = when {
        chosen -> Tone.CHOSEN
        picked -> Tone.PICKED
        underPlayhead -> Tone.PLAYHEAD
        else -> Tone.OFF
    }
}

/**
 * Fileira nova surge com fade (spec am-ui §4.3: alfa 0→1 em 600 ms,
 * Decelerate). "Nova" = uma camada que não estava na leitura anterior da MESMA
 * composição; abrir projeto, entrar ou sair de um grupo (lista trocada inteira)
 * não anima nada. Par do `TimelineRowFade` do iOS.
 */
internal object RowFade {
    const val DURATION_MS = 600L

    /** Opacidade [elapsedMs] depois de nascer: Decelerate(1) = 1 − (1 − t)². */
    fun alpha(elapsedMs: Long): Float {
        if (elapsedMs <= 0L) return 0f
        if (elapsedMs >= DURATION_MS) return 1f
        val t = elapsedMs.toFloat() / DURATION_MS
        return 1f - (1f - t) * (1f - t)
    }

    /**
     * Camadas de [current] que não estavam em [previous] (ordenado). Null =
     * primeira leitura desta composição: nada nasce. Sem nenhuma camada em
     * comum com uma lista anterior não vazia, a lista foi TROCADA (outro
     * projeto, dentro de um grupo): nada nasce também. Lista anterior vazia e
     * UMA camada agora = a primeira camada adicionada ao projeto vazio.
     */
    fun born(previous: LongArray?, current: LongArray): LongArray {
        if (previous == null || current.isEmpty()) return LongArray(0)
        if (previous.isEmpty()) return if (current.size == 1) current.copyOf() else LongArray(0)
        var n = 0
        var shared = false
        val out = LongArray(current.size)
        for (id in current) {
            if (Arrays.binarySearch(previous, id) >= 0) shared = true else out[n++] = id
        }
        return if (!shared) LongArray(0) else out.copyOf(n)
    }
}
