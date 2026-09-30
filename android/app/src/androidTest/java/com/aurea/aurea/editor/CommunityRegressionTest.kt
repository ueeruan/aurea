package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.content.ContentValues
import android.provider.MediaStore
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

/** Real editor gestures for the four community reports, with live engine state. */
class CommunityRegressionTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun launch() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 900, 30f, "Community regressions") }
        compose.waitUntil(15000) { store.project.title == "Community regressions" }
        compose.runOnIdle { store.snapping = false }
    }

    private fun seek(frame: Int) {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame }
    }

    private fun capture(name: String) {
        compose.waitForIdle(); Thread.sleep(650)
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }

    @Test fun markerButtonAndArrowsNavigateExactMarkersInsteadOfProjectEdges() {
        launch()
        for (frame in listOf(10, 40, 90)) {
            seek(frame)
            compose.onNodeWithTag("transport.marker").performClick()
            compose.waitUntil(5000) { frame in store.markers.frames }
        }
        seek(19)
        compose.onNodeWithTag("transport.next").performClick()
        compose.waitUntil(5000) { store.playhead == 40 }
        compose.onNodeWithTag("transport.previous").performClick()
        compose.waitUntil(5000) { store.playhead == 10 }
        compose.onNodeWithTag("transport.previous").performClick()
        compose.waitForIdle(); assertEquals(10, store.playhead)
        seek(90)
        compose.onNodeWithTag("transport.next").performClick()
        compose.waitForIdle(); assertEquals(90, store.playhead)
        compose.onNodeWithTag("transport.previous").performTouchInput { longClick() }
        compose.waitUntil(5000) { store.playhead == 0 }
        capture("markers")
    }

    @Test fun duplicateTextAndShapeMoveWithoutMovingTheOriginal() {
        launch()
        for (shape in listOf(false, true)) {
            val beforeIds = store.layers.map { it.id }.toSet()
            compose.runOnIdle { if (shape) store.addShape(1) else store.addText() }
            compose.waitUntil(5000) { store.layers.any { it.id !in beforeIds } && store.detail != null }
            val original = store.layers.first { it.id !in beforeIds }.id
            compose.waitUntil(5000) { store.primary == original && store.detail?.id == original }
            val before = store.queryDetail(original)!!
            compose.onNodeWithTag("transport.duplicate").performClick()
            compose.waitUntil(5000) { store.layers.size == beforeIds.size + 2 && store.primary != original && store.detail?.id == store.primary }
            val copy = store.primary!!
            assertEquals(setOf(copy), store.selection)
            val copied = store.queryDetail(copy)!!
            val corners = FloatArray(8)
            assertTrue(LayerGeometry.corners(copied, corners))
            compose.onNodeWithTag("editor.stage").performTouchInput {
                val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 720, 900, false) }
                val x = corners[0] * .8f + corners[2] * .2f
                val y = (corners[1] + corners[7]) / 2
                val start = Offset(mapper.sx(x), mapper.sy(y))
                swipe(start, start + Offset(0f, -90f), 350)
            }
            compose.waitUntil(5000) { store.queryDetail(copy)?.position != copied.position }
            assertEquals("original position", before.position, store.queryDetail(original)!!.position)
            assertEquals("original scale", before.scale, store.queryDetail(original)!!.scale)
            compose.runOnIdle { store.undo() }
            compose.waitUntil(5000) { store.queryDetail(copy)?.position == copied.position }
        }
    }

    @Test fun fittingAndLinkedSliderPreserveDepthAndSmallScaleRatios() {
        launch()
        compose.runOnIdle { store.addShape3D(0) }
        compose.waitUntil(10000) { store.detail?.kind == 10 && store.gizmo != null }
        val id = store.primary!!
        // Fit must repair the old XY-only fit path, retaining a uniform 3D volume.
        compose.runOnIdle { store.setScale3(floatArrayOf(.05f, .05f, 1f)) }
        compose.waitUntil(5000) { store.detail!!.scale == listOf(.05f, .05f, 1f) }
        compose.runOnIdle { LayerOps.fitToCanvas(store, listOf(id), fill = false) }
        compose.waitUntil(5000) { abs(store.detail!!.scale[0] - .05f) > .001f }
        val fit = store.detail!!.scale
        assertEquals(abs(fit[0]), abs(fit[1]), .00001f)
        assertEquals(abs(fit[0]), abs(fit[2]), .00001f)
        compose.onNodeWithText(context.getString(R.string.sh_dock_transform)).performClick()
        compose.onNodeWithContentDescription(context.getString(R.string.panel_escala)).performClick()
        for (auto in listOf(false, true)) {
            compose.runOnIdle { store.autoKeyTransforms = auto; store.setScale3(floatArrayOf(.05f, .1f, .15f)); store.scaleAxesLinked = true }
            compose.waitUntil(5000) { store.detail!!.scale == listOf(.05f, .1f, .15f) }
            compose.onNodeWithTag("transform.scale.x").performTouchInput {
                down(center); moveBy(Offset(-32f, 0f), 80); moveBy(Offset(-5f, 0f), 80); up()
            }
            compose.waitUntil(5000) { abs(store.detail!!.scale[0] - .05f) > .00001f }
            val scale = store.detail!!.scale
            assertTrue("downsizing actually shrinks", scale[0] < .05f)
            assertEquals("height follows the same factor", scale[0] * 2, scale[1], .00001f)
            assertEquals("depth follows the same factor", scale[0] * 3, scale[2], .00001f)
            compose.runOnIdle { store.undo() }
            compose.waitUntil(5000) { store.detail!!.scale == listOf(.05f, .1f, .15f) }
        }
        val density = context.resources.displayMetrics.density
        for (tag in listOf("gizmo.tool", "gizmo.space", "stage.autokey")) {
            val bounds = compose.onNodeWithTag(tag).fetchSemanticsNode().boundsInRoot
            assertTrue("compact control retains a 48 dp touch target", bounds.width / density >= 47.9f)
            assertTrue("compact control does not grow with its label", bounds.width / density <= 48.1f)
        }
        compose.runOnIdle { store.setScale3(floatArrayOf(1f, 1f, 1f)); store.setTransform(7, 35f) }
        capture("compact-controls")
    }

    @Test fun pinchKeepsDepthProportionalWhenTheObjectIsViewedFromTheSide() {
        launch()
        compose.runOnIdle { store.addShape3D(0); StageView.zoomLock = false }
        compose.waitUntil(10000) { store.detail?.kind == 10 && store.gizmo != null }
        for (angle in listOf(0f, 65f)) {
            compose.runOnIdle { store.setScale3(floatArrayOf(1f, 1f, 1f)); store.setTransform(7, angle) }
            compose.waitUntil(5000) { store.detail!!.scale == listOf(1f, 1f, 1f) && store.detail!!.rotation[1] == angle }
            val gizmo = store.gizmo!!.copyOf()
            compose.onNodeWithTag("editor.stage").performTouchInput {
                val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 720, 900, false) }
                val x = mapper.sx(gizmo[0]); val y = mapper.sy(gizmo[1])
                down(0, Offset(x - 70f, y)); down(1, Offset(x + 70f, y))
                for (i in 1..4) {
                    updatePointerTo(0, Offset(x - 70f + i * 8f, y))
                    updatePointerTo(1, Offset(x + 70f - i * 8f, y)); move(80)
                }
                up(0); up(1)
            }
            compose.waitUntil(5000) { store.detail!!.scale[0] < .9f }
            val scaled = store.detail!!.scale
            assertEquals(scaled[0], scaled[1], .00001f)
            assertEquals(scaled[0], scaled[2], .00001f)
            compose.runOnIdle { store.undo() }
            compose.waitUntil(5000) { store.detail!!.scale == listOf(1f, 1f, 1f) }
        }
    }

    @Test fun linkedVideoWidthAndHeightFollowBothSlidersWithAndWithoutAutoKey() {
        val video = File(context.cacheDir, "linked-scale-video.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        try {
            launch()
            compose.runOnIdle { store.importVideo(android.net.Uri.fromFile(video)) }
            compose.waitUntil(20000) { store.detail?.kind == 1 }
            compose.onNodeWithText(context.getString(R.string.sh_dock_transform)).performClick()
            compose.onNodeWithContentDescription(context.getString(R.string.panel_escala)).performClick()
            for (auto in listOf(false, true)) for (axis in listOf("x", "y")) {
                compose.runOnIdle { store.autoKeyTransforms = auto; store.setScale3(floatArrayOf(.05f, .1f, 1f)); store.scaleAxesLinked = true }
                compose.waitUntil(5000) { store.detail!!.scale == listOf(.05f, .1f, 1f) }
                compose.onNodeWithTag("transform.scale.$axis").performTouchInput {
                    down(center); moveBy(Offset(-32f, 0f), 80); moveBy(Offset(-5f, 0f), 80); up()
                }
                compose.waitUntil(5000) { store.detail!!.scale[0] < .05f }
                val scale = store.detail!!.scale
                assertEquals("both video dimensions use the same factor", scale[0] * 2, scale[1], .00001f)
                assertEquals("2D depth stays unchanged", 1f, scale[2], .00001f)
                compose.runOnIdle { store.undo() }
                compose.waitUntil(5000) { store.detail!!.scale == listOf(.05f, .1f, 1f) }
            }
        } finally { video.delete() }
    }

    @Test fun embeddedGalleryOrdersNewestFirstSwitchesTypesAndImportsBoth() {
        for (permission in listOf("READ_MEDIA_IMAGES", "READ_MEDIA_VIDEO")) {
            instrumentation.uiAutomation.executeShellCommand("pm grant ${context.packageName} android.permission.$permission").close()
        }
        val resolver = context.contentResolver
        val created = mutableListOf<android.net.Uri>()
        val prefix = "aurea-gallery-${System.currentTimeMillis()}"
        fun insert(name: String, video: Boolean, seconds: Long): android.net.Uri {
            val uri = resolver.insert(if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, name)
                    put(MediaStore.MediaColumns.MIME_TYPE, if (video) "video/mp4" else "image/png")
                    put(MediaStore.MediaColumns.RELATIVE_PATH, if (video) "Movies/AureaTests" else "Pictures/AureaTests")
                    put(MediaStore.MediaColumns.IS_PENDING, 1)
                })!!
            created.add(uri)
            resolver.openOutputStream(uri)!!.use { out ->
                if (video) instrumentation.context.assets.open("motion-fixture.mp4").use { it.copyTo(out) }
                else Bitmap.createBitmap(80, 60, Bitmap.Config.ARGB_8888).also {
                    it.eraseColor(if (name.contains("new")) android.graphics.Color.CYAN else android.graphics.Color.MAGENTA)
                    it.compress(Bitmap.CompressFormat.PNG, 100, out); it.recycle()
                }
            }
            resolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0); put(MediaStore.MediaColumns.DATE_ADDED, seconds) }, null, null)
            return uri
        }
        try {
            val now = System.currentTimeMillis() / 1000
            insert("$prefix-old.png", false, now - 60)
            insert("$prefix-new.png", false, now)
            insert("$prefix-video.mp4", true, now)
            launch()
            compose.onNodeWithTag("addBar.Media").performClick()
            compose.waitUntil(10000) { compose.onAllNodesWithTag("gallery.item.$prefix-new.png").fetchSemanticsNodes().isNotEmpty() }
            val ordered = galleryItems(context, false).filter { it.name.startsWith(prefix) }
            assertEquals(listOf("$prefix-new.png", "$prefix-old.png"), ordered.map { it.name })
            compose.onNodeWithTag("gallery.videos").performClick()
            compose.waitUntil(10000) { compose.onAllNodesWithTag("gallery.item.$prefix-video.mp4").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithTag("gallery.item.$prefix-new.png").assertDoesNotExist()
            compose.onNodeWithTag("gallery.photos").performClick()
            compose.waitUntil(10000) { compose.onAllNodesWithTag("gallery.item.$prefix-new.png").fetchSemanticsNodes().isNotEmpty() }
            capture("gallery-photos")
            compose.onNodeWithTag("gallery.item.$prefix-new.png").performClick()
            compose.waitUntil(15000) { store.layers.any { it.kind == 2 } }
            compose.runOnIdle { store.clearSelection() }
            compose.onNodeWithTag("addBar.Media").performClick()
            compose.onNodeWithTag("gallery.videos").performClick()
            compose.waitUntil(10000) { compose.onAllNodesWithTag("gallery.item.$prefix-video.mp4").fetchSemanticsNodes().isNotEmpty() }
            capture("gallery-videos")
            compose.onNodeWithTag("gallery.item.$prefix-video.mp4").performClick()
            compose.waitUntil(20000) { store.layers.any { it.kind == 1 } }
            assertEquals(2, store.layers.size)
        } finally { created.forEach { resolver.delete(it, null, null) } }
    }
}
