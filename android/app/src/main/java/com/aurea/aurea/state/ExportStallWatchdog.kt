package com.aurea.aurea.state

/**
 * A última rede contra "a exportação parou num percentual e nunca termina".
 *
 * O motor já tem prazo em toda espera (export/ExportWatchdog.hpp: GPU 120 s,
 * quadro 60 s, worker do encoder 45 s) e conclui com falha em vez de travar.
 * Isto é para o caso que nem ele vê: o progresso (quadros e mensagem) não muda
 * por [stallMs] → a tela pede o cancelamento; sem conclusão [giveUpMs] depois,
 * a tela desiste, mostra o motivo e se libera (o motor termina sozinho depois).
 *
 * Os números são os de kExportUiStallSeconds / kExportUiGiveUpSeconds do motor;
 * o AureaModel.swift repete a mesma regra. Sem relógio próprio: quem chama passa
 * o tempo (testável na JVM).
 */
class ExportStallWatchdog(
    private val stallMs: Long = STALL_MS,
    private val giveUpMs: Long = GIVE_UP_MS,
) {
    enum class Verdict { Running, Cancel, GiveUp }

    private var started = false
    private var lastDone = 0
    private var lastMessage = ""
    private var lastChangeMs = 0L
    private var cancelAtMs = 0L

    /** A tela já pediu o cancelamento por falta de progresso. */
    var cancelled: Boolean = false
        private set

    /** Uma leitura do progresso. O [Verdict.Cancel] vem uma vez só. */
    fun observe(nowMs: Long, framesDone: Int, message: String): Verdict {
        if (!started || framesDone != lastDone || message != lastMessage) {
            started = true
            lastDone = framesDone
            lastMessage = message
            lastChangeMs = nowMs
        }
        if (!cancelled) {
            if (nowMs - lastChangeMs < stallMs) return Verdict.Running
            cancelled = true
            cancelAtMs = nowMs
            return Verdict.Cancel
        }
        return if (nowMs - cancelAtMs >= giveUpMs) Verdict.GiveUp else Verdict.Running
    }

    companion object {
        /** kExportUiStallSeconds (ExportWatchdog.hpp). */
        const val STALL_MS = 180_000L
        /** kExportUiGiveUpSeconds (ExportWatchdog.hpp). */
        const val GIVE_UP_MS = 15_000L
        /** kExportSafeModeMax: refazer no modo de segurança no máximo até o nível 2. */
        const val SAFE_MODE_MAX = 2

        /**
         * O próximo modo de segurança: o que o MOTOR sugeriu, só se subir (nunca
         * repete nem volta) e até o máximo. 0 = não refazer.
         */
        fun nextSafeMode(current: Int, suggested: Int): Int =
            if (suggested > current && suggested <= SAFE_MODE_MAX) suggested else 0
    }
}
