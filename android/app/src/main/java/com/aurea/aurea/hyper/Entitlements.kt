package com.aurea.aurea.hyper

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.captions.KeyVault
import com.aurea.aurea.conta.ContaApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest

// =============================================================================
//  O PORTÃO do AureaHyper (compra única `aureahyper_lifetime`).
//
//  Hoje não há cobrança: o acesso é concedido por conta Aurea no servidor
//  (discovery/entitlements.js, GET /api/entitlements) e o app só confere. A
//  loja entra depois como outro [EntitlementProvider] (Play Billing:
//  BillingClient.queryPurchasesAsync + verificação no servidor), sem mudar a
//  tela nem o editor — eles só leem [HyperEntitlements.access].
//
//  Regras (EntitlementRules, testadas em EntitlementRulesTest):
//    · nunca libera sem pelo menos UMA confirmação do servidor para esta conta;
//    · a confirmação vale offline por [EntitlementRules.GRACE_MS] (30 dias);
//    · trocar de conta, sair ou excluir a conta apaga o cache;
//    · 401 (sessão inválida) apaga o cache; sem rede/servidor, o cache segue.
//  O par do iOS: HyperModel.swift (HyperEntitlements).
// =============================================================================

/** O resultado de uma consulta. A consulta nunca lança. */
sealed interface EntitlementCheck {
    data class Granted(val products: Set<String>, val checkedAtSeconds: Long) : EntitlementCheck
    /** A sessão não vale mais (401). */
    data object Unauthorized : EntitlementCheck
    /** Sem rede, servidor fora (5xx) ou resposta ilegível: nada muda. */
    data object Unreachable : EntitlementCheck
}

/** Quem diz o que a conta comprou. Hoje o servidor; amanhã a loja também. */
interface EntitlementProvider {
    suspend fun fetch(sessionToken: String): EntitlementCheck
}

/** GET /api/entitlements com o token da sessão Aurea. */
class ServerEntitlementProvider : EntitlementProvider {
    override suspend fun fetch(sessionToken: String): EntitlementCheck = withContext(Dispatchers.IO) {
        val r = ContaApi.entitlements(sessionToken)
        interpret(r.status, r.corpo)
    }

    companion object {
        fun interpret(status: Int, body: JSONObject): EntitlementCheck = when {
            status == 401 -> EntitlementCheck.Unauthorized
            status in 200..299 -> {
                val list = body.optJSONArray("products")
                if (list == null) EntitlementCheck.Unreachable
                else EntitlementCheck.Granted((0 until list.length()).mapNotNull { list.optString(it).ifBlank { null } }.toSet(),
                    body.optLong("checkedAt", 0L))
            }
            else -> EntitlementCheck.Unreachable
        }
    }
}

/** O que ficou da última confirmação do servidor (cifrado no KeyVault). */
data class EntitlementCache(val account: String, val products: Set<String>, val checkedAtMillis: Long) {
    fun toJson(): String = JSONObject().put("account", account).put("products", JSONArray(products.sorted()))
        .put("checkedAt", checkedAtMillis).toString()

    companion object {
        fun parse(raw: String?): EntitlementCache? = runCatching {
            val o = JSONObject(raw ?: return null)
            val list = o.getJSONArray("products")
            EntitlementCache(o.getString("account"), (0 until list.length()).map { list.getString(it) }.toSet(), o.getLong("checkedAt"))
        }.getOrNull()
    }
}

/** O que a tela mostra. */
enum class HyperAccess {
    /** Primeira consulta em andamento (nunca houve confirmação). */
    Checking,
    Unlocked,
    /** O servidor disse que esta conta não tem o recurso. */
    Locked,
    /** Sem conta: entrar é o primeiro passo. */
    SignedOut,
    /** Nunca foi confirmado e não deu para perguntar (sem rede). */
    NeedsCheck,
    /** A última confirmação passou do prazo offline. */
    Expired,
    /** Sem config remota em dia (assinada, emitida há ≤ 7 dias): "conecte-se" (config/AppConfig.kt). */
    NeedsConfig,
    /** A config remota desligou o AureaHyper (mode "off"). */
    Off,
}

object EntitlementRules {
    const val GRACE_MS = 30L * 24 * 3600 * 1000
    /** Relógio atrasado mais que isto em relação à confirmação = não confia. */
    const val CLOCK_SKEW_MS = 24L * 3600 * 1000

