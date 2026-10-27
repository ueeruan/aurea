package com.aurea.aurea.state

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import com.aurea.aurea.MainActivity
import com.aurea.aurea.R

/**
 * 0.3.2 (docs/diagnostico/export-engine-2.0.md, 1.1): o export continua com o
 * app em segundo plano. Antes o `onStop` suspendia o motor, que cancelava o
 * export; ir para outro app ou apagar a tela perdia o vídeo inteiro.
 *
 * Serviço em primeiro plano do tipo `mediaProcessing` (Android 15+) ou
 * `dataSync` (Android 10–14), com a notificação de progresso e um wake lock
 * parcial (a CPU não dorme com a tela apagada). Começa quando o export começa
 * (o app está na frente, como o Android exige) e sai quando ele termina.
 * Sem permissão de notificação, o serviço roda igual e só a notificação não
 * aparece na gaveta.
 */
class ExportForegroundService : Service() {
    private var wakeLock: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            release()
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        val type = when {
            Build.VERSION.SDK_INT >= 35 -> ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
            Build.VERSION.SDK_INT >= 29 -> ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            else -> 0
        }
        val started = runCatching {
            // 0.3.5: a chamada da plataforma, direto. O ServiceCompat desta versão do
            // androidx não conhece o mediaProcessing (API 35) e mandava o tipo 0, que o
            // Android 14+ recusa ("FGS with type none"): o export seguia sem o serviço e
            // o sistema podia matá-lo no meio (bug 12).
            if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION_ID, notification(this, percent = -1), type)
            else startForeground(NOTIFICATION_ID, notification(this, percent = -1))
        }
        if (started.isFailure) {
            // Android 12+ recusa começar de segundo plano: o export segue, só
            // sem a garantia de continuar fora do app.
            Log.w(TAG, "foreground service refused", started.exceptionOrNull())
            stopSelf()
            return START_NOT_STICKY
        }
        if (wakeLock == null) {
            wakeLock = (getSystemService(Context.POWER_SERVICE) as PowerManager)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "aurea:export").apply {
                    setReferenceCounted(false)
                    acquire(MAX_WAKE_MS)
                }
        }
        // Morto pelo sistema, não volta sozinho: o motor decide o que retomar.
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        release()
        super.onDestroy()
    }

    private fun release() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    companion object {
        private const val TAG = "AureaExportService"
        private const val CHANNEL = "aurea_export"
        private const val NOTIFICATION_ID = 0x3202
        private const val ACTION_STOP = "com.aurea.aurea.export.STOP"
        /** Teto do wake lock: um export nunca segura a CPU além disto. */
        private const val MAX_WAKE_MS = 6L * 60 * 60 * 1000
        @Volatile private var running = false
        @Volatile private var lastPercent = -2

        /** O export começou (chamado com o app na frente). */
        fun start(context: Context) {
            val app = context.applicationContext
            lastPercent = -2
            runCatching {
                androidx.core.content.ContextCompat.startForegroundService(app, Intent(app, ExportForegroundService::class.java))
                running = true
            }.onFailure { Log.w(TAG, "could not start export service", it) }
        }

        /** Progresso na notificação (só quando o inteiro muda). */
        fun update(context: Context, framesDone: Int, framesTotal: Int) {
            if (!running) return
            val percent = if (framesTotal > 0) (framesDone.toLong() * 100 / framesTotal).toInt().coerceIn(0, 100) else -1
            if (percent == lastPercent) return
            lastPercent = percent
            val app = context.applicationContext
            runCatching {
                (app.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                    .notify(NOTIFICATION_ID, notification(app, percent))
            }
        }

        /** O export terminou (ok, falha ou cancelado). */
        fun stop(context: Context) {
            if (!running) return
            running = false
            val app = context.applicationContext
            runCatching { app.startService(Intent(app, ExportForegroundService::class.java).setAction(ACTION_STOP)) }
                .onFailure { runCatching { app.stopService(Intent(app, ExportForegroundService::class.java)) } }
        }

        private fun notification(context: Context, percent: Int): Notification {
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            if (Build.VERSION.SDK_INT >= 26 && manager.getNotificationChannel(CHANNEL) == null) {
                manager.createNotificationChannel(NotificationChannel(CHANNEL,
                    context.getString(R.string.export_service_channel), NotificationManager.IMPORTANCE_LOW))
            }
            val open = PendingIntent.getActivity(context, 0,
                Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
            return NotificationCompat.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(context.getString(R.string.export_service_title))
                .setContentText(if (percent >= 0) context.getString(R.string.export_service_progress, percent)
                    else context.getString(R.string.export_service_preparing))
                .setProgress(100, percent.coerceAtLeast(0), percent < 0)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setSilent(true)
                .setContentIntent(open)
                .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
                .build()
        }
    }
}
