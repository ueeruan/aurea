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

    @JvmStatic
    fun clipboardRead(): String? {
        val out = AtomicReference<String?>(null)
        val done = CountDownLatch(1)
        main.post {
            out.set(runCatching { clipboard()?.primaryClip?.getItemAt(0)?.coerceToText(PluginRuntime.app)?.toString() }.getOrNull())
            done.countDown()
        }
        done.await(5, TimeUnit.SECONDS)
        return out.get()
    }

    @JvmStatic
    fun clipboardWrite(text: String): Boolean {
        val done = CountDownLatch(1)
        var ok = false
        main.post {
            ok = runCatching { clipboard()?.setPrimaryClip(ClipData.newPlainText("AUREA", text)); true }.getOrDefault(false)
            done.countDown()
        }
        done.await(5, TimeUnit.SECONDS)
        return ok
    }

    /** [nome, caminho, tamanho] ou nulo (cancelado). Espera a pessoa escolher (até 5 min). */
    @JvmStatic
    fun pickFile(pluginId: String, types: Array<String>): Array<String>? {
        val request = PickRequest(types.toList())
        main.post { pickRequest = request }
        request.latch.await(5, TimeUnit.MINUTES)
        main.post { if (pickRequest === request) pickRequest = null }
        return request.result.get()
    }

    /** O seletor voltou: copia para o cache do app (o motor lê caminho) e solta o script. */
    fun completePick(request: PickRequest, uri: Uri?) {
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
                // 1 MB é o que o script lê; copia um pouco mais para o motor recusar com a mensagem certa.
                context.contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { out ->
                        val buffer = ByteArray(16 * 1024)
                        var total = 0L
                        while (total <= (1L shl 20) + 1) {
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
            when (it.lowercase()) {
                "json" -> "application/json"
                "txt", "text" -> "text/plain"
                "csv" -> "text/csv"
                "srt" -> "application/x-subrip"
                "xml" -> "text/xml"
                else -> "*/*"
            }
        }.distinct().toTypedArray() + "application/octet-stream"
    }
}
