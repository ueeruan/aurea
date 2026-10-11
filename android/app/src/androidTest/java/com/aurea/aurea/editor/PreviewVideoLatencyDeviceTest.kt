package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.os.Build
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Live SurfaceFlinger pixels and native timing, without synchronous offscreen
 * capture changing playback. The same instrumentation can compare target app
 * builds 2155/2156; its report records the installed target's actual build. */
class PreviewVideoLatencyDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun realVideoRawAndCompositorStayVisibleThroughPlaySeekAndReopen() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
        @Suppress("DEPRECATION")
        val build = if (Build.VERSION.SDK_INT >= 28) packageInfo.longVersionCode else packageInfo.versionCode.toLong()
        val fullHd = InstrumentationRegistry.getArguments().getString("aureaPreviewFullHdFixture") == "true"
        val filePrefix = "preview-latency" + if (fullHd) "-fullhd" else ""
        val width = if (fullHd) 1920 else 480
        val height = if (fullHd) 1080 else 320
        val report = File(context.filesDir, "$filePrefix-report.jsonl")
        report.writeText(JSONObject().put("targetBuild", build).put("kind", "live-preview")
            .put("fixtureWidth",width).put("fixtureHeight",height).toString()+"\n")
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(width,height,30f,"Preview video latency") }
        compose.waitUntil(15000) { store.project.title == "Preview video latency" && !store.projectOperationBusy }
        var source = File(context.filesDir,"$filePrefix-owned-source.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> source.outputStream().use { input.copyTo(it) } }
        val engine = store.engineForStress
        val statusBuffer=ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        val perfBuffer=ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())
        fun nativeStatus(): EngineStatus {
            assertTrue(engine.readStatus(statusBuffer))
            return EngineStatus().also { it.readFrom(statusBuffer) }
        }
        fun perf(): PerfStats { assertTrue(engine.readPerf(perfBuffer)); return PerfStats.read(perfBuffer) }
        fun record(label: String, elapsed: Long, extra: JSONObject = JSONObject()) {
            val stats=perf()
            extra.put("sample",label).put("elapsedMs",elapsed).put("targetBuild",build)
                .put("playhead",nativeStatus().playhead).put("previewFps",stats.previewFps.toDouble())
                .put("cpuMs",stats.cpuFrameMs.toDouble()).put("gpuMs",stats.gpuFrameMs.toDouble())
                .put("decodeMs",stats.decodeMs.toDouble()).put("seekMs",stats.lastSeekMs.toDouble())
                .put("presentMs",stats.presentMs.toDouble()).put("acquireMs",stats.acquireMs.toDouble())
                .put("droppedFrames",stats.droppedFrames).put("staleFrames",stats.staleFrames)
                .put("decodedCacheBytes",stats.decodedCacheBytes).put("gpuBytes",stats.gpuMemoryBytes)
                .put("hardwareDecoder",stats.hardwareDecoder).put("decoder",stats.decoder)
                .put("pacingP50Ms",stats.pacingP50Ms.toDouble()).put("pacingP95Ms",stats.pacingP95Ms.toDouble())
            report.appendText(extra.toString()+"\n")
        }
        fun visiblePixels(label: String, save: Boolean = false): Boolean {
            val began=SystemClock.elapsedRealtime()
            val bounds=compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInWindow
            val shot=checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            val capturedMs=SystemClock.elapsedRealtime()-began
            var cyan=0; var magenta=0; var colored=0; var sampled=0
            try {
                val left=bounds.left.toInt().coerceIn(0,shot.width-1)
                val top=bounds.top.toInt().coerceIn(0,shot.height-1)
                val right=bounds.right.toInt().coerceIn(left+1,shot.width)
                val bottom=bounds.bottom.toInt().coerceIn(top+1,shot.height)
                for(y in top until bottom step 3) for(x in left until right step 3) {
                    val px=shot.getPixel(x,y); val r=Color.red(px); val g=Color.green(px); val b=Color.blue(px)
                    if(g>170 && b>170 && r<90) ++cyan
                    if(r>170 && b>170 && g<90) ++magenta
                    if(maxOf(r,g,b)-minOf(r,g,b)>40 && maxOf(r,g,b)>100) ++colored
                    ++sampled
                }
                val valid=cyan>5 && magenta>5 && colored>150
                record(label,SystemClock.elapsedRealtime()-began,JSONObject().put("cyan",cyan).put("magenta",magenta)
                    .put("colored",colored).put("sampled",sampled).put("validPixels",valid).put("screenshotMs",capturedMs))
                if(save || !valid) {
                    val cropped=Bitmap.createBitmap(shot,left,top,right-left,bottom-top)
                    try { File(context.filesDir,"$filePrefix-$label.png").outputStream().use {
                        cropped.compress(Bitmap.CompressFormat.PNG,100,it)
                    } } finally { if(cropped!==shot) cropped.recycle() }
                }
                return valid
            } finally { shot.recycle() }
        }
        if(fullHd) {
            val original=engine.importVideo(source.absolutePath,"Owned Full HD fixture source")
            assertTrue(original>0)
            compose.runOnIdle {
                store.setCompositionDuration(60)
                engine.beginCommandBatch()
                CommandBatch(engine).apply {
                    setLayerTimeRange(original,0,60)
                    setPosition(original,960f,540f,0f)
                    setScale(original,4f,3.375f,1f)
                }
                assertEquals(3,engine.submitCommands())
                store.pause(); store.seek(0)
            }
            compose.waitUntil(15000) { store.project.durationFrames==60 && nativeStatus().playhead==0L }
            val output=File(context.filesDir,"$filePrefix-owned-fixture.mp4")
            val progress=ExportProgress()
            val exportBuffer=ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            val fixtureAt=SystemClock.elapsedRealtime()
            assertEquals(0,engine.startExport(output.absolutePath,1080,30.0,0,8))
            try {
                while(!progress.finished) {
                    assertTrue(engine.exportProgress(exportBuffer)); progress.readFrom(exportBuffer)
                    assertTrue("Owned Full HD fixture export timed out",SystemClock.elapsedRealtime()-fixtureAt<120000)
                    if(!progress.finished) SystemClock.sleep(100)
                }
                assertEquals(progress.message,0,progress.result)
                assertEquals(60,progress.framesDone)
                assertFalse("Fixture export must use actual decoded frames",progress.frameFallback)
            } finally { if(!progress.finished) engine.cancelExport() }
            record("fixture-export",SystemClock.elapsedRealtime()-fixtureAt,JSONObject().put("bytes",output.length()))
            val metadata=MediaMetadataRetriever()
            try {
                metadata.setDataSource(output.absolutePath)
                assertEquals("1920",metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH))
                assertEquals("1080",metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
            } finally { metadata.release() }
            source=output
            compose.runOnIdle { store.newProject(width,height,30f,"Full HD preview latency") }
            compose.waitUntil(15000) { store.project.title=="Full HD preview latency" && !store.projectOperationBusy }
        }
        val importedAt=SystemClock.elapsedRealtime()
        assertTrue(engine.importVideo(source.absolutePath,"Owned moving video")>0)
        compose.runOnIdle { store.setCompositionDuration(60) }
        compose.waitUntil(15000) { store.project.durationFrames == 60 }
        val visibleDeadline=SystemClock.elapsedRealtime()+10000
        var firstVisible=false
        while(SystemClock.elapsedRealtime()<visibleDeadline && !firstVisible) {
            firstVisible=visiblePixels("cold")
            if(!firstVisible) SystemClock.sleep(50)
        }
        val coldMs=SystemClock.elapsedRealtime()-importedAt
        record("cold-ready",coldMs)
        assertTrue("Owned video never became visible",firstVisible)
        assertTrue("Cold video preview should not stall for decoder restart ($coldMs ms)",coldMs<5000)
        val saved=File(context.filesDir,"$filePrefix-project.aurea")
        assertEquals(0,engine.saveProject(saved.absolutePath))
        try {
            for(round in 0..2) for(raw in listOf(false,true)) {
                val prefix="${if(raw) "raw" else "compositor"}-$round"
                if(round>0) {
                    val loadAt=SystemClock.elapsedRealtime()
                    assertEquals(0,engine.loadProject(saved.absolutePath))
                    record("$prefix-reopen",SystemClock.elapsedRealtime()-loadAt)
                }
                assertTrue(engine.setRawPlayback(raw))
                compose.runOnIdle { store.pause(); store.seek(0) }
                compose.waitUntil(5000) { nativeStatus().playhead == 0L && !nativeStatus().playing }
                assertTrue("Video must stay visible before playback: $prefix",visiblePixels("$prefix-before"))
                val playAt=SystemClock.elapsedRealtime()
                compose.runOnIdle { store.play() }
                compose.waitUntil(5000) { nativeStatus().playing && nativeStatus().playhead>0 }
                val startedMs=SystemClock.elapsedRealtime()-playAt
                record("$prefix-start",startedMs)
                assertTrue("Warm video play should start promptly: $prefix $startedMs ms",startedMs<2500)
                var pictureCount=0
                // Capture cost varies by driver. Always sample three displayed
                // pictures, recording that cost instead of counting a slow
                // screenshot as a decoder stall. Native timeline advancement
                // is checked separately; the final picture can remain visible
                // if the two-second fixture ends while a screenshot is read.
                repeat(3) {
                    assertTrue("Blank/flat green playback frame: $prefix sample=$pictureCount",visiblePixels("$prefix-play-$pictureCount"))
                    ++pictureCount; SystemClock.sleep(80)
                }
                val advanced=nativeStatus().playhead
                record("$prefix-played",SystemClock.elapsedRealtime()-playAt,JSONObject().put("pictureSamples",pictureCount).put("advancedFrames",advanced))
                assertTrue("Actual timeline must advance with visible video: $prefix frames=$advanced samples=$pictureCount",advanced>=8 && pictureCount==3)
                compose.runOnIdle { store.pause() }
                for(frame in listOf(45,5,55,15)) {
                    val seekAt=SystemClock.elapsedRealtime()
                    compose.runOnIdle { store.seek(frame) }
                    compose.waitUntil(5000) { nativeStatus().playhead==frame.toLong() && !nativeStatus().playing }
                    // Observe the actual displayed surface while decoder work
                    // catches up; missing frames must retain valid picture.
                    assertTrue("Blank/flat green seek frame: $prefix frame=$frame",visiblePixels("$prefix-seek-$frame"))
                    record("$prefix-seek-$frame-ready",SystemClock.elapsedRealtime()-seekAt)
                }
            }
            report.appendText(JSONObject().put("result","PASS").put("targetBuild",build).toString()+"\n")
        } finally { compose.runOnIdle { store.pause() }; engine.setRawPlayback(false) }
    }
}
