package com.aurea.aurea.mcp

import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.URI
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/** AureaMCP no app: WebSocket próprio, executor (permissões/confirmação/rotas) e o filtro de URL de mídia. */
class McpTest {

    // --- WebSocket (RFC 6455) contra um servidor de mentira em 127.0.0.1 ----------------

    private fun readHead(inp: InputStream): String {
        val b = ByteArrayOutputStream(); var last = 0
        while (true) { val c = inp.read(); if (c < 0) break; b.write(c); last = (last shl 8) or c; if (last == 0x0D0A0D0A) break }
        return b.toString("ISO-8859-1")
    }

    /** Lê um quadro do cliente (sempre mascarado) e devolve opcode + texto. */
    private fun readClientFrame(inp: InputStream): Pair<Int, ByteArray> {
        val b0 = inp.read(); val b1 = inp.read()
        assertTrue("cliente sempre mascara", b1 and 0x80 != 0)
        var len = b1 and 0x7F
        if (len == 126) len = (inp.read() shl 8) or inp.read()
        val mask = ByteArray(4).also { inp.read(it) }
        val p = ByteArray(len); var off = 0
        while (off < len) off += inp.read(p, off, len - off)
        for (i in p.indices) p[i] = (p[i].toInt() xor mask[i and 3].toInt()).toByte()
        return (b0 and 0x0F) to p
    }

    private fun writeServerFrame(out: OutputStream, opcode: Int, payload: ByteArray, fin: Boolean = true) {
        out.write((if (fin) 0x80 else 0) or opcode)
        if (payload.size < 126) out.write(payload.size) else { out.write(126); out.write(payload.size shr 8); out.write(payload.size and 0xFF) }
        out.write(payload); out.flush()
    }

    @Test
    fun webSocketHandshakeTextPingFragmentsAndClose() {
        val server = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
        val got = mutableListOf<String>()
        var authHeader = ""
        var pongPayload = ByteArray(0)
        val serverDone = CountDownLatch(1)
        thread {
            server.accept().use { s ->
                val inp = s.getInputStream(); val out = s.getOutputStream()
                val head = readHead(inp)
                authHeader = head.lines().first { it.startsWith("Authorization:") }
                val key = head.lines().first { it.startsWith("Sec-WebSocket-Key:") }.substringAfter(':').trim()
                out.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${McpSocket.acceptFor(key)}\r\n\r\n".toByteArray()); out.flush()
                val (op, hello) = readClientFrame(inp)
                assertEquals(1, op)
                writeServerFrame(out, 1, "echo:${String(hello)}".toByteArray())
                writeServerFrame(out, 9, "hb".toByteArray())                    // ping → o cliente responde pong
                val (pongOp, pong) = readClientFrame(inp); assertEquals(10, pongOp); pongPayload = pong
                writeServerFrame(out, 1, "par".toByteArray(), fin = false)        // mensagem em dois pedaços
                writeServerFrame(out, 0, "tes".toByteArray())
                writeServerFrame(out, 1, "x".repeat(300).toByteArray())           // tamanho de 16 bits
                writeServerFrame(out, 8, byteArrayOf(0x03, 0xE8.toByte()))        // fecha 1000
                runCatching { readClientFrame(inp) }
            }
            serverDone.countDown()
        }
        val ws = McpSocket(URI("ws://127.0.0.1:${server.localPort}/api/mcp/app/socket?device=abc"), mapOf("Authorization" to "Bearer t"))
        ws.connect()
        ws.sendText("""{"t":"hello"}""")
        val code = ws.readLoop { got += it }
        assertTrue(serverDone.await(5, TimeUnit.SECONDS))
        server.close()
        assertEquals("Authorization: Bearer t", authHeader)
        assertEquals(listOf("""echo:{"t":"hello"}""", "partes", "x".repeat(300)), got)
        assertArrayEquals("hb".toByteArray(), pongPayload)
        assertEquals(1000, code)
        assertTrue(ws.closed)
    }

