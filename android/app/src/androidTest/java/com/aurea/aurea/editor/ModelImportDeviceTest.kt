package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Exercises native URI staging and the shared importers in the isolated app. */
class ModelImportDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun fbxGlbAndZippedTexturedObjImportAndSurviveReopen() {
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
        compose.runOnIdle { store.newProject(320, 240, 30f, "Model import regressions") }
        compose.waitUntil(15000) { store.project.title == "Model import regressions" }
        val folder = File(context.filesDir, "model-import-regressions").apply { mkdirs() }
        fun copyAsset(asset: String, name: String) = File(folder, name).also { file ->
            instrumentation.context.assets.open(asset).use { input -> file.outputStream().use { input.copyTo(it) } }
        }
        // An extensionless GLB reproduces files delivered with generic provider names.
        val glb = copyAsset("model-triangle.glb", "provider-model")
        val fbx = copyAsset("animated-character.fbx", "animated.fbx")
        val png = ByteArrayOutputStream().also { output ->
            val bitmap = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888)
            try { bitmap.eraseColor(Color.GREEN); assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) }
            finally { bitmap.recycle() }
        }.toByteArray()
        val zip = File(folder, "textured-obj.zip")
        ZipOutputStream(zip.outputStream()).use { archive ->
            fun entry(name: String, bytes: ByteArray) { archive.putNextEntry(ZipEntry(name)); archive.write(bytes); archive.closeEntry() }
            entry("model/plane.obj", "mtllib plane.mtl\nv -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl green\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\n".toByteArray())
            entry("model/plane.mtl", "newmtl green\nKd 1 1 1\nmap_Kd textures/green.png\n".toByteArray())
            entry("model/textures/green.png", png)
        }
        val ids = mutableListOf<Long>()
        for (file in listOf(glb, fbx, zip)) {
            val before = store.layers.map { it.id }.toSet()
            compose.runOnIdle { store.importModel(Uri.fromFile(file)) }
            try { compose.waitUntil(45000) { store.layers.any { it.id !in before } } }
            catch (error: Throwable) { throw AssertionError("${file.name} import failed: ${store.errorMessage}; busy=${store.busyMessage}; optimize=${store.modelOptimize}", error) }
            val layer = store.layers.single { it.id !in before }
            assertEquals(10, layer.kind)
            assertTrue("${file.name} lost companion textures", store.engineForStress.modelMissingTextures(layer.id).isEmpty())
            ids += layer.id
        }
        val objFolder = File(store.engineForStress.modelFolder(ids.last()))
        assertTrue(File(objFolder, "plane.mtl").isFile)
        assertArrayEquals(png, File(objFolder, "textures/green.png").readBytes())
        val project = File(folder, "models.aurea")
        compose.runOnIdle { assertEquals(0, store.engineForStress.saveProject(project.absolutePath)); assertEquals(0, store.engineForStress.loadProject(project.absolutePath)) }
        ids.forEach { assertTrue("Reload lost textures", store.engineForStress.modelMissingTextures(it).isEmpty()) }
        File(context.getExternalFilesDir(null), "model-import-result.txt").writeText("PASS extensionless GLB, animated FBX, ZIP OBJ+MTL+PNG, save/reopen\n")
    }
}
