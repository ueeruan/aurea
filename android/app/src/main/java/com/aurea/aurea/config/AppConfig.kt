package com.aurea.aurea.config

import android.content.Context
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.hyper.HyperAccess
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.math.BigInteger
import java.security.KeyFactory
import java.security.PublicKey
import java.security.Signature
import java.security.spec.X509EncodedKeySpec

// =============================================================================
//  Configuração remota ASSINADA (0.3.2; servidor: discovery/appconfig.js).
//
//  GET /api/app-config?platform=android devolve {payload, signature}: o app
//  confere a assinatura ECDSA P-256/SHA-256 (r||s cru, base64) sobre os bytes
//  do `payload` com a chave PÚBLICA embutida ([AppConfigKeys]) e só então lê.
//  Recusa: sem assinatura, assinatura errada, outra plataforma, v ≠ 1,
//  vencida, emitida no futuro ou mais antiga que a guardada (replay).
//
//  O que ela decide ([AppConfigPolicy], testado em AppConfigTest):
//    · ATUALIZAÇÃO OBRIGATÓRIA: versionCode < minVersionCode da última config
//      verificada (nova ou guardada — a guardada vale mesmo vencida: a versão
//      mínima não "caduca" ficando offline). Sem nenhuma config: não bloqueia.
//    · AureaHyper: "free" libera para todos SÓ com uma config emitida há ≤ 7
//      dias e não vencida (senão: "conecte-se"); "entitled" = o portão por
//      conta (Entitlements.kt); "off" = bloqueado com o aviso da config.
//    · O aviso opcional (`message`) vira um banner na Home.
//  O par do iOS: AppConfig.swift.
// =============================================================================

object AppConfigKeys {
    /** A chave PÚBLICA (SPKI, base64) de discovery/scripts/gen-config-key.mjs. A privada nunca sai do Worker. */
    const val PUBLIC_KEY_SPKI =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE8cKccQcdlWE04+nNFF7OYcWJqbpQQOFJpG9vRRc4NGUWyGnsPUphLfnDZmg3Z2kblZ0e6W/MTZPhFR6vp+kFhg=="
    const val PLATFORM = "android"
    /** A loja, quando a config não diz para onde ir. */
    const val PLAY_LISTING = "https://play.google.com/store/apps/details?id=com.aurea.editor"   // o app da Play (build "play")
}

enum class HyperMode(val wire: String) {
    Free("free"), Entitled("entitled"), Off("off");
    companion object { fun of(s: String?): HyperMode? = entries.firstOrNull { it.wire == s } }
}

/** A config já verificada. `payload`/`signature` vão para o cache tal como vieram. */
data class AppConfig(
    val platform: String,
    val minVersionCode: Long,
    val latestVersionCode: Long,
    val updateUrl: String,
    val hyperMode: HyperMode,
    val message: Map<String, String>,
    val issuedAt: Long,
    val expiresAt: Long,
    val payload: String,
    val signature: String,
) {
    /** O aviso no idioma do app: o idioma, depois inglês, depois português. */
    fun messageFor(language: String): String? {
        val tag = when (language) { "pt" -> "pt-BR"; "in" -> "id"; else -> language }
        return message[tag] ?: message["en"] ?: message["pt-BR"]
    }
}

object AppConfigVerifier {
    /** Validade máxima que o app aceita numa config (o servidor permite até 30 dias). */
    const val MAX_LIFETIME_S = 31L * 86400
    const val CLOCK_SKEW_S = 86400L

