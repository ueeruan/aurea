package com.aurea.aurea.home

import android.graphics.Bitmap
import android.net.Uri
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.UiImageCaches
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class ImageMemoryAuditTest {
    @Test fun veryTallPhotoIsSampledBeforeBitmapAllocation() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val file = File.createTempFile("tall-image-", ".png", context.cacheDir)
        val source = Bitmap.createBitmap(64, 16_384, Bitmap.Config.ARGB_8888)
        try { file.outputStream().use { assertTrue(source.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
        finally { source.recycle() }
        try {
            val thumbnail = checkNotNull(HomeThumbnails(context.resources).decodeFile(file.path, 512))
            try { assertTrue("Home must also bound portrait height", thumbnail.height <= 1024) }
            finally { thumbnail.recycle() }
            val imported = checkNotNull(AureaEngine.decodeBitmapRgba(context, Uri.fromFile(file), 2048))
            try { assertTrue(imported.height <= 2048); assertTrue(imported.width > 0) }
            finally { imported.recycle() }
        } finally { file.delete() }
    }

    @Test fun memoryWarningEvictsHomeImagesAndAnInflightLoadDoesNotRefillCache() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val thumbnails = HomeThumbnails(context.resources)
        val first = thumbnails.load("first") { Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888) }
        assertNotNull(first)
        assertSame(first, thumbnails.peek("first"))
        UiImageCaches.trim()
        assertNull(thumbnails.peek("first"))
        assertNotNull(thumbnails.load("late") {
            UiImageCaches.trim()
            Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888)
        })
        assertNull(thumbnails.peek("late"))
    }
}
