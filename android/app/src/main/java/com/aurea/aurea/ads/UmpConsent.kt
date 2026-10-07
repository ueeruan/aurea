package com.aurea.aurea.ads

import android.app.Activity
import android.content.Context
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform
import java.util.concurrent.atomic.AtomicBoolean

/**
 * O consentimento oficial do Google (UMP), comum a qualquer provedor: ele grava
 * o consentimento no padrão IAB TCF, que o AdMob, o LevelPlay e as redes
 * mediadas leem. Nada de consentimento presumido: `aoDecidir(true)` só quando
 * `canRequestAds()` permite.
 */
object UmpConsent {
    fun pedir(activity: Activity, aoDecidir: (podePedir: Boolean) -> Unit) {
        val uma = AtomicBoolean(false)
        val decidir = { ok: Boolean -> if (uma.compareAndSet(false, true)) aoDecidir(ok) }
        val consent: ConsentInformation = UserMessagingPlatform.getConsentInformation(activity.applicationContext)
        consent.requestConsentInfoUpdate(activity, ConsentRequestParameters.Builder().build(), {
            // O formulário oficial só aparece se a região/usuário exigir.
            UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) { _ -> decidir(consent.canRequestAds()) }
        }, { _ ->
            // Sem rede para atualizar: vale o que já estava decidido antes.
            decidir(consent.canRequestAds())
        })
        // Sessões anteriores já decidiram: pode seguir em paralelo (recomendação do Google).
        if (consent.canRequestAds()) decidir(true)
    }

    /**
     * GDPR/UMP: onde a região exige (EEE, Reino Unido, Suíça), a pessoa tem de
     * poder rever o consentimento a qualquer hora — a linha "Privacidade dos
     * anúncios" em Ajustes só aparece quando isto é verdade.
     */
    fun opcoesDePrivacidadeExigidas(context: Context): Boolean = runCatching {
        UserMessagingPlatform.getConsentInformation(context.applicationContext).privacyOptionsRequirementStatus ==
            ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED
    }.getOrDefault(false)

    fun mostrarOpcoesDePrivacidade(activity: Activity, aoTerminar: () -> Unit = {}) {
        runCatching { UserMessagingPlatform.showPrivacyOptionsForm(activity) { aoTerminar() } }
            .onFailure { aoTerminar() }
    }
}
