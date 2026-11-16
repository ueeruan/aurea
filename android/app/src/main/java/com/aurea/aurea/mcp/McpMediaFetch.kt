package com.aurea.aurea.mcp

import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.Inet6Address
import java.net.InetAddress
import java.net.Socket
import java.net.URL
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLSocketFactory

/**
 * AureaMCP `import_media_url`: o APP baixa (o Worker nunca busca URL de usuário).
 * Só https, só endereço público (nada de rede local, loopback ou metadados de
 * nuvem), redirecionamentos conferidos um a um, tipo de mídia e tamanho com teto.
 *
 * DNS-rebinding: além de resolver o nome antes, o endereço da CONEXÃO (o que o
 * sistema resolveu de novo ao conectar) é conferido em [AddressCheckSocketFactory],
 * antes do TLS e de qualquer byte do pedido — um nome que trocou para 10.x entre
 * as duas resoluções cai ali. Cancelar a ferramenta para o download ([cancelled]).
 */
object McpMediaFetch {
    const val MAX_BYTES = 500L * 1024 * 1024
    private const val MAX_REDIRECTS = 3

    class Rejected(message: String) : IOException(message)

    /** Endereço que um agente pode mandar o aparelho buscar. */
    fun isPublic(a: InetAddress): Boolean {
        if (a.isAnyLocalAddress || a.isLoopbackAddress || a.isLinkLocalAddress || a.isSiteLocalAddress || a.isMulticastAddress) return false
        val b = a.address
        if (a is Inet6Address) {
            val first = b[0].toInt() and 0xFF
            if (first and 0xFE == 0xFC) return false                       // fc00::/7 (ULA)
            if (b.take(10).all { it.toInt() == 0 } && (b[10].toInt() and 0xFF) == 0xFF && (b[11].toInt() and 0xFF) == 0xFF) {
                return isPublic(InetAddress.getByAddress(b.copyOfRange(12, 16)))   // ::ffff:a.b.c.d
            }
            return true
        }
        val o0 = b[0].toInt() and 0xFF; val o1 = b[1].toInt() and 0xFF
        if (o0 == 0 || o0 >= 224) return false
        if (o0 == 100 && o1 in 64..127) return false                       // CGNAT 100.64/10
        if (o0 == 169 && o1 == 254) return false                           // metadados de nuvem
        if (o0 == 192 && o1 == 0 && (b[2].toInt() and 0xFF) == 0) return false
        if (o0 == 198 && (o1 == 18 || o1 == 19)) return false
        return true
    }

    /** Extensão pela content-type aceita; null = tipo que o Aurea não importa. */
    fun extensionFor(contentType: String?, path: String): String? {
        val t = contentType?.substringBefore(';')?.trim()?.lowercase().orEmpty()
        val fromPath = path.substringAfterLast('/').substringAfterLast('.', "").lowercase().takeIf { it.length in 2..5 }
        return when {
            t.startsWith("video/") -> when (t) { "video/quicktime" -> "mov"; "video/webm" -> "webm"; else -> "mp4" }
            t.startsWith("image/") -> when (t) { "image/png" -> "png"; "image/webp" -> "webp"; "image/gif" -> "gif"; "image/heic" -> "heic"; else -> "jpg" }
            t.startsWith("audio/") -> when (t) { "audio/mpeg" -> "mp3"; "audio/wav", "audio/x-wav" -> "wav"; "audio/aac" -> "aac"; "audio/ogg" -> "ogg"; else -> "m4a" }
            t == "application/octet-stream" && fromPath in MEDIA_EXT -> fromPath
            else -> null
        }
    }

    private val MEDIA_EXT = setOf("mp4", "mov", "m4v", "webm", "mkv", "jpg", "jpeg", "png", "webp", "gif", "heic", "mp3", "wav", "m4a", "aac", "ogg")

