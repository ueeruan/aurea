package com.aurea.aurea.diagnostics

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream

class CrashReportTest {
    private val base = CrashReport(
        reportId = "exit-1790000000000-4242", appVersion = "2.0.0-beta2", appBuild = "2124", os = "Android",
        osVersion = "14 (SDK 34)", deviceModel = "SM-A515F", manufacturer = "samsung", abi = "arm64-v8a",
        reason = "CRASH_NATIVE", phase = "Aurea build=2124 phase=VIDEO_NATIVE", timestamp = 1_790_000_000_000L,
        stack = "signal 11", stackTruncated = false,
    )

    @Test fun jsonDaCaixaIdaEVoltaEEnvioSegueOContratoDoWorker() {
        assertEquals(base, CrashReport.fromJson(JSONObject(base.toJson().toString())))
        val envio = base.paraEnvio("3f2b8c1e-9d4a-4e6f-8a7b-1c2d3e4f5a6b", "dona@aurea.app")
        for (campo in listOf("reportId", "installId", "platform", "appVersion", "appBuild", "os", "osVersion",
            "deviceModel", "manufacturer", "abi", "reason", "phase", "timestamp", "stack", "email")) {
            assertTrue(campo, envio.has(campo))
        }
        assertEquals("android", envio.getString("platform"))
        assertFalse("sem conta, sem e-mail", base.paraEnvio("3f2b8c1e-9d4a-4e6f-8a7b-1c2d3e4f5a6b", null).has("email"))
        assertFalse("a caixa de saida nao guarda e-mail", base.toJson().has("email"))
        assertNull(CrashReport.fromJson(base.toJson().put("reportId", "x")))
        assertNull(CrashReport.fromJson(JSONObject("{}")))
    }

    @Test fun soMotivosDeFalhaViramRelatorio() {
        assertEquals("CRASH", CrashReport.motivoReportavel(4, 100))
        assertEquals("CRASH_NATIVE", CrashReport.motivoReportavel(5, 400))
        assertEquals("ANR", CrashReport.motivoReportavel(6, 100))
        assertEquals("EXCESSIVE_RESOURCE_USAGE", CrashReport.motivoReportavel(9, 400))
        assertEquals("LOW_MEMORY", CrashReport.motivoReportavel(3, 100))
        assertEquals("LOW_MEMORY", CrashReport.motivoReportavel(3, 200))
        assertNull("processo em cache morto no fundo e rotina", CrashReport.motivoReportavel(3, 400))
        for (normal in listOf(0, 1, 2, 10, 11, 13, 16)) assertNull(normal.toString(), CrashReport.motivoReportavel(normal, 100))
    }

    @Test fun cadaCrashEColetadoUmaVezEORegistroTemTeto() {
        val l = CrashLedger.ler(null)
        l.marcar("exit-1-1")
        l.marcar("exit-1-1")
        assertEquals(1, l.tamanho)
        assertTrue(l.contem("exit-1-1"))
        repeat(CrashLedger.MAX + 10) { l.marcar("exit-x-$it") }
        assertEquals(CrashLedger.MAX, l.tamanho)
        assertFalse("o mais antigo sai primeiro", l.contem("exit-1-1"))
        val volta = CrashLedger.ler(l.serializar())
        assertTrue(volta.contem("exit-x-${CrashLedger.MAX + 9}"))
        assertEquals(0, CrashLedger.ler("lixo{").tamanho)
    }

    @Test fun caminhosDoUsuarioSaemBibliotecasEPilhaFicam() {
        val entrada = """
            java.io.FileNotFoundException: /storage/emulated/0/DCIM/Minhas Férias.mp4: open failed: ENOENT (No such file or directory)
            uri=content://media/external/video/media/42 e file:///sdcard/x.aurea
            projeto /data/user/0/com.aurea.aurea/files/projects/Cliente Secreto.aurea
            	at com.aurea.aurea.state.EditorStore.importVideo(EditorStore.kt:2336)
                  #00 pc 0000000000123456  /data/app/~~ab==/com.aurea.aurea-cd==/lib/arm64/libaurea.so (aurea::decode+12)
            /private/var/mobile/Containers/Data/Application/ABC/Documents/Projeto X.aurea
        """.trimIndent()
        val s = CrashTexto.sanitizar(entrada)
        for (vazou in listOf("Minhas Férias", "media/external", "x.aurea", "Cliente Secreto", "Projeto X")) {
            assertFalse(vazou, s.contains(vazou))
        }
        assertTrue(s.contains("/storage/<removido>: open failed: ENOENT"))
        assertTrue(s.contains("content://<removido>"))
        assertTrue(s.contains("EditorStore.kt:2336"))
        assertTrue(s.contains("/data/app/~~ab==/com.aurea.aurea-cd==/lib/arm64/libaurea.so (aurea::decode+12)"))
    }

