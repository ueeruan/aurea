package com.aurea.aurea.captions

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import android.net.Uri
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.nio.ByteBuffer
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Uma palavra falada, em segundos da mídia. */
data class Word(val text: String, val start: Double, val end: Double)

/**
 * Quem transcreve. O editor não depende de nenhum: sem provedor (sem chave),
 * tudo funciona e as legendas podem vir de um SRT.
 */
interface SpeechToTextProvider {
    val name: String
    /** Transcreve o áudio (bloqueante; chamar fora da thread da UI). */
    @Throws(IOException::class)
    fun transcribe(audio: File, language: String?): List<Word>
}

/**
 * Chave da API guardada SÓ no aparelho: cifrada com AES-GCM por uma chave do
 * Android Keystore (não sai do hardware); nas preferências fica só o texto
 * cifrado. Nunca vai para log, projeto ou backup.
 */
class KeyVault(context: Context) {
    private val prefs = context.getSharedPreferences("aurea_cofre", Context.MODE_PRIVATE)

    private fun secret(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build(),
        )
        return gen.generateKey()
    }

    fun has(name: String): Boolean = prefs.contains(name)

    fun put(name: String, value: String) {
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, secret())
        val data = c.iv + c.doFinal(value.toByteArray(Charsets.UTF_8))
        prefs.edit().putString(name, Base64.encodeToString(data, Base64.NO_WRAP)).apply()
    }

    fun get(name: String): String? {
        val b64 = prefs.getString(name, null) ?: return null
        return try {
            val data = Base64.decode(b64, Base64.NO_WRAP)
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.DECRYPT_MODE, secret(), GCMParameterSpec(128, data, 0, 12))
            String(c.doFinal(data, 12, data.size - 12), Charsets.UTF_8)
        } catch (_: Exception) {
            null   // chave do Keystore trocada (restauração): pede de novo
        }
    }

    fun remove(name: String) = prefs.edit().remove(name).apply()

    companion object {
        private const val ALIAS = "aurea_cofre_v1"
        const val GROQ = "groq_api_key"
    }
}

/**
 * Groq (Whisper na nuvem), API compatível com a da OpenAI: multipart com o
 * arquivo, `verbose_json` e tempo por palavra. Só roda quando o usuário toca
 * em "Gerar legendas".
 */
class GroqWhisperProvider(private val apiKey: String, private val model: String = "whisper-large-v3-turbo") : SpeechToTextProvider {
    override val name = "Groq Whisper"

    override fun transcribe(audio: File, language: String?): List<Word> {
        if (audio.length() > MAX_BYTES) throw IOException("Áudio grande demais para a Groq (limite de 25 MB).")
        val boundary = "aurea" + System.nanoTime()
        val conn = URL("https://api.groq.com/openai/v1/audio/transcriptions").openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.connectTimeout = 20_000
        conn.readTimeout = 180_000
        conn.setRequestProperty("Authorization", "Bearer $apiKey")
        conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
        conn.setFixedLengthStreamingMode(multipartLength(boundary, audio, language))
        conn.outputStream.use { out ->
            fun field(name: String, value: String) {
                out.write("--$boundary\r\nContent-Disposition: form-data; name=\"$name\"\r\n\r\n$value\r\n".toByteArray())
            }
            field("model", model)
            field("response_format", "verbose_json")
            field("timestamp_granularities[]", "word")
            field("timestamp_granularities[]", "segment")
            if (!language.isNullOrBlank()) field("language", language)
            out.write(filePartHeader(boundary, audio).toByteArray())
            audio.inputStream().use { it.copyTo(out) }
            out.write("\r\n--$boundary--\r\n".toByteArray())
        }
        val code = conn.responseCode
        val body = (if (code in 200..299) conn.inputStream else conn.errorStream)?.bufferedReader()?.use { it.readText() } ?: ""
        conn.disconnect()
        if (code == 401) throw IOException("A Groq recusou a chave (401). Confira a chave nos Ajustes.")
        if (code == 429) throw IOException("Limite de uso da Groq atingido (429). Tente de novo mais tarde.")
        if (code !in 200..299) {
            val msg = runCatching { JSONObject(body).getJSONObject("error").getString("message") }.getOrNull()
            throw IOException("A Groq respondeu $code${msg?.let { ": $it" } ?: ""}")
        }
        return parseVerbose(JSONObject(body))
    }

    private fun filePartHeader(boundary: String, f: File) =
        "--$boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"${f.name}\"\r\n" +
            "Content-Type: ${if (f.extension == "m4a") "audio/mp4" else "application/octet-stream"}\r\n\r\n"

    private fun multipartLength(boundary: String, f: File, language: String?): Long {
        fun field(name: String, value: String) = "--$boundary\r\nContent-Disposition: form-data; name=\"$name\"\r\n\r\n$value\r\n".toByteArray().size
        var n = field("model", model) + field("response_format", "verbose_json") +
            field("timestamp_granularities[]", "word") + field("timestamp_granularities[]", "segment")
        if (!language.isNullOrBlank()) n += field("language", language)
        return n + filePartHeader(boundary, f).toByteArray().size + f.length() + "\r\n--$boundary--\r\n".toByteArray().size
    }

