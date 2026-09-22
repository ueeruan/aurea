package com.aurea.aurea.engine

/**
 * Tipos das expressões (motor: expr/Expression.hpp). O texto atravessa a JNI
 * em bytes UTF-8 com campos separados por U+001F; a fonte, quando vem, é o
 * último campo (pode conter qualquer coisa, inclusive quebras de linha).
 */

/** Chave de uma trilha: a mesma dos keyframes. */
data class TrackKey(val property: Int, val effectIndex: Int = -1, val paramIndex: Int = 0)

/** Diagnóstico de sintaxe/execução. Linha e coluna começam em 1 (0 = sem posição). */
data class ExpressionDiag(val ok: Boolean, val line: Int, val column: Int, val message: String) {
    companion object {
        val OK = ExpressionDiag(true, 0, 0, "")

        internal fun parse(f: List<String>, at: Int): ExpressionDiag = ExpressionDiag(
            ok = f.getOrNull(at) == "1",
            line = f.getOrNull(at + 1)?.toIntOrNull() ?: 0,
            column = f.getOrNull(at + 2)?.toIntOrNull() ?: 0,
            message = f.getOrNull(at + 3) ?: "",
        )

        fun decode(bytes: ByteArray?): ExpressionDiag? {
            bytes ?: return null
            return parse(String(bytes, Charsets.UTF_8).split(SEP, limit = 4), 0)
        }
    }
}

/** Estado de uma expressão no playhead. `value` = resultado na unidade guardada. */
data class ExpressionInfo(
    val exists: Boolean,
    val enabled: Boolean,
    val error: ExpressionDiag,
    val value: Float,
    val source: String,
) {
    companion object {
        fun decode(bytes: ByteArray?): ExpressionInfo? {
            bytes ?: return null
            val f = String(bytes, Charsets.UTF_8).split(SEP, limit = 8)
            if (f.size < 8) return null
            return ExpressionInfo(
                exists = f[0] == "1",
                enabled = f[1] == "1",
                error = ExpressionDiag.parse(f, 2),
                value = f[6].toFloatOrNull() ?: 0f,
                source = f[7],
            )
        }
    }
}

/** Trilha com expressão na camada (para o "=" das linhas). */
data class ExpressionRow(val key: TrackKey, val enabled: Boolean, val hasError: Boolean) {
    companion object {
        fun decode(a: IntArray?): List<ExpressionRow> {
            a ?: return emptyList()
            return List(a.size / 4) { i ->
                val f = a[i * 4 + 3]
                ExpressionRow(TrackKey(a[i * 4], a[i * 4 + 1], a[i * 4 + 2]), (f and 1) != 0, (f and 2) != 0)
            }
        }
    }
}

/** Como o "=" de uma linha aparece. */
enum class ExpressionLook { None, On, Off, Error }

private const val SEP = ''
