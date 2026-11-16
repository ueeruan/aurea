package com.aurea.aurea.state

import android.content.Context

/**
 * 0.5.3 (P0-1): o que o export de vídeo lembra de um processo para o outro.
 *
 * - Marca "em curso": posta quando o export de vídeo começa e tirada no fim
 *   dele (pronto, falha ou cancelado). Se o processo morre no meio, ela fica —
 *   é como o próximo export sabe que o anterior foi morto, em QUALQUER Android
 *   (o histórico de saídas do sistema só existe no Android 11+ e só conta quatro
 *   motivos; a morte pelo gerenciador de bateria do fabricante vinha com outro).
 * - Degrau de memória ([MAX_STEP] = o mais apertado): sobe um a cada export que
 *   começa depois de uma morte e volta a zero no primeiro que termina. O motor
 *   aperta o orçamento de memória por ele (AureaEngine.setExportMemoryStep), e a
 *   retomada do checkpoint não repete igual a tentativa que o sistema matou.
 *
 * Só números e um booleano: nada de projeto, caminho ou conta. Chamar fora da
 * main thread (grava com commit: a marca tem de estar no disco antes da morte).
 */
internal object ExportKillMemory {
    const val MAX_STEP = 3
    private const val PREFS = "aurea_export_run"
    private const val IN_FLIGHT = "video_in_flight"
    private const val STEP = "exportKillStep"

    /** A regra, sem disco: morreu → um degrau acima, até [MAX_STEP]; senão fica. */
    fun stepAfterStart(step: Int, died: Boolean): Int =
        (step.coerceIn(0, MAX_STEP) + if (died) 1 else 0).coerceAtMost(MAX_STEP)

    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** Um export de vídeo começa. true = a marca do anterior ficou (o processo morreu no meio dele). */
    fun begin(context: Context): Boolean = runCatching {
        val p = prefs(context)
        val left = p.getBoolean(IN_FLIGHT, false)
        p.edit().putBoolean(IN_FLIGHT, true).commit()
        left
    }.getOrDefault(false)

    /** O export de vídeo acabou (qualquer desfecho que o app viu). */
    fun end(context: Context) {
        runCatching { prefs(context).edit().remove(IN_FLIGHT).commit() }
    }

    /** O degrau deste export: o guardado, um acima se o anterior morreu. */
    fun stepForStart(context: Context, died: Boolean): Int = runCatching {
        val p = prefs(context)
        val before = p.getInt(STEP, 0)
        val now = stepAfterStart(before, died)
        if (now != before) p.edit().putInt(STEP, now).commit()
        now
    }.getOrDefault(0)

    /** Terminou com o arquivo pronto: o próximo export volta ao orçamento cheio. */
    fun done(context: Context) {
        runCatching { prefs(context).let { if (it.getInt(STEP, 0) != 0) it.edit().putInt(STEP, 0).commit() } }
    }
}