    @Test
    fun webSocketRefusesPlainTextOutsideLocalhostAndBadAccept() {
        val e = runCatching { McpSocket(URI("ws://example.com/x"), emptyMap()).connect() }.exceptionOrNull()
        assertTrue(e?.message?.contains("localhost") == true)
        val server = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
        thread { server.accept().use { s -> readHead(s.getInputStream()); s.getOutputStream().write("HTTP/1.1 101 OK\r\nSec-WebSocket-Accept: errado\r\n\r\n".toByteArray()); s.getOutputStream().flush(); Thread.sleep(200) } }
        val bad = runCatching { McpSocket(URI("ws://127.0.0.1:${server.localPort}/"), emptyMap()).connect() }.exceptionOrNull()
        server.close()
        assertTrue(bad is McpSocket.HandshakeFailure)
        assertEquals("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", McpSocket.acceptFor("dGhlIHNhbXBsZSBub25jZQ=="))   // exemplo da RFC 6455
    }

    // --- executor ----------------------------------------------------------------------------

    private class FakeHost : McpHost {
        val calls = mutableListOf<String>()
        private fun r(name: String) = McpReply.ok(name).also { calls += name }
        override suspend fun listProjects(query: String?, limit: Int) = r("list:$query:$limit")
        override suspend fun openProject(id: String) = r("open:$id")
        override suspend fun createProject(title: String, width: Int, height: Int, fps: Double, background: FloatArray?) = r("create:$title:${width}x$height@$fps:${background?.toList()}")
        override suspend fun renameProject(title: String) = r("rename:$title")
        override suspend fun duplicateProject(id: String) = r("dup:$id")
        override suspend fun deleteProject(id: String) = r("delete:$id")
        override suspend fun saveProject() = r("save")
        override suspend fun closeProject() = r("close")
        override suspend fun renderFrame(seconds: Double?, maxSize: Int) = McpReply(true, "frame", byteArrayOf(1, 2, 3)).also { calls += "frame:$seconds:$maxSize" }
        override suspend fun exportVideo(args: JSONObject) = r("export")
        override suspend fun exportStatus() = r("status")
        override suspend fun cancelExport() = r("cancel")
        override suspend fun importMediaUrl(url: String, overlay: Boolean) = r("import:$url:$overlay")
        override suspend fun listDeviceMedia(kind: String, limit: Int, beforeId: String?) = r("gallery:$kind:$limit:$beforeId")
        override suspend fun importDeviceMedia(ids: List<String>, overlay: Boolean, trimUs: LongRange?) =
            r("galleryImport:${ids.joinToString(",")}:$overlay" + (trimUs?.let { ":${it.first}-${it.last}" } ?: ""))
        override fun projectTitle(id: String) = if (id == "a.aurea") "Reel A" else null
        override fun currentProject() = "Reel A" to true
    }

    private class Rig(perms: Map<String, Boolean> = McpCatalog.DEFAULTS, var ask: Boolean = false, var approve: Boolean = true) {
        val host = FakeHost()
        val engine = mutableListOf<Pair<String, String>>()
        val asked = mutableListOf<Triple<String, String, String>>()
        val panels = mutableListOf<String>()
        var changed = 0
        val exec = McpExecutor(
            engineCall = { n, a -> engine += n to a; if (n == "open_panel") """{"ok":true,"ui":"open_panel","panel":"export"}""" else """{"ok":true}""" },
            host = host,
            allowed = { perms[it] == true },
            askEveryEdit = { ask },
            confirm = { t, a, d -> asked += Triple(t, a, d); approve },
            onUi = { panels += it },
            onModelChanged = { changed++ },
        )
        fun run(tool: String, args: String = "{}") = runBlocking { exec.run(tool, JSONObject(args), "Claude") }
    }