    fun publicKey(spkiBase64: String): PublicKey? = runCatching {
        KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(java.util.Base64.getDecoder().decode(spkiBase64.trim())))
    }.getOrNull()

    /**
     * Confere e lê. `allowExpired` só para o CACHE (a versão mínima continua
     * valendo offline); a resposta nova tem de estar no prazo.
     */
    fun parse(payload: String, signatureB64: String, key: PublicKey?, expectedPlatform: String, nowSeconds: Long,
              allowExpired: Boolean = false): AppConfig? {
        if (key == null || payload.isEmpty() || payload.length > 16 * 1024) return null
        val raw = decodeBase64(signatureB64) ?: return null
        val der = rawToDer(raw) ?: return null
        val ok = runCatching {
            Signature.getInstance("SHA256withECDSA").run {
                initVerify(key)
                update(payload.toByteArray(Charsets.UTF_8))
                verify(der)
            }
        }.getOrDefault(false)
        if (!ok) return null
        return runCatching {
            val o = JSONObject(payload)
            if (o.optInt("v", 0) != 1) return null
            val platform = o.getString("platform")
            if (platform != expectedPlatform) return null
            val issued = o.getLong("issuedAt")
            val expires = o.getLong("expiresAt")
            if (expires <= issued || expires - issued > MAX_LIFETIME_S) return null
            if (issued - nowSeconds > CLOCK_SKEW_S) return null
            if (!allowExpired && nowSeconds >= expires) return null
            val mode = HyperMode.of(o.optJSONObject("hyper")?.optString("mode")) ?: return null
            val messages = HashMap<String, String>()
            o.optJSONObject("message")?.let { m -> m.keys().forEach { k -> m.optString(k).takeIf { it.isNotBlank() }?.let { messages[k] = it } } }
            AppConfig(platform, o.getLong("minVersionCode"), o.optLong("latestVersionCode"), o.optString("updateUrl"), mode,
                messages, issued, expires, payload, signatureB64)
        }.getOrNull()
    }

    private fun decodeBase64(s: String): ByteArray? = runCatching { java.util.Base64.getDecoder().decode(s.trim()) }.getOrNull()

    /** r||s (64 bytes, IEEE P1363, o que o WebCrypto assina) → DER (o que o `SHA256withECDSA` lê). */
    fun rawToDer(raw: ByteArray): ByteArray? {
        if (raw.size != 64) return null
        fun integer(bytes: ByteArray): ByteArray {
            val v = BigInteger(1, bytes).toByteArray()   // já com o 0x00 quando o bit alto vem ligado
            return byteArrayOf(0x02, v.size.toByte()) + v
        }
        val r = integer(raw.copyOfRange(0, 32))
        val s = integer(raw.copyOfRange(32, 64))
        val out = ByteArrayOutputStream()
        out.write(0x30); out.write(r.size + s.size); out.write(r); out.write(s)
        return out.toByteArray()
    }
}

object AppConfigPolicy {
    /** "free" exige uma config emitida há no máximo isto. */
    const val FREE_WINDOW_S = 7L * 86400

    fun updateRequired(config: AppConfig?, versionCode: Long): Boolean = config != null && versionCode < config.minVersionCode

    fun fresh(config: AppConfig?, nowSeconds: Long): Boolean = config != null && nowSeconds < config.expiresAt &&
        nowSeconds - config.issuedAt <= FREE_WINDOW_S && config.issuedAt - nowSeconds <= AppConfigVerifier.CLOCK_SKEW_S

    /** Uma resposta nova só substitui a guardada se não for mais velha (replay de config antiga). */
    fun accept(candidate: AppConfig, cached: AppConfig?): Boolean = cached == null || candidate.issuedAt >= cached.issuedAt

    /** O acesso ao AureaHyper. `entitlement` = o portão por conta; `fetching` = consulta em voo. */
    fun hyperAccess(config: AppConfig?, entitlement: HyperAccess, nowSeconds: Long, fetching: Boolean): HyperAccess {
        if (!fresh(config, nowSeconds)) return if (fetching) HyperAccess.Checking else HyperAccess.NeedsConfig
        return when (config!!.hyperMode) {
            HyperMode.Free -> HyperAccess.Unlocked
            HyperMode.Entitled -> entitlement
            HyperMode.Off -> HyperAccess.Off
        }
    }
}

