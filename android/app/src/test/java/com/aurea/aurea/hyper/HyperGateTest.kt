package com.aurea.aurea.hyper

import com.aurea.aurea.home.licenseGroup
import com.aurea.aurea.home.licenseTitle
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** O portão (Entitlements.kt), as receitas e os formatos da ponte do AureaHyper. */
class HyperGateTest {
    private val email = "pessoa@exemplo.com"
    private val product = HyperEntitlements.PRODUCT
    private val now = 1_800_000_000_000L
    private fun cache(products: Set<String>, at: Long = now, who: String = email) =
        EntitlementCache(EntitlementRules.accountKey(who), products, at)

    @Test fun signedOutNeverUnlocks() {
        assertEquals(HyperAccess.SignedOut, EntitlementRules.access(cache(setOf(product)), null, product, now, checking = false))
    }

    @Test fun neverCheckedNeverUnlocks() {
        assertEquals(HyperAccess.NeedsCheck, EntitlementRules.access(null, email, product, now, checking = false))
        assertEquals(HyperAccess.Checking, EntitlementRules.access(null, email, product, now, checking = true))
    }

    @Test fun confirmedUnlocksAndSurvivesOfflineForTheGracePeriod() {
        assertEquals(HyperAccess.Unlocked, EntitlementRules.access(cache(setOf(product)), email, product, now, false))
        val almost = now + EntitlementRules.GRACE_MS - 1
        assertEquals(HyperAccess.Unlocked, EntitlementRules.access(cache(setOf(product)), email, product, almost, false))
        val after = now + EntitlementRules.GRACE_MS + 1
        assertEquals(HyperAccess.Expired, EntitlementRules.access(cache(setOf(product)), email, product, after, false))
    }

    @Test fun clockRolledFarBackDoesNotExtendTheGrace() {
        val past = now - EntitlementRules.CLOCK_SKEW_MS - 1
        assertEquals(HyperAccess.Expired, EntitlementRules.access(cache(setOf(product)), email, product, past, false))
    }

    @Test fun otherAccountsCacheDoesNotCount() {
        val other = cache(setOf(product), who = "outra@exemplo.com")
        assertEquals(HyperAccess.NeedsCheck, EntitlementRules.access(other, email, product, now, false))
        // E-mail com maiúsculas/espaços é a mesma conta.
        assertEquals(HyperAccess.Unlocked, EntitlementRules.access(cache(setOf(product)), " Pessoa@Exemplo.com ", product, now, false))
    }

    @Test fun confirmedWithoutTheProductIsLocked() {
        assertEquals(HyperAccess.Locked, EntitlementRules.access(cache(emptySet()), email, product, now, false))
    }

    @Test fun serverResponsesMapToChecks() {
        val ok = ServerEntitlementProvider.interpret(200, JSONObject("""{"products":["aureahyper_lifetime"],"checkedAt":5}"""))
        assertEquals(EntitlementCheck.Granted(setOf(product), 5), ok)
        assertEquals(EntitlementCheck.Unauthorized, ServerEntitlementProvider.interpret(401, JSONObject()))
        // 503 (tabela fora), sem rede (0) e 200 sem a lista: nada muda — nunca "não tem".
        assertEquals(EntitlementCheck.Unreachable, ServerEntitlementProvider.interpret(503, JSONObject()))
        assertEquals(EntitlementCheck.Unreachable, ServerEntitlementProvider.interpret(0, JSONObject()))
        assertEquals(EntitlementCheck.Unreachable, ServerEntitlementProvider.interpret(200, JSONObject("{}")))
    }

    @Test fun cacheRoundTrips() {
        val c = cache(setOf(product, "x"))
        assertEquals(c, EntitlementCache.parse(c.toJson()))
        assertNull(EntitlementCache.parse("lixo"))
        assertFalse(c.toJson().contains(email))   // o e-mail não fica em claro
    }

    @Test fun recipePackOrderMatchesTheBridge() {
        val r = HyperRecipe(upscale = 4, upscaleMode = HyperUpscaleMode.Anime, denoise = 0.25f, deblock = true, deinterlace = 2,
            face = true, faceStrength = 0.75f, targetFps = 60.0, slowMotion = 8, stabilize = true, stabilizeStrength = 0.5f,
            stabilizeToKeyframes = true, path = HyperPath.Max)
        assertArrayEquals(doubleArrayOf(4.0, 1.0, 0.25, 1.0, 2.0, 1.0, 0.75, 60.0, 8.0, 1.0, 0.5, 1.0, 3.0), r.pack(), 1e-9)
        assertEquals(HyperRecipe.PACKED_SIZE, r.pack().size)
    }

    @Test fun presetsMapToTheRecipeFields() {
        val restore = HyperPreset.Restore.recipe()
        assertTrue(restore.denoise > 0f && restore.deblock && restore.deinterlace == 1 && restore.upscale == 2 && restore.face)
        assertEquals(4, HyperPreset.Sharper.recipe(4).upscale)
        assertEquals(2, HyperPreset.Sharper.recipe().upscale)
        assertEquals(60.0, HyperPreset.Fps60.recipe().targetFps, 0.0)
        assertEquals(8, HyperPreset.SlowMotion.recipe(8).slowMotion)
        assertEquals(HyperUpscaleMode.Anime, HyperPreset.Anime.recipe().upscaleMode)
        assertTrue(HyperPreset.Stabilize.recipe().stabilize)
        for (p in HyperPreset.entries) assertFalse("${p.id} não pode ser identidade", p.recipe().identity)
        // O rosto fica desligado por padrão fora da receita de restauração.
        assertFalse(HyperRecipe().face)
        assertTrue(HyperRecipe().identity)
    }

