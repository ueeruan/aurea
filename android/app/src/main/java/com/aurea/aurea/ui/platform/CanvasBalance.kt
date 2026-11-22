package com.aurea.aurea.ui.platform

import androidx.compose.ui.graphics.drawscope.DrawScope

/**
 * 1.0.6: desenho que mexe direto na pilha do Canvas (save/saveLayer/restore)
 * termina com a pilha na MESMA altura do começo.
 *
 * Um restore a mais dentro de um `drawBehind` não falha na hora: falha no
 * restore do próprio Compose, depois que o bloco já voltou ("Underflow in
 * restore - more restores than saves", LayoutNodeDrawScope.drawDirect — 27
 * crashes em 2222/2225, 107 desde o 2219). Um save a menos vazava clipe e
 * alpha para o resto da tela. Aqui a diferença é desfeita no fim: o que sobrou
 * de save é fechado e o que foi restaurado a mais é reaberto, e o aviso vai
 * para o log para achar o desenho culpado.
 *
 * Vale nas duas plataformas do código Kotlin (Android e Aurea PC): só usa o
 * [platformCanvas] e as operações que a camada do PC também tem.
 */
inline fun DrawScope.balancedCanvas(tag: String, block: DrawScope.() -> Unit) {
    val canvas = platformCanvas
    val depth = canvas.saveCount
    try {
        block()
    } finally {
        val now = platformCanvas.saveCount
        if (now != depth) {
            if (now > depth) platformCanvas.restoreToCount(depth)
            else repeat(depth - now) { platformCanvas.save() }
            CanvasBalance.report(tag, now - depth)
        }
    }
}

object CanvasBalance {
    @Volatile private var reported = 0

    /** No máximo alguns avisos por processo: o mesmo desenho erra a cada quadro. */
    fun report(tag: String, delta: Int) {
        if (reported >= 8) return
        reported++
        android.util.Log.w("AureaCanvas", "pilha do Canvas desequilibrada em $tag: ${if (delta > 0) "+" else ""}$delta (corrigida)")
    }
}
