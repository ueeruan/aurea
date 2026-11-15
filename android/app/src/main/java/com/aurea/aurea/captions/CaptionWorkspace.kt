package com.aurea.aurea.captions

/**
 * 0.5.3 (legendas no jeito do Premiere): contas puras das abas Transcrição e
 * Legendas. Gêmeas em Swift (`CaptionWorkspace` em CaptionsWorkspace.swift).
 * Nada aqui edita o projeto: as regras de edição dos blocos moram no motor
 * (`edit_caption_track`); isto só organiza o que a tela mostra e a transcrição,
 * que é dado da interface (cache por arquivo de mídia).
 */
object CaptionWorkspace {
    private const val SENTENCE_END = ".!?…。！？"

    fun endsSentence(text: String): Boolean = text.trimEnd().lastOrNull()?.let { it in SENTENCE_END } == true

    /**
     * Onde começa cada parágrafo da transcrição: depois de uma pausa de pelo
     * menos [pauseSec], depois de um fim de frase quando o parágrafo já tem
     * [softWords] palavras, e no máximo a cada [maxWords] palavras.
     */
    fun paragraphStarts(words: List<Word>, pauseSec: Double = 1.0, softWords: Int = 25, maxWords: Int = 60): List<Int> {
        if (words.isEmpty()) return emptyList()
        val out = mutableListOf(0)
        var count = 1
        for (i in 1 until words.size) {
            val prev = words[i - 1]
            val pause = words[i].start - prev.end >= pauseSec
            val sentence = endsSentence(prev.text) && count >= softWords
            if (pause || sentence || count >= maxWords) { out += i; count = 1 } else count++
        }
        return out
    }

    /** Relógio do parágrafo: "m:ss" (ou "h:mm:ss"). */
    fun clock(sec: Double): String {
        val s = sec.coerceAtLeast(0.0).toLong()
        val h = s / 3600; val m = s / 60 % 60; val r = s % 60
        return if (h > 0) String.format(java.util.Locale.ROOT, "%d:%02d:%02d", h, m, r) else String.format(java.util.Locale.ROOT, "%d:%02d", m, r)
    }

    /** Código de tempo da lista: "mm:ss:qq" (quadros), com a hora quando houver. */
    fun timecode(frame: Int, fps: Double): String {
        val rate = if (fps > 0) fps else 30.0
        val f = frame.coerceAtLeast(0)
        val whole = kotlin.math.round(rate).toInt().coerceAtLeast(1)
        val sec = (f / rate).toLong()
        val q = (f - kotlin.math.round(sec * rate).toInt()).coerceIn(0, whole - 1)
        val h = sec / 3600; val m = sec / 60 % 60; val s = sec % 60
        return if (h > 0) String.format(java.util.Locale.ROOT, "%d:%02d:%02d:%02d", h, m, s, q) else String.format(java.util.Locale.ROOT, "%02d:%02d:%02d", m, s, q)
    }

    /** Quantas palavras inteiras ficam antes do [cursor] (o "dividir no cursor"). */
    fun wordsBefore(text: String, cursor: Int): Int {
        val head = text.substring(0, cursor.coerceIn(0, text.length))
        val count = head.split(Regex("\\s+")).count { it.isNotEmpty() }
        // Cursor no meio de uma palavra: ela vai inteira para a direita.
        val inside = cursor in 1 until text.length && !text[cursor - 1].isWhitespace() && !text[cursor].isWhitespace()
        return if (inside) count - 1 else count
    }

    /**
     * Buscar e substituir na transcrição: cada palavra troca só o próprio
     * texto e guarda o tempo. Palavra que fica vazia sai. Devolve a lista nova
     * e quantas trocas houve.
     */
    fun replaceWords(words: List<Word>, find: String, replacement: String, matchCase: Boolean): Pair<List<Word>, Int> {
        if (find.isEmpty()) return words to 0
        var total = 0
        val out = ArrayList<Word>(words.size)
        for (w in words) {
            var hits = 0
            var at = w.text.indexOf(find, ignoreCase = !matchCase)
            if (at < 0) { out += w; continue }
            val b = StringBuilder()
            var from = 0
            while (at >= 0) {
                b.append(w.text, from, at).append(replacement)
                from = at + find.length
                hits++
                at = w.text.indexOf(find, from, ignoreCase = !matchCase)
            }
            b.append(w.text, from, w.text.length)
            total += hits
            val t = b.toString().trim()
            if (t.isNotEmpty()) out += w.copy(text = t)
        }
        return out to total
    }

    /** Índice do bloco na agulha ([frame] da timeline), ou -1. */
    fun activeIndex(track: CaptionTrack, frame: Int): Int {
        val local = frame - track.shift
        return track.segments.indexOfFirst { local >= it.start && local < it.end }
    }
}
