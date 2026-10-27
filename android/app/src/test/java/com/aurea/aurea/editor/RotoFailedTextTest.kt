package com.aurea.aurea.editor

import com.aurea.aurea.R
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Bugs 0.3.4 ("Rotobrush não inicia, mesmo depois de seguir o aviso"): o motor
 * manda o motivo da falha em status[3] (Renderer::roto_status) e o aviso diz o
 * que fazer para cada um. Os mesmos códigos no iOS (`rotoFailedKey`).
 */
class RotoFailedTextTest {
    @Test
    fun eachReasonHasItsOwnNotice() {
        assertEquals(R.string.roto_failed, rotoFailedText(1L))
        assertEquals(R.string.om_roto_failed_model, rotoFailedText(2L))
        assertEquals(R.string.om_roto_failed_decode, rotoFailedText(3L))
        assertEquals(R.string.om_roto_failed_model, rotoFailedText(4L))
        // Código desconhecido (motor mais novo): o aviso geral.
        assertEquals(R.string.roto_failed, rotoFailedText(9L))
    }
}
