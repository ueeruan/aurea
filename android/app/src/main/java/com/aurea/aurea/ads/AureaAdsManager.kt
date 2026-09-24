package com.aurea.aurea.ads

import java.util.concurrent.atomic.AtomicBoolean

/**
 * O que o manager precisa de um provedor de anúncio. O [GoogleAdsBackend] é o
 * AdMob; os testes usam um falso. `host` é a Activity (Any aqui para o núcleo
 * não depender de Android).
 *
 * Contrato: todo callback é chamado no máximo uma vez, na thread principal.
 */
interface AdsBackend {
    /** Consentimento (fluxo oficial) + inicialização do SDK. `podePedir` = canRequestAds. */
    fun initialize(host: Any, aoTerminar: (podePedir: Boolean) -> Unit)
    fun loadAppOpen(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit)
    fun loadInterstitial(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit)
    /** Mostra o que está carregado. `false` = não havia o que mostrar. */
    fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit): Boolean
    /** Solta o anúncio carregado (vencido ou já usado). */
    fun release(kind: AdKind)
}

/**
 * Anúncios do Aurea: App Open e o interstitial da exportação — e só.
 *
 * Nenhuma tela conhece o SDK: elas falam com este objeto. REGRA MAIS
 * IMPORTANTE: falha de publicidade nunca quebra função do Aurea — todo
 * caminho público está em [seguro] e todo "mostrar" chama o `continuar` do
 * chamador exatamente uma vez, com anúncio, sem anúncio, com erro ou offline.
 */
object AureaAdsManager {

    // -- dependências (trocadas nos testes) ----------------------------------
    internal var backend: AdsBackend? = null
    internal var frequency: AdsFrequencyController? = null
    internal var ids: AdsIds = AdsIds("", "")
    internal var agora: () -> Long = { System.currentTimeMillis() }
    /** Agenda `bloco` para daqui a `ms` na thread principal. */
    internal var agendar: (ms: Long, bloco: () -> Unit) -> Unit = { _, _ -> }
    internal var log: (String) -> Unit = {}

    // -- estado ----------------------------------------------------------------
    private var iniciado = false
    @Volatile private var podePedir = false
    private var appOpenPronto = 0L          // instante em que carregou; 0 = não há
    private var interstitialPronto = 0L
    private var carregandoAppOpen = false
    private var carregandoInterstitial = false
    @Volatile private var telaCheiaAberta = false
    private var aberturaFriaResolvida = false
    private var foiParaFundoEm = 0L
    private var hostAtual: java.lang.ref.WeakReference<Any>? = null

    /** Liga o manager (MainActivity.onCreate). Registra a abertura e pede consentimento. */
    fun initialize(host: Any, backend: AdsBackend, frequency: AdsFrequencyController, ids: AdsIds) = seguro {
        if (iniciado) return@seguro
        iniciado = true
        this.backend = backend
        this.frequency = frequency
        this.ids = ids
        frequency.registerLaunch()
        backend.initialize(host) { ok ->
            podePedir = ok
            log("[AUREA ADS] SDK initialized (consentimento permite anúncios: $ok)")
            if (ok) {
                // Carrega fora do momento crítico: é o começo do app, antes de qualquer edição.
                if (!frequency.isFirstUse) preloadAppOpen()
                preloadExportInterstitial()
            }
        }
    }

    // -- host (Activity) — só referência fraca: nada de segurar a tela ------
    fun attach(host: Any) = seguro { hostAtual = java.lang.ref.WeakReference(host) }
    fun detach(host: Any) = seguro { if (hostAtual?.get() === host) hostAtual = null }

    // -- App Open ----------------------------------------------------------------

    fun preloadAppOpen() = seguro {
        val b = backend ?: return@seguro
        if (!podePedir || !ids.appOpenEnabled || carregandoAppOpen || appOpenValido()) return@seguro
        carregandoAppOpen = true
        b.loadAppOpen(ids.appOpen, aoCarregar = {
            carregandoAppOpen = false
            appOpenPronto = agora()
            log("[AUREA ADS] AppOpen loaded")
        }, aoFalhar = { e ->
            carregandoAppOpen = false
            onAdFailed(AdKind.AppOpen, e)
        })
    }

