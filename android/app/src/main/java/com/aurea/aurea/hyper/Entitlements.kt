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
//  O PORTÃO do conteúdo pago (AureaHyper, efeitos pagos, Texto 3D).
//
//  16/11/2026 (decisão do dono): ASSINATURAS. O AureaHyper (1.0.9: semanal,
//  mensal ou anual) libera tudo; os efeitos pagos e o Texto 3D tiveram a sua
//  assinatura avulsa (fora de venda, reconhecida para quem tem); o legado `aureahyper_lifetime` (concedido pelo dono)
//  segue liberando tudo. A Play cobra (hyper/Subscriptions.kt), o servidor
//  confere com a loja e diz o que a conta tem AGORA (GET /api/entitlements:
//  produtos + vencimento de cada assinatura). QUAL produto libera QUAL item é
//  regra do motor (hyper::has_access, via AureaEngine.nativeHasPaidAccess): o
//  app só guarda o conjunto de produtos da conta e pergunta.
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
    /**
     * [expiresSeconds]: vencimento (segundos Unix) de cada assinatura; produto
     * fora do mapa não vence. [purchasable]: o que o servidor aceita vender agora
     * (vazio no beta fechado ou sem as chaves da loja).
     */
    data class Granted(val products: Set<String>, val checkedAtSeconds: Long,
                       val expiresSeconds: Map<String, Long> = emptyMap(),
                       val purchasable: Set<String> = emptySet()) : EntitlementCheck
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
                    body.optLong("checkedAt", 0L), expiresOf(body), stringSet(body.optJSONArray("purchasable")))
            }
            else -> EntitlementCheck.Unreachable
        }

        /** {"expires": {"aurea_fx_twitch": 1800000000}} → produto → segundos Unix (valores inválidos ficam de fora). */
        fun expiresOf(body: JSONObject): Map<String, Long> {
            val o = body.optJSONObject("expires") ?: return emptyMap()
            return o.keys().asSequence().mapNotNull { k -> o.optLong(k, -1L).takeIf { it > 0 }?.let { k to it } }.toMap()
        }

        fun stringSet(a: JSONArray?): Set<String> =
            if (a == null) emptySet() else (0 until a.length()).mapNotNull { a.optString(it).ifBlank { null } }.toSet()
    }
}

/**
 * O que ficou da última confirmação do servidor (cifrado no KeyVault).
 * [expiresMillis]: vencimento de cada assinatura (o resto não vence).
 */