    @Test fun corteEmBytesNaoParteCaractere() {
        val (texto, cortou) = CrashTexto.truncarUtf8("é".repeat(10), 5)
        assertTrue(cortou)
        assertEquals("éé", texto)
        assertEquals("abc" to false, CrashTexto.truncarUtf8("abc", 3))
        val tomb = "backtrace:\n#00 pc 1\n--------- tail end of log main\n01-01 segredo\n"
        assertFalse(CrashTexto.semLogDoTombstone(tomb).contains("segredo"))
        assertTrue(CrashTexto.semLogDoTombstone(tomb).contains("#00 pc 1"))
    }

    // --- tombstone.proto de teste --------------------------------------------
    private class Pb {
        val out = ByteArrayOutputStream()
        fun varint(v: Long): Pb { var x = v; while (true) { if ((x and 0x7FL.inv()) == 0L) { out.write(x.toInt()); return this }; out.write(((x and 0x7F) or 0x80).toInt()); x = x ushr 7 } }
        fun int(campo: Int, v: Long) = varint((campo.toLong() shl 3) or 0).varint(v)
        fun bytes(campo: Int, b: ByteArray): Pb { varint((campo.toLong() shl 3) or 2).varint(b.size.toLong()); out.write(b); return this }
        fun str(campo: Int, s: String) = bytes(campo, s.toByteArray())
        fun msg(campo: Int, m: Pb) = bytes(campo, m.out.toByteArray())
        fun fixo64(campo: Int): Pb { varint((campo.toLong() shl 3) or 1); out.write(ByteArray(8)); return this }
        fun bytes() = out.toByteArray()
    }

    private fun frame(pc: Long, fn: String, off: Long) = Pb().int(1, pc).int(2, pc + 0x7000).int(3, 99).str(4, fn).int(5, off)
        .str(6, "/data/app/x/lib/arm64/libaurea.so").int(7, 0).str(8, "abcd1234")

    private fun tombstone(codigo: Long = 1): ByteArray {
        val sinal = Pb().int(1, 11).str(2, "SIGSEGV").int(3, codigo).str(4, "SEGV_MAPERR").int(8, 1).int(9, 0x10)
        val render = Pb().int(1, 1240).str(2, "RenderThread").msg(3, Pb().str(1, "x0").int(2, 5))
            .msg(4, frame(0x1234, "aurea::render", 16)).msg(4, frame(0x5678, "", 0)).str(7, "nota do unwinder")
        val main = Pb().int(1, 1234).str(2, "com.aurea.aurea").msg(4, frame(0x9999, "main", 1))
        return Pb().int(1, 3).str(2, "samsung/a51/a51:14/UP1A/1:user/release-keys").str(3, "0").str(4, "2026-09-27")
            .int(5, 1234).int(6, 1240).int(7, 10123).str(8, "u:r:untrusted_app").str(9, "com.aurea.aurea")
            .msg(10, sinal).str(14, "boom").msg(15, Pb().str(1, "null pointer dereference"))
            .msg(16, Pb().int(1, 1234).msg(2, main)).msg(16, Pb().int(1, 1240).msg(2, render))
            .msg(17, Pb().int(1, 0).str(7, "/data/app/x/lib/arm64/libaurea.so")).fixo64(99)
            .msg(18, Pb().str(1, "main").msg(2, Pb().str(6, "log com dado do app"))).int(20, 42).bytes()
    }

    @Test fun tombstoneProtobufViraTextoDoLogcatSemLog() {
        val texto = TombstoneTexto.decodificar(tombstone())
        assertNotNull(texto)
        texto!!
        assertTrue(texto, texto.contains("Build fingerprint: 'samsung/a51/a51:14/UP1A/1:user/release-keys'"))
        assertTrue(texto, texto.contains("pid: 1234, tid: 1240, name: RenderThread  >>> com.aurea.aurea <<<"))
        assertTrue(texto, texto.contains("signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000000000000010"))
        assertTrue(texto, texto.contains("Abort message: 'boom'"))
        assertTrue(texto, texto.contains("Cause: null pointer dereference"))
        assertTrue(texto, texto.contains("      #00 pc 0000000000001234  /data/app/x/lib/arm64/libaurea.so (aurea::render+16) (BuildId: abcd1234)"))
        assertTrue(texto, texto.contains("      #01 pc 0000000000005678  /data/app/x/lib/arm64/libaurea.so (BuildId: abcd1234)"))
        assertTrue(texto, texto.contains("NOTE: nota do unwinder"))
        assertFalse("so a thread que caiu", texto.contains("0000000000009999"))
        assertFalse("log nunca entra", texto.contains("log com dado do app"))
    }

    @Test fun codigoNegativoEProtobufQuebradoNaoDerrubam() {
        assertTrue(TombstoneTexto.decodificar(tombstone(codigo = -6))!!.contains("code -6 (SEGV_MAPERR)"))
        val inteiro = tombstone()
        assertNull(TombstoneTexto.decodificar(inteiro.copyOf(inteiro.size - 5)))
        assertNull(TombstoneTexto.decodificar(byteArrayOf(0x0B, 0x01)))   // tipo de grupo: nao suportado
        assertNotNull(TombstoneTexto.decodificar(ByteArray(0)))
    }
}
