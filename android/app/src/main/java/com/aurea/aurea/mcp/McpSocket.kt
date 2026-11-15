package com.aurea.aurea.mcp

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory

/**
 * AureaMCP: cliente WebSocket (RFC 6455) mínimo, sem biblioteca nova — o app
 * usa `HttpURLConnection` em tudo, e o Aurea PC compila este mesmo Kotlin na JVM.
 * Só o que o relay precisa: texto, ping/pong, fechamento, mensagens fragmentadas.
 *
 * `wss://` com TLS do sistema e conferência do nome do servidor (o SSLSocket
 * cru não confere sozinho). `ws://` só para 127.0.0.1/localhost (testes).
 *
 * Uso: [connect] bloqueia até o 101 e devolve; depois [readLoop] (bloqueante,
 * numa thread de IO) entrega as mensagens até fechar. [sendText] é seguro de
 * qualquer thread.
 */
class McpSocket(
    private val uri: URI,
    private val headers: Map<String, String>,
    private val connectTimeoutMs: Int = 15_000,
    /** Sem NADA chegando por este tempo = conexão morta (o app manda "ping" a cada 25 s). */
    private val readTimeoutMs: Int = 70_000,
) {
    private var socket: Socket? = null
    private var input: InputStream? = null
    private var output: OutputStream? = null
    private val writeLock = Any()
    private val random = SecureRandom()
    @Volatile var closed = false
        private set

    class HandshakeFailure(val status: Int, message: String) : IOException(message)

    fun connect() {
        val secure = when (uri.scheme) {
            "wss" -> true
            "ws" -> if (uri.host == "127.0.0.1" || uri.host == "localhost") false else throw IOException("ws:// only for localhost")
            else -> throw IOException("bad scheme")
        }
        val host = uri.host ?: throw IOException("no host")
        val port = if (uri.port > 0) uri.port else if (secure) 443 else 80
        val raw = Socket().apply { connect(InetSocketAddress(host, port), connectTimeoutMs); soTimeout = readTimeoutMs; tcpNoDelay = true }
        val s: Socket = if (secure) {
            val ssl = (SSLSocketFactory.getDefault() as SSLSocketFactory).createSocket(raw, host, port, true) as SSLSocket
            ssl.startHandshake()
            if (!HttpsURLConnection.getDefaultHostnameVerifier().verify(host, ssl.session)) {
                ssl.close(); throw IOException("TLS hostname mismatch")
            }
            ssl
        } else raw
        socket = s
        val inp = BufferedInputStream(s.getInputStream())
        val out = BufferedOutputStream(s.getOutputStream())
        input = inp; output = out

        val keyBytes = ByteArray(16).also { random.nextBytes(it) }
        val key = Base64.getEncoder().encodeToString(keyBytes)
        val path = (uri.rawPath?.takeIf { it.isNotEmpty() } ?: "/") + (uri.rawQuery?.let { "?$it" } ?: "")
        val req = StringBuilder()
            .append("GET ").append(path).append(" HTTP/1.1\r\n")
            .append("Host: ").append(host).append(if (uri.port > 0) ":${uri.port}" else "").append("\r\n")
            .append("Upgrade: websocket\r\nConnection: Upgrade\r\n")
            .append("Sec-WebSocket-Key: ").append(key).append("\r\n")
            .append("Sec-WebSocket-Version: 13\r\n")
        for ((k, v) in headers) {
            require(k.none { it == '\r' || it == '\n' || it == ':' } && v.none { it == '\r' || it == '\n' }) { "bad header" }
            req.append(k).append(": ").append(v).append("\r\n")
        }
        req.append("\r\n")
        out.write(req.toString().toByteArray(Charsets.UTF_8)); out.flush()

        val head = readHead(inp)
        val lines = head.split("\r\n")
        val status = lines.firstOrNull()?.split(' ')?.getOrNull(1)?.toIntOrNull() ?: 0
        if (status != 101) { close(); throw HandshakeFailure(status, "handshake $status") }
        val accept = lines.drop(1).firstOrNull { it.lowercase().startsWith("sec-websocket-accept:") }?.substringAfter(':')?.trim()
        if (accept != acceptFor(key)) { close(); throw HandshakeFailure(101, "bad accept") }
    }

    /** Lê mensagens até o fechamento. `onText` roda na thread do laço. Devolve o código de fechamento. */
    fun readLoop(onText: (String) -> Unit): Int {
        val inp = input ?: throw IOException("not connected")
        var message: java.io.ByteArrayOutputStream? = null
        var messageOpcode = 0
        try {
            while (!closed) {
                val b0 = inp.read().also { if (it < 0) throw EOFException() }
                val b1 = inp.read().also { if (it < 0) throw EOFException() }
                val fin = b0 and 0x80 != 0
                val opcode = b0 and 0x0F
                val masked = b1 and 0x80 != 0
                var len = (b1 and 0x7F).toLong()
                if (len == 126L) len = ((readByte(inp) shl 8) or readByte(inp)).toLong()
                else if (len == 127L) { len = 0; repeat(8) { len = (len shl 8) or readByte(inp).toLong() } }
                if (len < 0 || len > MAX_MESSAGE) throw IOException("frame too large")
                val mask = if (masked) ByteArray(4).also { readFully(inp, it) } else null
                val payload = ByteArray(len.toInt()).also { readFully(inp, it) }
                if (mask != null) for (i in payload.indices) payload[i] = (payload[i].toInt() xor mask[i and 3].toInt()).toByte()
                when (opcode) {
                    0x0, 0x1, 0x2 -> {
                        if (opcode != 0) { message = java.io.ByteArrayOutputStream(); messageOpcode = opcode }
                        val m = message ?: throw IOException("continuation without start")
                        m.write(payload)
                        if (m.size() > MAX_MESSAGE) throw IOException("message too large")
                        if (fin) {
                            message = null
                            if (messageOpcode == 0x1) onText(String(m.toByteArray(), Charsets.UTF_8))
                        }
                    }
                    0x8 -> {
                        val code = if (payload.size >= 2) ((payload[0].toInt() and 0xFF) shl 8) or (payload[1].toInt() and 0xFF) else 1005
                        runCatching { writeFrame(0x8, payload.copyOf(minOf(2, payload.size))) }
                        close()
                        return code
                    }
                    0x9 -> writeFrame(0xA, payload)
                    0xA -> Unit
                    else -> throw IOException("bad opcode $opcode")
                }
            }
        } finally {
            close()
        }
        return 1006
    }

    fun sendText(text: String) = writeFrame(0x1, text.toByteArray(Charsets.UTF_8))

    /** Fecha educadamente (frame de fechamento) e solta o socket. */
    fun close(code: Int = 1000) {
        if (closed) return
        runCatching { writeFrame(0x8, byteArrayOf((code shr 8).toByte(), code.toByte())) }
        closed = true
        runCatching { socket?.close() }
    }

    private fun writeFrame(opcode: Int, payload: ByteArray) {
        val out = output ?: throw IOException("not connected")
        synchronized(writeLock) {
            if (closed) throw IOException("closed")
            out.write(0x80 or opcode)
            val n = payload.size
            when {
                n < 126 -> out.write(0x80 or n)
                n < 65536 -> { out.write(0x80 or 126); out.write(n shr 8); out.write(n and 0xFF) }
                else -> { out.write(0x80 or 127); for (i in 7 downTo 0) out.write(((n.toLong() shr (8 * i)) and 0xFF).toInt()) }
            }
            val mask = ByteArray(4).also { random.nextBytes(it) }
            out.write(mask)
            val masked = ByteArray(n) { i -> (payload[i].toInt() xor mask[i and 3].toInt()).toByte() }
            out.write(masked)
            out.flush()
        }
    }

    companion object {
        const val MAX_MESSAGE = 4L * 1024 * 1024
        private const val GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

        fun acceptFor(key: String): String =
            Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-1").digest((key + GUID).toByteArray(Charsets.US_ASCII)))

        private fun readByte(inp: InputStream): Int = inp.read().also { if (it < 0) throw EOFException() }

        private fun readFully(inp: InputStream, buf: ByteArray) {
            var off = 0
            while (off < buf.size) {
                val r = inp.read(buf, off, buf.size - off)
                if (r < 0) throw EOFException()
                off += r
            }
        }

        /** Cabeçalho HTTP até a linha em branco (limite de 16 KB). */
        private fun readHead(inp: InputStream): String {
            val b = java.io.ByteArrayOutputStream()
            var last4 = 0
            while (true) {
                val c = inp.read()
                if (c < 0) throw EOFException()
                b.write(c)
                last4 = (last4 shl 8) or c
                if (last4 == 0x0D0A0D0A) break
                if (b.size() > 16 * 1024) throw IOException("header too large")
            }
            return String(b.toByteArray(), Charsets.ISO_8859_1)
        }
    }
}