data class EntitlementCache(val account: String, val products: Set<String>, val checkedAtMillis: Long,
                            val expiresMillis: Map<String, Long> = emptyMap()) {
    /** Os produtos que valem em [nowMillis]: uma assinatura vencida (com a folga) não conta, mesmo offline. */
    fun owned(nowMillis: Long): Set<String> =
        products.filterTo(mutableSetOf()) { p -> expiresMillis[p]?.let { it + EntitlementRules.SUBSCRIPTION_SLACK_MS > nowMillis } ?: true }

    fun toJson(): String = JSONObject().put("account", account).put("products", JSONArray(products.sorted()))
        .put("checkedAt", checkedAtMillis)
        .apply { if (expiresMillis.isNotEmpty()) put("expires", JSONObject(expiresMillis.toSortedMap() as Map<*, *>)) }
        .toString()

    companion object {
        fun parse(raw: String?): EntitlementCache? = runCatching {
            val o = JSONObject(raw ?: return null)
            val list = o.getJSONArray("products")
            val exp = o.optJSONObject("expires")
            EntitlementCache(o.getString("account"), (0 until list.length()).map { list.getString(it) }.toSet(), o.getLong("checkedAt"),
                exp?.keys()?.asSequence()?.associateWith { exp.getLong(it) } ?: emptyMap())
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
    /**
     * Uma assinatura continua valendo até 1 dia depois do vencimento guardado: a
     * renovação da loja acontece no vencimento e o app só pergunta ao abrir/entrar.
     */
    const val SUBSCRIPTION_SLACK_MS = 24L * 3600 * 1000

    /** A conta como chave do cache (o e-mail não fica em claro no cache). */
    fun accountKey(email: String): String {
        val bytes = MessageDigest.getInstance("SHA-256").digest(email.trim().lowercase().toByteArray(Charsets.UTF_8))
        return bytes.take(16).joinToString("") { "%02x".format(it) }
    }

    /**
     * O acesso a [product] para a conta [email] agora. `checking` = há uma
     * consulta em voo (só muda o caso "nunca confirmado").
     */
    fun access(cache: EntitlementCache?, email: String?, product: String, nowMillis: Long, checking: Boolean): HyperAccess =
        access(cache, email, nowMillis, checking) { product in it }

    /**
     * O acesso a um item pago: [granted] recebe os produtos que valem agora e diz
     * se eles liberam o item (a regra do motor, hyper::has_access).
     */
    fun access(cache: EntitlementCache?, email: String?, nowMillis: Long, checking: Boolean,
               granted: (Set<String>) -> Boolean): HyperAccess {
        if (email == null) return HyperAccess.SignedOut
        val mine = cache?.takeIf { it.account == accountKey(email) }
            ?: return if (checking) HyperAccess.Checking else HyperAccess.NeedsCheck
        val age = nowMillis - mine.checkedAtMillis
        if (age < -CLOCK_SKEW_MS || age > GRACE_MS) return if (checking) HyperAccess.Checking else HyperAccess.Expired
        return if (granted(mine.owned(nowMillis))) HyperAccess.Unlocked else HyperAccess.Locked
    }
}

/**
 * O estado do portão para o processo inteiro (Home, editor, tela Hyper). A
 * conta (ContaViewModel) chama [refresh] ao abrir o app e ao entrar, e
 * [clear] ao sair/excluir.
 */
object HyperEntitlements {
    /** O legado vitalício (hyper::kProductId), concedido pelo dono; libera tudo como o AureaHyper mensal. */
    const val PRODUCT = "aureahyper_lifetime"   // hyper::kProductId
    private const val KEY_CACHE = "hyper_entitlements"

    /**
     * A regra do motor: os produtos da conta liberam o item cuja assinatura avulsa
     * é `itemProduct` (null = só o AureaHyper)? Os testes da JVM trocam por outra.
     */
    var accessRule: (Set<String>, String?) -> Boolean = { owned, item ->
        com.aurea.aurea.engine.AureaEngine.nativeHasPaidAccess(owned.toTypedArray(), item)
    }

    private var vault: KeyVault? = null
    private var provider: EntitlementProvider = ServerEntitlementProvider()
    private val scope by lazy { CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate) }
    private var cache: EntitlementCache? = null
    private var email: String? = null
    private var sessionToken: String? = null
    private var checking = false
    private var verification = 0L

    /** O acesso ao AureaHyper (as 3 ferramentas Hyper, a tela da Home): o que libera TUDO. */
    var access by mutableStateOf(HyperAccess.NeedsConfig)
        private set
    /** O que o servidor aceita vender agora (GET /api/entitlements: `purchasable`). */
    var purchasable by mutableStateOf<Set<String>>(emptySet())
        private set
    /** Muda a cada recálculo: quem lê [accessFor] numa composição recompõe junto. */
    private var revision by mutableStateOf(0)
    /** Só o portão por conta (o que vale no modo "entitled" da config remota). */
    var entitlementAccess by mutableStateOf(HyperAccess.SignedOut)
        private set

    val unlocked: Boolean get() = access == HyperAccess.Unlocked

    /**
     * Liberado pela plataforma, sem conta nem config remota: o Aurea PC liga
     * (pedido do dono em 11/11/2026). No celular fica falso e nada muda.
     */
    @Volatile var platformUnlocked: Boolean = false
        set(value) { field = value; recompute() }

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
        val request = ++verification
        if (this.sessionToken != sessionToken) purchasable = emptySet()
        this.sessionToken = sessionToken
        email = accountEmail
        checking = sessionToken != null && accountEmail != null
        recompute()
        if (sessionToken == null || accountEmail == null) return
        scope.launch {
            val result = runCatching { provider.fetch(sessionToken) }.getOrDefault(EntitlementCheck.Unreachable)
            if (request != verification) return@launch
            checking = false
            when (result) {
                is EntitlementCheck.Granted -> {
                    purchasable = result.purchasable
                    store(EntitlementCache(EntitlementRules.accountKey(accountEmail), result.products,
                        System.currentTimeMillis(), result.expiresSeconds.mapValues { it.value * 1000 }))
                }
                EntitlementCheck.Unauthorized -> store(null)
                EntitlementCheck.Unreachable -> Unit
            }
            recompute()
        }
    }

    /**
     * A loja confirmou uma compra e o servidor respondeu com os produtos da conta
     * (POST /api/entitlements/purchase): vale na hora, sem esperar outra consulta.
     */
    fun acceptServerProducts(expectedSession: String, products: Set<String>, expiresSeconds: Map<String, Long>): Boolean {
        val who = email ?: return false
        if (sessionToken != expectedSession) return false
        ++verification
        checking = false
        store(EntitlementCache(EntitlementRules.accountKey(who), products, System.currentTimeMillis(),
            expiresSeconds.mapValues { it.value * 1000 }))
        recompute()
        return true
    }

    /** Os produtos da conta que valem agora (vazio sem confirmação do servidor). */
    fun ownedProducts(): Set<String> {
        revision
        val who = email ?: return emptySet()
        val mine = cache?.takeIf { it.account == EntitlementRules.accountKey(who) } ?: return emptySet()
        return mine.owned(System.currentTimeMillis())
    }

    /**
     * O acesso a um item pago cuja assinatura avulsa é [itemProduct] (null = só o
     * AureaHyper). Lê estado do Compose: a tela recompõe quando o acesso muda.
     */
    fun accessFor(itemProduct: String?, subscriptionOnly: Boolean = false): HyperAccess {
        revision
        if (platformUnlocked) return HyperAccess.Unlocked
        val entitlement = EntitlementRules.access(cache, email, System.currentTimeMillis(), checking) { owned ->
            runCatching { accessRule(owned, itemProduct) }.getOrDefault(false)
        }
        val store = com.aurea.aurea.config.AppConfigStore
        return com.aurea.aurea.config.AppConfigPolicy.hyperAccess(
            store.config, entitlement, store.now(), store.fetching, subscriptionOnly = subscriptionOnly,
        )
    }

    /** Saiu ou excluiu a conta. */
    fun clear() {
        ++verification
        checking = false
        sessionToken = null
        email = null
        purchasable = emptySet()
        store(null)
        recompute()
    }

    private fun store(value: EntitlementCache?) {
        cache = value
        runCatching { if (value == null) vault?.remove(KEY_CACHE) else vault?.put(KEY_CACHE, value.toJson()) }
    }

    private fun recompute() {
        // O AureaHyper (o que libera tudo): a regra do motor com item nulo.
        entitlementAccess = EntitlementRules.access(cache, email, System.currentTimeMillis(), checking) { owned ->
            runCatching { accessRule(owned, null) }.getOrDefault(false)
        }
        revision++
        // A config remota manda: "free" libera, "entitled" usa o portão acima, "off" bloqueia.
        val store = com.aurea.aurea.config.AppConfigStore
        access = if (platformUnlocked) HyperAccess.Unlocked
            else com.aurea.aurea.config.AppConfigPolicy.hyperAccess(store.config, entitlementAccess, store.now(), store.fetching)
    }

    /** A config remota mudou (ou o relógio passou do prazo): recalcula. */
    fun recheck() = recompute()
}
