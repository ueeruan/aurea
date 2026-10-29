package com.aurea.aurea.captions

import android.content.Context
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.engine.AureaEngine
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

// =============================================================================
//  0.4.1: legendas automáticas na NUVEM — Whisper large-v3-turbo no Workers AI,
//  pelo servidor do Aurea (discovery/transcribe.js, POST /api/transcribe).
//
//  O motor corta o áudio da camada nas pausas (Engine::export_speech_chunks:
//  WAV 16 kHz mono, trechos de 25–45 s, silêncio fica de fora); cada trecho vai
//  com a sessão da conta e volta como palavras com tempo, que somam o início do
//  trecho. O áudio não é guardado no servidor. Sem conta ou sem rede: falha com
//  [Failure] e quem chama usa o Whisper local. Par do iOS: CloudCaptions.swift.
// =============================================================================

object CloudCaptions {
    class Failure(message: String, val auth: Boolean = false) : IOException(message)

    private fun token(context: Context): String =
        runCatching { KeyVault(context).get("conta_token").orEmpty() }.getOrDefault("")

    /** Pode usar a nuvem agora (há sessão de conta guardada). */
    fun available(context: Context): Boolean = token(context).isNotBlank()

    /**
     * Transcreve a camada [layer]. [progress] recebe 0..100 (corte do áudio até 30,
     * envio dos trechos até 100). [active] falso = cancelar.
     */
    fun transcribe(
        context: Context, engine: AureaEngine, layer: Long, language: String?,
        progress: (Int) -> Unit, active: () -> Boolean,
    ): List<Word> {
        val session = token(context)
        if (session.isBlank()) throw Failure("sem sessao", auth = true)
        val dir = File(context.cacheDir, "cloud-captions").apply { deleteRecursively(); mkdirs() }
        try {
            progress(2)
            val chunks = engine.exportSpeechChunks(layer, dir.absolutePath)
            if (!active()) return emptyList()
            progress(30)
            val lang = language?.takeIf { it.isNotBlank() && it != "auto" }?.lowercase() ?: "auto"
            val out = ArrayList<Word>()
            chunks.forEachIndexed { i, (start, _, path) ->
                if (!active()) return emptyList()
                val file = File(path)
                for (w in send(file, lang, session)) out += Word(w.text, w.start + start, w.end + start)
                file.delete()
                progress(30 + 70 * (i + 1) / chunks.size.coerceAtLeast(1))
            }
            return out
        } finally {
            dir.deleteRecursively()
        }
    }

    private fun send(file: File, lang: String, session: String): List<Word> {
        val bytes = file.readBytes()
        val conn = (URL("${ContaApi.BASE}/api/transcribe?lang=$lang").openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = 15_000
            readTimeout = 120_000
            doOutput = true
            setRequestProperty("Authorization", "Bearer $session")
            setRequestProperty("Content-Type", "audio/wav")
            setFixedLengthStreamingMode(bytes.size)
        }
        try {
            conn.outputStream.use { it.write(bytes) }
            val status = conn.responseCode
            if (status == 401) throw Failure("sessao invalida", auth = true)
            if (status != 200) throw Failure("servidor $status")
            val body = conn.inputStream.use { it.readBytes().toString(Charsets.UTF_8) }
            val words = JSONObject(body).optJSONArray("words") ?: return emptyList()
            return (0 until words.length()).mapNotNull { k ->
                val o = words.optJSONObject(k) ?: return@mapNotNull null
                val t = o.optString("w").trim()
                val s = o.optDouble("s", Double.NaN)
                val e = o.optDouble("e", Double.NaN)
                if (t.isEmpty() || !s.isFinite() || !e.isFinite()) null else Word(t, s, maxOf(s, e))
            }
        } finally {
            conn.disconnect()
        }
    }
}
