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
        val session = intent?.getLongExtra(EXTRA_SESSION, 0L) ?: 0L
        if (intent?.action == ACTION_STOP) {
            if (session != activeSession || exportActive) return START_NOT_STICKY
            release()
            foreground = false
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        // 0.5.3 (P0-4): os tipos declarados no manifesto, na ordem. O Android 15+
        // dá a cada tipo um orçamento diário próprio: mediaProcessing esgotado
        // (ou recusado pelo fabricante) ainda deixa o dataSync.
        var refusal: Throwable? = null
        val started = foregroundTypes(Build.VERSION.SDK_INT).any { type ->
            runCatching {
                // 0.3.5: a chamada da plataforma, direto. O ServiceCompat desta versão do
                // androidx não conhece o mediaProcessing (API 35) e mandava o tipo 0, que o
                // Android 14+ recusa ("FGS with type none"): o export seguia sem o serviço e
                // o sistema podia matá-lo no meio (bug 12).
                if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION_ID, notification(this, percent = -1), type)
                else startForeground(NOTIFICATION_ID, notification(this, percent = -1))
            }.onFailure { refusal = it; Log.w(TAG, "foreground service type $type refused", it) }.isSuccess
        }
        if (!started) {
            // Android 12+ recusa começar de segundo plano: o export segue, só
            // sem a garantia de continuar fora do app. 0.5.3: a recusa fica
            // registrada (o Exporter a põe no relato local), `running` deixa de
            // mentir e o wake lock de reserva do processo continua segurando a CPU.
            Log.w(TAG, "foreground service refused", refusal)
            if (session == activeSession) {
                refused = true
                running = false
                foreground = false
            }
            stopSelfResult(startId)
            return START_NOT_STICKY
        }
        // Aceito: vale mesmo depois de um onDestroy da instância anterior. Com o
        // export já encerrado (este comando chegou atrasado), o STOP vem logo atrás.
        foreground = true
        if (session != activeSession || !exportActive) {
            // A pending start must enter foreground even when its export has ended.
            // Only terminate it if it is still the latest service command.
            if (!exportActive) {
                foreground = false
                ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
                stopSelfResult(startId)
            }
            return START_NOT_STICKY
        }
        running = true
        if (wakeLock == null) {
            wakeLock = (getSystemService(Context.POWER_SERVICE) as PowerManager)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "aurea:export").apply {
                    setReferenceCounted(false)
                    acquire(MAX_WAKE_MS)
                }
        }
        // 0.5.3: o wake lock do serviço assumiu; o de reserva do processo sai.
        releaseFallbackWake()
        // Morto pelo sistema, não volta sozinho: o motor decide o que retomar.
        return START_NOT_STICKY
    }

    /**
     * 0.3.6: Android 15+ limita `mediaProcessing` (e `dataSync`) a 6 h por dia
     * em segundo plano. Passado o limite o sistema chama isto e, se o serviço
     * não sair em poucos segundos, encerra o app com erro — o export morria sem
     * aviso. Aqui o serviço sai na hora (o que o sistema exige) e avisa: o
     * export continua enquanto o sistema deixar, o motor mantém o checkpoint, e
     * se o processo for encerrado a próxima exportação continua de onde parou.
     */
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "foreground service timeout (type $fgsType); leaving foreground and warning the user")
        timedOut = true
        release()
        runCatching { ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE) }
        running = false
        foreground = false
        runCatching {
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .notify(TIMEOUT_NOTIFICATION_ID, timeoutNotification(this))
        }
        stopSelf()
    }

    override fun onDestroy() {
        release()
        // 0.5.3 (P0-4): sem serviço, `running` não pode continuar verdadeiro (a
        // notificação de progresso seguiria sendo postada e o stop() mandaria um
        // comando a um serviço morto). Destruído com o export ainda em curso (o
        // sistema, ou a corrida de um export que começa quando o anterior acaba):
        // a CPU fica com o wake lock de reserva.
        running = false
        foreground = false
        if (exportActive && !timedOut) acquireFallbackWake(applicationContext)
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
        private const val TIMEOUT_NOTIFICATION_ID = 0x3203
        private const val ACTION_STOP = "com.aurea.aurea.export.STOP"
        private const val EXTRA_SESSION = "export_session"
        @Volatile private var activeSession = 0L
        /** Teto do wake lock: um export nunca segura a CPU além disto. */
        private const val MAX_WAKE_MS = 6L * 60 * 60 * 1000
        @Volatile private var running = false
        @Volatile private var lastPercent = -2
        /** 0.3.6: o sistema esgotou o tempo do serviço neste export (onTimeout). */
        @Volatile var timedOut = false
            private set
        /** 0.5.3 (P0-4): o Android recusou o serviço em primeiro plano neste export (todos os tipos). */
        @Volatile var refused = false
            private set
        /** 0.5.3: o `startForeground` foi aceito e o serviço segue em primeiro plano. */
        @Volatile private var foreground = false
        /** 0.5.3: há um export entre [start] e [stop] (com ou sem serviço). */
        @Volatile private var exportActive = false
        /** 0.5.3: wake lock do PROCESSO enquanto o serviço não assume (recusado, nunca começou, destruído). */
        private var fallbackWake: PowerManager.WakeLock? = null

        /**
         * 0.5.3 (P0-4): os tipos de serviço em primeiro plano a tentar, na ordem.
         * Android 15+: mediaProcessing e, recusado, dataSync (os dois estão no
         * manifesto); Android 10–14: dataSync; antes disso não há tipo (0).
         */
        internal fun foregroundTypes(sdk: Int): List<Int> = when {
            sdk >= 35 -> listOf(ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            sdk >= 29 -> listOf(ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
            else -> listOf(0)
        }

        /** O estado do serviço numa palavra, para o relato local do export. */
        internal fun stateLabel(timedOut: Boolean, refused: Boolean, foreground: Boolean, running: Boolean): String = when {
            timedOut -> "timeout"
            refused -> "refused"
            foreground -> "started"
            running -> "starting"
            else -> "off"
        }

        /** "started", "refused", "timeout", "starting" (pedido e ainda sem resposta) ou "off"; "+wake" = o wake lock de reserva está segurando a CPU. */
        fun diagnosticState(): String =
            stateLabel(timedOut, refused, foreground, running) + if (fallbackHeld()) "+wake" else ""

        @Synchronized private fun fallbackHeld(): Boolean = runCatching { fallbackWake?.isHeld == true }.getOrDefault(false)

        /**
         * 0.5.3 (P0-4): sem o serviço não havia wake lock nenhum — com a tela
         * apagada a CPU dormia e o export congelava. Este é do processo, com o
         * mesmo teto; sai quando o serviço assume e no [stop].
         */
        @Synchronized private fun acquireFallbackWake(app: Context) {
            if (fallbackWake?.isHeld == true) return
            runCatching {
                fallbackWake = (app.getSystemService(Context.POWER_SERVICE) as PowerManager)
                    .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "aurea:export-fallback").apply {
                        setReferenceCounted(false)
                        acquire(MAX_WAKE_MS)
                    }
            }.onFailure { Log.w(TAG, "fallback wake lock unavailable", it) }
        }

        @Synchronized private fun releaseFallbackWake() {
            runCatching { fallbackWake?.let { if (it.isHeld) it.release() } }
            fallbackWake = null
        }

        /** O export começou (chamado com o app na frente). */
        @Synchronized fun start(context: Context): Long {
            val app = context.applicationContext
            val session = ++activeSession
            lastPercent = -2
            timedOut = false
            refused = false
            foreground = false
            exportActive = true
            // 0.5.3: a CPU fica acordada desde já; o serviço troca pelo dele ao começar.
            acquireFallbackWake(app)
            runCatching {
                (app.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).cancel(TIMEOUT_NOTIFICATION_ID)
            }
            running = true
            runCatching {
                val intent = Intent(app, ExportForegroundService::class.java).putExtra(EXTRA_SESSION, session)
                androidx.core.content.ContextCompat.startForegroundService(app, intent)
            }.onFailure {
                running = false
                refused = true
                Log.w(TAG, "could not start export service", it)
            }
            return session
        }

        /** Progresso na notificação (só quando o inteiro muda). */
        fun update(context: Context, framesDone: Int, framesTotal: Int) {
            if (!running || !foreground || !exportActive) return
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
        @Synchronized fun stop(context: Context, session: Long) {
            if (session != activeSession) return
            // 0.5.3: antes do `running` — recusado ou destruído, o serviço não está
            // "rodando", mas o wake lock de reserva pode estar seguro.
            exportActive = false
            releaseFallbackWake()
            if (!running) return
            running = false
            val app = context.applicationContext
            runCatching { app.startService(Intent(app, ExportForegroundService::class.java).setAction(ACTION_STOP).putExtra(EXTRA_SESSION, session)) }
                .onFailure { runCatching { app.stopService(Intent(app, ExportForegroundService::class.java)) } }
        }

        /** 0.3.6: aviso (não fixo) de que o sistema tirou o export do primeiro plano. */
        private fun timeoutNotification(context: Context): Notification {
            ensureChannel(context)
            val text = com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export036_timeout_text)
            return NotificationCompat.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export036_timeout_title))
                .setContentText(text)
                .setStyle(NotificationCompat.BigTextStyle().bigText(text))
                .setAutoCancel(true)
                .setContentIntent(openApp(context))
                .build()
        }

        private fun ensureChannel(context: Context) {
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            if (Build.VERSION.SDK_INT >= 26 && manager.getNotificationChannel(CHANNEL) == null) {
                manager.createNotificationChannel(NotificationChannel(CHANNEL,
                    com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export_service_channel), NotificationManager.IMPORTANCE_LOW))
            }
        }

        private fun openApp(context: Context): PendingIntent = PendingIntent.getActivity(context, 0,
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

        private fun notification(context: Context, percent: Int): Notification {
            ensureChannel(context)
            val open = openApp(context)
            return NotificationCompat.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export_service_title))
                .setContentText(if (percent >= 0) com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export_service_progress, percent)
                    else com.aurea.aurea.ui.i18n.AppText.get(context, R.string.export_service_preparing))
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