/** O estado da config para o processo inteiro (MainActivity atualiza; AureaApp, Home e Hyper leem). */
object AppConfigStore {
    private const val PREFS = "aurea_app_config"
    private const val KEY_PAYLOAD = "payload"
    private const val KEY_SIGNATURE = "signature"
    private const val KEY_DISMISSED = "dismissed"
    /** No máximo uma consulta a cada 30 min na volta ao app (a abertura e o Hyper forçam). */
    const val MIN_INTERVAL_MS = 30L * 60 * 1000

    private var appContext: Context? = null
    private val scope by lazy { CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate) }
    private val key by lazy { AppConfigVerifier.publicKey(AppConfigKeys.PUBLIC_KEY_SPKI) }
    private var lastAttempt = 0L
    private var listeners = mutableListOf<() -> Unit>()

    var config by mutableStateOf<AppConfig?>(null)
        private set
    var fetching by mutableStateOf(false)
        private set
    var dismissedMessage by mutableStateOf("")
        private set

    val updateRequired: Boolean get() = AppConfigPolicy.updateRequired(config, BuildConfig.VERSION_CODE.toLong())

    fun now(): Long = System.currentTimeMillis() / 1000

    /** Quem depende da config (o portão do Hyper) recalcula quando ela muda. */
    fun onChange(listener: () -> Unit) { listeners += listener }

    fun init(context: Context) {
        if (appContext != null) return
        appContext = context.applicationContext
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        dismissedMessage = prefs.getString(KEY_DISMISSED, "") ?: ""
        val payload = prefs.getString(KEY_PAYLOAD, null)
        val signature = prefs.getString(KEY_SIGNATURE, null)
        if (payload != null && signature != null) {
            // O cache é reverificado: adulterar o arquivo só faz a config sumir.
            config = AppConfigVerifier.parse(payload, signature, key, AppConfigKeys.PLATFORM, now(), allowExpired = true)
        }
    }

    /** Abertura (`force`) e volta ao app (no máximo a cada 30 min). */
    fun refresh(force: Boolean = false) {
        val t = SystemClock.elapsedRealtime()
        if (!force && lastAttempt != 0L && t - lastAttempt < MIN_INTERVAL_MS) return
        scope.launch { fetch() }
    }

    /** Antes de um job Hyper: consulta agora (até ~10 s) e devolve se a config está em dia. */
    suspend fun refreshNow(): Boolean {
        withTimeoutOrNull(10_000) { fetch() }
        return AppConfigPolicy.fresh(config, now())
    }

    private suspend fun fetch() {
        if (fetching) return
        lastAttempt = SystemClock.elapsedRealtime()
        fetching = true
        notifyChange()
        try {
            val r = withContext(Dispatchers.IO) { ContaApi.appConfig(AppConfigKeys.PLATFORM, BuildConfig.VERSION_CODE.toLong()) }
            if (!r.ok) return
            val payload = r.corpo.optString("payload")
            val signature = r.corpo.optString("signature")
            val fresh = AppConfigVerifier.parse(payload, signature, key, AppConfigKeys.PLATFORM, now()) ?: return
            if (!AppConfigPolicy.accept(fresh, config)) return
            config = fresh
            appContext?.getSharedPreferences(PREFS, Context.MODE_PRIVATE)?.edit()
                ?.putString(KEY_PAYLOAD, payload)?.putString(KEY_SIGNATURE, signature)?.apply()
        } finally {
            fetching = false
            notifyChange()
        }
    }

    /** O banner da Home fecha até o servidor trocar o texto. */
    fun dismissMessage(text: String) {
        dismissedMessage = text
        appContext?.getSharedPreferences(PREFS, Context.MODE_PRIVATE)?.edit()?.putString(KEY_DISMISSED, text)?.apply()
    }

    /** Só os testes instrumentados (não grava no cache, não fala com a rede). */
    @androidx.annotation.VisibleForTesting
    internal fun overrideForTest(value: AppConfig?) {
        config = value
        lastAttempt = SystemClock.elapsedRealtime()
        notifyChange()
    }

    private fun notifyChange() { listeners.forEach { runCatching { it() } } }
}
