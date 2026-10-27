package com.aurea.aurea.state

import org.junit.Assert.*
import org.junit.Test

/** Omega grid (0.3.5): a conta pura do seletor "Controlado por". */
class GridControlTest {
    private val shape = 5
    private val nodes = listOf(
        GridNode(10, GRID_NULL_KIND, "Grade", parentIndex = 3),   // a grade, filha do nulo 40
        GridNode(20, shape, "Forma", parentIndex = 0),            // elemento da grade
        GridNode(30, GRID_NULL_KIND, "Elemento nulo", parentIndex = 0),
        GridNode(40, GRID_NULL_KIND, "Nulo", parentIndex = 4),
        GridNode(50, GRID_NULL_KIND, "Raiz", parentIndex = -1),
    )

    @Test fun currentIsTheGridParentAndListHasEveryOtherNull() {
        val c = gridControlChoices(nodes, grid = 10)
        assertEquals(40L, c.current)
        assertEquals("Nulo", c.currentName)
        // A própria grade fica fora; o nulo que segue a grade fica (o motor recusa o ciclo e a UI avisa).
        assertEquals(listOf(30L, 40L, 50L), c.nulls.map { it.id })
    }

    @Test fun unlinkedGridShowsNone() {
        val c = gridControlChoices(nodes.map { if (it.id == 10L) it.copy(parentIndex = -1) else it }, grid = 10)
        assertEquals(0L, c.current)
        assertNull(c.currentName)
    }

    @Test fun missingGridGivesNoneAndKeepsNulls() {
        val c = gridControlChoices(nodes, grid = 999)
        assertEquals(0L, c.current)
        assertEquals(4, c.nulls.size)
    }

    @Test fun linkCodesMatchTheEngine() {
        // grid::kLink* em engine/include/aurea/timeline/GridLayout.hpp.
        assertEquals(listOf(0, 1, 2, 3, 4, 5),
            listOf(GridLink.OK, GridLink.NOT_GRID, GridLink.NOT_NULL, GridLink.CYCLE, GridLink.LOCKED, GridLink.FAILED))
    }
}
