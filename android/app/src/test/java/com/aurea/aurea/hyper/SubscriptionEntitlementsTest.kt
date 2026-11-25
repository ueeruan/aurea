package com.aurea.aurea.hyper

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Assinaturas mensais (16/11/2026): o servidor manda produtos + vencimento, o
 * cache guarda os dois e uma assinatura vencida não libera nem offline. A regra
 * "qual produto libera qual item" é do motor (hyper::has_access, test_hyper.cpp);
 * aqui ela é imitada só para exercitar o portão sem a ponte nativa.
 */
class SubscriptionEntitlementsTest {
    private val email = "pessoa@exemplo.com"
    private val now = 1_800_000_000_000L
    private val day = 24L * 3600 * 1000

    /** A mesma regra de hyper::has_access (AureaHyper semanal/mensal/anual/vitalício ou o produto do item). */
    private fun engineRule(item: String?): (Set<String>) -> Boolean = { owned ->
        listOf("aurea_hyper_semanal", "aurea_hyper_mensal", "aurea_hyper_anual", "aureahyper_lifetime").any { it in owned } ||
            (item != null && item in owned)
    }

    private fun cache(products: Set<String>, expires: Map<String, Long> = emptyMap(), at: Long = now) =
        EntitlementCache(EntitlementRules.accountKey(email), products, at, expires)

    @Test fun serverAnswerCarriesExpiryAndPurchasable() {
        val body = JSONObject("""{"products":["aurea_fx_twitch","aureahyper_lifetime"],"checkedAt":7,
            "expires":{"aurea_fx_twitch":1800000500,"lixo":"x","zero":0},"purchasable":["aurea_hyper_mensal","aurea_fx_twitch",""]}""")
        val r = ServerEntitlementProvider.interpret(200, body)
        assertEquals(EntitlementCheck.Granted(setOf("aurea_fx_twitch", "aureahyper_lifetime"), 7,
            mapOf("aurea_fx_twitch" to 1_800_000_500L), setOf("aurea_hyper_mensal", "aurea_fx_twitch")), r)
        // Servidor antigo (sem `expires`/`purchasable`): nada vence, nada à venda.
        val old = ServerEntitlementProvider.interpret(200, JSONObject("""{"products":["aureahyper_lifetime"],"checkedAt":5}"""))
        assertEquals(EntitlementCheck.Granted(setOf("aureahyper_lifetime"), 5), old)
    }

    @Test fun cacheKeepsExpiryAndReadsOldFormat() {
        val c = cache(setOf("aurea_texto3d", "aureahyper_lifetime"), mapOf("aurea_texto3d" to now + day))
        assertEquals(c, EntitlementCache.parse(c.toJson()))
        val old = """{"account":"${EntitlementRules.accountKey(email)}","products":["aureahyper_lifetime"],"checkedAt":$now}"""
        assertEquals(cache(setOf("aureahyper_lifetime")), EntitlementCache.parse(old))
    }

    @Test fun expiredSubscriptionStopsCountingAfterTheSlack() {
        val c = cache(setOf("aurea_fx_twitch", "aureahyper_lifetime"), mapOf("aurea_fx_twitch" to now))
        assertEquals(setOf("aurea_fx_twitch", "aureahyper_lifetime"), c.owned(now + EntitlementRules.SUBSCRIPTION_SLACK_MS - 1))
        assertEquals(setOf("aureahyper_lifetime"), c.owned(now + EntitlementRules.SUBSCRIPTION_SLACK_MS + 1))
    }

    @Test fun singleSubscriptionUnlocksOnlyItsItem() {
        val c = cache(setOf("aurea_fx_twitch"), mapOf("aurea_fx_twitch" to now + 30 * day))
        fun access(item: String?, at: Long = now) = EntitlementRules.access(c, email, at, false, engineRule(item))
        assertEquals(HyperAccess.Unlocked, access("aurea_fx_twitch"))
        assertEquals(HyperAccess.Locked, access("aurea_fx_signal"))
        assertEquals(HyperAccess.Locked, access("aurea_texto3d"))
        assertEquals(HyperAccess.Locked, access(null), "as ferramentas Hyper só vêm no AureaHyper")
        // Venceu (e passou a folga) sem renovar: bloqueia, mesmo com a confirmação recente.
        val after = now + 30 * day + EntitlementRules.SUBSCRIPTION_SLACK_MS + 1
        val fresh = cache(setOf("aurea_fx_twitch"), mapOf("aurea_fx_twitch" to now + 30 * day), at = after - day)
        assertEquals(HyperAccess.Locked, EntitlementRules.access(fresh, email, after, false, engineRule("aurea_fx_twitch")))
    }

    @Test fun hyperMonthlyAndLegacyUnlockEverything() {
        for (all in listOf("aurea_hyper_mensal", "aureahyper_lifetime")) {
            val c = cache(setOf(all), if (all == "aurea_hyper_mensal") mapOf(all to now + day) else emptyMap())
            for (item in listOf("aurea_fx_twitch", "aurea_texto3d", null)) {
                assertEquals("$all → $item", HyperAccess.Unlocked, EntitlementRules.access(c, email, now, false, engineRule(item)))
            }
        }
    }

    @Test fun signedOutAndOtherAccountNeverUnlock() {
        val c = cache(setOf("aurea_hyper_mensal"), mapOf("aurea_hyper_mensal" to now + day))
        assertEquals(HyperAccess.SignedOut, EntitlementRules.access(c, null, now, false, engineRule("aurea_fx_twitch")))
        assertEquals(HyperAccess.NeedsCheck, EntitlementRules.access(c, "outra@exemplo.com", now, false, engineRule("aurea_fx_twitch")))
        // A forma antiga (um produto) continua igual.
        assertEquals(HyperAccess.Unlocked, EntitlementRules.access(c, email, "aurea_hyper_mensal", now, false))
        assertTrue(EntitlementRules.SUBSCRIPTION_SLACK_MS in 1..EntitlementRules.GRACE_MS)
    }

    private fun assertEquals(expected: Any?, actual: Any?, message: String) = assertEquals(message, expected, actual)
}
