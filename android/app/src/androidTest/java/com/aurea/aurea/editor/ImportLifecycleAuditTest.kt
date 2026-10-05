package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class ImportLifecycleAuditTest {
    @get:Rule val compose = createComposeRule()

    @Test fun failedBeatAnalysisReleasesTheProjectAndAllowsAnotherAttempt() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        val pcmBytes = 48000 * 2 // One second, shorter than the detector's supported interval.
        val wav = File(context.cacheDir, "short-beat-analysis.wav")
        wav.writeBytes(ByteBuffer.allocate(44 + pcmBytes).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + pcmBytes); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(48000); putInt(96000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(pcmBytes)
        }.array())
        lateinit var store: EditorStore
        var created = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            created = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { created && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Beat error recovery") }
        compose.waitUntil(10000) { store.project.title == "Beat error recovery" && !store.projectOperationBusy }
        compose.runOnIdle { store.importAudio(Uri.fromFile(wav)) }
        compose.waitUntil(15000) { store.layers.size == 1 && store.busyMessage == null }
        repeat(2) {
            compose.runOnIdle {
                store.select(store.layers.single().id)
                store.detectBeats()
                assertTrue(store.detectingBeats)
                store.newProject(320, 180, 30f, "Must wait for analysis")
                assertEquals("Beat error recovery", store.project.title)
            }
            compose.waitUntil(10000) { !store.detectingBeats }
        }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Beat controls recovered") }
        compose.waitUntil(10000) { store.project.title == "Beat controls recovered" && !store.projectOperationBusy }
        assertTrue(store.layers.isEmpty())
    }

    @Test fun unreadableImportsRecoverAndCannotSwitchTheirDestinationProject() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var created = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            created = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { created && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Import destination") }
        compose.waitUntil(10000) { store.project.title == "Import destination" }
        val missing = Uri.parse("content://com.aurea.missing.provider/unreadable")
        compose.runOnIdle {
            store.importImage(missing)
            store.newProject(320, 180, 30f, "Wrong destination")
        }
        compose.waitUntil(10000) { store.busyMessage == null && store.errorMessage != null }
        assertEquals("Import destination", store.project.title)
        assertTrue(store.layers.isEmpty())
        compose.runOnIdle { store.dismissError(); store.importAudio(missing) }
        compose.waitUntil(10000) { store.busyMessage == null && store.errorMessage != null }
        compose.runOnIdle { store.dismissError(); store.importSvg(missing) }
        compose.waitUntil(10000) { store.errorMessage != null }
        compose.runOnIdle { store.dismissError(); store.newProject(320, 180, 30f, "Recovered import") }
        compose.waitUntil(10000) { store.project.title == "Recovered import" }
        assertTrue(store.layers.isEmpty())
    }

    @Test fun teardownReturnsWithoutWaitingForQueuedLifecycleWork() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val app = instrumentation.targetContext.applicationContext as Application
        lateinit var store: EditorStore
        instrumentation.runOnMainSync { store = EditorStore(app) }
        compose.waitUntil(30000) { store.engineReady }
        val field = EditorStore::class.java.getDeclaredField("lifecycleThread").apply { isAccessible = true }
        val executor = field.get(store) as ExecutorService
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        executor.execute { entered.countDown(); release.await(10, TimeUnit.SECONDS) }
        assertTrue(entered.await(5, TimeUnit.SECONDS))
        try {
            var elapsed = Long.MAX_VALUE
            instrumentation.runOnMainSync {
                val started = System.nanoTime()
                store.shutdown()
                elapsed = (System.nanoTime() - started) / 1_000_000
                // Late OS callbacks must not enqueue onto a shut down executor.
                store.onEnterForeground()
                store.onEnterBackground()
                store.onTrimMemory(80)
            }
            assertTrue("Main thread waited ${elapsed}ms for background work", elapsed < 500)
            assertFalse(store.engineReady)
        } finally {
            release.countDown()
            assertTrue(executor.awaitTermination(30, TimeUnit.SECONDS))
        }
    }

    @Test fun firstSaveFailureCannotOverwriteThePreviousProject() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var created = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            created = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { created && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Preserved before IO failure") }
        compose.waitUntil(10000) { store.project.path != null && !store.projectOperationBusy }
        val previous = java.io.File(checkNotNull(store.project.path))
        val originalBytes = previous.readBytes()
        val directory = checkNotNull(previous.parentFile)
        assertTrue(directory.setWritable(false, false))
        try {
            compose.runOnIdle { store.newProject(640, 360, 24f, "New project with failed first save") }
            compose.waitUntil(10000) { store.errorMessage != null && !store.projectOperationBusy }
            assertNotEquals(previous.absolutePath, store.project.path)
        } finally { assertTrue(directory.setWritable(true, true)) }
        compose.runOnIdle { store.dismissError(); store.saveProject() }
        compose.waitUntil(10000) { !store.projectOperationBusy }
        assertNull(store.errorMessage)
        assertTrue(java.io.File(checkNotNull(store.project.path)).isFile)
        assertArrayEquals("The prior project must remain byte-for-byte intact", originalBytes, previous.readBytes())
    }
}
