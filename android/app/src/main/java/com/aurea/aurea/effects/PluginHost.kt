package com.aurea.aurea.effects

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * 0.3.4 PLUGINS: a E/S que o motor pede para os scripts — rede, área de
 * transferência e seletor de arquivos. Chamado pela JNI (aurea_jni.cpp,
 * `nativeInstallPluginHost`) NA THREAD DO SCRIPT, depois que o motor já
 * conferiu a permissão e o domínio. Nada aqui decide permissão.
 *
 * Rede: HTTPS, sem seguir redirecionamento (o domínio conferido é o contatado),
 * tempo e tamanho que o motor manda. Par do iOS: `PluginHostIO` (PluginsV2.swift).
 */
object PluginHost {
    private val main = Handler(Looper.getMainLooper())

    /** Pedido de arquivo esperando o seletor (a folha do plugin mostra o seletor do sistema). */
    class PickRequest(val types: List<String>) {
        val latch = CountDownLatch(1)
        val result = AtomicReference<Array<String>?>(null)
    }
    var pickRequest by mutableStateOf<PickRequest?>(null)

    /** [ok, status, url, erro, cabeçalhos "k\nv\n...", corpo]. */
    @JvmStatic
    fun fetch(url: String, method: String, headers: Array<String>, body: String, timeoutMs: Int, maxBytes: Int): Array<String> {
        var connection: HttpURLConnection? = null
        return try {
            connection = (URL(url).openConnection() as HttpURLConnection).apply {
                instanceFollowRedirects = false
                connectTimeout = timeoutMs
                readTimeout = timeoutMs
                requestMethod = method
                useCaches = false
                var i = 0
                while (i + 1 < headers.size) { setRequestProperty(headers[i], headers[i + 1]); i += 2 }
                if (body.isNotEmpty() && method != "GET" && method != "HEAD") {
                    doOutput = true
                    outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                }
            }
            val status = connection.responseCode
            val stream = if (status >= 400) connection.errorStream else connection.inputStream
            val bytes = java.io.ByteArrayOutputStream()
            stream?.use { input ->
                val buffer = ByteArray(16 * 1024)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    bytes.write(buffer, 0, n)
                    if (bytes.size() > maxBytes) return arrayOf("0", "0", url, "resposta maior que o limite", "", "")
                }
            }
            val head = StringBuilder()
            connection.headerFields.forEach { (k, v) -> if (k != null) head.append(k).append('\n').append(v.joinToString(", ")).append('\n') }
            arrayOf("1", status.toString(), url, "", head.toString(), bytes.toString(Charsets.UTF_8.name()))
        } catch (e: java.net.SocketTimeoutException) {
            arrayOf("0", "0", url, "tempo esgotado", "", "")
        } catch (e: Exception) {
            arrayOf("0", "0", url, e.javaClass.simpleName + (e.message?.let { ": $it" } ?: ""), "", "")
        } finally {
            connection?.disconnect()
        }
    }

    private fun clipboard(): ClipboardManager? = PluginRuntime.app?.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
    private fun readClipboard(): String? = runCatching {
        val context = PluginRuntime.app ?: return null
        clipboard()?.primaryClip?.getItemAt(0)?.coerceToText(context)?.toString()
    }.getOrNull()

    private fun writeClipboard(text: String): Boolean = runCatching {
        val manager = clipboard() ?: return false
        manager.setPrimaryClip(ClipData.newPlainText("AUREA", text))
        true
    }.getOrDefault(false)

    @JvmStatic
    fun clipboardRead(): String? {
        if (Looper.myLooper() == main.looper) return readClipboard()
        val out = AtomicReference<String?>(null)
        val done = CountDownLatch(1)
        main.post {
            out.set(readClipboard())
            done.countDown()
        }
        done.await(5, TimeUnit.SECONDS)
        return out.get()
    }

    @JvmStatic
    fun clipboardWrite(text: String): Boolean {
        if (Looper.myLooper() == main.looper) return writeClipboard(text)
        val done = CountDownLatch(1)
        val ok = java.util.concurrent.atomic.AtomicBoolean(false)
        main.post {
            ok.set(writeClipboard(text))
            done.countDown()
        }
        return done.await(5, TimeUnit.SECONDS) && ok.get()
    }

    /** [nome, caminho, tamanho] ou nulo (cancelado). Espera a pessoa escolher (até 5 min). */
    @JvmStatic
    fun pickFile(pluginId: String, types: Array<String>): Array<String>? {
        if (Looper.myLooper() == main.looper) return null
        val request = PickRequest(types.toList())
        main.post {
            if (pickRequest == null) pickRequest = request else request.latch.countDown()
        }
        return try {
            if (request.latch.await(5, TimeUnit.MINUTES)) request.result.get() else null
        } finally {
            main.post { if (pickRequest === request) pickRequest = null }
        }
    }

    /** O seletor voltou: copia para o cache do app (o motor lê caminho) e solta o script. */
    fun completePick(request: PickRequest, uri: Uri?) {
        if (pickRequest !== request) return
        val context = PluginRuntime.app
        if (uri == null || context == null) {
            request.latch.countDown()
            pickRequest = null
            return
        }
        Thread {
            runCatching {
                val name = context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                    if (c.moveToFirst()) c.getString(0) else null
                } ?: "arquivo"
                val dir = File(context.cacheDir, "plugin-files").apply { mkdirs() }
                val target = File(dir, name.replace(Regex("[^A-Za-z0-9._-]"), "_"))
                // 0.4.6: 64 MB é o que o script lê (era 1 MB); copia um pouco mais
                // para o motor recusar com a mensagem certa.
                context.contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { out ->
                        val buffer = ByteArray(64 * 1024)
                        var total = 0L
                        while (total <= (64L shl 20) + 1) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            out.write(buffer, 0, n)
                            total += n
                        }
                    }
                }
                request.result.set(arrayOf(name, target.absolutePath, target.length().toString()))
            }
            request.latch.countDown()
        }.start()
        pickRequest = null
    }

    /** Tipos do plugin ("json", "txt", "csv"...) → tipos MIME do seletor. */
    fun mimeTypes(types: List<String>): Array<String> {
        if (types.isEmpty()) return arrayOf("*/*")
        return types.map {
            when (it.trim().removePrefix(".").lowercase()) {
                "json" -> "application/json"
                "txt", "text" -> "text/plain"
                "csv" -> "text/csv"
                "srt" -> "application/x-subrip"
                "xml" -> "text/xml"
                "bin" -> "application/octet-stream"
                else -> "*/*"
            }
        }.distinct().toTypedArray() + "application/octet-stream"
    }
}