    /** A conta como chave do cache (o e-mail não fica em claro no cache). */
    fun accountKey(email: String): String {
        val bytes = MessageDigest.getInstance("SHA-256").digest(email.trim().lowercase().toByteArray(Charsets.UTF_8))
        return bytes.take(16).joinToString("") { "%02x".format(it) }
    }

    /**
     * O acesso a [product] para a conta [email] agora. `checking` = há uma
     * consulta em voo (só muda o caso "nunca confirmado").
     */
    fun access(cache: EntitlementCache?, email: String?, product: String, nowMillis: Long, checking: Boolean): HyperAccess {
        if (email == null) return HyperAccess.SignedOut
        val mine = cache?.takeIf { it.account == accountKey(email) }
            ?: return if (checking) HyperAccess.Checking else HyperAccess.NeedsCheck
        val age = nowMillis - mine.checkedAtMillis
        if (age < -CLOCK_SKEW_MS || age > GRACE_MS) return if (checking) HyperAccess.Checking else HyperAccess.Expired
        return if (product in mine.products) HyperAccess.Unlocked else HyperAccess.Locked
    }
}

/**
 * O estado do portão para o processo inteiro (Home, editor, tela Hyper). A
 * conta (ContaViewModel) chama [refresh] ao abrir o app e ao entrar, e
 * [clear] ao sair/excluir.
 */
object HyperEntitlements {
    const val PRODUCT = "aureahyper_lifetime"   // hyper::kProductId
    private const val KEY_CACHE = "hyper_entitlements"

    private var vault: KeyVault? = null
    private var provider: EntitlementProvider = ServerEntitlementProvider()
    private val scope by lazy { CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate) }
    private var cache: EntitlementCache? = null
    private var email: String? = null
    private var inFlight = 0

    var access by mutableStateOf(HyperAccess.NeedsConfig)
        private set
    /** Só o portão por conta (o que vale no modo "entitled" da config remota). */
    var entitlementAccess by mutableStateOf(HyperAccess.SignedOut)
        private set

    val unlocked: Boolean get() = access == HyperAccess.Unlocked

    /** Testes e, no futuro, a loja: troca quem responde. */
    fun useProvider(p: EntitlementProvider) { provider = p }

    fun init(context: Context) {
        if (vault != null) return
        com.aurea.aurea.config.AppConfigStore.init(context)
        com.aurea.aurea.config.AppConfigStore.onChange { recompute() }
        vault = runCatching { KeyVault(context.applicationContext) }.getOrNull()
        cache = EntitlementCache.parse(runCatching { vault?.get(KEY_CACHE) }.getOrNull())
        recompute()
    }

    /** Pergunta ao servidor (sem sessão = fora). Idempotente; pode chamar à vontade. */
    fun refresh(sessionToken: String?, accountEmail: String?) {
        email = accountEmail
        if (sessionToken == null || accountEmail == null) { recompute(); return }
        inFlight++
        recompute()
        scope.launch {
            val result = runCatching { provider.fetch(sessionToken) }.getOrDefault(EntitlementCheck.Unreachable)
            inFlight = (inFlight - 1).coerceAtLeast(0)
            if (email != accountEmail) { recompute(); return@launch }   // trocou de conta no meio
            when (result) {
                is EntitlementCheck.Granted -> store(EntitlementCache(EntitlementRules.accountKey(accountEmail), result.products,
                    System.currentTimeMillis()))
                EntitlementCheck.Unauthorized -> store(null)
                EntitlementCheck.Unreachable -> Unit
            }
            recompute()
        }
    }

    /** Saiu ou excluiu a conta. */
    fun clear() {
        email = null
        store(null)
        recompute()
    }

    private fun store(value: EntitlementCache?) {
        cache = value
        runCatching { if (value == null) vault?.remove(KEY_CACHE) else vault?.put(KEY_CACHE, value.toJson()) }
    }

    private fun recompute() {
        entitlementAccess = EntitlementRules.access(cache, email, PRODUCT, System.currentTimeMillis(), inFlight > 0)
        // A config remota manda: "free" libera, "entitled" usa o portão acima, "off" bloqueia.
        val store = com.aurea.aurea.config.AppConfigStore
        access = com.aurea.aurea.config.AppConfigPolicy.hyperAccess(store.config, entitlementAccess, store.now(), store.fetching)
    }

    /** A config remota mudou (ou o relógio passou do prazo): recalcula. */
    fun recheck() = recompute()
}
