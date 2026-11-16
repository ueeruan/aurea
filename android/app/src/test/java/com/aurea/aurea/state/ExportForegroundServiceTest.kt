package com.aurea.aurea.state

import android.content.pm.ServiceInfo
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * 0.5.3 (P0-4): os tipos de serviço em primeiro plano tentados, na ordem, e o
 * estado do serviço que vai para o relato local do export.
 */
class ExportForegroundServiceTest {
    private val dataSync = ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
    private val mediaProcessing = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING

    @Test fun typesToTryFollowTheAndroidVersion() {
        // Android 8–9: startForeground sem tipo.
        assertEquals(listOf(0), ExportForegroundService.foregroundTypes(26))
        assertEquals(listOf(0), ExportForegroundService.foregroundTypes(28))
        // Android 10–14: dataSync.
        assertEquals(listOf(dataSync), ExportForegroundService.foregroundTypes(29))
        assertEquals(listOf(dataSync), ExportForegroundService.foregroundTypes(34))
        // Android 15+: mediaProcessing e, recusado, o outro tipo declarado no manifesto.
        assertEquals(listOf(mediaProcessing, dataSync), ExportForegroundService.foregroundTypes(35))
        assertEquals(listOf(mediaProcessing, dataSync), ExportForegroundService.foregroundTypes(36))
    }

    @Test fun theTwoTypesAreTheOnesTheManifestDeclares() {
        // android:foregroundServiceType="dataSync|mediaProcessing" (AndroidManifest.xml).
        assertEquals(1, dataSync)
        assertEquals(1 shl 13, mediaProcessing)
    }

    @Test fun refusalAndTimeoutNeverReadAsStarted() {
        fun label(timedOut: Boolean = false, refused: Boolean = false, foreground: Boolean = false, running: Boolean = false) =
            ExportForegroundService.stateLabel(timedOut, refused, foreground, running)
        assertEquals("off", label())
        assertEquals("starting", label(running = true))                       // pedido, ainda sem resposta
        assertEquals("started", label(foreground = true, running = true))
        // Recusado: `running` é desligado junto, mas mesmo um estado velho não esconde a recusa.
        assertEquals("refused", label(refused = true))
        assertEquals("refused", label(refused = true, running = true))
        // O tempo esgotado (onTimeout) ganha de tudo: o serviço JÁ tinha começado.
        assertEquals("timeout", label(timedOut = true, foreground = true, running = true))
        assertEquals("timeout", label(timedOut = true, refused = true))
    }
}
