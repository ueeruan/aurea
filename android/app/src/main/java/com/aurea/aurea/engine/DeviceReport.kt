package com.aurea.aurea.engine

/**
 * O que o MOTOR decidiu para este aparelho, em números.
 *
 * Existe para a UI poder mostrar a decisão com lastro em vez de um "otimizado"
 * sem número atrás — e para a folha "Novo projeto" não oferecer 4K num aparelho
 * cujo decoder/encoder não dá conta. Os dois lados leem a MESMA decisão: quem
 * decide é o motor, na subida, a partir da sondagem do aparelho.
 */
class DeviceReport(private val v: LongArray) {

    val totalCores: Int get() = v[0].toInt()
    val performanceCores: Int get() = v[1].toInt()
    val efficiencyCores: Int get() = v[2].toInt()

    /** RAM total e disponível do aparelho, em MB. */
    val totalMemoryMb: Int get() = v[3].toInt()
    val availableMemoryMb: Int get() = v[4].toInt()

    /** O teto que o motor se dá para caches, quadros e texturas. */
    val budgetMb: Int get() = v[5].toInt()

    val maxTexture: Int get() = v[6].toInt()
    val maxPreviewWidth: Int get() = v[7].toInt()
    val maxPreviewHeight: Int get() = v[8].toInt()
    val maxExportWidth: Int get() = v[9].toInt()
    val maxExportHeight: Int get() = v[10].toInt()

    val decodeParallelism: Int get() = v[11].toInt()
    val workers: Int get() = v[12].toInt()

    /** Escala inicial do preview que o motor escolheu (PreviewScale). */
    val initialScale: Int get() = v[13].toInt()

    /** Maior resolução de export que este aparelho aguenta, entre as oferecidas. */
    fun exportCeiling(resolutions: List<Int>): Int {
        val cap = if (maxExportHeight in 1 until Int.MAX_VALUE) maxExportHeight else return resolutions.max()
        return resolutions.filter { it <= cap }.maxOrNull() ?: resolutions.min()
    }

    /** "8 núcleos (4+4) · 5,8 GB · orçamento 1,2 GB" — a linha dos Ajustes. */
    fun summary(): String {
        val gb = totalMemoryMb / 1024f
        val budget = budgetMb / 1024f
        val cores = if (efficiencyCores > 0) "$totalCores núcleos ($performanceCores+$efficiencyCores)"
        else "$totalCores núcleos"
        return buildString {
            append(cores)
            append(" · ")
            append("%.1f GB".format(gb))
            append(" · orçamento ")
            append("%.1f GB".format(budget))
        }
    }

    companion object {
        const val SLOTS = 14
    }
}
