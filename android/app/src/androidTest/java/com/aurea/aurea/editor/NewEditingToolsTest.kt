package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.LocalRotoModel
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class NewEditingToolsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun downloadOnceCutoutExportGridAndLayeredPsd() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val engine = store.engineForStress
        val directory = engine.foregroundModelDirectory()
        runBlocking { LocalRotoModel.prepare(directory) {} }
        val model = File(directory, "u2netp.bin")
        val modified = model.lastModified()
        runBlocking { LocalRotoModel.prepare(directory) {} }
        assertEquals("A valid model must not be downloaded again", modified, model.lastModified())
        compose.runOnIdle { store.newProject(320,320,30f,"AI cutout") }
        compose.waitUntil(15000) { store.project.title == "AI cutout" }
        val pixels = ByteBuffer.allocateDirect(320*320*4)
        for(y in 0 until 320)for(x in 0 until 320) {
            val circle=(x-160)*(x-160)+(y-160)*(y-160)<6400
            pixels.put((if(circle)220 else 30).toByte()).put((if(circle)40 else 120).toByte()).put((if(circle)30 else 70).toByte()).put(255.toByte())
        }
        pixels.rewind()
        val imageFile=File(context.filesDir,"roto-source.png")
        val bitmap=android.graphics.Bitmap.createBitmap(320,320,android.graphics.Bitmap.Config.ARGB_8888)
        bitmap.copyPixelsFromBuffer(pixels);pixels.rewind()
        imageFile.outputStream().use {assertTrue(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG,100,it))};bitmap.recycle()
        var layer=0L
        compose.runOnIdle {
            layer=engine.importImage(pixels,320,320,"Circle",imageFile.absolutePath)
            assertTrue(layer>0)
            store.select(layer)
            store.setCompositionDuration(4)
        }
        compose.waitUntil(10000) { store.layers.any { it.id==layer } }
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.key.rotobrush"),listOf(layer)) }
        val rows=ByteBuffer.allocateDirect(64).order(ByteOrder.nativeOrder())
        val blob=ByteBuffer.allocateDirect(1024)
        compose.waitUntil(30000) { engine.queryLayerEffects(layer,rows,2,blob)==1 }
        val path=File(context.filesDir,"ai-cutout.aurea")
        assertEquals(0,engine.saveProject(path.absolutePath))
        assertEquals(0,engine.loadProject(path.absolutePath))
        val output=File(context.filesDir,"ai-cutout.mp4")
        assertEquals(0,engine.startExport(output.absolutePath,320,30.0,0,4))
        val progress=ExportProgress();val state=ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        try {
            compose.waitUntil(180000) { engine.exportProgress(state) && run {progress.readFrom(state);progress.finished} }
            assertEquals(progress.message,0,progress.result);assertEquals(4,progress.framesDone)
            assertFalse(progress.frameFallback)
            val media=MediaMetadataRetriever()
            try {
                media.setDataSource(output.absolutePath)
                val frame=media.getFrameAtTime(0,MediaMetadataRetriever.OPTION_CLOSEST)!!
                val center=frame.getPixel(160,160);val corner=frame.getPixel(0,0)
                assertTrue(android.graphics.Color.red(center)>100)
                assertTrue("The green background must be cut out",android.graphics.Color.green(corner)<15)
                frame.recycle()
            } finally {media.release()}
        } finally {if(!progress.finished)engine.cancelExport()}
        compose.runOnIdle {store.newProject(320,240,30f,"Grid and PSD")}
        compose.waitUntil(15000) {store.project.title=="Grid and PSD"}
        val ids=mutableListOf<Long>()
        compose.runOnIdle {
            repeat(6) {ids+=engine.addShape(0)}
            val grid=engine.createGrid(ids.toLongArray());assertTrue(grid>0)
            store.select(grid)
        }
        compose.waitUntil(10000) {store.layers.size==7}
        val psd=File(context.filesDir,"groups-mask.psd")
        instrumentation.context.assets.open(psd.name).use {input -> psd.outputStream().use {input.copyTo(it)}}
        val imported=engine.importPsd(psd.absolutePath,"PSD layers");assertTrue(imported>0)
        compose.runOnIdle {store.openPrecomp(imported)}
        compose.waitUntil(15000) {store.layers.size==1 && store.layers.first().kind==12}
        val group=store.layers.first().id
        compose.runOnIdle {store.openPrecomp(group)}
        compose.waitUntil(15000) {store.layers.size==1 && store.layers.first().kind==2}
        assertEquals("Coração",store.layers.first().name)
    }
}
