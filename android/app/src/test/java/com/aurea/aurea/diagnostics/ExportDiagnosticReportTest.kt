package com.aurea.aurea.diagnostics

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 0.5.3 (P1-3): o relato LOCAL do export leva a ficha do aparelho, a escada e o
 * estado do serviço; acima do teto é cortado campo a campo e nunca some calado.
 * (P2) O arquivo só é regravado quando algo muda ou a cada 10 s.
 */
class ExportDiagnosticReportTest {
    private fun device() = JSONObject().put("manufacturer", "samsung").put("model", "SM-A235M").put("androidSdk", 33)
        .put("androidVersion", "13").put("buildId", "TP1A.220624.014").put("hardware", "qcom").put("board", "bengal")
        .put("process64Bit", false).put("abis", JSONArray(listOf("armeabi-v7a", "armeabi")))
        .put("socManufacturer", "QTI").put("socModel", "SM6225").put("gpu", "Adreno (TM) 610 · driver 512.530.0")
        .put("plat", "bengal").put("roHw", "qcom").put("cpuHw", "Qualcomm Technologies, Inc BENGAL")
        .put("lowRam", false).put("memClass", 256)

    private fun memory() = JSONObject().put("javaMaxBytes", 268435456L).put("javaUsedBytes", 41234567L)
        .put("nativeHeapBytes", 512345678L).put("ramTotal", 3_800_000_000L).put("ramAvail", 900_000_000L).put("ramLow", false)

    private fun disk() = JSONObject().put("cacheFree", 5_000_000_000L).put("galleryFree", 5_000_000_000L)
    private fun power() = JSONObject().put("thermal", 2).put("saver", true)
    private fun run() = JSONObject().put("step", 1).put("killed", true).put("fgs", "refused+wake")
        .put("enc", "surface:c2.qti.avc.encoder!stall>buffer:c2.qti.avc.encoder").put("sd", true)

    private fun ladderEntry(level: Int, code: Int) = JSONObject().put("l", level).put("c", code).put("f", 2).put("at", 19)
        .put("of", 38).put("fl", 0x02000012).put("w", if (code == 0) "done" else "failed")

    private fun attempt(token: String, padding: Int = 0) = JSONObject().put("sessionToken", token).put("elapsedMs", 12_345)
        .put("sessionStage", "failed").put("producerStage", "render").put("encoderStage", "codec_open")
        .put("encoder", "c2.qti.avc.encoder").put("backend", "surface").put("safeMode", 1).put("result", 15)
        .put("firstPacketMs", -1).put("requestedColor", "bt709").put("rendered", 19).put("submitted", 19)
        .put("unresolvedSites", "x".repeat(padding))

    private fun compose(attempts: List<JSONObject>, ladder: List<JSONObject>, limit: Int) =
        ExportDiagnosticReport.compose(device(), memory(), disk(), power(), run(), ladder, attempts, limit)

    @Test fun everythingTheTriageNeedsIsInTheLocalReport() {
        val text = compose(listOf(attempt("a"), attempt("b")), listOf(ladderEntry(0, 15), ladderEntry(1, 0)), ExportDiagnosticReport.LIMIT)
        assertTrue(text.length <= ExportDiagnosticReport.LIMIT)
        val r = JSONObject(text)
        assertEquals(1, r.getInt("schema"))
        assertFalse("nada foi cortado", r.has("cut"))
        val d = r.getJSONObject("device")
        assertEquals("Adreno (TM) 610 · driver 512.530.0", d.getString("gpu"))
        assertEquals("bengal", d.getString("plat"))
        assertEquals("Qualcomm Technologies, Inc BENGAL", d.getString("cpuHw"))
        assertFalse(d.getBoolean("process64Bit"))
        assertEquals(256, d.getInt("memClass"))
        assertEquals(900_000_000L, r.getJSONObject("memory").getLong("ramAvail"))
        assertEquals(5_000_000_000L, r.getJSONObject("disk").getLong("cacheFree"))
        assertEquals(2, r.getJSONObject("power").getInt("thermal"))
        assertTrue(r.getJSONObject("power").getBoolean("saver"))
        val run = r.getJSONObject("run")
        assertEquals(1, run.getInt("step"))
        assertEquals("refused+wake", run.getString("fgs"))
        assertTrue(run.getString("enc").startsWith("surface:"))
        // A escada: cada tentativa com degrau, código, motivo e o quadro em que parou.
        val ladder = r.getJSONArray("ladder")
        assertEquals(2, ladder.length())
        assertEquals(15, ladder.getJSONObject(0).getInt("c"))
        assertEquals(19, ladder.getJSONObject(0).getInt("at"))
        assertEquals(1, ladder.getJSONObject(1).getInt("l"))
        assertEquals(2, r.getJSONArray("attempts").length())
    }

