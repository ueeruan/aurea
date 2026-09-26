package com.aurea.aurea.captions

import android.app.ActivityManager
import android.content.Context
import android.os.Process
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Only model weights are downloaded. This class never receives user audio. */
object WhisperModels {
    @Synchronized
    fun prepare(context: Context, progress: (String) -> Unit): File {
        val memory = ActivityManager.MemoryInfo().also { (context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(it) }
        val base = Process.is64Bit() && !memory.lowMemory && memory.availMem >= 2L * 1024 * 1024 * 1024
        val name = if (base) "base" else "tiny"
        val size = if (base) 59707625L else 32152673L
        val hash = if (base) "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898" else "818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7"
        val dir = File(context.filesDir, "whisper").apply { mkdirs() }
        val target = File(dir, "ggml-$name-q5_1.bin")
        fun digest(file: File): String {
            val md = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input -> val buffer = ByteArray(65536); while (true) { val n = input.read(buffer); if (n < 0) break; md.update(buffer, 0, n) } }
            return md.digest().joinToString("") { "%02x".format(it) }
        }
        if (target.length() == size && digest(target) == hash) return target
        if (dir.usableSpace < size * 2) throw IOException("Libere espaço para baixar o modelo Whisper ($name).")
        val partial = File(dir, target.name + ".part")
        val connection = URL("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${target.name}").openConnection() as HttpURLConnection
        connection.connectTimeout = 20000; connection.readTimeout = 30000
        try {
            if (connection.responseCode != 200) throw IOException("Não foi possível baixar o modelo Whisper. Conecte à internet na primeira utilização.")
            connection.inputStream.use { input -> partial.outputStream().use { output ->
                val buffer = ByteArray(65536); var received = 0L
                while (true) {
                    if (Thread.currentThread().isInterrupted) throw IOException("Download cancelado")
                    val n = input.read(buffer); if (n < 0) break
                    received += n; if (received > size) throw IOException("Modelo inválido")
                    output.write(buffer, 0, n); progress("Baixando Whisper $name: ${received * 100 / size}%")
                }
            } }
            if (partial.length() != size || digest(partial) != hash) throw IOException("Falha na verificação do modelo Whisper")
            if (!partial.renameTo(target)) throw IOException("Não foi possível guardar o modelo")
            return target
        } finally { connection.disconnect(); partial.delete() }
    }
}