    /**
     * Abertura fria: chamado UMA vez, quando o carregamento do Aurea termina.
     * Se o anúncio ainda não chegou, a Home segue e ele não aparece depois.
     */
    fun onAppLoaded(host: Any, trabalhando: Boolean) = seguro {
        if (aberturaFriaResolvida) return@seguro
        aberturaFriaResolvida = true
        showAppOpenIfAvailable(host, trabalhando)
    }

    fun onBackground() = seguro {
        foiParaFundoEm = agora()
        // Prepara o próximo App Open com o app parado — fora de momento crítico.
        preloadAppOpen()
    }

    /** Volta do segundo plano (onStart). A abertura fria é do [onAppLoaded]. */
    fun onForeground(host: Any, trabalhando: Boolean) = seguro {
        if (!aberturaFriaResolvida || foiParaFundoEm == 0L) return@seguro
        val fora = agora() - foiParaFundoEm
        foiParaFundoEm = 0L
        val f = frequency ?: return@seguro
        if (fora < f.policy.minBackgroundForAppOpenMs) return@seguro   // seletor de fotos, anúncio, etc.
        showAppOpenIfAvailable(host, trabalhando)
    }

    /** Mostra o App Open se TUDO permitir; senão segue sem ele. `continuar` sempre é chamado. */
    fun showAppOpenIfAvailable(host: Any?, trabalhando: Boolean, continuar: () -> Unit = {}) {
        val feito = AtomicBoolean(false)
        val fim = { if (feito.compareAndSet(false, true)) continuar() }
        var entregue = false
        seguro {
            val b = backend
            val f = frequency
            val bloqueio = when {
                b == null || f == null || host == null -> "anúncios não iniciados"
                !podePedir -> "sem consentimento/SDK"
                telaCheiaAberta -> "outro anúncio na tela"
                !appOpenValido() -> "App Open não carregado"
                else -> f.appOpenBlock(agora(), trabalhando)
            }
            if (bloqueio != null) {
                log("[AUREA ADS] Ad unavailable - continuing normally (AppOpen: $bloqueio)")
                return@seguro
            }
            entregue = true
            mostrar(AdKind.AppOpen, b!!, f!!, host!!, fim)
        }
        // Bloqueado, sem SDK ou exceção antes do `mostrar`: segue sem anúncio.
        if (!entregue) fim()
    }

    // -- Interstitial da exportação ---------------------------------------------

    fun preloadExportInterstitial() = seguro {
        val b = backend ?: return@seguro
        if (!podePedir || !ids.interstitialEnabled || carregandoInterstitial || interstitialValido()) return@seguro
        carregandoInterstitial = true
        b.loadInterstitial(ids.interstitial, aoCarregar = {
            carregandoInterstitial = false
            interstitialPronto = agora()
            log("[AUREA ADS] Interstitial loaded")
        }, aoFalhar = { e ->
            carregandoInterstitial = false
            onAdFailed(AdKind.ExportInterstitial, e)
        })
    }

    /**
     * No ponto seguro da exportação (o vídeo JÁ está salvo): mostra o
     * interstitial se houver e se a frequência deixar. `continuar` é chamado
     * exatamente uma vez — na hora, se não houver anúncio; ao fechar, se houver.
     * A exportação nunca espera um anúncio carregar.
     */
    fun showExportInterstitialIfAvailable(continuar: () -> Unit) {
        val feito = AtomicBoolean(false)
        val fim = { if (feito.compareAndSet(false, true)) continuar() }
        var entregue = false
        seguro {
            val b = backend
            val f = frequency
            val host = hostAtual?.get()
            val bloqueio = when {
                b == null || f == null -> "anúncios não iniciados"
                host == null -> "tela não visível"
                !podePedir -> "sem consentimento/SDK"
                telaCheiaAberta -> "outro anúncio na tela"
                !interstitialValido() -> "interstitial não carregado"
                else -> f.exportBlock(agora())
            }
            if (bloqueio != null) {
                log("[AUREA ADS] Ad unavailable - continuing normally (Interstitial: $bloqueio)")
                return@seguro
            }
            entregue = true
            mostrar(AdKind.ExportInterstitial, b!!, f!!, host!!, fim)
        }
        // Bloqueado, sem SDK ou exceção antes do `mostrar`: a exportação segue sem anúncio.
        if (!entregue) fim()
    }