    @Test fun pathsAreNeverBlockedByTheDevice() {
        // 0.3.4 (bugs-034, hyper): 32 bits, sem GPU ou sem medição não bloqueiam mais.
        val all = HyperEstimate(doubleArrayOf(12.0, 12.0, 30.0, 90.0))
        val p32 = HyperDeviceProfile(measured = true, abi64 = false, defaultPath = HyperPath.Turbo)
        val noGpu = HyperDeviceProfile(measured = true, abi64 = true)
        val unmeasured = HyperDeviceProfile(measured = false)
        for (profile in listOf(p32, noGpu, unmeasured, null))
            for (path in HyperPath.Choices) assertEquals(HyperPathBlock.None, HyperPathRules.block(path, all, profile))
        // Só os AJUSTES tiram um caminho (nenhuma rede para eles), e igual em todo aparelho.
        val e = HyperEstimate(doubleArrayOf(-1.0, 12.0, -1.0, -1.0))
        assertEquals(HyperPathBlock.None, HyperPathRules.block(HyperPath.Turbo, e, p32))
        assertEquals(HyperPathBlock.None, HyperPathRules.block(HyperPath.Max, e, p32))   // 0.3.5: sem estimativa também não bloqueia
        assertEquals(HyperPathRules.block(HyperPath.Lite, e, p32), HyperPathRules.block(HyperPath.Lite, e, HyperDeviceProfile(measured = true, abi64 = true, vulkan = true)))
        assertEquals(HyperPathBlock.None, HyperPathRules.block(HyperPath.Lite, null, p32))
        // A recomendação continua escolhendo o padrão; sugerido indisponível → o primeiro disponível.
        assertEquals(HyperPath.Turbo, HyperPathRules.initial(all, p32))
        assertEquals(HyperPath.Turbo, HyperPathRules.initial(e, HyperDeviceProfile(measured = true, defaultPath = HyperPath.Lite)))
        assertEquals(HyperPath.Turbo, HyperPathRules.initial(HyperEstimate(doubleArrayOf(-1.0, 1.0, 2.0, 3.0)), null))
    }

    @Test fun heavierPathThanSuggestedShowsTheRiskWarning() {
        val lite = HyperDeviceProfile(measured = true, abi64 = true, defaultPath = HyperPath.Lite)
        assertFalse(HyperPathRules.risky(HyperPath.Turbo, lite))
        assertFalse(HyperPathRules.risky(HyperPath.Lite, lite))
        assertTrue(HyperPathRules.risky(HyperPath.Max, lite))
        val weak = HyperDeviceProfile(measured = true, abi64 = false, defaultPath = HyperPath.Turbo)
        assertTrue(HyperPathRules.risky(HyperPath.Lite, weak))
        assertTrue(HyperPathRules.risky(HyperPath.Max, weak))
        // Sem medição: Lite/Max avisam; Turbo nunca.
        assertTrue(HyperPathRules.risky(HyperPath.Lite, null))
        assertTrue(HyperPathRules.risky(HyperPath.Max, HyperDeviceProfile(measured = false)))
        assertFalse(HyperPathRules.risky(HyperPath.Turbo, null))
        assertFalse(HyperPathRules.risky(HyperPath.Max, HyperDeviceProfile(measured = true, defaultPath = HyperPath.Max)))
    }

    @Test fun bridgePayloadsDecode() {
        val est = HyperEstimate.decode(doubleArrayOf(0.0, 10.0, 20.0, -1.0, 1e9, 3840.0, 2160.0, 60.0))
        assertNotNull(est)
        assertTrue(est!!.available(HyperPath.Lite) && !est.available(HyperPath.Max))
        assertEquals(3840, est.outputWidth)
        assertNull(HyperEstimate.decode(doubleArrayOf(1.0)))
        val raw = IntArray(HyperJobInfo.SIZE)
        intArrayOf(2, 3, 3, 1, 3, 0, 0, 0).copyInto(raw)                     // Prepare feito, Upscale rodando
        raw[8] = 1; raw[9] = 1; raw[12] = 1; raw[13] = 2; raw[14] = 1; raw[15] = 400
        raw[16 + 3] = HyperPath.Lite.code
        val job = HyperJobInfo.decode(raw)!!
        assertTrue(job.active && job.running && job.audioMuted)
        assertEquals(HyperStepState.Done, job.state(HyperStep.Prepare))
        assertEquals(HyperStepState.Running, job.state(HyperStep.Upscale))
        assertFalse(job.present(HyperStep.Clean))
        assertEquals(HyperPath.Lite, job.path(HyperStep.Upscale))
        assertEquals(0.4f, job.progress, 1e-6f)
        val profile = HyperDeviceProfile.parse("""{"measured":true,"abi64":true,"defaultPath":2,"backendNote":"vk \"ok\""}""")
        assertEquals(HyperPath.Lite, profile!!.defaultPath)
        assertEquals("vk \"ok\"", profile.backendNote)
    }

    @Test fun licenseFilesGroupAndTitle() {
        assertEquals(0, licenseGroup("licenses/oss/THIRD_PARTY_HYPER.md"))
        assertEquals(0, licenseGroup("licenses/oss/hyper/span-x2/LICENSE"))
        assertEquals(1, licenseGroup("licenses/native-export/FFmpeg-LGPL-2.1.txt"))
        assertEquals(2, licenseGroup("licenses/oss/ai/LICENSE-ncnn.txt"))
        assertEquals(4, licenseGroup("LICENSE-Roboto.txt"))
        assertEquals("hyper · span-x2 · LICENSE", licenseTitle("licenses/oss/hyper/span-x2/LICENSE"))
        assertEquals("FFmpeg-LGPL-2.1", licenseTitle("licenses/native-export/FFmpeg-LGPL-2.1.txt"))
    }
}
