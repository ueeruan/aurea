package com.aurea.aurea.editor

import android.app.Application
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import com.aurea.aurea.ui.theme.AureaTheme
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.net.Uri
import java.io.File
import com.aurea.aurea.home.HomeScreen

/** Interactive production editor for local UI review; absent from debug/release APKs. */
class UiReviewActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Opening the separate TESTE UI launcher enters this experiment directly.
        val reviewReference = intent.getBooleanExtra("review_reference_ui", intent.extras == null)
        setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(application as Application))
            AureaTheme {
                if (intent.getBooleanExtra("review_home", false) && store.screen == Screen.Home) HomeScreen(store) else EditorScreen(store)
            }
            LaunchedEffect(store) {
                snapshotFlow { store.engineReady }.first { it }
                if (intent.getBooleanExtra("review_home", false)) return@LaunchedEffect
                if (intent.getBooleanExtra("review_camera", false)) {
                    val cameraTitle = "AUREA · Câmera + nulo · 3D sem inclinação"
                    store.newProject(720, 720, 30f, cameraTitle)
                    snapshotFlow { store.project.title }.first { it == cameraTitle }
                    snapshotFlow { store.projectOperationBusy }.first { !it }
                    val file = withContext(Dispatchers.IO) {
                        File(cacheDir, "review-bars.png").also { output -> assets.open("review-bars.png").use { source -> output.outputStream().use { source.copyTo(it) } } }
                    }
                    store.importImage(Uri.fromFile(file))
                    snapshotFlow { store.layers.size }.first { it == 1 }
                    val media = store.layers.single().id
                    store.addNull(true)
                    snapshotFlow { store.layers.size }.first { it == 2 }
                    val nullId = store.layers.first { it.kind == 6 }.id
                    store.addCamera()
                    snapshotFlow { store.layers.size }.first { it == 3 }
                    val cameraId = store.layers.first { it.kind == 8 }.id
                    store.setParent(cameraId, nullId)
                    store.select(media)
                    store.enableLayer3D()
                    store.setTransform(7, 25f, nullId)
                    store.seek(0)
                    return@LaunchedEffect
                }
                val title = if (reviewReference) "AUREA · TESTE da timeline" else "AUREA · UI compacta"
                store.newProject(720, 720, 30f, title)
                snapshotFlow { store.project.title }.first { it == title }
                repeat(3) { n ->
                    store.addShape(n)
                    snapshotFlow { store.layers.size }.first { it == n + 1 }
                }
                val ids = store.layers.map { it.id }.toLongArray()
                store.setLayerRanges(ids, intArrayOf(30, 45, 60), intArrayOf(150, 180, 210))
                store.select(ids[0])
                store.seek(90)
                if (reviewReference) {
                    ids.forEachIndexed { index, id ->
                        store.select(id)
                        snapshotFlow { store.detail?.id }.first { it == id }
                        val start = 30 + index * 15
                        store.seek(start + 15)
                        snapshotFlow { store.playhead }.first { it == start + 15 }
                        store.setTransform2(0, 180f + index * 140f, 1, 240f + index * 100f)
                        store.toggleTransformKeyframe(intArrayOf(0, 1))
                        snapshotFlow { store.keyframes[id].orEmpty().size }.first { it >= 2 }
                        store.seek(start + 75)
                        snapshotFlow { store.playhead }.first { it == start + 75 }
                        store.setTransform2(0, 500f - index * 100f, 1, 240f + index * 100f)
                        snapshotFlow { store.keyframes[id].orEmpty().size }.first { it >= 4 }
                    }
                    store.seek(90)
                    when (intent.getStringExtra("review_selection")) {
                        "precomp" -> store.precompose(ids.toList())
                        "single" -> store.select(ids[0])
                        "multi" -> { store.selectAll(); store.changeLayerSelectMode(true) }
                        else -> store.clearSelection()
                    }
                }
                intent.getStringExtra("review_effect")?.let { key ->
                    ids.forEach { store.setLayerVisible(it, false) }
                    val asset = if (key == "aurea.light.starglow") "review-impulse.png" else "review-bars.png"
                    val file = withContext(Dispatchers.IO) {
                        File(cacheDir, asset).also { output -> assets.open(asset).use { source -> output.outputStream().use { source.copyTo(it) } } }
                    }
                    store.importImage(Uri.fromFile(file))
                    snapshotFlow { store.layers.size }.first { it == 4 }
                    val source = store.layers.first { it.id !in ids }.id
                    snapshotFlow { store.primary }.first { it == source }
                    val type = com.aurea.aurea.editor.panels.effectTypeId(key)
                    store.addEffectAndFocus(type)
                    val effect = snapshotFlow { store.effects }.first { list -> list.any { it.typeId == type } }
                        .first { it.typeId == type }
                    if (key.startsWith("aurea.transition.wipe_")) {
                        val params = snapshotFlow { store.effectParams[effect.effectId] }.first { !it.isNullOrEmpty() }!!
                        store.setEffectParam(effect.effectId, params.first { it.index == 0 }, 50f)
                    }
                    if (key == "aurea.glitch.video_glitch") {
                        val params = snapshotFlow { store.effectParams[effect.effectId] }.first { !it.isNullOrEmpty() }!!
                        store.setEffectParam(effect.effectId, params.first { it.index == 2 }, 100f)
                        store.setEffectParam(effect.effectId, params.first { it.index == 4 }, -30f)
                    }
                }
                if (intent.getBooleanExtra("review_tracking", false)) {
                    ids.forEach { store.setLayerVisible(it, false) }
                    val file = withContext(Dispatchers.IO) {
                        File(cacheDir, "review-motion.mp4").also { output -> assets.open("review-motion.mp4").use { source -> output.outputStream().use { source.copyTo(it) } } }
                    }
                    store.importVideo(Uri.fromFile(file))
                    snapshotFlow { store.layers.size }.first { it == 4 }
                    store.seek(105)
                }
            }
        }
    }
}