    @Test
    fun engineToolsGoToTheEngineAndRefreshTheUi() {
        val rig = Rig()
        assertTrue(rig.run("get_project_state").ok)
        assertEquals(0, rig.changed)
        assertTrue(rig.run("run_ae_script", """{"code":"app.project.activeItem"}""").ok)
        assertEquals("run_ae_script" to """{"code":"app.project.activeItem"}""", rig.engine.last())
        assertEquals(1, rig.changed)
        rig.run("open_panel", """{"panel":"export"}""")
        assertEquals(listOf("export"), rig.panels)
        assertTrue(rig.host.calls.isEmpty())
    }

    @Test
    fun permissionsBlockBeforeAnythingRuns() {
        val rig = Rig(McpCatalog.DEFAULTS + ("edit" to false) + ("export" to false))
        val r = rig.run("run_ae_script", """{"code":"x"}""")
        assertFalse(r.ok)
        assertTrue(r.text.contains("\"edit\""))
        assertFalse(rig.run("export_video").ok)
        assertTrue(rig.engine.isEmpty() && rig.host.calls.isEmpty())
        assertTrue(rig.run("list_projects").ok)
    }

    @Test
    fun deleteAlwaysAsksAndDenialStopsIt() {
        val rig = Rig(approve = false)
        val r = rig.run("delete_project", """{"id":"a.aurea"}""")
        assertFalse(r.ok)
        assertEquals(Triple("delete_project", "Claude", "Reel A"), rig.asked.single())
        assertTrue(rig.host.calls.isEmpty())
        rig.approve = true
        assertTrue(rig.run("delete_project", """{"id":"a.aurea"}""").ok)
        assertEquals("delete:a.aurea", rig.host.calls.single())
    }

    @Test
    fun askEveryEditCoversEditsButNotReads() {
        val rig = Rig(ask = true)
        rig.run("get_project_state"); rig.run("render_frame"); rig.run("export_status")
        assertTrue(rig.asked.isEmpty())
        rig.run("run_ae_script", """{"code":"layer.remove()"}""")
        assertEquals("layer.remove()", rig.asked.single().third)
    }

    @Test
    fun hostArgumentsAreValidated() {
        val rig = Rig()
        assertFalse(rig.run("create_project", """{"title":"","width":1080,"height":1920,"fps":30}""").ok)
        assertFalse(rig.run("create_project", """{"title":"x","width":9,"height":1920,"fps":30}""").ok)
        assertTrue(rig.run("create_project", """{"title":" Reel ","width":1080,"height":1920,"fps":30,"background":[2,0.5,-1]}""").ok)
        assertEquals("create:Reel:1080x1920@30.0:[1.0, 0.5, 0.0]", rig.host.calls.last())
        assertFalse(rig.run("import_media_url", """{"url":"http://x.com/a.mp4"}""").ok)
        assertTrue(rig.run("import_media_url", """{"url":"https://x.com/a.mp4","overlay":true}""").ok)
        val frame = rig.run("render_frame", """{"seconds":1.5,"maxSize":99999}""")
        assertEquals("frame:1.5:1280", rig.host.calls.last())
        val content = frame.toContent()
        assertEquals("image", content.getJSONObject(0).getString("type"))
        assertEquals("AQID", content.getJSONObject(0).getString("data"))
        assertFalse(rig.run("open_project").ok)
        // Ferramenta fora do host vai ao motor, que recusa nome desconhecido ("unknown tool").
        rig.run("no_such_tool_xyz")
        assertEquals("no_such_tool_xyz", rig.engine.last().first)
    }

