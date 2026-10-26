package com.aurea.aurea.editor.panels

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega painel (área 04, P1-11): grupos e vagas do editor de curva da
 * referência e o arrasto RELATIVO da alça. Gêmeo dos casos do iOS
 * (`curveCategoryOf`, `curveSlots`, `curveDragHandle` em AnimationTools.swift).
 */
class CurveCategoryTest {
    @Test fun everyCurveBelongsToOneGroup() {
        assertEquals(CurveCategory.Curves, curveCategoryOf(Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)))
        assertEquals(CurveCategory.Curves, curveCategoryOf(Ease(Interp.BEZIER, .42f, 0f, .58f, 1f)))
        assertEquals(CurveCategory.Curves, curveCategoryOf(Ease(Interp.EASE_IN, 0f, 0f, 1f, 1f)))
        assertEquals(CurveCategory.Bounces, curveCategoryOf(DefaultBounce))
        assertEquals(CurveCategory.Bounces, curveCategoryOf(Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f)))
        assertEquals(CurveCategory.Bounces, curveCategoryOf(Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f)))
        assertEquals(CurveCategory.Steps, curveCategoryOf(Ease(Interp.STEPS, 0f, 0f, 1f, 1f)))
        assertEquals(CurveCategory.Steps, curveCategoryOf(Ease(Interp.HOLD, 0f, 0f, 1f, 1f)))
    }

    @Test fun eachGroupHasFourSlotsAndItsFirstSlotIsItsOwn() {
        for (c in CurveCategory.entries) {
            val slots = curveSlots(c)
            assertEquals("slots of $c", 4, slots.size)
            val first = slots.first()
            assertNotNull(first)
            assertEquals(c, curveCategoryOf(first!!.ease))
            // Todas as vagas de um grupo pertencem a ele.
            slots.filterNotNull().forEach { assertEquals(c, curveCategoryOf(it.ease)) }
        }
        // Oscilar e Degraus elásticos esperam as curvas novas do motor (P2-14).
        assertNull(curveSlots(CurveCategory.Bounces)[3])
        assertNull(curveSlots(CurveCategory.Steps)[2])
    }

    @Test fun tappingAGroupAppliesItsFirstSlotOnlyWhenItChanges() {
        val bezier = Ease(Interp.BEZIER, .3f, 0f, .7f, 1f)
        assertEquals(DefaultBounce, curveCategoryEase(CurveCategory.Bounces, bezier))
        assertEquals(Interp.STEPS, curveCategoryEase(CurveCategory.Steps, bezier)?.interp)
        // O grupo que já está ativo não mexe na curva (os parâmetros ficam).
        assertNull(curveCategoryEase(CurveCategory.Curves, bezier))
        assertNull(curveCategoryEase(CurveCategory.Bounces, DefaultBounce.bounce(count = 6)))
    }

    @Test fun onlyNonBezierCurvesLightASlot() {
        val linear = Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)
        assertTrue(curveSlotActive(linear, curveSlots(CurveCategory.Curves)[0]!!.ease))
        val bezier = Ease(Interp.BEZIER, .42f, 0f, 1f, 1f)
        curveSlots(CurveCategory.Curves).filterNotNull().forEach { assertFalse(curveSlotActive(bezier, it.ease)) }
        assertTrue(curveSlotActive(DefaultBounce.bounce(count = 5), curveSlots(CurveCategory.Bounces)[0]!!.ease))
    }

    @Test fun handleDragIsRelativeToWhereItStarted() {
        // 200 × 100 px, faixa 0..1: 50 px à direita = +0,25 em x; 20 px para cima = +0,2 em y.
        val p = dragHandle(.25f, .5f, 50f, -20f, 200f, 100f, 0f, 1f, 0f, overshoot = true)
        assertEquals(.5f, p[0], 1e-5f)
        assertEquals(.7f, p[1], 1e-5f)
        // Sem passar do ponto, y fica em 0..1; com ele, até −2..3.
        assertEquals(1f, dragHandle(.5f, .9f, 0f, -80f, 200f, 100f, 0f, 1f, 0f, overshoot = false)[1], 1e-5f)
        assertEquals(1.7f, dragHandle(.5f, .9f, 0f, -80f, 200f, 100f, 0f, 1f, 0f, overshoot = true)[1], 1e-5f)
        // x nunca sai de 0..1 e encaixa em 0 e 1 perto das bordas.
        assertEquals(1f, dragHandle(.9f, .5f, 400f, 0f, 200f, 100f, 0f, 1f, 0f, overshoot = true)[0], 1e-5f)
        assertEquals(1f, dragHandle(.9f, .5f, 18f, 0f, 200f, 100f, 0f, 1f, 4f, overshoot = true)[0], 1e-5f)
    }
}
