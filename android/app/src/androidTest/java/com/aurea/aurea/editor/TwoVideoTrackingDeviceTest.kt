package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.SystemClock
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.TrackingPanel
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs

/** Real MediaCodec sources, native analysis, production panel callbacks and project round-trip.
 * Keep this test in the isolated .uitest application; generated videos contain only the
 * existing synthetic fixture. No analysis rows, transforms or results are mocked. */
class TwoVideoTrackingDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun project(title: String, width: Int, height: Int) {
        compose.runOnIdle { store.newProject(width,height,30f,title) }
        compose.waitUntil(30000) { store.project.title==title && !store.projectOperationBusy }
    }
    private fun importVideoFixture(file: File, expectedVideos: Int): Long {
        var before=emptySet<Long>()
        compose.runOnIdle { before=store.layers.map { it.id }.toSet(); store.importVideo(Uri.fromFile(file)) }
        compose.waitUntil(30000) { store.busyMessage==null && store.layers.count { it.kind==1 }==expectedVideos }
        var id=0L
        compose.runOnIdle {
            assertNull(store.errorMessage)
            id=store.layers.single { it.kind==1 && it.id !in before }.id
        }
        return id
    }
    private fun select(id: Long) {
        compose.runOnIdle { store.select(id,openOptions=false); store.refreshMotionStatus() }
        compose.waitUntil(5000) { store.primary==id && store.detail?.id==id }
    }
    private fun keys(id: Long): Map<String,Float> {
        var result=emptyMap<String,Float>()
        compose.runOnIdle {
            result=store.keyframes[id].orEmpty().filter { it.property==31 }
                .associate { "${it.property}:${it.paramIndex}:${it.time}" to it.value }
            assertTrue("Every native tracking key must be finite",result.values.all { it.isFinite() })
        }
        return result
    }
    private fun sameKeys(expected: Map<String,Float>,actual: Map<String,Float>) {
        assertEquals(expected.keys,actual.keys)
        expected.forEach { (key,value) -> assertEquals(key,value,actual.getValue(key),.0001f) }
    }
    private fun analyzeSelected() {
        compose.onNodeWithText(context.getString(R.string.trk_tool_stabilizer)).performScrollTo().performClick()
        compose.waitUntil(90000) { store.motionStatus[0].toInt()==2 || store.motionStatus[0].toInt()==3 }
        compose.runOnIdle {
            assertEquals(store.motionMessage,2,store.motionStatus[0].toInt())
            assertEquals(60,store.motionStatus[3].toInt())
            assertTrue("Real analysis needs reliable samples",store.motionStatus[2]>=2f)
        }
    }
    private fun applySelected(id: Long): Map<String,Float> {
        compose.onNodeWithText(context.getString(R.string.trk_apply_stabilization)).performScrollTo().performClick()
        compose.waitUntil(10000) { store.effects.size==1 && store.keyframes[id].orEmpty().any { it.property==31 } }
        return keys(id).also { assertTrue("Native corner-path keys missing",it.size>=16) }
    }

    /** Same small native export workflow already used by HeavyEditingSoakHarness.
     * A horizontal reflection makes B's moving pixels and motion differ from A,
     * without requiring another binary asset in the source patch. */
    private fun reflectedVideo(a: File,b: File,width: Int,height: Int) {
        project("Tracking reflected fixture",width,height)
        val id=importVideoFixture(a,1)
        val engine=store.engineForStress
        compose.runOnIdle {
            store.setCompositionDuration(60)
            engine.beginCommandBatch()
            CommandBatch(engine).apply {
                setLayerTimeRange(id,0,60)
                setPosition(id,width*.5f,height*.5f,0f)
                setScale(id,-1f,1f,1f)
            }
            assertTrue(engine.submitCommands()>0)
        }
        val progress=ExportProgress()
        val buffer=ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        assertEquals(0,engine.startExport(b.absolutePath,minOf(width,height),30.0,0,2,trimToContent=true))
        val began=SystemClock.elapsedRealtime()
        var movedAt=began; var previous=-1
        try {
            while(!progress.finished) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if(progress.framesDone!=previous) { previous=progress.framesDone; movedAt=SystemClock.elapsedRealtime() }
                assertTrue("Synthetic B export stalled at $previous",SystemClock.elapsedRealtime()-movedAt<90000)
                assertTrue("Synthetic B export deadline exceeded",SystemClock.elapsedRealtime()-began<120000)
                Thread.sleep(100)
            }
            assertEquals(progress.message,0,progress.result)
            assertEquals(60,progress.framesDone)
            assertFalse("Synthetic B must contain exact video frames",progress.frameFallback)
        } finally { if(!progress.finished)engine.cancelExport() }
        fun sha(file: File)=MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
        assertNotEquals("Fixtures must be different real MP4 sources",sha(a),sha(b))
        val reader=MediaMetadataRetriever()
        try {
            reader.setDataSource(b.absolutePath)
            assertEquals(width.toString(),reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH))
            assertEquals(height.toString(),reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
            val frame=checkNotNull(reader.getFrameAtTime(1_000_000,MediaMetadataRetriever.OPTION_CLOSEST))
            try {
                val colors=(0 until frame.width step maxOf(1,frame.width/24)).map { frame.getPixel(it,frame.height/2) }
                assertTrue("Generated B must contain decoded moving texture",colors.distinct().size>5)
            } finally { frame.recycle() }
        } finally { reader.release() }
        File(b.parentFile,"fixture-provenance.txt").writeText("A=${sha(a)}\nB=${sha(b)}\nB=native H.264 reflection; dimensions=${width}x$height; frames=60\n")
    }

    @Test fun twoRealVideosKeepTheirOwnAnalysisKeysUndoAndReopenedProject() {
        assertTrue(context.packageName.endsWith(".uitest"))
        val folder=File(context.filesDir,"tracking-two-source-${System.currentTimeMillis()}").apply { check(mkdirs()) }
        val aFile=File(folder,"source-A.mp4")
        val bFile=File(folder,"source-B-reflected.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> aFile.outputStream().use { input.copyTo(it) } }
        val meta=MediaMetadataRetriever()
        val size=try {
            meta.setDataSource(aFile.absolutePath)
            checkNotNull(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)).toInt() to
                checkNotNull(meta.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)).toInt()
        } finally { meta.release() }
        val width=size.first; val height=size.second
        compose.setContent {
            store=viewModel(factory=ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui=remember { EditorUi() }
            val env=remember(store) { PanelEnv(store,{},{},{},{},{},{null}) }
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store,ui,Modifier.fillMaxWidth().weight(1f))
                    Box(Modifier.fillMaxWidth().height(360.dp)) { TrackingPanel(env) }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        reflectedVideo(aFile,bFile,width,height)
        project("Tracking two independent videos",width,height)
        val a=importVideoFixture(aFile,1); val b=importVideoFixture(bFile,2)
        select(a)
        // The standalone production panel initially opens Camera 3D. The
        // editor's motion-tools menu selects 2D; exercise that actual mode
        // control here rather than searching for a hidden Stabilizer action.
        compose.onNodeWithText(context.getString(R.string.cam_mode_2d)).performClick()
        compose.runOnIdle {
            assertFalse("Motion tracking must use the 2D tools panel",store.cameraTrackerVisible)
            assertNull(store.pointPick)
        }
        compose.onNodeWithText(context.getString(R.string.trk_tool_stabilizer)).assertExists()
        // Start A through the actual panel and switch to B while its worker may
        // still be active. Even after completion, A must never become B's UI result.
        compose.onNodeWithText(context.getString(R.string.trk_tool_stabilizer)).performScrollTo().performClick()
        select(b)
        val nativeStatus=FloatArray(12)
        compose.waitUntil(90000) {
            store.engineForStress.motionTrackStatus(nativeStatus)
            nativeStatus[0].toInt()!=1
        }
        compose.runOnIdle { store.refreshMotionStatus(); assertEquals(0,store.motionStatus[0].toInt()); assertTrue(store.effects.isEmpty()) }
        compose.onAllNodesWithText(context.getString(R.string.trk_apply_stabilization)).assertCountEquals(0)
        select(a)
        compose.waitUntil(5000) { store.motionStatus[0].toInt()==2 }
        compose.runOnIdle { assertEquals(a,store.engineForStress.motionTrackSource()) }
        val aKeys=applySelected(a)
        select(b)
        compose.waitUntil(5000) { store.motionStatus[0].toInt()==0 }
        analyzeSelected()
        compose.runOnIdle { assertEquals(b,store.engineForStress.motionTrackSource()) }
        val bKeys=applySelected(b)
        sameKeys(aKeys,keys(a))
        val shared=aKeys.keys.intersect(bKeys.keys)
        assertTrue("Fixture paths must have overlapping sample times",shared.isNotEmpty())
        val distinct=shared.maxOf { abs(aKeys.getValue(it)-bKeys.getValue(it)) }
        assertTrue("Reflection must exercise distinguishable native motion; max path delta=$distinct",distinct>.01f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.effects.isEmpty() && store.keyframes[b].orEmpty().none { it.property==31 } }
        sameKeys(aKeys,keys(a))
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) { store.effects.size==1 && store.keyframes[b].orEmpty().any { it.property==31 } }
        sameKeys(bKeys,keys(b))
        val saved=AtomicBoolean(false); var path=""
        compose.runOnIdle { path=checkNotNull(store.project.path); store.saveProject { saved.set(true) } }
        compose.waitUntil(30000) { saved.get() && !store.projectOperationBusy }
        assertTrue(File(path).length()>0)
        project("Temporary tracking reset",width,height)
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(30000) { !store.projectOperationBusy && store.layers.any { it.id==a } && store.layers.any { it.id==b } }
        select(a); compose.waitUntil(5000) { store.motionStatus[0].toInt()==2 }
        compose.runOnIdle { assertEquals(a,store.engineForStress.motionTrackSource()) }
        sameKeys(aKeys,keys(a))
        select(b); compose.waitUntil(5000) { store.motionStatus[0].toInt()==2 }
        compose.runOnIdle { assertEquals(b,store.engineForStress.motionTrackSource()) }
        sameKeys(bKeys,keys(b))
        File(folder,"result.txt").writeText("PASS: real A/B decoding and analysis; source-bound UI/apply; distinct native paths maxDelta=$distinct; B undo preserves A; redo/save/reopen restore both.\n")
    }
}