    // -- comum ---------------------------------------------------------------------

    private fun mostrar(kind: AdKind, b: AdsBackend, f: AdsFrequencyController, host: Any, fim: () -> Unit) {
        val abriu = AtomicBoolean(false)
        val terminou = AtomicBoolean(false)
        val nome = if (kind == AdKind.AppOpen) "AppOpen" else "Interstitial"
        val acabar = acabar@{ falhou: String? ->
            if (!terminou.compareAndSet(false, true)) return@acabar
            telaCheiaAberta = false
            if (falhou == null) onAdDismissed(kind) else onAdFailed(kind, falhou)
            fim()
        }
        telaCheiaAberta = true
        val tentou = try {
            b.show(kind, host,
                aoAbrir = {
                    abriu.set(true)
                    f.recordShown(kind, agora())
                    log("[AUREA ADS] $nome shown")
                },
                aoFechar = { acabar(null) },
                aoFalhar = { e -> acabar(e) })
        } catch (t: Throwable) {
            acabar(t.message ?: t.javaClass.simpleName); return
        }
        // Consumido: a referência sai já (tenha aberto ou não). O próximo é
        // preparado depois, num momento apropriado (fundo, abertura, reset do export).
        consumir(kind)
        if (!tentou) { acabar("sem anúncio para mostrar"); return }
        // Fail-safe: o SDK não confirmou que abriu → o fluxo segue; e um teto
        // absoluto para qualquer anúncio que nunca avise que fechou.
        try {
            agendar(f.policy.showStartTimeoutMs) { if (!abriu.get()) acabar("não abriu em ${f.policy.showStartTimeoutMs} ms") }
            agendar(f.policy.showHardCapMs) { acabar("sem aviso de fechamento; seguindo") }
        } catch (t: Throwable) {
            if (!abriu.get()) acabar("agendador falhou: ${t.message}")
        }
    }

    private fun consumir(kind: AdKind) = seguro {
        when (kind) {
            AdKind.AppOpen -> appOpenPronto = 0L
            AdKind.ExportInterstitial -> interstitialPronto = 0L
        }
        backend?.release(kind)
    }

    private fun appOpenValido(): Boolean {
        val f = frequency ?: return false
        if (appOpenPronto == 0L) return false
        if (agora() - appOpenPronto > f.policy.appOpenMaxAgeMs) { consumir(AdKind.AppOpen); return false }
        return true
    }

    private fun interstitialValido(): Boolean {
        val f = frequency ?: return false
        if (interstitialPronto == 0L) return false
        if (agora() - interstitialPronto > f.policy.interstitialMaxAgeMs) { consumir(AdKind.ExportInterstitial); return false }
        return true
    }

    fun onAdDismissed(kind: AdKind) = seguro {
        log("[AUREA ADS] ${if (kind == AdKind.AppOpen) "AppOpen" else "Interstitial"} dismissed")
    }

    fun onAdFailed(kind: AdKind, erro: String) = seguro {
        log("[AUREA ADS] Ad error: ${if (kind == AdKind.AppOpen) "AppOpen" else "Interstitial"}: $erro")
    }

    /** Nenhuma falha de publicidade sai daqui. */
    private inline fun seguro(bloco: () -> Unit) {
        try {
            bloco()
        } catch (t: Throwable) {
            if (t is VirtualMachineError) throw t
            runCatching { log("[AUREA ADS] Ad error: ${t.javaClass.simpleName}: ${t.message}") }
        }
    }

    /** Só para testes: volta ao estado de processo novo. */
    internal fun resetForTest() {
        backend = null; frequency = null; ids = AdsIds("", "")
        iniciado = false; podePedir = false
        appOpenPronto = 0L; interstitialPronto = 0L
        carregandoAppOpen = false; carregandoInterstitial = false
        telaCheiaAberta = false; aberturaFriaResolvida = false; foiParaFundoEm = 0L
        hostAtual = null
    }
}
