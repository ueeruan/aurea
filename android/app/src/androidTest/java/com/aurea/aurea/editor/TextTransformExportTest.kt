package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.media.MediaMetadataRetriever
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.writeParamVector
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class TextTransformExportTest {
    @get:Rule val compose = createComposeRule()
    @Test fun textEffectAndOscillateReachEncodedPixelsAfterProjectReload() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,180,30f,"Text effect export") }
        compose.waitUntil(15000) { store.project.title == "Text effect export" }
        compose.runOnIdle {
            val layer = store.engineForStress.addText("A")
            store.select(layer); store.setTextSize(60f)
            store.setLayerRanges(longArrayOf(layer), intArrayOf(0), intArrayOf(30))
            store.setCompositionDuration(30)
            store.addEffect(effectTypeId("aurea.text.transform"))
        }
        compose.waitUntil(5000) { store.effects.size == 1 }
        val text = store.effects.single().effectId
        compose.waitUntil(5000) { store.paramOf(text,11) != null }
        compose.runOnIdle {
            store.setEffectParam(text,store.paramOf(text,10)!!,1f)
            store.writeParamVector(text,store.paramOf(text,11)!!,floatArrayOf(1f,0f,0f,1f))
        }
        compose.waitUntil(5000) { store.paramOf(text,11)?.value?.take(3) == listOf(1f,0f,0f) }
        compose.runOnIdle {
            store.toggleEffectKeyframe(text,store.paramOf(text,11)!!)
            store.toggleEffectKeyframe(text,store.paramOf(text,5)!!)
            store.seek(29)
        }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 29 }
        compose.runOnIdle {
            store.setEffectParam(text,store.paramOf(text,11)!!,0f,0)
            store.setEffectParam(text,store.paramOf(text,11)!!,1f,2)
            store.setEffectParam(text,store.paramOf(text,5)!!,70f,0)
            store.addEffect(effectTypeId("aurea.motion.oscillate.cycles"))
        }
        compose.waitUntil(5000) { store.effects.size == 2 }
        val osc = store.effects.last().effectId
        compose.waitUntil(5000) { store.paramOf(osc,5) != null }
        val project = File(context.filesDir,"text-effect-export.aurea")
        compose.runOnIdle {
            for ((p,v) in listOf(1 to 0f,2 to 0f,3 to 20f,5 to .25f)) store.setEffectParam(osc,store.paramOf(osc,p)!!,v)
            assertEquals(0,store.engineForStress.saveProject(project.absolutePath))
            store.newProject(320,180,30f,"Reload text effects")
        }
        compose.waitUntil(10000) { store.layers.isEmpty() }
        compose.runOnIdle { store.openProject(project.absolutePath) }
        compose.waitUntil(15000) { store.layers.size == 1 }
        val output=File(context.filesDir,"text-effect-export.mp4")
        val progress=ExportProgress(); val buffer=ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        compose.runOnIdle { assertEquals(0,store.engineForStress.startExport(output.absolutePath,180,30.0,0,4)) }
        try {
            compose.waitUntil(120000) { store.engineForStress.exportProgress(buffer) && run { progress.readFrom(buffer);progress.finished } }
            assertEquals(progress.message,0,progress.result)
            assertEquals(30,progress.framesDone)
            val media=MediaMetadataRetriever()
            try {
                media.setDataSource(output.absolutePath)
                fun pixels(time:Long):DoubleArray {
                    val bitmap=checkNotNull(media.getFrameAtTime(time,MediaMetadataRetriever.OPTION_CLOSEST))
                    File(context.filesDir,"text-effect-$time.png").outputStream().use { bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG,100,it) }
                    val result=DoubleArray(4)
                    for(y in 0 until bitmap.height) for(x in 0 until bitmap.width) {
                        val p=bitmap.getPixel(x,y); val red=Color.red(p);val blue=Color.blue(p)
                        val weight=(red+blue).toDouble()
                        result[0]+=red;result[1]+=blue;result[2]+=(x+.5)*weight;result[3]+=weight
                    }
                    bitmap.recycle();result[2]/=result[3];return result
                }
                val first=pixels(0);val last=pixels(966667)
                assertTrue("First frame red",first[0]>first[1]*2)
                assertTrue("Last frame blue",last[1]>last[0]*2)
                assertEquals("Animated text offset is measured in output pixels",70.0,last[2]-first[2],3.0)
                assertTrue("Oscillate shifts the centered glyph right",first[2]>170)
            } finally { media.release() }
        } finally { if(!progress.finished) store.engineForStress.cancelExport() }
    }
}
