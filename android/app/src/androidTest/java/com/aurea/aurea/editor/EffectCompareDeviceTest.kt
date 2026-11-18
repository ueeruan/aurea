package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.hyper.EntitlementCheck
import com.aurea.aurea.hyper.EntitlementProvider
import com.aurea.aurea.hyper.HyperAccess
import com.aurea.aurea.hyper.HyperEntitlements
import com.aurea.aurea.hyper.ServerEntitlementProvider
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.After
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * Comparação com o original (After Effects): uma imagem, um efeito com o padrão,
 * quadros escolhidos. Argumentos do `am instrument`: `image` (arquivo legível pelo
 * app), `effect` (chave do motor), `frames` (ex. "30,75"), `size` (lado da comp).
 * Saída: files/effect-compare/<frame>.png.
 */
class EffectCompareDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    private class AllAccess : EntitlementProvider {
        override suspend fun fetch(sessionToken: String): EntitlementCheck =
            EntitlementCheck.Granted(setOf("aurea_hyper_mensal", HyperEntitlements.PRODUCT), 1)
    }

    @After fun tearDown() {
        com.aurea.aurea.config.AppConfigStore.overrideForTest(null)
        HyperEntitlements.useProvider(ServerEntitlementProvider())
        compose.runOnUiThread { HyperEntitlements.clear() }
    }

    @Test fun renderEffectFrames() {
        check(context.packageName.endsWith(".uitest"))
        val args = InstrumentationRegistry.getArguments()
        val image = File(checkNotNull(args.getString("image")))
        val effect = checkNotNull(args.getString("effect"))
        val frames = (args.getString("frames") ?: "30").split(",").map { it.trim().toInt() }
        val size = (args.getString("size") ?: "607").toInt()
        val now = System.currentTimeMillis() / 1000
        com.aurea.aurea.config.AppConfigStore.overrideForTest(com.aurea.aurea.config.AppConfig("android", 0, 0, "",
            com.aurea.aurea.config.HyperMode.Entitled, emptyMap(), now - 60, now + 86400, "", ""))
        HyperEntitlements.useProvider(AllAccess())
        compose.runOnUiThread { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com") }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.waitUntil(5000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.runOnIdle { store.newProject(size, size, 30f, "Comparar efeito") }
        compose.waitUntil(15000) { store.project.title == "Comparar efeito" && !store.projectOperationBusy }
        compose.runOnIdle { store.importImage(android.net.Uri.fromFile(image)) }
        compose.waitUntil(20000) { store.layers.size == 1 && store.detail != null }
        val type = effectTypeId(effect)
        compose.runOnIdle { store.addEffect(type) }
        compose.waitUntil(8000) { store.effects.any { it.typeId == type } }
        val dir = File(context.filesDir, "effect-compare").apply { deleteRecursively(); mkdirs() }
        for (f in frames) {
            compose.runOnIdle { store.seek(f) }
            compose.waitForIdle(); SystemClock.sleep(400); compose.waitForIdle()
            store.captureBitmap(size)?.recycle()
            SystemClock.sleep(300)
            val bmp = checkNotNull(store.captureBitmap(size)) { "quadro $f nulo" }
            File(dir, "$f.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        }
    }
}
