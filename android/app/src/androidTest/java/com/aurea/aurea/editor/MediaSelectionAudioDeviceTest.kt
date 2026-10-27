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
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.sin

/**
 * Bugs 0.3.4 (seleção de mídia, bug 5): o áudio escolhido em "Outros arquivos
 * e nuvem" da faixa de mídia entra pelo MESMO lote das fotos e vídeos
 * (`importMediaBatch`) e vira camada de áudio (kind 3), sem `READ_MEDIA_AUDIO`.
 */
class MediaSelectionAudioDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun audioPickedWithPhotosBecomesAnAudioLayer() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Bugs 034 media audio") }
        compose.waitUntil(10000) { store.project.title == "Bugs 034 media audio" && !store.projectOperationBusy }

        val audio = File(context.filesDir, "bugs034-media-audio.wav")
        val frames = 48000 * 2
        audio.writeBytes(ByteBuffer.allocate(44 + frames * 4).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(frames * 4)
            repeat(frames) { n ->
                val sample = (sin(2 * PI * 330.0 * n / 48000) * 6000).toInt().toShort()
                putShort(sample); putShort(sample)
            }
        }.array())
        val photo = File(context.filesDir, "bugs034-media-photo.png")
        val bitmap = Bitmap.createBitmap(32, 24, Bitmap.Config.ARGB_8888)
        try { bitmap.eraseColor(Color.GREEN); photo.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
        finally { bitmap.recycle() }

        compose.runOnIdle { store.importMediaBatch(listOf(Uri.fromFile(photo), Uri.fromFile(audio))) }
        compose.waitUntil(30000) { store.busyMessage == null && store.layers.size == 2 }
        compose.runOnIdle {
            assertNull(store.errorMessage)
            assertEquals("o WAV vira camada de áudio", 1, store.layers.count { it.kind == 3 })
            assertEquals("a foto continua imagem", 1, store.layers.count { it.kind == 2 })
            assertEquals(2, store.selection.size)
        }
    }
}
