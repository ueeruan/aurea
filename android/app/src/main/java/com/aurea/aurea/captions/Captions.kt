package com.aurea.aurea.captions

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.KeyStore
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Uma palavra falada, em segundos da mídia. */
data class Word(val text: String, val start: Double, val end: Double)

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