    @Test fun oversizedReportIsCutFieldByFieldAndSaysWhatLeft() {
        val attempts = listOf(attempt("a", 1500), attempt("b", 1500), attempt("c", 600))
        val full = compose(attempts, List(12) { ladderEntry(it % 7, 15) }, 100_000)
        for (limit in listOf(3000, 2000, 1200, 700, 300, 120, 40)) {
            assertTrue("o caso tem de passar do teto", full.length > limit)
            val text = ExportDiagnosticReport.fit(JSONObject(full), limit)
            assertTrue("teto $limit: ${text.length}", text.length <= limit)
            val r = JSONObject(text)                      // continua JSON válido
            assertEquals(1, r.getInt("schema"))
            assertTrue("o corte fica registrado", r.getJSONArray("cut").length() > 0)
        }
        // No teto do relato enviado: a última tentativa inteira e a GPU continuam lá.
        val r = JSONObject(ExportDiagnosticReport.fit(JSONObject(full), ExportDiagnosticReport.REPORT_LIMIT))
        val kept = r.getJSONArray("attempts")
        assertEquals("c", kept.getJSONObject(kept.length() - 1).getString("sessionToken"))
        assertEquals(19, kept.getJSONObject(kept.length() - 1).getInt("submitted"))
        assertEquals("Adreno (TM) 610 · driver 512.530.0", r.getJSONObject("device").getString("gpu"))
        assertEquals("SM-A235M", r.getJSONObject("device").getString("model"))
        // As antigas ficam com a fronteira da falha e o codec, sem o resto.
        assertEquals("c2.qti.avc.encoder", kept.getJSONObject(0).getString("encoder"))
        assertFalse(kept.getJSONObject(0).has("unresolvedSites"))
    }

    @Test fun fitNeverChangesWhatAlreadyFitsNorTheObjectItReceives() {
        val small = "{\"schema\":1,\"attempts\":[]}"
        assertEquals(small, ExportDiagnosticReport.fitText(small, 3000))
        val report = JSONObject(compose(listOf(attempt("a", 3000)), emptyList(), 100_000))
        val before = report.toString()
        ExportDiagnosticReport.fit(report, 500)
        assertEquals(before, report.toString())
        // Texto que não é JSON: cortado, nunca uma exceção no envio do relato.
        assertEquals(10, ExportDiagnosticReport.fitText("x".repeat(50), 10).length)
    }

    @Test fun reportBodyStaysWithinWhatTheEndpointAccepts() {
        val local = compose(listOf(attempt("a", 1800), attempt("b", 1800)), List(12) { ladderEntry(it % 7, 15) },
            ExportDiagnosticReport.LIMIT)
        assertTrue(local.length > ExportDiagnosticReport.REPORT_LIMIT)
        assertTrue(local.length <= ExportDiagnosticReport.LIMIT)
        val steps = ProblemReport.stepsWithExportDiagnostics("abri e exportei", "falhou: motivo=2 codigo=15", local)
        assertTrue(steps.length <= ProblemReport.STEPS_MAX)
        assertTrue(steps.contains("[export] falhou: motivo=2 codigo=15"))
        val evidence = steps.substringAfter("[export-diagnostic] ")
        assertTrue(evidence.length <= ExportDiagnosticReport.REPORT_LIMIT)
        assertEquals(1, JSONObject(evidence).getInt("schema"))   // JSON inteiro, não cortado no meio
    }

    @Test fun fileIsRewrittenOnChangeEveryTenSecondsOrWhenForced() {
        assertTrue(ExportDiagnosticReport.shouldWrite(force = true, changed = false, sinceLastWriteMs = 0))
        assertTrue(ExportDiagnosticReport.shouldWrite(force = false, changed = true, sinceLastWriteMs = 0))
        assertFalse(ExportDiagnosticReport.shouldWrite(force = false, changed = false, sinceLastWriteMs = 9_999))
        assertTrue(ExportDiagnosticReport.shouldWrite(force = false, changed = false, sinceLastWriteMs = 10_000))
    }

    @Test fun countersAndClocksAreNotAChangeButStagesAre() {
        val a = attempt("a")
        val later = attempt("a").put("elapsedMs", 99_999).put("rendered", 500).put("submitted", 498)
        assertEquals(ExportDiagnosticReport.signatureOf(a, 1), ExportDiagnosticReport.signatureOf(later, 1))
        val nextStage = attempt("a").put("encoderStage", "drain")
        assertFalse(ExportDiagnosticReport.signatureOf(a, 1) == ExportDiagnosticReport.signatureOf(nextStage, 1))
        assertFalse(ExportDiagnosticReport.signatureOf(a, 1) == ExportDiagnosticReport.signatureOf(attempt("b"), 1))
        assertFalse(ExportDiagnosticReport.signatureOf(a, 1) == ExportDiagnosticReport.signatureOf(a, 2))
        assertFalse(ExportDiagnosticReport.signatureOf(null, 0) == ExportDiagnosticReport.signatureOf(a, 1))
    }

    @Test fun cpuinfoHardwareLineIsReadWhenTheKernelHasOne() {
        val old = "processor\t: 0\nBogoMIPS\t: 26.00\n\nHardware\t: MT6765V/CB\nRevision\t: 0000\n"
        assertEquals("MT6765V/CB", ExportDeviceFacts.cpuinfoHardware(old))
        assertEquals("", ExportDeviceFacts.cpuinfoHardware("processor\t: 0\nFeatures\t: fp asimd\n"))
        assertEquals("", ExportDeviceFacts.cpuinfoHardware(""))
        assertTrue(ExportDeviceFacts.cpuinfoHardware("Hardware : " + "y".repeat(500)).length <= 80)
    }
}
