package com.aurea.aurea.state

import android.provider.MediaStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** "O vídeo exportado não aparece na galeria" (beta 07/10): as regras do GalleryPublish. */
class GalleryPublishTest {
    private val now = 1_791_400_000_123L

    @Test
    fun videoIsPendingWithTheRightMimeFolderAndDates() {
        val cols = GalleryPublish.pendingColumns("Aurea 1.mp4", ExportFormat.Video.mime,
            GalleryPublish.relativeFolder(ExportFormat.Video), now).toMap()
        assertEquals("Aurea 1.mp4", cols[MediaStore.MediaColumns.DISPLAY_NAME])
        assertEquals("video/mp4", cols[MediaStore.MediaColumns.MIME_TYPE])
        assertEquals("Movies/Aurea", cols[MediaStore.MediaColumns.RELATIVE_PATH])
        assertEquals(1, cols[MediaStore.MediaColumns.IS_PENDING])
        // DATE_TAKEN em ms (linha do tempo da galeria); DATE_ADDED/MODIFIED em s.
        assertEquals(now, cols[MediaStore.Video.VideoColumns.DATE_TAKEN])
        assertEquals(now / 1000, cols[MediaStore.MediaColumns.DATE_ADDED])
        assertEquals(now / 1000, cols[MediaStore.MediaColumns.DATE_MODIFIED])
    }

    @Test
    fun imagesGoToPicturesAndTheZipToDownloadsWithoutDateTaken() {
        val gif = GalleryPublish.pendingColumns("a.gif", ExportFormat.Gif.mime, GalleryPublish.relativeFolder(ExportFormat.Gif), now).toMap()
        assertEquals("image/gif", gif[MediaStore.MediaColumns.MIME_TYPE])
        assertEquals("Pictures/Aurea", gif[MediaStore.MediaColumns.RELATIVE_PATH])
        assertTrue(gif.containsKey(MediaStore.Video.VideoColumns.DATE_TAKEN))
        val png = GalleryPublish.pendingColumns("a.png", ExportFormat.Frame.mime, GalleryPublish.relativeFolder(ExportFormat.Frame), now).toMap()
        assertEquals("image/png", png[MediaStore.MediaColumns.MIME_TYPE])
        val zip = GalleryPublish.pendingColumns("a.zip", ExportFormat.Sequence.mime,
            GalleryPublish.relativeFolder(ExportFormat.Sequence), now, dateTaken = false).toMap()
        assertEquals("application/zip", zip[MediaStore.MediaColumns.MIME_TYPE])
        assertEquals("Download/Aurea", zip[MediaStore.MediaColumns.RELATIVE_PATH])
        assertFalse(zip.containsKey(MediaStore.Video.VideoColumns.DATE_TAKEN))
    }

    @Test
    fun publishingClearsPendingSoEveryGallerySeesIt() {
        val cols = GalleryPublish.publishColumns(now).toMap()
        assertEquals(0, cols[MediaStore.MediaColumns.IS_PENDING])
        assertEquals(now / 1000, cols[MediaStore.MediaColumns.DATE_MODIFIED])
    }

    @Test
    fun onlyAndroid8And9UseTheLegacyPublicFolder() {
        assertTrue(GalleryPublish.legacyStorage(26))
        assertTrue(GalleryPublish.legacyStorage(28))
        assertFalse(GalleryPublish.legacyStorage(29))
        assertFalse(GalleryPublish.legacyStorage(35))
    }
}
