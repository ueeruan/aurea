package com.aurea.aurea.state

import com.aurea.aurea.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Beta 2026-10-03: profundidade/chanfro do texto 3D "não mudavam" (o refresh
 * do fim do arrasto devolvia a receita antiga antes do envio atrasado) e o
 * HDRI do Poly Haven dava "erro 17" sem dizer o motivo.
 */
class Scene3DInputsTest {
    @Test
    fun pendingDragValueSurvivesTheEndOfGestureRefresh() {
        // Arrasto da profundidade: 0,25 → 2,0 ainda não enviado; o motor ainda diz 0,25.
        val shown = text3dAfterRefresh(pending = true, pendingLayer = 7L, current = 7L, local = 2.0f) { 0.25f }
        assertEquals(2.0f, shown)
        // Enviado (pendente = falso): o motor volta a mandar.
        assertEquals(2.0f, text3dAfterRefresh(pending = false, pendingLayer = 7L, current = 7L, local = 0.25f) { 2.0f })
    }

    @Test
    fun pendingValueNeverLeaksToAnotherLayer() {
        assertEquals(0.5f, text3dAfterRefresh(pending = true, pendingLayer = 7L, current = 8L, local = 2.0f) { 0.5f })
        assertNull(text3dAfterRefresh(pending = true, pendingLayer = 7L, current = null, local = 2.0f) { null })
        assertEquals(0.5f, text3dAfterRefresh<Float>(pending = true, pendingLayer = 7L, current = 7L, local = null) { 0.5f })
    }

    @Test
    fun polyHavenFilesPassTheExtensionFilter() {
        assertEquals("hdr", hdriExtensionOf("kloofendal_48d_partly_cloudy_puresky_2k.hdr"))
        assertEquals("exr", hdriExtensionOf("kloofendal_48d_partly_cloudy_puresky_1k.EXR"))
        assertEquals("zip", hdriExtensionOf("sky.zip"))
        assertEquals("hdr", hdriExtensionOf(null))   // provedor sem nome: o motor decide pelo conteúdo
        assertNull(hdriExtensionOf("ceu.tiff"))
        assertNull(hdriExtensionOf("sem_extensao"))
    }

    @Test
    fun engineCodesBecomeSpecificMessages() {
        assertEquals(R.string.msg_hdri_err_format, hdriErrorMessage(17L))
        assertEquals(R.string.msg_hdri_err_unreadable, hdriErrorMessage(10L))
        assertEquals(R.string.msg_hdri_err_unreadable, hdriErrorMessage(-1_000L))
        assertEquals(R.string.msg_hdri_err_corrupt, hdriErrorMessage(11L))
        assertEquals(R.string.msg_hdri_err_too_large, hdriErrorMessage(9L))
        assertNull(hdriErrorMessage(27L))
    }
}
