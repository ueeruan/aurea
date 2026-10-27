package com.aurea.aurea.state

// =============================================================================
// Omega grid (0.3.5): a grade "controlada por" um nulo
//
// A regra mora no motor (Engine::set_grid_control_null): a camada controladora
// da grade vira filha do nulo, com a compensação do vincular, e a grade inteira
// segue o nulo e toda a cadeia de pais dele (nulo → nulo → …, sem teto). Aqui
// ficam só os códigos de resposta e a conta pura do seletor "Controlado por"
// (testável na JVM; o par do iOS é `GridControlChoices` no fim de EffectsView.swift).
// =============================================================================

/** Respostas de `AureaEngine.setGridControlNull` (= `grid::kLink*` do motor). */
object GridLink {
    const val OK = 0
    const val NOT_GRID = 1
    const val NOT_NULL = 2
    const val CYCLE = 3
    const val LOCKED = 4
    const val FAILED = 5
}

/** Uma camada como o seletor precisa: id, tipo (`LayerType.kind`), nome e o índice do pai na lista (−1 = nenhum). */
data class GridNode(val id: Long, val kind: Int, val name: String, val parentIndex: Int)

/** O que o seletor mostra: o pai vigente (0 = Nenhum) e os nulos que podem controlar a grade. */
data class GridControlChoices(val current: Long, val currentName: String?, val nulls: List<GridNode>)

/** Tipo de camada Nulo (`LayerKind::Null`). */
const val GRID_NULL_KIND = 6

/**
 * Os nulos da composição menos a própria grade. Os que seguem a grade (filhos
 * dela) continuam na lista: o motor recusa o vínculo que fecharia o ciclo e a
 * UI explica por quê.
 */
fun gridControlChoices(nodes: List<GridNode>, grid: Long): GridControlChoices {
    val self = nodes.indexOfFirst { it.id == grid }
    val parent = nodes.getOrNull(self)?.parentIndex?.let { nodes.getOrNull(it) }
    return GridControlChoices(
        current = parent?.id ?: 0L,
        currentName = parent?.name,
        nulls = nodes.filter { it.kind == GRID_NULL_KIND && it.id != grid },
    )
}
