package com.aurea.aurea.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * O relatório do aparelho do lado da UI (§109): as frases saem dos MESMOS slots
 * que o motor escreve (`write_device_report`, testado no host em
 * test_device.cpp). Aqui: o aparelho de entrada sintético de lá, slot a slot.
 */
class DeviceReportTest {

    /** O perfil LOW de test_device.cpp: 3 GB, Mali-T830, H.264 1080p, sem HEVC. */
    private fun lowSlots(): LongArray = LongArray(DeviceReport.SLOTS).also { v ->
        v[0] = 8; v[1] = 4; v[2] = 4; v[3] = 2800; v[4] = 900; v[5] = 225
        v[6] = 8192; v[7] = 1920; v[8] = 1080; v[9] = 1920; v[10] = 1080
        v[11] = 1; v[12] = 2; v[13] = 3
        v[14] = 0; v[15] = 1                       // LOW, pela memória
        v[20] = 1L or 2L or 16L or 128L            // codecs medidos, H.264 hw (dec/enc); sem HEVC
        v[21] = 2                                  // teto pelo codificador
        v[24] = 50; v[25] = 4; v[26] = 160; v[27] = 16; v[28] = 0; v[29] = 1024; v[30] = 720; v[31] = 1800
    }

    @Test
    fun lowDeviceSaysWhatItCannotDo() {
        val r = DeviceReport(lowSlots())
        assertEquals("Aparelho de entrada", r.tierLabel())
        assertTrue(r.tierReason()!!.contains("memória"))
        assertFalse(r.exports(2160))
        assertTrue(r.exports(1080))
        assertEquals(1080, r.exportCeiling(listOf(720, 1080, 1440, 2160)))
        val export = r.exportLimitReason()!!
        assertTrue(export, export.contains("1080p") && export.contains("codificador") && export.contains("1920 × 1080"))
        assertFalse(r.hevcExportAvailable)
        assertTrue(r.hevcExportReason()!!.startsWith("HEVC indisponível"))
        val lines = r.limitations()
        assertTrue(lines.any { it.contains("HEVC") && it.contains("software") })
        assertTrue(lines.any { it.contains("1/4") })
        assertTrue(r.planSummary().contains("1/4"))
    }

    @Test
    fun unmeasuredCodecsHideNothing() {
        // Sem tabela de codecs (sondagem falhou): nada é marcado indisponível.
        val v = lowSlots()
        v[20] = 0
        v[21] = 0
        v[10] = 2160; v[9] = 3840
        val r = DeviceReport(v)
        assertTrue(r.hevcExportAvailable)
        assertNull(r.hevcExportReason())
        assertNull(r.exportLimitReason())
        assertTrue(r.exports(2160))
    }

    @Test
    fun hotPreviewAnnouncesTheLighterFlow() {
        val v = lowSlots()
        v[24] = 12; v[28] = 3
        val r = DeviceReport(v)
        assertTrue(r.limitations().any { it.startsWith("Optical flow de alta qualidade indisponível") })
        assertTrue(r.thermalLabel().contains("muito quente"))
    }
}
