package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Matrix
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.PixelCopy
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.floor
import kotlin.math.ceil
import kotlin.math.pow
import kotlin.math.roundToInt

/** Reads the presented SurfaceView, without asking captureFrame to render a fresh frame. */
class MotionTilePreviewRotationDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun pausedTilePreviewChangesWithTheProductionRotationGesture() = verifyPreview(explicit3D = true)

    @Test fun rotationGestureProjectsAnImageWithoutAnExplicit3DFlag() = verifyPreview(explicit3D = false)

    @Test fun grazingWhitePlaneCoversFrontFacingRaysBeyondTheOldRasterWall() =
        verifyPreview(explicit3D = true, pattern = "white")

    @Test fun grazingStripePreviewMatchesAnIntegratedPixelInsteadOfAliasedBands() =
        verifyPreview(explicit3D = true, pattern = "stripes")

    private fun verifyPreview(explicit3D: Boolean, pattern: String = "gradient") {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        val mode = (if (explicit3D) "explicit" else "implicit") + if (pattern == "gradient") "" else "-$pattern"
        val directory = File(context.getExternalFilesDir(null), "motion-tile-preview-beta3/$mode").apply { mkdirs() }
        val report = File(directory, "result.txt").apply { writeText("Live SurfaceView, own texture, paused frame 0, explicit3D=$explicit3D\n") }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val title = "Motion Tile live rotation beta 3 $mode"
        compose.runOnIdle { store.newProject(512, 288, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
        val engine = store.engineForStress
        val source = Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888)
        for (y in 0 until 128) for (x in 0 until 128) {
            source.setPixel(x, y, when (pattern) {
                "white" -> Color.WHITE
                "stripes" -> if ((y / 4) % 2 == 0) Color.BLACK else Color.WHITE
                else -> Color.rgb(35 + x * 210 / 127, 40 + y * 180 / 127, if (x < 45) 220 else 65)
            })
        }
        val sourcePath = File(context.filesDir, "motion-tile-preview-own.png")
        sourcePath.outputStream().use { assertTrue(source.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        val rgba = ByteBuffer.allocateDirect(128 * 128 * 4)
        source.copyPixelsToBuffer(rgba)
        rgba.rewind()
        source.recycle()
        var layer = 0L
        compose.runOnIdle {
            store.setCompositionBackground(0f, 0f, 0f, 1f)
            layer = engine.importImage(rgba, 128, 128, "Own asymmetric tile", sourcePath.absolutePath)
            assertTrue(layer > 0)
            if (explicit3D) assertTrue(engine.setLayer3D(layer, true))
            engine.beginCommandBatch()
            CommandBatch(engine).setPreviewScale(false, 1, 1)
            assertTrue(engine.submitCommands() > 0)
            store.select(layer)
        }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }
        assertEquals("Untouched image flags must exercise the requested path", explicit3D,
            (checkNotNull(store.detail).flags and PodLayout.FLAG_THREE_D) != 0)
        val tileType = effectTypeId("aurea.stylize.motion_tile")
        compose.runOnIdle { store.addEffect(tileType, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == tileType } }
        val tile = store.effects.single { it.typeId == tileType }.effectId
        compose.waitUntil(10000) { store.effectParams[tile]?.any { it.index == 5 } == true }
        compose.runOnIdle {
            store.setEffectParam(tile, store.effectParams.getValue(tile).single { it.index == 5 }, 1f)
            store.pause()
            store.seek(0)
        }
        compose.waitUntil(10000) { !store.playing && store.playhead == 0 && store.primary == layer && store.detail?.hasWorldCorners == true }

        fun findSurface(view: View): SurfaceView? {
            if (view is SurfaceView) return view
            if (view is ViewGroup) for (i in 0 until view.childCount) {
                val found = findSurface(view.getChildAt(i))
                if (found != null) return found
            }
            return null
        }
        fun presented(): Bitmap? {
            val latch = CountDownLatch(1)
            var status = PixelCopy.ERROR_UNKNOWN
            lateinit var bitmap: Bitmap
            instrumentation.runOnMainSync {
                val activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).single()
                val surface = checkNotNull(findSurface(activity.window.decorView))
                check(surface.width > 0 && surface.height > 0 && surface.holder.surface.isValid)
                bitmap = Bitmap.createBitmap(surface.width, surface.height, Bitmap.Config.ARGB_8888)
                PixelCopy.request(surface, bitmap, { result -> status = result; latch.countDown() }, Handler(Looper.getMainLooper()))
            }
            assertTrue("PixelCopy must finish without blocking the main thread", latch.await(3, TimeUnit.SECONDS))
            if (status == PixelCopy.ERROR_SOURCE_NO_DATA) {
                // The Surface can exist before its first buffer is queued.
                // Wait within readChanged's existing deadline, without an
                // edit/seek/capture that could force the preview to update.
                bitmap.recycle()
                return null
            }
            assertEquals("Actual presented surface must contain a frame", PixelCopy.SUCCESS, status)
            return bitmap
        }
        var presentationSupportX = 3f
        var presentationSupportY = 3f
        fun samples(bitmap: Bitmap): IntArray {
            // Crop the fitted composition, excluding letterbox bars. PixelCopy
            // already excludes the Compose gizmo, outline and diagnostic HUD.
            val scale = min(bitmap.width / 512f, bitmap.height / 288f)
            val width = (512 * scale).roundToInt().coerceIn(1, bitmap.width)
            val height = (288 * scale).roundToInt().coerceIn(1, bitmap.height)
            // Display bilinear reconstruction spans one composition texel;
            // Bitmap resizing plus fitted-rectangle rounding adds at most
            //1.5 viewport texels. A center behind the horizon can therefore
            // legitimately contain front-facing color in this capture.
            presentationSupportX = ceil(1f + 1.5f * 512 / width)
            presentationSupportY = ceil(1f + 1.5f * 288 / height)
            val crop = Bitmap.createBitmap(bitmap, (bitmap.width - width) / 2, (bitmap.height - height) / 2, width, height)
            val sampled = Bitmap.createScaledBitmap(crop, 256, 144, true)
            val colors = IntArray(256 * 144)
            sampled.getPixels(colors, 0, 256, 0, 0, 256, 144)
            if (sampled !== crop) sampled.recycle()
            if (crop !== bitmap) crop.recycle()
            return colors
        }
        fun changes(a: IntArray, b: IntArray): Int = a.indices.count { index ->
            abs(Color.red(a[index]) - Color.red(b[index])) > 8 ||
                abs(Color.green(a[index]) - Color.green(b[index])) > 8 ||
                abs(Color.blue(a[index]) - Color.blue(b[index])) > 8
        }
        fun visible(colors: IntArray) = colors.count { maxOf(Color.red(it), Color.green(it), Color.blue(it)) > 35 }
        data class Domain(val compared: Int, val wrong: Int, val behind: Int, val litBehind: Int, val meanError: Double) {
            val matches get() = compared > 500 && wrong <= compared / 20 && litBehind == 0
        }
        fun domain(colors: IntArray, corners: FloatArray): Domain {
            // The model exposes the projected original quad. Four points
            // define an independent homography from layer UV to composition.
            // Tiling screen pixels instead of that domain cannot match this
            // reference's smooth asymmetric red/green gradients.
            val forward = Matrix()
            check(forward.setPolyToPoly(floatArrayOf(0f, 0f, 1f, 0f, 1f, 1f, 0f, 1f), 0, corners, 0, 4))
            val inverse = Matrix()
            check(forward.invert(inverse))
            val coefficients = FloatArray(9)
            inverse.getValues(coefficients)
            val center = floatArrayOf(.5f, .5f)
            forward.mapPoints(center)
            fun denominator(x: Float, y: Float) = coefficients[6] * x + coefficients[7] * y + coefficients[8]
            val centerW = denominator(center[0], center[1])
            fun sourceCoordinate(value: Float): Float {
                val repeated = ((value % 2f) + 2f) % 2f
                val mirrored = if (repeated <= 1f) repeated else 2f - repeated
                return (mirrored * 128f - .5f).coerceIn(0f, 127f)
            }
            var count = 0; var wrong = 0; var behind = 0; var litBehind = 0; var totalError = 0L
            if (pattern == "stripes") {
                fun localY(x: Float, y: Float): Float =
                    (coefficients[3] * x + coefficients[4] * y + coefficients[5]) / denominator(x, y)
                fun linear(value: Int): Double {
                    val v = value / 255.0
                    return if (v <= .04045) v / 12.92 else ((v + .055) / 1.055).pow(2.4)
                }
                // PixelCopy is cropped and reduced to256x144, hence each
                // measured pixel spans2x2 composition pixels. Integrate the
                // inverse homography over its full height; the source rows
                // are independent of X. Keep the old24x domain here to
                // isolate filtering from the separate white coverage case.
                for (y in 3 until 141) {
                    val low = localY(257f, y * 2f + .0002f)
                    val high = localY(257f, y * 2f + 1.9998f)
                    if (!low.isFinite() || !high.isFinite() || minOf(low, high) < -11.5f || maxOf(low, high) > 12.5f) continue
                    if (denominator(257f, y * 2f) * centerW <= 0 || denominator(257f, y * 2f + 2) * centerW <= 0) continue
                    val span = abs(high - low) * 128
                    if (span < 32 || span > 2048) continue
                    var expected = 0.0
                    repeat(2048) { n ->
                        val sample = sourceCoordinate(localY(257f, y * 2f + (n + .5f) * 2 / 2048))
                        val first = floor(sample).toInt()
                        val fraction = sample - first
                        expected += (first / 4 % 2) * (1 - fraction) + (minOf(first + 1, 127) / 4 % 2) * fraction
                    }
                    expected /= 2048
                    val error = abs(linear(Color.red(colors[y * 256 + 128])) - expected)
                    count++; if (error > .14) wrong++; totalError += (error * 255).roundToInt()
                }
                return Domain(count, wrong, 0, 0, totalError.toDouble() / count.coerceAtLeast(1))
            }
            for (y in 3 until 141 step 3) for (x in 3 until 253 step 3) {
                val qx = (x + .5f) * 2f; val qy = (y + .5f) * 2f
                val color = colors[y * 256 + x]
                val w = denominator(qx, qy)
                if (w * centerW < 0f) {
                    // Require the ENTIRE presented sampling support behind
                    // the physical horizon, rather than only its center.
                    val fullSupportBehind = listOf(-1f, 1f).all { sx -> listOf(-1f, 1f).all { sy ->
                        denominator(qx + sx * presentationSupportX, qy + sy * presentationSupportY) * centerW < 0f
                    } }
                    if (fullSupportBehind) {
                        behind++
                        if (maxOf(Color.red(color), Color.green(color), Color.blue(color)) > 12) litBehind++
                    }
                    continue
                }
                val points = floatArrayOf(qx, qy, qx + 2f, qy, qx, qy + 2f)
                inverse.mapPoints(points)
                if (points.any { !it.isFinite() }) continue
                if (pattern == "white") {
                    // Exclude a footprint crossing the physical horizon and
                    // rays beyond100 source tiles (the far depth frustum).
                    // This still reaches well beyond the old24x raster wall.
                    if (denominator(qx + 2, qy + 2) * centerW <= 0 || points.any { abs(it) > 100 }) continue
                    count++
                    val error = 255 - minOf(Color.red(color), Color.green(color), Color.blue(color))
                    totalError += error * 2
                    if (error > 15) wrong++
                    continue
                }
                // At the horizon a screen pixel can contain many whole tiles.
                // Compare only resolvable texture points, without imposing a
                // point-sample oracle on a minified/antialiased footprint.
                if (maxOf(abs(points[2] - points[0]), abs(points[3] - points[1]),
                          abs(points[4] - points[0]), abs(points[5] - points[1])) > .035f) continue
                val expectedR = (35f + sourceCoordinate(points[0]) * 210f / 127f).roundToInt()
                val expectedG = (40f + sourceCoordinate(points[1]) * 180f / 127f).roundToInt()
                val errorR = abs(Color.red(color) - expectedR)
                val errorG = abs(Color.green(color) - expectedG)
                count++
                totalError += errorR + errorG
                if (maxOf(errorR, errorG) > 12) wrong++
            }
            return Domain(count, wrong, behind, litBehind, totalError.toDouble() / (2 * count).coerceAtLeast(1))
        }
        fun readChanged(label: String, previous: IntArray?, corners: FloatArray): IntArray {
            val deadline = SystemClock.elapsedRealtime() + 8000
            var current = IntArray(256 * 144)
            var difference = 0
            var colored = 0
            var measured = Domain(0, 0, 0, 0, 0.0)
            do {
                val bitmap = presented()
                if (bitmap == null) {
                    if (SystemClock.elapsedRealtime() >= deadline) fail("The live surface never queued a frame for $label")
                    Thread.sleep(60)
                    continue
                }
                try {
                    current = samples(bitmap)
                    colored = visible(current)
                    difference = previous?.let { changes(it, current) } ?: 0
                    measured = domain(current, corners)
                    val matches = when (pattern) {
                        "white" -> measured.compared > 500 && measured.wrong == 0 && measured.litBehind == 0
                        "stripes" -> measured.compared > 0 && measured.wrong == 0
                        else -> measured.matches
                    }
                    val passed = matches && colored > current.size / 5 && (previous == null || difference > current.size / 12)
                    if (passed || SystemClock.elapsedRealtime() >= deadline) {
                        File(directory, "$label.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                        val result = "$label changed=$difference/${current.size} visible=$colored/${current.size} domain=$measured lowMemory=${store.appMemory.lowMemory} corners=${corners.contentToString()}"
                        report.appendText(result + "\n")
                        Log.i("AureaTilePreview", result)
                        assertFalse("The probe requires a device outside low-memory mode", store.appMemory.lowMemory)
                        assertTrue("Own texture must remain visible: $result", colored > current.size / 5)
                        assertTrue("Rotation changed only the overlay or retained a stale tile preview: $result",
                            previous == null || difference > current.size / 12)
                        assertTrue("Presented texture does not follow the projected layer's mirrored domain: $result", matches)
                        return current
                    }
                } finally { bitmap.recycle() }
                Thread.sleep(60)
            } while (SystemClock.elapsedRealtime() < deadline)
            fail("No updated live preview for $label: changed=$difference visible=$colored domain=$measured")
            return current
        }
        if (pattern == "stripes") {
            compose.runOnIdle { store.setTransform(TrackProperty.ROTATION_X, -101.2f) }
            compose.waitUntil(10000) { abs((store.detail?.rotation?.get(0) ?: 0f) + 101.2f) < .01f }
        }
        var previous = readChanged(if (pattern == "stripes") "x-initial-minus101" else "x-zero", null, checkNotNull(store.detail).worldCorners.copyOf())
        compose.runOnIdle { store.beginGesture("rotação") }
        for (angle in if (pattern == "stripes") listOf(-88.4f, -101.2f) else listOf(-101.2f, -88.4f, -221.2f)) {
            compose.runOnIdle { store.setTransform(TrackProperty.ROTATION_X, angle) }
            compose.waitUntil(10000) { abs((store.detail?.rotation?.get(0) ?: 0f) - angle) < .01f }
            // Keep the same selection, effect and modes. A deselect/seek or
            // forced capture here could accidentally repair a missing redraw.
            assertEquals(layer, store.primary)
            assertFalse(store.playing)
            assertEquals(0, store.playhead)
            assertEquals("The gesture must work without changing the persisted 3D flag", explicit3D,
                (checkNotNull(store.detail).flags and PodLayout.FLAG_THREE_D) != 0)
            previous = readChanged("x-${angle.toString().replace('.', '_')}", previous, checkNotNull(store.detail).worldCorners.copyOf())
        }
        compose.runOnIdle { store.endGesture() }
    }
}
