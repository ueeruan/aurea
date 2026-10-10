package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.AureaPropertyPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Real native schema, controls, tracks, history and project persistence in the isolated app. */
class BuiltinPropertyPanelTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun panel(domain: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            val values = remember(store.primary, store.detail, store.curveRevision, store.playhead) {
                if (domain == "light") store.lightInfo()
                else store.queryMaterials().firstOrNull()?.copyOfRange(2,8)
            }
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().weight(1f))
                    Column(Modifier.fillMaxWidth().height(360.dp).verticalScroll(rememberScrollState())) {
                        values?.let { AureaPropertyPanel(store, domain, it, if (domain == "material") 0 else -1) }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,180,30f,"Property panel $domain") }
        compose.waitUntil(15000) { store.project.title == "Property panel $domain" && !store.projectOperationBusy }
    }
    private fun progress(tag: String, value: Float) {
        compose.onNodeWithTag(tag).performScrollTo().performSemanticsAction(SemanticsActions.SetProgress) { it(value) }
    }
    @Test fun spotConeAndRgbKeysUseNativeTracksAndUndoThenSurviveReopen() {
        panel("light")
        compose.runOnIdle { store.addLight(2) }
        compose.waitUntil(5000) { store.lightInfo()?.get(0) == 2f }
        val layer = store.primary!!
        assertEquals(3, store.builtinPropertySchema!!.panels.getValue("light").single { it.id == "color" }.components)
        compose.onNodeWithTag("property.light.color.3").assertDoesNotExist()
        progress("property.light.coneAngle",60f)
        compose.waitUntil(5000) { store.lightInfo()?.get(6) == 60f }
        compose.onNodeWithTag("property.light.coneAngle.keyall").performScrollTo().performClick()
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().any { it.property == 25 && it.time == 0 } }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.playhead == 30 }
        progress("property.light.coneAngle",120f)
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().any { it.property == 25 && it.time == 30 && it.value == 120f } }
        assertEquals(60f,store.keyframes[layer].orEmpty().single { it.property == 25 && it.time == 0 }.value,0f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().none { it.property == 25 && it.time == 30 } }
        compose.onNodeWithTag("property.light.color.keyall").performScrollTo().performClick()
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().count { it.property in 22..24 && it.time == 30 } == 3 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().none { it.property in 22..24 && it.time == 30 } }
        val path = store.project.path!!
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(15000) { saved && !store.projectOperationBusy }
        compose.runOnIdle { store.seek(0); store.setLightParam(6,150f) }
        compose.waitUntil(5000) { store.lightInfo()?.get(6) == 150f }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(15000) { !store.projectOperationBusy && store.layers.any { it.id == layer } }
        compose.runOnIdle { store.select(layer); store.seek(0) }
        compose.waitUntil(5000) { store.lightInfo()?.get(6) == 60f }
        assertTrue(store.keyframes[layer].orEmpty().any { it.property == 25 && it.time == 0 })
    }
    @Test fun importedMaterialRetainsPerChannelKeysAndGroupsRgbaUndo() {
        panel("material")
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val fixture = File(context.filesDir,"property-panel-triangle.gltf")
        // The original triangle deliberately has no material. This fixture declares
        // a PBR material so the native material query and panel have a real target.
        instrumentation.context.assets.open("model-material-triangle.gltf").use { input -> fixture.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importModel(Uri.fromFile(fixture)) }
        try { compose.waitUntil(20000) { store.queryMaterials().isNotEmpty() } }
        catch (error: Throwable) { throw AssertionError("Material fixture import failed: ${store.errorMessage}; busy=${store.busyMessage}; optimize=${store.modelOptimize}; selected=${store.primary}", error) }
        val layer = store.primary!!
        assertEquals(1, store.queryMaterials().size)
        assertEquals(.2f, store.queryMaterials().first()[6], 1e-6f)
        assertEquals(.7f, store.queryMaterials().first()[7], 1e-6f)
        progress("property.material.metallic",35f)
        compose.waitUntil(5000) { kotlin.math.abs(store.queryMaterials().first()[6] - .35f) < 1e-6f }
        compose.onNodeWithTag("property.material.baseColor.keyall").performScrollTo().performClick()
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().count { it.property == 37 && it.effectIndex == 0 && it.paramIndex in 0..3 && it.time == 0 } == 4 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().none { it.property == 37 && it.paramIndex in 0..3 } }
        progress("property.material.baseColor.3",40f)
        compose.waitUntil(5000) { kotlin.math.abs(store.queryMaterials().first()[5] - .4f) < 1e-6f }
        compose.onNodeWithTag("property.material.baseColor.key3").performScrollTo().performClick()
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().any { it.property == 37 && it.paramIndex == 3 && it.value == .4f } }
        assertTrue(store.keyframes[layer].orEmpty().none { it.property == 37 && it.paramIndex in 0..2 })
    }
}
