@file:Suppress("unused", "UNUSED_PARAMETER")

package com.aurea.aurea.state

import android.content.Context

/**
 * No Android a exportação roda num serviço de primeiro plano (para o sistema
 * não matar o app). No PC o processo não é morto em segundo plano; o progresso
 * vai para a barra de tarefas do Windows.
 */
class ExportForegroundService {
    companion object {
        @Volatile var timedOut = false
        /** 0.5.3: par do Android (relato local do export); no PC não há serviço a recusar. */
        @Volatile var refused = false
        fun diagnosticState(): String = "pc"
        fun start(context: Context) { timedOut = false; com.aurea.pc.PcApp.taskbarProgress(0f) }
        fun update(context: Context, framesDone: Int, framesTotal: Int) {
            if (framesTotal > 0) com.aurea.pc.PcApp.taskbarProgress(framesDone.toFloat() / framesTotal)
        }
        fun stop(context: Context) = com.aurea.pc.PcApp.taskbarProgress(-1f)
    }
}
