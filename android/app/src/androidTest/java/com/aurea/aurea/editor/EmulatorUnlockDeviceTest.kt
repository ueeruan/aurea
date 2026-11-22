package com.aurea.aurea.editor

import android.net.Uri
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import com.aurea.aurea.AureaApplication
import com.aurea.aurea.MainActivity
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.detectHeadbopPhoto
import com.aurea.aurea.effects.HyperEffectGate
import com.aurea.aurea.effects.Text3DGate
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.hyper.HyperAccess
import com.aurea.aurea.hyper.HyperEntitlements
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlinx.coroutines.runBlocking

class EmulatorUnlockDeviceTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test fun privateOptInUnlocksTheEditorAndIllustratedPose() {
        val app = compose.activity.application as AureaApplication
        assumeTrue(File(app.filesDir, "unlock-emulator-editor").isFile)
        val store = app.editorStore()
        compose.waitUntil(35_000) { store.engineReady && store.catalog.isNotEmpty() }
        compose.runOnIdle {
            assertEquals(HyperAccess.Unlocked, HyperEntitlements.access)
            assertTrue(store.requireHyperSubscription())
            assertFalse(Text3DGate.locked())
            val paid = store.catalog.filter { AureaEngine.nativeEffectRequiresHyper(it.typeId) }
            assertTrue(paid.isNotEmpty())
            assertTrue(paid.none { HyperEffectGate.locked(it.typeId) })
        }

        val image = File(app.filesDir, "aurea104-fixtures/yuji-action.png")
        val weights = File(app.filesDir, "models/pose/humanart-tiny.bin")
        assertTrue(image.isFile && weights.isFile)
        val title = "AutoRig · Teste no emulador"
        compose.runOnIdle { store.newProject(480, 576, 30f, title) }
        compose.waitUntil(20_000) { store.project.title == title && store.busyMessage == null }
        compose.runOnIdle { store.importImage(Uri.fromFile(image)) }
        compose.waitUntil(25_000) { store.busyMessage == null && store.layers.any { it.kind == 2 } }
        val layer = compose.runOnIdle { store.layers.single { it.kind == 2 }.id }
        val puppet = effectTypeId("aurea.distort.puppet")
        compose.runOnIdle { store.addEffectAndFocus(puppet) }
        val effect = compose.runOnIdle { store.effects.single { it.typeId == puppet }.effectId }
        val pose = runBlocking {
            detectHeadbopPhoto(app, image.path, rigOnly = true, illustratedModel = weights.path.removeSuffix(".bin"))
        }
        val keypoints = requireNotNull(pose).first.copyOfRange(0, 2 + 33 * 4)
        compose.runOnIdle {
            assertEquals(3, store.puppetAutoRigImage(layer, effect, keypoints))
            assertFalse(store.hyperOptionPrompt)
            assertEquals(3, store.puppetPins(layer, effect).size / PuppetStage.PIN_FLOATS)
        }
    }
}
