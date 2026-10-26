package com.aurea.aurea.config

import com.aurea.aurea.hyper.HyperAccess
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A config remota assinada (AppConfig.kt). Os vetores foram assinados pelo
 * MESMO código do Worker (discovery/appconfig.js, WebCrypto, r||s cru) com
 * uma chave SÓ de teste — confere a ponte WebCrypto → SHA256withECDSA (DER).
 */
class AppConfigTest {
    private val testKey = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEoBGq68+zl92r9OLopSEynt8DP6S8oF0OhLXc1P5M/zDs4l3botH5ciKjlsgfzVWDaIVyUfw13WbPAhMHq0zDqw=="
    private val freePayload = "{\"v\":1,\"platform\":\"android\",\"minVersionCode\":0,\"latestVersionCode\":0,\"updateUrl\":\"https://play.google.com/store/apps/details?id=com.aurea.aurea\",\"hyper\":{\"mode\":\"free\"},\"message\":null,\"issuedAt\":1800000000,\"expiresAt\":1800604800}"
    private val freeSig = "LBXuHlkbQnL8cu8uahdKQNwRq7dADeY0bXlEYrB9bd/yUCT9rThZujf6ZeUdc8mgBd+1mWqcWEBY9bKBEjRVtQ=="
    private val updatePayload = "{\"v\":1,\"platform\":\"android\",\"minVersionCode\":999999,\"latestVersionCode\":0,\"updateUrl\":\"https://example.com/aurea.apk\",\"hyper\":{\"mode\":\"free\"},\"message\":null,\"issuedAt\":1800000000,\"expiresAt\":1800604800}"
    private val updateSig = "nMZD7U7PiEQ1OTjNZ6vzTAn0AByzhZrQRpJRLw4JOuS0O/d6CRYifeY1A1xEqPiOaG+OVwWwRy+5NPLcSqXw5g=="
    private val entitledPayload = "{\"v\":1,\"platform\":\"android\",\"minVersionCode\":0,\"latestVersionCode\":0,\"updateUrl\":\"https://play.google.com/store/apps/details?id=com.aurea.aurea\",\"hyper\":{\"mode\":\"entitled\"},\"message\":null,\"issuedAt\":1800000000,\"expiresAt\":1800604800}"
    private val entitledSig = "9QxUkBptPB6Dk717EXqGo8Ere3hJHtNMGqQHZXoIHTPSbgqvW2C7Slf0J0pLgSnZBXdCW/qIlpeYf5leIORBjA=="
    private val offPayload = "{\"v\":1,\"platform\":\"android\",\"minVersionCode\":0,\"latestVersionCode\":0,\"updateUrl\":\"https://play.google.com/store/apps/details?id=com.aurea.aurea\",\"hyper\":{\"mode\":\"off\"},\"message\":{\"pt-BR\":\"Pausado\",\"en\":\"Paused\"},\"issuedAt\":1800000000,\"expiresAt\":1800604800}"
    private val offSig = "e+OsIanHwx8bFlDn6dyEXCyioheNp3WAiS2P8NpbvLKFA0XiYuVZkAKPv0cw/EL2Frg9mccZbxiZ3m6rClSE6A=="
    private val iosPayload = "{\"v\":1,\"platform\":\"ios\",\"minVersionCode\":0,\"latestVersionCode\":0,\"updateUrl\":\"\",\"hyper\":{\"mode\":\"free\"},\"message\":null,\"issuedAt\":1800000000,\"expiresAt\":1800604800}"
    private val iosSig = "+TknbupzHnWuYfLUwSxnm6D/ycUCyeuj/qzfzaLGI8pZRSULOMAJq1XRoVmozhxPiSr89a3Y2gS92C2Qlp8rIA=="
    private val issued = 1_800_000_000L
    private val key = AppConfigVerifier.publicKey(testKey)
    private fun parse(payload: String, sig: String, now: Long = issued + 60, platform: String = "android", allowExpired: Boolean = false) =
        AppConfigVerifier.parse(payload, sig, key, platform, now, allowExpired)

    @Test fun validConfigVerifiesAndParses() {
        val c = parse(freePayload, freeSig)
        assertNotNull(c)
        assertEquals(HyperMode.Free, c!!.hyperMode)
        assertEquals(0L, c.minVersionCode)
        assertEquals(issued + 7 * 86400, c.expiresAt)
        assertEquals("Paused", parse(offPayload, offSig)!!.messageFor("en"))
        assertEquals("Pausado", parse(offPayload, offSig)!!.messageFor("pt"))
        assertEquals("Paused", parse(offPayload, offSig)!!.messageFor("ru"))   // sem russo → inglês
    }

