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
    fun loadRewarded(unitId: String, aoCarregar: () -> Unit, aoFalhar: (String) -> Unit)
    /**
     * Mostra o que está carregado. `false` = não havia o que mostrar.
     * `aoRecompensar` é o callback OFICIAL de recompensa do SDK
     * (onUserEarnedReward) — só o Rewarded chama; fechar nunca chama.
     */
    fun show(kind: AdKind, host: Any, aoAbrir: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit,
             aoRecompensar: () -> Unit): Boolean
    /** Solta o anúncio carregado (vencido ou já usado). */
    fun release(kind: AdKind)
    /**
     * Quem será recompensado pelo PRÓXIMO Rewarded, para a verificação
     * server-side (LevelPlay: Dynamic User ID, que volta no callback assinado).
     */
    fun setRewardUserId(id: String) {}
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
                aoFalhar = { e -> acabar(e) },
                aoRecompensar = {})
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
            AdKind.AiRewarded -> rewardedPronto = 0L
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

    // -- Rewarded da geração por IA ---------------------------------------------

    private var rewardedPronto = 0L
    private var carregandoRewarded = false
    private val esperandoRewarded = ArrayList<Pair<() -> Unit, (String) -> Unit>>()

    /** Há um Rewarded carregado e dentro da validade, pronto para aparecer agora. */
    fun rewardedReady(): Boolean = podePedir && rewardedValido()

    /**
     * Amarra o próximo Rewarded a um id do servidor (o ticket da geração). O
     * callback server-to-server do provedor devolve esse id ASSINADO — é ele,
     * e não o callback do app, que libera a geração paga.
     */
    fun definirUsuarioDaRecompensa(id: String) {
        seguro { backend?.setRewardUserId(id) }
    }

    /**
     * Carrega o Rewarded da IA (ao entrar na tela AI Video, e depois de consumido).
     * `aoCarregar`/`aoFalhar` avisam quem está esperando por ELE ("Preparando geração...").
     * Cada um é chamado no máximo uma vez.
     */
    fun preloadRewarded(aoCarregar: (() -> Unit)? = null, aoFalhar: ((String) -> Unit)? = null) {
        val respondido = AtomicBoolean(false)
        val ok = { if (respondido.compareAndSet(false, true)) aoCarregar?.invoke() }
        val falha = { e: String -> if (respondido.compareAndSet(false, true)) aoFalhar?.invoke(e) }
        var encaminhado = false
        seguro {
            if (rewardedValido()) { encaminhado = true; ok(); return@seguro }
            val b = backend
            val motivo = when {
                b == null -> "anúncios não iniciados"
                !podePedir -> "sem consentimento/SDK"
                !ids.aiRewardedEnabled -> "sem ID de rewarded"
                else -> null
            }
            if (motivo != null) { encaminhado = true; falha(motivo); return@seguro }
            esperandoRewarded += ({ ok(); Unit } to { e: String -> falha(e); Unit })
            encaminhado = true
            if (carregandoRewarded) return@seguro
            carregandoRewarded = true
            b!!.loadRewarded(ids.aiRewarded, aoCarregar = {
                carregandoRewarded = false
                rewardedPronto = agora()
                log("[AUREA ADS] Rewarded loaded")
                avisarEspera(null)
            }, aoFalhar = { e ->
                carregandoRewarded = false
                onAdFailed(AdKind.AiRewarded, e)
                avisarEspera(e)
            })
        }
        if (!encaminhado) falha("erro ao preparar o anúncio")
    }

    private fun avisarEspera(erro: String?) {
        val lista = ArrayList(esperandoRewarded)
        esperandoRewarded.clear()
        lista.forEach { (ok, falha) -> seguro { if (erro == null) ok() else falha(erro) } }
    }

    /**
     * Mostra o Rewarded. A RECOMPENSA só existe por `aoRecompensa`, que vem do
     * callback oficial do SDK (onUserEarnedReward). `aoFechar(ganhou)` é só o
     * aviso de que a tela fechou — nunca concede nada. Devolve `false` (e chama
     * `aoFalhar`) se não havia anúncio para mostrar.
     */
    fun showRewarded(
        aoAbrir: () -> Unit,
        aoRecompensa: () -> Unit,
        aoFechar: (ganhou: Boolean) -> Unit,
        aoFalhar: (String) -> Unit,
    ): Boolean {
        var mostrou = false
        var motivo: String? = "erro ao mostrar o anúncio"
        seguro {
            val b = backend
            val f = frequency
            val host = hostAtual?.get()
            motivo = when {
                b == null || f == null -> "anúncios não iniciados"
                host == null -> "tela não visível"
                !podePedir -> "sem consentimento/SDK"
                telaCheiaAberta -> "outro anúncio na tela"
                !rewardedValido() -> "rewarded não carregado"
                else -> null
            }
            if (motivo != null) return@seguro
            val abriu = AtomicBoolean(false)
            val ganhou = AtomicBoolean(false)
            val fechou = AtomicBoolean(false)
            val fecharUmaVez = { erro: String? ->
                if (fechou.compareAndSet(false, true)) {
                    telaCheiaAberta = false
                    if (erro == null) {
                        onAdDismissed(AdKind.AiRewarded)
                        seguro { aoFechar(ganhou.get()) }
                    } else {
                        onAdFailed(AdKind.AiRewarded, erro)
                        seguro { if (abriu.get()) aoFechar(ganhou.get()) else aoFalhar(erro) }
                    }
                }
            }
            telaCheiaAberta = true
            val tentou = try {
                b!!.show(AdKind.AiRewarded, host!!,
                    aoAbrir = {
                        if (abriu.compareAndSet(false, true)) {
                            f!!.recordShown(AdKind.AiRewarded, agora())
                            log("[AUREA ADS] Rewarded shown")
                            seguro { aoAbrir() }
                        }
                    },
                    aoFechar = { fecharUmaVez(null) },
                    aoFalhar = { e -> fecharUmaVez(e) },
                    aoRecompensar = {
                        if (ganhou.compareAndSet(false, true)) {
                            log("[AUREA ADS] Reward earned")
                            seguro { aoRecompensa() }
                        }
                    })
            } catch (t: Throwable) {
                motivo = t.message ?: t.javaClass.simpleName
                false
            }
            consumir(AdKind.AiRewarded)
            if (!tentou) {
                telaCheiaAberta = false
                if (motivo == null) motivo = "sem anúncio para mostrar"
                return@seguro
            }
            mostrou = true
            try {
                agendar(f!!.policy.showStartTimeoutMs) {
                    if (!abriu.get()) fecharUmaVez("não abriu em ${f.policy.showStartTimeoutMs} ms")
                }
            } catch (_: Throwable) {
            }
        }
        if (!mostrou) {
            log("[AUREA ADS] Ad unavailable - continuing normally (Rewarded: $motivo)")
            aoFalhar(motivo ?: "anúncio indisponível")
        }
        return mostrou
    }

    private fun rewardedValido(): Boolean {
        val f = frequency ?: return false
        if (rewardedPronto == 0L) return false
        if (agora() - rewardedPronto > f.policy.rewardedMaxAgeMs) { consumir(AdKind.AiRewarded); return false }
        return true
    }

    private fun nomeDe(kind: AdKind) = when (kind) {
        AdKind.AppOpen -> "AppOpen"
        AdKind.ExportInterstitial -> "Interstitial"
        AdKind.AiRewarded -> "Rewarded"
    }

    fun onAdDismissed(kind: AdKind) = seguro {
        log("[AUREA ADS] ${nomeDe(kind)} dismissed")
    }

    fun onAdFailed(kind: AdKind, erro: String) = seguro {
        log("[AUREA ADS] Ad error: ${nomeDe(kind)}: $erro")
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
        appOpenPronto = 0L; interstitialPronto = 0L; rewardedPronto = 0L
        carregandoRewarded = false; esperandoRewarded.clear()
        carregandoAppOpen = false; carregandoInterstitial = false
        telaCheiaAberta = false; aberturaFriaResolvida = false; foiParaFundoEm = 0L
        hostAtual = null
    }
}