    /** Baixa para [dir]; devolve o arquivo. Lança [Rejected] com o motivo para o agente. */
    /**
     * Fábrica TLS que só deixa seguir uma conexão cujo endereço remoto passa em
     * [allowed]. O HttpsURLConnection (Android e JVM do PC) liga o socket TCP e
     * depois o "embrulha" em TLS por `createSocket(socket, host, port, autoClose)`:
     * é ali que o endereço real é conferido. Nome do certificado: o host original.
     */
    class AddressCheckSocketFactory(private val inner: SSLSocketFactory, private val allowed: (InetAddress) -> Boolean) : SSLSocketFactory() {
        private fun check(s: Socket): Socket {
            val a = s.inetAddress
            if (a == null || !allowed(a)) {
                runCatching { s.close() }
                throw Rejected("the host resolved to a private or local address")
            }
            return s
        }
        override fun createSocket(s: Socket, host: String?, port: Int, autoClose: Boolean): Socket { check(s); return inner.createSocket(s, host, port, autoClose) }
        override fun createSocket(host: String?, port: Int): Socket = check(inner.createSocket(host, port))
        override fun createSocket(host: String?, port: Int, localHost: InetAddress?, localPort: Int): Socket = check(inner.createSocket(host, port, localHost, localPort))
        override fun createSocket(host: InetAddress?, port: Int): Socket = check(inner.createSocket(host, port))
        override fun createSocket(address: InetAddress?, port: Int, localAddress: InetAddress?, localPort: Int): Socket =
            check(inner.createSocket(address, port, localAddress, localPort))
        override fun getDefaultCipherSuites(): Array<String> = inner.defaultCipherSuites
        override fun getSupportedCipherSuites(): Array<String> = inner.supportedCipherSuites
    }

    fun download(
        url: String, dir: File, maxBytes: Long = MAX_BYTES,
        resolve: (String) -> Array<InetAddress> = { InetAddress.getAllByName(it) },
        cancelled: () -> Boolean = { false },
    ): File {
        var current = URL(url)
        repeat(MAX_REDIRECTS + 1) {
            if (current.protocol != "https") throw Rejected("only https URLs")
            val addresses = runCatching { resolve(current.host) }.getOrElse { throw Rejected("host not found") }
            if (addresses.isEmpty() || !addresses.all(::isPublic)) throw Rejected("private or local addresses are not allowed")
            val c = (current.openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = false
                connectTimeout = 15_000
                readTimeout = 30_000
                setRequestProperty("User-Agent", "Aurea-MCP")
                if (this is HttpsURLConnection) sslSocketFactory = AddressCheckSocketFactory(HttpsURLConnection.getDefaultSSLSocketFactory(), ::isPublic)
            }
            try {
                val code = c.responseCode
                if (code in 300..399) {
                    val next = c.getHeaderField("Location") ?: throw Rejected("redirect without location")
                    current = URL(current, next)
                    return@repeat
                }
                if (code !in 200..299) throw Rejected("HTTP $code")
                val ext = extensionFor(c.contentType, current.path) ?: throw Rejected("not a video, image or audio file (${c.contentType})")
                val declared = c.contentLengthLong
                if (declared > maxBytes) throw Rejected("file larger than ${maxBytes / 1048576} MB")
                dir.mkdirs()
                val out = File(dir, "mcp_${System.currentTimeMillis()}_${(1000..9999).random()}.$ext")
                var total = 0L
                try {
                    c.inputStream.use { inp ->
                        out.outputStream().use { o ->
                            val buf = ByteArray(64 * 1024)
                            while (true) {
                                val n = inp.read(buf)
                                if (n < 0) break
                                if (cancelled()) throw Rejected("cancelled")
                                total += n
                                if (total > maxBytes) throw Rejected("file larger than ${maxBytes / 1048576} MB")
                                o.write(buf, 0, n)
                            }
                        }
                    }
                } catch (e: Exception) {
                    out.delete(); throw e
                }
                if (total == 0L) { out.delete(); throw Rejected("empty file") }
                return out
            } finally {
                c.disconnect()
            }
        }
        throw Rejected("too many redirects")
    }
}