    @Test fun badSignatureTamperedPayloadOrOtherKeyIsRejected() {
        assertNull(parse(freePayload, entitledSig))
        assertNull(parse(freePayload.replace("\"free\"", "\"off\""), freeSig))
        assertNull(parse(freePayload, ""))
        assertNull(parse(freePayload, "nao-e-base64!!"))
        assertNull(parse("", freeSig))
        assertNull(AppConfigVerifier.parse(freePayload, freeSig, AppConfigVerifier.publicKey(AppConfigKeys.PUBLIC_KEY_SPKI), "android", issued))
        assertNull(AppConfigVerifier.parse(freePayload, freeSig, null, "android", issued))
    }

    @Test fun wrongPlatformExpiredOrFutureIsRejected() {
        assertNull(parse(iosPayload, iosSig))
        assertNotNull(parse(iosPayload, iosSig, platform = "ios"))
        assertNull(parse(freePayload, freeSig, now = issued + 7 * 86400))          // vencida
        assertNotNull(parse(freePayload, freeSig, now = issued + 7 * 86400, allowExpired = true))   // cache: só a versão mínima
        assertNull(parse(freePayload, freeSig, now = issued - 2 * 86400))          // emitida "no futuro"
    }

    @Test fun rawSignatureConvertsToDer() {
        val raw = ByteArray(64) { if (it == 0 || it == 32) 0x80.toByte() else 1 }
        val der = AppConfigVerifier.rawToDer(raw)!!
        assertEquals(0x30, der[0].toInt())
        assertEquals(der.size - 2, der[1].toInt())
        assertEquals(33, der[3].toInt())     // bit alto ligado → 0x00 na frente
        assertNull(AppConfigVerifier.rawToDer(ByteArray(63)))
    }

    @Test fun versionGate() {
        val update = parse(updatePayload, updateSig)!!
        assertTrue(AppConfigPolicy.updateRequired(update, 2198))
        assertFalse(AppConfigPolicy.updateRequired(update, 999999))
        assertFalse(AppConfigPolicy.updateRequired(parse(freePayload, freeSig), 2198))
        // Sem nenhuma config (offline desde sempre): nunca bloqueia.
        assertFalse(AppConfigPolicy.updateRequired(null, 1))
        // Guardada e vencida: a versão mínima continua valendo.
        val cached = parse(updatePayload, updateSig, now = issued + 30 * 86400, allowExpired = true)
        assertTrue(AppConfigPolicy.updateRequired(cached, 2198))
        assertEquals("https://example.com/aurea.apk", update.updateUrl)
    }

    @Test fun hyperPolicyFreeEntitledOff() {
        val now = issued + 3600
        val free = parse(freePayload, freeSig, now)!!
        val entitled = parse(entitledPayload, entitledSig, now)!!
        val off = parse(offPayload, offSig, now)!!
        assertEquals(HyperAccess.Unlocked, AppConfigPolicy.hyperAccess(free, HyperAccess.SignedOut, now, false))
        assertEquals(HyperAccess.SignedOut, AppConfigPolicy.hyperAccess(entitled, HyperAccess.SignedOut, now, false))
        assertEquals(HyperAccess.Locked, AppConfigPolicy.hyperAccess(entitled, HyperAccess.Locked, now, false))
        assertEquals(HyperAccess.Unlocked, AppConfigPolicy.hyperAccess(entitled, HyperAccess.Unlocked, now, false))
        assertEquals(HyperAccess.Off, AppConfigPolicy.hyperAccess(off, HyperAccess.Unlocked, now, false))
    }

    @Test fun freeNeedsARecentConfig() {
        val free = parse(freePayload, freeSig, allowExpired = true)!!
        // Nunca buscou: "conecte-se" (ou conferindo, se há consulta em voo).
        assertEquals(HyperAccess.NeedsConfig, AppConfigPolicy.hyperAccess(null, HyperAccess.Unlocked, issued, false))
        assertEquals(HyperAccess.Checking, AppConfigPolicy.hyperAccess(null, HyperAccess.Unlocked, issued, true))
        // Dentro dos 7 dias: liberado; passou do prazo: bloqueia de novo.
        assertEquals(HyperAccess.Unlocked, AppConfigPolicy.hyperAccess(free, HyperAccess.SignedOut, issued + 6 * 86400, false))
        assertEquals(HyperAccess.NeedsConfig, AppConfigPolicy.hyperAccess(free, HyperAccess.SignedOut, issued + 7 * 86400, false))
        // Relógio voltado muito para trás também não vale.
        assertEquals(HyperAccess.NeedsConfig, AppConfigPolicy.hyperAccess(free, HyperAccess.SignedOut, issued - 3 * 86400, false))
    }

    @Test fun olderConfigDoesNotReplaceNewer() {
        val a = parse(freePayload, freeSig)!!
        val older = a.copy(issuedAt = a.issuedAt - 10)
        assertTrue(AppConfigPolicy.accept(a, null))
        assertTrue(AppConfigPolicy.accept(a, older))
        assertFalse(AppConfigPolicy.accept(older, a))
    }

    @Test fun embeddedProductionKeyIsAValidP256Key() {
        assertNotNull(AppConfigVerifier.publicKey(AppConfigKeys.PUBLIC_KEY_SPKI))
    }
}