    companion object {
        const val MAX_BYTES = 25L * 1024 * 1024

        private fun letters(s: String) = s.lowercase().filter { it.isLetterOrDigit() }

        /**
         * As palavras do Whisper vêm sem pontuação; as frases (segments), com.
         * Casa as duas pelo texto dentro do tempo da frase e fica com a grafia
         * da frase ("editar." fecha a legenda no fim da frase).
         */
        private fun punctuate(words: MutableList<Word>, segs: JSONArray) {
            var wi = 0
            for (i in 0 until segs.length()) {
                val s = segs.getJSONObject(i)
                val end = s.optDouble("end")
                val tokens = s.optString("text").trim().split(Regex("\\s+")).filter { letters(it).isNotEmpty() }
                var ti = 0
                while (wi < words.size && words[wi].start < end + 0.05 && ti < tokens.size) {
                    if (letters(words[wi].text) == letters(tokens[ti])) {
                        words[wi] = words[wi].copy(text = tokens[ti])
                        ti++
                    }
                    wi++
                }
            }
        }

        /** `words` do verbose_json; sem eles, as frases divididas pelo tamanho das palavras. */
        fun parseVerbose(o: JSONObject): List<Word> {
            val out = ArrayList<Word>()
            o.optJSONArray("words")?.let { a ->
                for (i in 0 until a.length()) {
                    val w = a.getJSONObject(i)
                    val t = w.optString("word").trim()
                    if (t.isNotEmpty()) out += Word(t, w.optDouble("start"), w.optDouble("end"))
                }
            }
            val segs = o.optJSONArray("segments")
            if (out.isNotEmpty()) {
                if (segs != null) punctuate(out, segs)
                return out
            }
            if (segs == null) return out
            for (i in 0 until segs.length()) {
                val s = segs.getJSONObject(i)
                val ws = s.optString("text").trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
                if (ws.isEmpty()) continue
                val a = s.optDouble("start")
                val b = s.optDouble("end")
                val total = ws.sumOf { it.length + 1 }.toDouble()
                var t = a
                for (w in ws) {
                    val d = (b - a) * (w.length + 1) / total
                    out += Word(w, t, t + d)
                    t += d
                }
            }
            return out
        }
    }
}

/**
 * O som da mídia num .m4a pequeno, sem recodificar (a trilha AAC é copiada
 * como está). Trilha que não cabe em MP4 → erro claro, nada é enviado.
 */
object AudioExtractor {
    @Throws(IOException::class)
    fun extract(context: Context, source: String, out: File): File {
        val ex = MediaExtractor()
        try {
            if (source.startsWith("content://")) ex.setDataSource(context, Uri.parse(source), null) else ex.setDataSource(source)
            var track = -1
            for (i in 0 until ex.trackCount) {
                val mime = ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("audio/")) { track = i; break }
            }
            if (track < 0) throw IOException("Esta mídia não tem som.")
            val format = ex.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: ""
            if (mime != MediaFormat.MIMETYPE_AUDIO_AAC) throw IOException("Formato de som não suportado para transcrição ($mime).")
            ex.selectTrack(track)
            out.parentFile?.mkdirs()
            val mux = MediaMuxer(out.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            val dst = mux.addTrack(format)
            mux.start()
            val buf = ByteBuffer.allocate(1 shl 20)
            val info = MediaCodec.BufferInfo()
            while (true) {
                val n = ex.readSampleData(buf, 0)
                if (n < 0) break
                info.set(0, n, ex.sampleTime, if (ex.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0)
                mux.writeSampleData(dst, buf, info)
                ex.advance()
            }
            mux.stop()
            mux.release()
            return out
        } finally {
            ex.release()
        }
    }
}

/**
 * Transcrições já feitas, por mídia (sha1 do caminho + tamanho): trocar o
 * estilo ou gerar de novo não manda o áudio outra vez.
 */
class TranscriptCache(context: Context) {
    private val dir = File(context.filesDir, "legendas").apply { mkdirs() }

    fun keyFor(source: String, size: Long): String {
        val d = MessageDigest.getInstance("SHA-1").digest("$source|$size".toByteArray())
        return d.joinToString("") { "%02x".format(it) }
    }

    fun load(key: String): List<Word>? {
        val f = File(dir, "$key.json")
        if (!f.exists()) return null
        return runCatching {
            val a = JSONArray(f.readText())
            List(a.length()) { i -> a.getJSONObject(i).let { Word(it.getString("w"), it.getDouble("s"), it.getDouble("e")) } }
        }.getOrNull()
    }

    fun save(key: String, words: List<Word>) {
        val a = JSONArray()
        for (w in words) a.put(JSONObject().put("w", w.text).put("s", w.start).put("e", w.end))
        File(dir, "$key.json").writeText(a.toString())
    }
}
