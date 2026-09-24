package com.aurea.aurea.ads

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File

/**
 * A regra que não se negocia: anúncio nunca quebra nem segura uma função do
 * Aurea. Aqui o manager roda com um backend FALSO (sem SDK, sem rede) e um
 * relógio controlado.
 */
class AureaAdsManagerTest {

    /** Backend falso: guarda os callbacks para o teste decidir o que acontece. */
    private class Falso : AdsBackend {
        var podePedir = true
        var falharInit = false
        var lancarNoShow = false
        var lancarNoLoad = false
        var appOpenCarregado: (() -> Unit)? = null
        var appOpenFalhou: ((String) -> Unit)? = null
        var interCarregado: (() -> Unit)? = null
        var interFalhou: ((String) -> Unit)? = null
        var tem = mutableSetOf<AdKind>()
        var mostrados = mutableListOf<AdKind>()
        var abrir: (() -> Unit)? = null
        var fechar: (() -> Unit)? = null
        var falharShow: ((String) -> Unit)? = null

        override fun initialize(host: Any, aoTerminar: (Boolean) -> Unit) {
            if (falharInit) throw IllegalStateException("SDK quebrado")
            aoTerminar(podePedir)
        }
        override fun loadAppOpen(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
            if (lancarNoLoad) throw RuntimeException("sem rede")
            appOpenCarregado = { tem += AdKind.AppOpen; aoCarregar() }; appOpenFalhou = aoFalhar
        }
        override fun loadInterstitial(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
            if (lancarNoLoad) throw RuntimeException("sem rede")
            interCarregado = { tem += AdKind.ExportInterstitial; aoCarregar() }; interFalhou = aoFalhar
        }
        override fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit): Boolean {
            if (lancarNoShow) throw RuntimeException("show explodiu")
            if (kind !in tem) return false
            mostrados += kind; abrir = aoAbrir; fechar = aoFechar; falharShow = aoFalhar
            return true
        }
        override fun release(kind: AdKind) { tem -= kind }
    }

    private var agora = 1_000_000_000L
    private val agendados = mutableListOf<Pair<Long, () -> Unit>>()
    private val logs = mutableListOf<String>()
    private val host = Any()
    private lateinit var falso: Falso
    private lateinit var store: MemoryAdsStore

    @Before
    fun preparar() {
        AureaAdsManager.resetForTest()
        AureaAdsManager.agora = { agora }
        AureaAdsManager.agendar = { ms, b -> agendados += (agora + ms) to b }
        AureaAdsManager.log = { logs += it }
        falso = Falso()
        store = MemoryAdsStore()
    }

    @After
    fun limpar() = AureaAdsManager.resetForTest()

    private fun iniciar(aberturasAntes: Long = 0) {
        store.putLong("launches", aberturasAntes)
        AureaAdsManager.initialize(host, falso, AdsFrequencyController(store),
            AdsIds(AdsConfig.TEST_APP_OPEN, AdsConfig.TEST_INTERSTITIAL))
        AureaAdsManager.attach(host)
    }

    private fun passar(ms: Long) {
        agora += ms
        agendados.filter { it.first <= agora }.also { agendados.removeAll(it) }.forEach { it.second() }
    }

    // -- App Open ------------------------------------------------------------

    @Test
    fun `primeira abertura nao mostra App Open`() {
        iniciar(aberturasAntes = 0)
        assertEquals("nem carrega na 1a abertura", null, falso.appOpenCarregado)
        AureaAdsManager.onAppLoaded(host, trabalhando = false)
        assertTrue(falso.mostrados.isEmpty())
    }

    @Test
    fun `segunda abertura pode mostrar`() {
        iniciar(aberturasAntes = 1)
        falso.appOpenCarregado!!()
        AureaAdsManager.onAppLoaded(host, trabalhando = false)
        assertEquals(listOf(AdKind.AppOpen), falso.mostrados)
        falso.abrir!!(); falso.fechar!!()
        assertTrue(logs.any { "AppOpen shown" in it } && logs.any { "AppOpen dismissed" in it })
    }

    @Test
    fun `App Open indisponivel nao bloqueia a Home e nao aparece atrasado`() {
        iniciar(aberturasAntes = 1)
        var home = false
        AureaAdsManager.showAppOpenIfAvailable(host, trabalhando = false) { home = true }
        assertTrue("a Home segue na hora", home)
        AureaAdsManager.onAppLoaded(host, trabalhando = false)
        falso.appOpenCarregado!!()          // chegou DEPOIS do carregamento do app
        AureaAdsManager.onAppLoaded(host, trabalhando = false)   // abertura fria já resolvida
        assertTrue("não interrompe a Home depois", falso.mostrados.isEmpty())
        assertTrue(logs.any { "Ad unavailable - continuing normally" in it })
    }

    @Test
    fun `no editor ou exportando nao ha App Open`() {
        iniciar(aberturasAntes = 3)
        falso.appOpenCarregado!!()
        AureaAdsManager.onAppLoaded(host, trabalhando = true)
        assertTrue(falso.mostrados.isEmpty())
    }

    @Test
    fun `foregrounds proximos nao repetem App Open`() {
        iniciar(aberturasAntes = 3)
        falso.appOpenCarregado!!()
        AureaAdsManager.onAppLoaded(host, false)
        falso.abrir!!(); falso.fechar!!()
        // volta rápida (seletor de fotos): nada
        AureaAdsManager.onBackground(); passar(5_000); falso.appOpenCarregado!!()
        AureaAdsManager.onForeground(host, false)
        // volta depois de 1 min, mas dentro do cooldown: nada
        AureaAdsManager.onBackground(); passar(60_000)
        AureaAdsManager.onForeground(host, false)
        assertEquals(1, falso.mostrados.size)
    }

    // -- Exportação ----------------------------------------------------------

    @Test
    fun `exportacao funciona sem anuncio (manager nem iniciado)`() {
        var resultado = false
        AureaAdsManager.showExportInterstitialIfAvailable { resultado = true }
        assertTrue(resultado)
    }

    @Test
    fun `interstitial indisponivel nao bloqueia a exportacao`() {
        iniciar(aberturasAntes = 3)
        var resultado = 0
        AureaAdsManager.showExportInterstitialIfAvailable { resultado++ }
        assertEquals(1, resultado)
        assertTrue(falso.mostrados.isEmpty())
    }

    @Test
    fun `anuncio fechado continua o fluxo, uma vez so`() {
        iniciar(aberturasAntes = 3)
        falso.interCarregado!!()
        var resultado = 0
        AureaAdsManager.showExportInterstitialIfAvailable { resultado++ }
        assertEquals("espera o anúncio fechar", 0, resultado)
        falso.abrir!!(); falso.fechar!!(); falso.fechar!!()
        assertEquals(1, resultado)
        passar(10 * 60_000)   // os fail-safes agendados não chamam de novo
        assertEquals(1, resultado)
        assertTrue(logs.any { "Interstitial shown" in it } && logs.any { "Interstitial dismissed" in it })
    }

    @Test
    fun `anuncio que nao abre nao prende o resultado`() {
        iniciar(aberturasAntes = 3)
        falso.interCarregado!!()
        var resultado = 0
        AureaAdsManager.showExportInterstitialIfAvailable { resultado++ }
        passar(AdsPolicy().showStartTimeoutMs)
        assertEquals(1, resultado)
    }

    // -- Falhas --------------------------------------------------------------

    @Test
    fun `erro do SDK nao derruba nada`() {
        falso.falharInit = true
        iniciar(aberturasAntes = 3)                      // initialize explode: engolido
        var r = 0
        AureaAdsManager.onAppLoaded(host, false)
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals(1, r)

        preparar(); falso.lancarNoShow = true
        iniciar(aberturasAntes = 3)
        falso.tem += AdKind.ExportInterstitial
        falso.interCarregado!!()
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals("show explodiu e o fluxo seguiu", 2, r)
        assertTrue(logs.any { "Ad error" in it })

        preparar()
        iniciar(aberturasAntes = 3)
        falso.interCarregado!!()
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        falso.falharShow!!("show 3: ad reused")
        assertEquals("falha ao mostrar segue o fluxo", 3, r)
    }

    @Test
    fun `offline nao causa crash`() {
        falso.lancarNoLoad = true
        iniciar(aberturasAntes = 3)
        var r = 0
        AureaAdsManager.onAppLoaded(host, false)
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals(1, r)

        preparar()
        iniciar(aberturasAntes = 3)
        falso.interFalhou!!("load 2: network error")
        falso.appOpenFalhou!!("load 2: network error")
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals(2, r)
        assertTrue(logs.any { "Ad error" in it && "network" in it })
    }

    @Test
    fun `sem consentimento nada e pedido`() {
        falso.podePedir = false
        iniciar(aberturasAntes = 3)
        assertEquals(null, falso.interCarregado)
        assertEquals(null, falso.appOpenCarregado)
    }

    // -- Frequência -----------------------------------------------------------

    @Test
    fun `frequency cap - um interstitial de exportacao por janela`() {
        iniciar(aberturasAntes = 3)
        falso.interCarregado!!()
        AureaAdsManager.showExportInterstitialIfAvailable {}
        falso.abrir!!(); falso.fechar!!()
        passar(AdsPolicy().fullscreenGapMs + 1)
        AureaAdsManager.preloadExportInterstitial(); falso.interCarregado!!()
        var r = 0
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals("segunda exportação na mesma janela: sem anúncio", 1, r)
        assertEquals(1, falso.mostrados.size)
        passar(AdsPolicy().exportWindowMs)
        AureaAdsManager.showExportInterstitialIfAvailable {}
        assertEquals("fora da janela volta a poder", 2, falso.mostrados.size)
    }

    @Test
    fun `dois anuncios de tela cheia nao aparecem em sequencia`() {
        iniciar(aberturasAntes = 3)
        falso.appOpenCarregado!!(); falso.interCarregado!!()
        AureaAdsManager.onAppLoaded(host, false)
        falso.abrir!!(); falso.fechar!!()
        passar(10_000)
        var r = 0
        AureaAdsManager.showExportInterstitialIfAvailable { r++ }
        assertEquals(1, r)
        assertEquals(listOf(AdKind.AppOpen), falso.mostrados)
    }

    @Test
    fun `frequencia e persistente entre aberturas`() {
        val f1 = AdsFrequencyController(store)
        f1.registerLaunch()
        assertTrue(f1.isFirstUse)
        f1.recordShown(AdKind.ExportInterstitial, 100L)
        val f2 = AdsFrequencyController(store)     // "reabriu o app"
        f2.registerLaunch()
        assertFalse(f2.isFirstUse)
        assertTrue(f2.exportBlock(200L) != null)
    }

    // -- IDs -------------------------------------------------------------------

    @Test
    fun `DEBUG nunca usa ID de producao`() {
        val debug = File("src/debug/res/values/ads_config.xml").readText()
        val ids = Regex("""<string name="(admob_[a-z_]+)"[^>]*>([^<]*)</string>""").findAll(debug)
            .associate { it.groupValues[1] to it.groupValues[2].trim() }
        assertEquals(setOf("admob_app_id", "admob_app_open_unit", "admob_export_interstitial_unit"), ids.keys)
        ids.forEach { (k, v) -> assertTrue("$k = $v não é ID de teste", AdsConfig.isTestId(v)) }
        assertEquals(AdsConfig.TEST_APP_OPEN, ids["admob_app_open_unit"])
        assertEquals(AdsConfig.TEST_INTERSTITIAL, ids["admob_export_interstitial_unit"])
        // Nenhum ID real espalhado no código: só AdsConfig conhece os de teste.
        val codigo = File("src/main/java").walkTopDown().filter { it.isFile && it.extension == "kt" }
            .filter { "ca-app-pub-" in it.readText() }.map { it.name }.toSet()
        assertEquals(setOf("AdsConfig.kt"), codigo)
    }
}
