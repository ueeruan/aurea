package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.writeParamVector
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

class TextManualPreviewTest {
    @get:Rule val compose = createComposeRule()

    @Test fun draggingEffectControlsChangesTheLiveTextPixels() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 640, 30f, "Manual text preview") }
        compose.waitUntil(10000) { store.project.title == "Manual text preview" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null }
        compose.runOnIdle {
            store.setTextContent("AUREA"); store.setTextSize(72f); store.setTextColor(1f,0f,0f,1f)
            store.addEffect(effectTypeId("aurea.text.transform"))
        }
        compose.waitUntil(5000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        compose.onNodeWithText(context.getString(R.string.sh_dock_effects)).performClick()
        compose.waitUntil(5000) { store.paramOf(effect, 9) != null }

        fun image(name: String): DoubleArray {
            compose.waitForIdle(); Thread.sleep(600)
            val bounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
            val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            File(context.filesDir,"manual-text-$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG,100,it) }
            var mass=0.0; var moment=0.0
            for (y in bounds.top.toInt().coerceAtLeast(0) until bounds.bottom.toInt().coerceAtMost(bitmap.height))
                for (x in bounds.left.toInt().coerceAtLeast(0) until bounds.right.toInt().coerceAtMost(bitmap.width)) {
                    val p=bitmap.getPixel(x,y)
                    val red=(Color.red(p)-maxOf(Color.green(p),Color.blue(p))).coerceAtLeast(0)
                    if (red>70) { mass+=red; moment+=x*red.toDouble() }
                }
            bitmap.recycle()
            return doubleArrayOf(mass, if(mass>0) moment/mass else 0.0)
        }
        val row=compose.onNodeWithTag("effects.param.$effect.5.0")
        row.performScrollTo()
        val before=image("before")
        assertTrue("visible red text", before[0]>1000)
        row.performTouchInput {
            val start=Offset(width*.53f,center.y)
            down(start); moveTo(start+Offset(55f,0f),250); up()
        }
        compose.waitUntil(5000) { abs(store.paramOf(effect,5)!!.value[0])>1f }
        val moved=image("offset")
        assertTrue("dragging offset updates the actual preview",abs(moved[1]-before[1])>8)

        repeat(12) {
            if (store.paramOf(effect,9)!!.value[0] > -70f) {
                val previous=store.paramOf(effect,9)!!.value[0]
                compose.onNodeWithTag("effects.param.$effect.9.0").performScrollTo().performTouchInput {
                    val start=Offset(width*.53f,center.y)
                    down(start); moveTo(start-Offset(65f,0f),250); up()
                }
                compose.waitUntil(5000) { store.paramOf(effect,9)!!.value[0]<previous }
            }
        }
        assertTrue("opacity control reaches a visible fade",store.paramOf(effect,9)!!.value[0]<=-70f)
        val faded=image("opacity")
        assertTrue("live opacity=${store.paramOf(effect,9)!!.value[0]}, red mass ${moved[0]} -> ${faded[0]}", faded[0]<moved[0]*.9)

        compose.runOnIdle {
            store.setEffectParam(effect,store.paramOf(effect,9)!!,0f)
            store.setEffectParam(effect,store.paramOf(effect,10)!!,1f)
            store.writeParamVector(effect,store.paramOf(effect,11)!!,floatArrayOf(1f,0f,0f,1f))
        }
        compose.waitUntil(5000) { store.paramOf(effect,9)!!.value[0] == 0f && store.paramOf(effect,10)!!.value[0] == 1f }
        val opaqueFill=image("fill-opaque")
        compose.runOnIdle { store.setEffectParam(effect,store.paramOf(effect,11)!!,0f,3) }
        compose.waitUntil(5000) { store.paramOf(effect,11)!!.value[3] == 0f }
        val transparentFill=image("fill-transparent")
        assertTrue("fill color alpha must change rendered text: ${opaqueFill[0]} -> ${transparentFill[0]}", transparentFill[0]<opaqueFill[0]*.8)
    }
}
