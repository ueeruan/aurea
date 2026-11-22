package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Path
import android.graphics.Region
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.config.AppConfig
import com.aurea.aurea.config.AppConfigStore
import com.aurea.aurea.config.HyperMode
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.hyper.EntitlementCheck
import com.aurea.aurea.hyper.EntitlementProvider
import com.aurea.aurea.hyper.HyperAccess
import com.aurea.aurea.hyper.HyperEntitlements
import com.aurea.aurea.hyper.ServerEntitlementProvider
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Text3DPreset
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

/** Device run with WAV, MP4, and PNG fixtures staged in the UI-test package's files directory. */
class Aurea104DeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    @After fun clearTestAccount() {
        compose.runOnUiThread { HyperEntitlements.clear() }
        HyperEntitlements.useProvider(ServerEntitlementProvider())
        AppConfigStore.overrideForTest(null)
    }

    private fun open(title: String) {
        assertTrue(context.packageName.endsWith(".uitest"))
        val now = System.currentTimeMillis() / 1000L
        AppConfigStore.overrideForTest(AppConfig("android", 0, 0, "", HyperMode.Free, emptyMap(),
            now - 60, now + 86400, "", ""))
        compose.runOnUiThread { HyperEntitlements.clear() }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30_000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(480, 320, 30f, title) }
        compose.waitUntil(15_000) { store.project.title == title && store.busyMessage == null }
    }

    private fun grantAnnualPlan() {
        HyperEntitlements.useProvider(object : EntitlementProvider {
            override suspend fun fetch(sessionToken: String) =
                EntitlementCheck.Granted(setOf("aurea_hyper_anual"), System.currentTimeMillis() / 1000L)
        })
        compose.runOnIdle { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com") }
        compose.waitUntil(10_000) {
            HyperEntitlements.accessFor(null, subscriptionOnly = true) == HyperAccess.Unlocked
        }
    }

    private fun confirmPaywall() {
        compose.onNodeWithTag("assinatura").assertExists()
        compose.runOnIdle { assertTrue(store.hyperOptionSubscriptionOnly) }
        compose.onNodeWithTag("assinatura.fechar").performClick()
    }

    private fun fixture(name: String): Uri {
        val file = File(context.filesDir, "aurea104-fixtures/$name")
        assumeTrue("Missing staged fixture $file", file.length() > 0)
        return Uri.fromFile(file)
    }

    private fun media(kind: Int): Long {
        compose.waitUntil(30_000) { store.busyMessage == null && store.layers.any { it.kind == kind } }
        return compose.runOnIdle {
            assertNull(store.errorMessage)
            store.layers.first { it.kind == kind }.id
        }
    }

    private fun capture(name: String, preview: Boolean = false): Bitmap {
        compose.waitForIdle()
        SystemClock.sleep(150)
        val bitmap = requireNotNull(store.captureBitmap(360, preview)) { "No rendered frame for $name" }
        val directory = File(context.filesDir, "aurea104-results").apply { mkdirs() }
        File(directory, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        return bitmap
    }

    private fun differentPixels(before: Bitmap, after: Bitmap): Int {
        assertEquals(before.width, after.width)
        assertEquals(before.height, after.height)
        val a = IntArray(before.width * before.height)
        val b = IntArray(a.size)
        before.getPixels(a, 0, before.width, 0, 0, before.width, before.height)
        after.getPixels(b, 0, after.width, 0, 0, after.width, after.height)
        return a.indices.count { a[it] != b[it] }
    }

    @Test fun autoBeatDetectsAudioAndChangesTheRenderedShapeWithoutAPlan() {
        open("1.0.4 · AutoBeat")
        compose.runOnIdle { store.importAudio(fixture("beats.wav")) }
        val audio = media(3)
        val audioName = compose.runOnIdle { store.layers.first { it.id == audio }.name }
        val effect = effectTypeId("aurea.move.autobeat")
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10_000) { store.primary != null && store.primary != audio && store.detail != null }
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.runOnIdle { store.addEffectAndFocus(effect) }
        compose.onNodeWithTag("effects.autobeat.source").performScrollTo().performClick()
        compose.onAllNodesWithText(audioName).onLast().performClick()
        compose.waitUntil(25_000) { !store.detectingBeats && store.markers.frames.isNotEmpty() }
        val beat = compose.runOnIdle {
            assertTrue(store.effects.any { it.typeId == effect && it.enabled })
            assertTrue(store.markers.frames.size >= 4)
            store.markers.frames.first().also { store.seek(it) }
        }
        val onBeat = capture("autobeat-pulse")
        compose.runOnIdle { store.removeEffect(store.effects.single { it.typeId == effect }.effectId) }
        compose.waitUntil(10_000) { store.effects.none { it.typeId == effect } }
        compose.runOnIdle { store.seek(beat) }
        val baseline = capture("autobeat-baseline")
        val differences = differentPixels(baseline, onBeat)
        assertTrue("The beat must visibly animate the shape (changed pixels=$differences)", differences > 75)
        Log.i("Aurea104Device", "AutoBeat markers=${store.markers.frames.size}, pulse pixels=$differences")
    }

    @Test fun planarTracksFourCornersAndAppliesAnimatedCornerPin() {
        open("1.0.4 · Superfície")
        compose.runOnIdle { store.importVideo(fixture("planar.mp4")) }
        val video = media(1)
        compose.runOnIdle { store.importImage(fixture("label.png")) }
        val label = media(2)
        compose.runOnIdle {
            store.select(video)
            store.seek(0)
        }
        openTrackingPanel()
        val surface = compose.onNodeWithText("${context.getString(R.string.trk_surface_quick)} · ${context.getString(R.string.hyper_title)}")
        surface.performScrollTo().performClick()
        compose.runOnIdle { assertNull(store.pointPick) }
        confirmPaywall()
        grantAnnualPlan()
        surface.performScrollTo().performClick()
        compose.runOnIdle {
            assertNotNull(store.pointPick)
            store.finishPointPick(75f, 70f)
            assertNotNull(store.pointPick)
            store.finishPointPick(286f, 234f)
            assertTrue(store.motionPointsReady)
            store.removeMotionPoint()
            assertFalse(store.motionPointsReady)
            store.finishPointPick(286f, 234f)
        }
        compose.onNodeWithTag("tracking.confirm").performScrollTo().performClick()
        compose.waitUntil(75_000) { store.motionStatus[0].toInt() in 2..3 }
        val initial = compose.runOnIdle {
            assertEquals(store.motionMessage, 2, store.motionStatus[0].toInt())
            assertEquals(2, store.motionStatus[4].toInt())
            store.seek(0)
            requireNotNull(store.motionSurface())
        }
        val tracked = compose.runOnIdle {
            store.seek(35)
            requireNotNull(store.motionSurface())
        }
        val horizontal = tracked[0] - initial[0]
        assertTrue("Expected motion of 70 source pixels, measured $horizontal", abs(horizontal - 70f) < 16f)
        compose.onNodeWithTag("tracking.review").assertExists()
        compose.runOnIdle {
            val csv = requireNotNull(store.motionCsv(video))
            assertTrue(csv.startsWith("timeline_frame,source_us,valid,confidence"))
            assertTrue(csv.lineSequence().count() >= 36)
            assertTrue(requireNotNull(store.motionTrail()).size >= 4 * 4)
            store.applyMotion(6)
            assertTrue(requireNotNull(store.masks).masks.single().keyCount > 8)
            store.undo()
        }
        compose.waitUntil(8_000) { store.masks?.masks.isNullOrEmpty() }
        compose.runOnIdle {
            store.applyMotion(2, target = label)
            store.select(label)
        }
        val cornerPin = effectTypeId("aurea.distort.corner_pin")
        compose.waitUntil(10_000) { store.effects.any { it.typeId == cornerPin } }
        compose.runOnIdle {
            val applied = store.effects.single { it.typeId == cornerPin }
            assertTrue(store.keyframes[label].orEmpty().count { it.effectIndex == applied.effectId } > 8)
        }
        capture("planar-tracked")
        Log.i("Aurea104Device", "Planar displacement=$horizontal px")
    }

    private fun openTrackingPanel() {
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.onNodeWithTag("aurea.effects.add").performScrollTo().performClick()
        compose.onNodeWithTag("effects.search").performClick()
        compose.onNodeWithTag("effects.search.field").performTextInput(context.getString(R.string.panel_rastreio))
        val tracking = effectCardId(EffectTool.Tracking.typeId)
        compose.onNodeWithTag("effects.search.results").performScrollToNode(hasTestTag("effects.result.$tracking"))
        compose.onNodeWithTag("effects.result.$tracking").performClick()
        compose.onNodeWithTag("effects.detail.variant.$tracking").performScrollTo().performClick()
    }

    @Test fun automaticPointUsesRealClipAndExportsChronologicalReview() {
        open("Smart Tracker · Ponto para trás")
        compose.runOnIdle { store.importVideo(fixture("planar.mp4")) }
        val source = media(1)
        compose.runOnIdle {
            store.seek(35)
            store.motionBackward = true
        }
        openTrackingPanel()
        compose.onNodeWithTag("tracking.autoPoint").performScrollTo().performClick()
        compose.waitUntil(45_000) { store.motionStatus[0].toInt() in 2..3 }
        compose.runOnIdle {
            assertEquals(store.motionMessage, 2, store.motionStatus[0].toInt())
            val timeline = store.motionReview(source)
            assertEquals(0f, timeline[0], 0f)
            assertEquals(35f, timeline[1], 0f)
            val frames = requireNotNull(store.motionCsv(source)).lineSequence().drop(1).filter { it.isNotBlank() }
                .map { it.substringBefore(',').toInt() }.toList()
            assertEquals((0..35).toList(), frames)
            assertTrue(store.engineForStress.startMotionTrack(source, 0, 0, true, FloatArray(0), 12f, 48f))
            store.pauseMotion(true)
        }
        compose.waitUntil(5_000) { store.motionStatus[11] != 0f }
        SystemClock.sleep(180)
        val held = compose.runOnIdle {
            val raw = FloatArray(12)
            store.engineForStress.motionTrackStatus(raw)
            raw[1]
        }
        SystemClock.sleep(180)
        compose.runOnIdle {
            val raw = FloatArray(12)
            store.engineForStress.motionTrackStatus(raw)
            assertEquals(held, raw[1], 0f)
            store.pauseMotion(false)
        }
        compose.waitUntil(45_000) {
            val raw = FloatArray(12)
            store.engineForStress.motionTrackStatus(raw)
            raw[0].toInt() == 2
        }
    }

    @Test fun cameraViewerDisplaysSolvedPointsAndCameraFrustum() {
        open("Câmera · Reconstrução")
        compose.runOnIdle { store.importVideo(fixture("camera-slip.mp4")) }
        val source = media(1)
        openTrackingPanel()
        compose.onNodeWithText(context.getString(R.string.panel_camera_3d)).performClick()
        compose.runOnIdle { store.startCameraTrack(1, 1, 55f, 2) }
        compose.waitUntil(95_000) { (store.cameraTrack?.state ?: 0) in 2..4 }
        compose.runOnIdle {
            val status = requireNotNull(store.cameraTrack)
            assertEquals(status.message, 2, status.state)
            store.seek(12)
            val rows = store.cameraReconstruction(source, 12)
            assertTrue(rows.indices.step(6).count { rows[it] == 0f } > 12)
            assertTrue(rows.indices.step(6).count { rows[it] == 1f } > 12)
            assertEquals(5, rows.indices.step(6).count { rows[it] == 2f })
            Log.i("Aurea104Device", "Camera solve error=${status.averageErrorPx} px, confidence=${status.confidence}")
        }
        compose.onNodeWithTag("cam.reconstruction").performScrollTo().performClick()
        compose.onNodeWithTag("cam.reconstruction.canvas").assertIsDisplayed()
    }

    @Test fun faceAndBodyCreateEditableLandmarkNulls() {
        open("Smart Tracker · Rosto e corpo")
        grantAnnualPlan()
        compose.runOnIdle { store.importVideo(fixture("pose-motion.mp4")) }
        val source = media(1)
        openTrackingPanel()
        compose.onNodeWithTag("tracking.face").performScrollTo().performClick()
        compose.waitUntil(90_000) { store.landmarkProgress == null && store.layers.any { it.name == "Head pose" } }
        compose.runOnIdle {
            val eye = store.layers.first { it.name == "Left eye" }
            store.select(eye.id, openOptions = false)
            assertTrue(store.keyframes[eye.id].orEmpty().size > 6)
            store.select(source)
        }
        compose.runOnIdle { store.trackLandmarks(false) }
        compose.waitUntil(90_000) { store.landmarkProgress == null && store.layers.any { it.name == "Left shoulder" } }
        compose.runOnIdle {
            val joint = store.layers.first { it.name == "Left shoulder" }
            store.select(joint.id, openOptions = false)
            assertTrue(store.keyframes[joint.id].orEmpty().size > 6)
        }
    }

    @Test fun inkPosterRequiresAPlanAndRendersNewMaterial() {
        open("1.0.4 · Pôster em tinta")
        grantAnnualPlan()
        compose.runOnIdle { store.addText3D(openEditor = false) }
        compose.waitUntil(15_000) { store.text3d != null }
        compose.runOnIdle {
            store.setText3D(store.text3d!!.copy(content = "AUREA"))
            HyperEntitlements.clear()
        }
        compose.onNodeWithTag("layer.panel.colorFill").performClick()
        val material = compose.onNodeWithTag("text3d.materialPreset.${Text3DPreset.InkPoster.ordinal}")
        material.performScrollTo().performClick()
        compose.runOnIdle { assertFalse(store.text3d!!.cartoon) }
        confirmPaywall()
        val original = capture("text-original")
        grantAnnualPlan()
        material.performScrollTo().performClick()
        compose.waitUntil(15_000) { store.text3d?.cartoon == true }
        compose.runOnIdle {
            val style = requireNotNull(store.text3d)
            assertFalse(style.toonSideAuto)
            assertTrue(style.outlineWidth > 0f && style.depth > 0f)
            assertTrue(style.color[0] > style.toonSideColor[0] + .5f)
        }
        val differences = differentPixels(original, capture("text-ink-poster"))
        assertTrue("The poster must change rendered pixels (changed pixels=$differences)", differences > 75)
        Log.i("Aurea104Device", "Ink Poster changed pixels=$differences")
    }

    @Test fun autoRigSeparatesAndAnimatesYuji() = checkLayeredRig("Yuji", "yuji-action.png")

    @Test fun autoRigSeparatesAndAnimatesLuffy() = checkLayeredRig("Luffy", "luffy.png")

    @Test fun autoRigKeepsExtendedHandOnTheSameLayer() {
        open("AutoRig · Mão em primeiro plano")
        grantAnnualPlan()
        compose.runOnIdle {
            store.setCompositionSize(672, 968)
            store.setCompositionDuration(240)
            store.importImage(fixture("autorig-extended-hand.png"))
        }
        media(2)
        val puppet = effectTypeId("aurea.distort.puppet")
        capture("extended-hand-source")
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.runOnIdle { store.addEffectAndFocus(puppet) }
        compose.onNodeWithTag("fx.puppet.autoRigParts").performScrollTo().performClick()
        compose.waitUntil(45_000) { store.layers.any { it.name.contains("Mão ") } }
        compose.runOnIdle { store.seek(173) }
        capture("extended-hand-173")
        compose.runOnIdle {
            val hand = store.layers.single { it.name.endsWith("Mão direita") }
            store.select(hand.id, openOptions = false)
            val masks = requireNotNull(store.masks)
            assertEquals(hand.id, masks.layer)
            val silhouette = Region()
            for (mask in masks.masks) {
                val path = Path().apply {
                    moveTo(mask.points[0], mask.points[1])
                    for (i in 1 until mask.count) lineTo(mask.points[i * 6], mask.points[i * 6 + 1])
                    close()
                }
                val filled = Region()
                filled.setPath(path, Region(0, 0, 4096, 4096))
                silhouette.op(filled, if (mask.op == 1) Region.Op.DIFFERENCE else Region.Op.UNION)
            }
            val handSamples = listOf(220f to 854f, 120f to 965f, 120f to 1080f, 160f to 1200f, 360f to 1040f)
            for ((x, y) in handSamples) {
                val local = requireNotNull(masks.toLayer(x * 672f / 1342f, y * 968f / 1934f))
                assertTrue("Hand region lost source pixel ($x, $y)", silhouette.contains(local[0].toInt(), local[1].toInt()))
            }
        }
    }

    private fun checkLayeredRig(character: String, file: String) {
        open("$character · AutoRig em camadas")
        grantAnnualPlan()
        compose.runOnIdle { store.importImage(fixture(file)) }
        val source = media(2)
        val puppet = effectTypeId("aurea.distort.puppet")
        val rest = capture("$character-layered-source")
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.runOnIdle { store.addEffectAndFocus(puppet) }
        compose.onNodeWithTag("fx.puppet.autoRigParts").performScrollTo().performClick()
        compose.waitUntil(45_000) { store.layers.count { it.kind == 2 } >= 3 }
        val layers = compose.runOnIdle { store.layers.filter { it.kind == 2 } }
        assertTrue(layers.any { it.id == source && it.name.endsWith("Tronco e base") })
        assertTrue(layers.any { it.name.endsWith("Rosto") })
        assertTrue(layers.any { it.name.endsWith("Cabelo") })
        assertTrue(layers.any { it.name.contains("Mão ") })
        val eyelids = compose.runOnIdle { store.layers.filter { it.name.contains("Pálpebra ") } }
        assertEquals(2, eyelids.size)
        for (layer in layers) {
            val effect = compose.runOnIdle {
                store.select(layer.id, openOptions = false)
                store.effects.single { it.typeId == puppet }.effectId
            }
            val bind = compose.runOnIdle { store.seek(0); store.puppetPins(layer.id, effect) }
            val moving = compose.runOnIdle { store.seek(16); store.puppetPins(layer.id, effect) }
            assertEquals(3 * PuppetStage.PIN_FLOATS, bind.size)
            assertTrue("Expected editable animated pins on ${layer.name}", bind.indices.any { abs(bind[it] - moving[it]) > .001f })
        }
        val moved = capture("$character-layered-motion")
        compose.runOnIdle { store.seek(0) }
        val recomposed = capture("$character-layered-rest")
        assertTrue("Expected the effect to change rendered pixels", differentPixels(recomposed, moved) > 75)
        assertTrue("Rest frame should preserve the source", differentPixels(rest, recomposed) < rest.width * rest.height / 4)
        compose.runOnIdle { store.seek(36) }
        capture("$character-layered-blink")
        Log.i("Aurea104Device", "$character layer names: ${layers.map { it.name }}")
    }

    @Test fun autoRigDetectsPngAndAnimatesEditablePins() {
        open("1.0.4 · AutoRig 2D")
        compose.runOnIdle { store.importImage(fixture("person.png")) }
        val image = media(2)
        val puppet = effectTypeId("aurea.distort.puppet")
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.runOnIdle { store.addEffectAndFocus(puppet) }
        val control = compose.onNodeWithTag("fx.puppet.autoRigImage")
        control.performScrollTo().performClick()
        confirmPaywall()
        val effect = compose.runOnIdle { store.effects.single { it.typeId == puppet }.effectId }
        compose.runOnIdle { assertTrue(store.puppetPins(image, effect).isEmpty()) }
        grantAnnualPlan()
        control.performScrollTo().performClick()
        compose.waitUntil(45_000) { store.puppetPins(image, effect).isNotEmpty() }
        compose.waitUntil(8_000) { puppetActive(store) }
        val first = compose.runOnIdle {
            assertTrue("Pin editor must stay open after creating the rig (editing=${PuppetStage.editing}, " +
                "stage layer=${PuppetStage.layer}, expected layer=$image, stage effect=${PuppetStage.effect}, " +
                "expected effect=$effect, selected=${store.selection}, detail=${store.detail?.id})", puppetActive(store))
            store.seek(0)
            store.puppetPins(image, effect)
        }
        val second = compose.runOnIdle {
            store.seek(15)
            store.puppetPins(image, effect)
        }
        assertTrue("Expected at least five pose pins, detected ${first.size / 4}", first.size >= 5 * 4)
        assertEquals(first.size, second.size)
        assertTrue("Pins must have animated positions", first.indices.any {
            it % 4 in 1..2 && abs(first[it] - second[it]) > .001f
        })
        val moved = capture("autorig-pose")
        compose.runOnIdle { store.seek(0) }
        val differences = differentPixels(capture("autorig-rest"), moved)
        assertTrue("The animated pins must deform the PNG (changed pixels=$differences)", differences > 75)
        Log.i("Aurea104Device", "AutoRig created ${first.size / 4} animated editable pins")
    }
}
