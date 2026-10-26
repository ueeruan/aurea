package com.aurea.aurea.hyper

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.home.AureaDonations
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * AureaHyper na Home: o portão (sem conta → bloqueado, sem botão de compra;
 * com o acesso → as telas 1–3) e o fluxo até a prévia. A conta é simulada por
 * um EntitlementProvider falso — nada de termos, cadastro ou rede.
 */
class HyperScreenDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    private class FakeProvider(private val products: Set<String>) : EntitlementProvider {
        var calls = 0
        override suspend fun fetch(sessionToken: String): EntitlementCheck { calls++; return EntitlementCheck.Granted(products, 1) }
    }

    /** Config remota em dia (sem rede): o modo do AureaHyper vem daqui. */
    private fun config(mode: com.aurea.aurea.config.HyperMode, minVersion: Long = 0) {
        val now = System.currentTimeMillis() / 1000
        com.aurea.aurea.config.AppConfigStore.overrideForTest(com.aurea.aurea.config.AppConfig("android", minVersion, minVersion, "",
            mode, emptyMap(), now - 60, now + 86400, "", ""))
    }

    private fun launch(signedIn: Boolean = false) {
        check(context.packageName.endsWith(".uitest"))
        com.aurea.aurea.captions.KeyVault(context).apply {
            remove("conta_token"); remove("conta_email")
            // Sessão falsa SÓ no cofre do app de teste: o portão pergunta ao provedor falso, nunca à rede.
            if (signedIn) { put("conta_token", "t".repeat(43)); put("conta_email", "teste@exemplo.com") }
        }
        context.getSharedPreferences("aurea.releaseNotes", android.content.Context.MODE_PRIVATE).edit()
            .putString("read", "${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}").commit()
        context.getSharedPreferences("aurea_hyper", android.content.Context.MODE_PRIVATE).edit().clear().commit()
        AureaDonations.launchPromptPending = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            com.aurea.aurea.ui.AureaApp(store)
        }
        compose.waitForIdle()
    }

    @After fun tearDown() {
        com.aurea.aurea.config.AppConfigStore.overrideForTest(null)
        com.aurea.aurea.captions.KeyVault(context).apply { remove("conta_token"); remove("conta_email") }
        HyperEntitlements.useProvider(ServerEntitlementProvider())
        compose.runOnUiThread { HyperEntitlements.clear() }
    }

    @Test fun withoutAccountTheScreenIsLockedAndHonest() {
        launch()
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Entitled) }
        compose.runOnUiThread { HyperEntitlements.clear(); store.openHyper() }
        compose.waitForIdle()
        assertEquals(Screen.Hyper, store.screen)
        compose.onNodeWithTag("hyper.bloqueado").assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.hyper_locked_badge)).assertIsDisplayed()
        compose.onNodeWithTag("hyper.bloqueado.entrar").performScrollTo().assertIsDisplayed()
        // Nenhum passo do fluxo nem botão de compra aparece sem o acesso.
        compose.onNodeWithTag("hyper.escolher.galeria").assertDoesNotExist()
        compose.onNodeWithTag("hyper.passo.indicador").assertDoesNotExist()
        compose.onNodeWithTag("hyper.voltar").performClick()
        compose.waitForIdle()
        assertEquals(Screen.Home, store.screen)
    }

    @Test fun accountWithoutTheProductStaysLocked() {
        val fake = FakeProvider(emptySet())
        HyperEntitlements.useProvider(fake)
        launch(signedIn = true)
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Entitled) }
        compose.runOnUiThread { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com"); store.openHyper() }
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.Locked }
        compose.onNodeWithText(context.getString(R.string.hyper_locked_not_owned)).performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag("hyper.bloqueado.verificar").performScrollTo().assertIsDisplayed()
        assertTrue(fake.calls >= 1)
    }

    @Test fun unlockedFlowReachesThePreview() {
        HyperEntitlements.useProvider(FakeProvider(setOf(HyperEntitlements.PRODUCT)))
        launch(signedIn = true)
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Entitled) }
        compose.runOnUiThread { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com") }
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.runOnUiThread { store.openHyper() }
        compose.waitForIdle()
        // A tela confere de novo ao abrir (provedor falso): o acesso continua liberado.
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.onNodeWithTag("hyper.escolher.galeria").assertIsDisplayed()
        compose.onNodeWithTag("hyper.escolher.gravar").assertIsDisplayed()
        compose.onNodeWithTag("hyper.passo.indicador").assertIsDisplayed()

        // 1 → 2: o vídeo de teste do APK uiTest entra pelo mesmo caminho da galeria.
        val video = File(context.cacheDir, "hyper-test.mp4")
        context.assets.open("review-motion.mp4").use { input ->
            video.outputStream().use { input.copyTo(it) }
        }
        compose.runOnUiThread { store.hyper.pick(Uri.fromFile(video)) }
        compose.waitUntil(15_000) { store.hyper.step == HyperFlow.Step.Recipe }
        compose.onNodeWithTag("hyper.continuar").performScrollTo().assertIsNotEnabled()
        compose.onNodeWithTag("hyper.receita.sharper").performScrollTo().performClick()
        compose.onNodeWithTag("hyper.receita.sharper.4").performScrollTo().performClick()
        compose.runOnIdle { assertEquals(4, store.hyper.recipe.upscale) }
        compose.onNodeWithTag("hyper.ajustar").performScrollTo().performClick()
        compose.onNodeWithTag("hyper.ajuste.rosto").performScrollTo().assertIsDisplayed()
        compose.runOnIdle { assertTrue("o rosto fica desligado fora da restauração", !store.hyper.recipe.face) }
        compose.onNodeWithTag("hyper.continuar").performScrollTo().assertIsEnabled().performClick()

        // 3: prévia, caminhos e o medidor (o motor decide o que está disponível).
        compose.waitUntil(5_000) { store.hyper.step == HyperFlow.Step.Preview }
        compose.onNodeWithTag("hyper.previa").assertIsDisplayed()
        for (p in listOf("turbo", "lite", "max")) compose.onNodeWithTag("hyper.caminho.$p").performScrollTo().assertIsDisplayed()
        compose.waitUntil(60_000) { !store.hyper.measuring && !store.hyper.estimating }
        compose.onNodeWithTag("hyper.comecar").performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag("hyper.voltar").performClick()
        compose.runOnIdle { assertEquals(HyperFlow.Step.Recipe, store.hyper.step) }
        video.delete()
    }

    @Test fun freeModeUnlocksWithoutAccountAndOffLocks() {
        launch()
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Free); HyperEntitlements.clear(); store.openHyper() }
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.onNodeWithTag("hyper.escolher.galeria").assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.hyper_locked_badge)).assertDoesNotExist()
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Off) }
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.Off }
        compose.onNodeWithText(context.getString(R.string.hyper_off_default)).performScrollTo().assertIsDisplayed()
        compose.runOnUiThread { com.aurea.aurea.config.AppConfigStore.overrideForTest(null) }
        compose.waitUntil(5_000) { HyperEntitlements.access == HyperAccess.NeedsConfig }
        compose.onNodeWithText(context.getString(R.string.hyper_needs_internet)).performScrollTo().assertIsDisplayed()
    }

    @Test fun versionBelowTheMinimumShowsOnlyTheUpdateScreen() {
        launch()
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Free, minVersion = com.aurea.aurea.BuildConfig.VERSION_CODE + 1L) }
        compose.waitForIdle()
        compose.onNodeWithTag("atualizacao.obrigatoria").assertIsDisplayed()
        compose.onNodeWithTag("atualizacao.abrir").assertIsDisplayed()
        compose.onNodeWithTag("home.projects").assertDoesNotExist()
        compose.runOnUiThread { config(com.aurea.aurea.config.HyperMode.Free) }
        compose.waitForIdle()
        compose.onNodeWithTag("atualizacao.obrigatoria").assertDoesNotExist()
    }
}