    @Test
    fun catalogMatchesTheWorker() {
        // O mesmo mapa de discovery/mcp_catalog.js (HOST_TOOLS): nome → capacidade.
        val worker = File(listOf("../../discovery/mcp_catalog.js", "../discovery/mcp_catalog.js").first { File(it).isFile }).readText()
        for ((tool, cap) in McpCatalog.HOST) {
            assertTrue("$tool no Worker", Regex("""name: "$tool", capability: "$cap"""").containsMatchIn(worker))
        }
        assertEquals(setOf("delete_project"), McpCatalog.CONFIRM)
        assertTrue(Regex("""name: "delete_project", capability: "delete", confirm: true""").containsMatchIn(worker))
        assertEquals("read", McpCatalog.capabilityOf("get_project_state"))
        assertEquals("edit", McpCatalog.capabilityOf("some_new_engine_tool"))
    }

    // --- mídia por URL --------------------------------------------------------------------------

    @Test
    fun onlyPublicAddressesAndMediaTypes() {
        fun ip(s: String) = InetAddress.getByName(s)
        for (bad in listOf("127.0.0.1", "10.0.0.5", "192.168.1.1", "172.16.0.1", "169.254.169.254", "100.64.1.1", "0.0.0.0", "::1", "fd00::1", "fe80::1", "::ffff:10.0.0.1", "224.0.0.1")) {
            assertFalse(bad, McpMediaFetch.isPublic(ip(bad)))
        }
        for (good in listOf("8.8.8.8", "104.16.0.1", "2606:4700::1111")) assertTrue(good, McpMediaFetch.isPublic(ip(good)))
        assertEquals("mp4", McpMediaFetch.extensionFor("video/mp4", "/a"))
        assertEquals("mov", McpMediaFetch.extensionFor("video/quicktime; x=1", "/a"))
        assertEquals("png", McpMediaFetch.extensionFor("image/png", "/a"))
        assertEquals("mp3", McpMediaFetch.extensionFor("audio/mpeg", "/a"))
        assertEquals("webm", McpMediaFetch.extensionFor("application/octet-stream", "/clip.webm"))
        assertNull(McpMediaFetch.extensionFor("text/html", "/a.mp4"))
        assertNull(McpMediaFetch.extensionFor("application/octet-stream", "/a.exe"))
        val refused = runCatching { McpMediaFetch.download("https://intranet.example/a.mp4", File("."), resolve = { arrayOf(ip("10.1.2.3")) }) }.exceptionOrNull()
        assertTrue(refused is McpMediaFetch.Rejected)
        assertTrue(runCatching { McpMediaFetch.download("http://example.com/a.mp4", File(".")) }.exceptionOrNull() is McpMediaFetch.Rejected)
    }

    // --- fase C/D -------------------------------------------------------------------------------

    @Test
    fun phaseCReadToolsAreReadsAndMatchTheWorker() {
        val worker = File(listOf("../../discovery/mcp_catalog.js", "../discovery/mcp_catalog.js").first { File(it).isFile }).readText()
        val set = Regex("""export const ENGINE_READ = new Set\(\[([^\]]*)\]""").find(worker)!!.groupValues[1]
        val workerRead = Regex("\"([a-z_]+)\"").findAll(set).map { it.groupValues[1] }.toSet()
        assertEquals(workerRead, McpCatalog.ENGINE_READ)
        for (t in listOf("get_layer_details", "get_keyframes", "get_effect", "list_fonts", "get_markers", "get_work_area")) assertEquals(t, "read", McpCatalog.capabilityOf(t))
        for (t in listOf("set_property", "add_keyframes", "add_effect", "set_text_style", "set_marker", "set_work_area")) assertEquals(t, "edit", McpCatalog.capabilityOf(t))
        // Leitura não manda a interface reler; escrita manda.
        val rig = Rig()
        rig.run("get_layer_details", """{"layer":"1"}"""); assertEquals(0, rig.changed)
        rig.run("set_property", """{"layer":"1","property":"opacity","value":0.5}"""); assertEquals(1, rig.changed)
    }

    @Test
    fun deviceGalleryNeedsItsToggleAndValidIds() {
        val off = Rig()
        assertFalse(off.run("list_device_media").ok)   // começa desligada
        assertTrue(off.host.calls.isEmpty())
        val rig = Rig(McpCatalog.DEFAULTS + ("device_media" to true))
        assertTrue(rig.run("list_device_media", """{"kind":"video","limit":500}""").ok)
        assertEquals("gallery:video:100:null", rig.host.calls.last())
        assertFalse(rig.run("list_device_media", """{"kind":"audio"}""").ok)
        assertTrue(rig.run("import_device_media", """{"ids":["video:12","image:7","video:12"],"overlay":true}""").ok)
        assertEquals("galleryImport:video:12,image:7:true", rig.host.calls.last())
        assertFalse(rig.run("import_device_media", """{"ids":["../etc/passwd"]}""").ok)
        assertFalse(rig.run("import_device_media", """{"ids":[]}""").ok)
        // 1.0.2: corte opcional (só o trecho entra no app).
        assertTrue(rig.run("import_device_media", """{"ids":["video:12"],"trimInSeconds":600,"trimOutSeconds":1200.5}""").ok)
        assertEquals("galleryImport:video:12:false:600000000-1200500000", rig.host.calls.last())
        assertTrue(rig.run("import_device_media", """{"ids":["video:12"],"trimOutSeconds":30}""").ok)
        assertEquals("galleryImport:video:12:false:0-30000000", rig.host.calls.last())
        assertFalse(rig.run("import_device_media", """{"ids":["video:12"],"trimInSeconds":30,"trimOutSeconds":10}""").ok)
        assertFalse(rig.run("import_device_media", """{"ids":["video:12"],"trimInSeconds":-1}""").ok)
        assertEquals("device_media", McpCatalog.capabilityOf("import_device_media"))
    }

    @Test
    fun pairLinkFillsOnlyValidCodes() {
        assertEquals("ABCD-2345", AureaMcp.pairCodeFromLink("aurea://mcp/pair?code=ABCD-2345"))
        assertEquals("ABCD-2345", AureaMcp.pairCodeFromLink("aurea://mcp/pair?code=abcd2345"))
        assertEquals("WXYZ-9876", AureaMcp.pairCodeFromLink("aurea://mcp/pair/?x=1&code=WXYZ%2D9876"))
        assertNull(AureaMcp.pairCodeFromLink("aurea://mcp/pair?code=ABCD-0000"))   // 0 não existe no alfabeto
        assertNull(AureaMcp.pairCodeFromLink("aurea://mcp/other?code=ABCD-2345"))
        assertNull(AureaMcp.pairCodeFromLink("https://mcp/pair?code=ABCD-2345"))
        assertNull(AureaMcp.pairCodeFromLink("aurea://mcp/pair"))
        assertNull(AureaMcp.pairCodeFromLink(null))
        assertNull(AureaMcp.pairCodeFromLink("not a uri ::"))
    }

    @Test
    fun connectedAddressIsCheckedBeforeTls() {
        // DNS-rebinding: o nome passou na 1ª resolução, mas a conexão caiu num endereço local.
        val server = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val raw = java.net.Socket("127.0.0.1", server.localPort)
        val factory = McpMediaFetch.AddressCheckSocketFactory(javax.net.ssl.HttpsURLConnection.getDefaultSSLSocketFactory(), McpMediaFetch::isPublic)
        val e = runCatching { factory.createSocket(raw, "cdn.example.com", 443, true) }.exceptionOrNull()
        assertTrue(e is McpMediaFetch.Rejected)
        assertTrue("o socket local é fechado", raw.isClosed)
        // Endereço aceito: segue para o TLS (o handshake só acontece depois).
        val raw2 = java.net.Socket("127.0.0.1", server.localPort)
        val open = McpMediaFetch.AddressCheckSocketFactory(javax.net.ssl.HttpsURLConnection.getDefaultSSLSocketFactory()) { true }
        val tls = open.createSocket(raw2, "localhost", server.localPort, true)
        assertTrue(tls is javax.net.ssl.SSLSocket)
        tls.close(); server.close()
    }

    @Test
    fun replyContentIsTextAfterImage() {
        val c: JSONArray = McpReply.ok("hi").toContent()
        assertEquals(1, c.length())
        assertEquals("hi", c.getJSONObject(0).getString("text"))
    }
}
