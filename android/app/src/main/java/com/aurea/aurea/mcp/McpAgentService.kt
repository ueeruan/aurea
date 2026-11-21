package com.aurea.aurea.mcp

import android.app.Application
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.aurea.aurea.AureaApplication
import com.aurea.aurea.MainActivity
import com.aurea.aurea.R
import com.aurea.aurea.state.ExportForegroundService
import com.aurea.aurea.ui.i18n.AppText

/**
 * AureaMCP fora do app (18/11/2026): o serviço em primeiro plano que mantém o
 * processo — e com ele a conexão do [AureaMcp] e o motor do editor — de pé com a
 * pessoa em outro app ou com a tela apagada.
 *
 * - Começa com o app na frente (AureaMCP ligado + "Continuar fora do app") ou por
 *   um push do FCM de prioridade alta ([McpPushService]; o Android isenta esse
 *   caso da regra de não começar serviço do segundo plano).
 * - Tipos: os mesmos do export ([ExportForegroundService.foregroundTypes]):
 *   mediaProcessing e, recusado, dataSync no Android 15+; dataSync no 10–14.
 *   O Android 15+ dá ~6 h por dia a cada tipo em segundo plano: no [onTimeout]
 *   o serviço sai na hora e avisa (abrir o Aurea zera a conta).
 * - Wake lock parcial SÓ enquanto uma chamada roda ([callStarted]/[callEnded]).
 * - Notificação fixa com Pausar e Parar; aprovações fora da tela chegam como
 *   notificação com Aprovar/Recusar ([McpActionReceiver]).
 *
 * Só Android: o PC não compila este arquivo (desktop/build.gradle.kts).
 */
class McpAgentService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // O STOP chega por startService (o serviço já está de pé): sai direto.
        if (intent?.action == ACTION_STOP) {
            leave()
            return START_NOT_STICKY
        }
        // startForegroundService exige o startForeground mesmo quando o serviço vai sair
        // logo (desligado nos ajustes): sem ele o Android derruba o app.
        val started = ExportForegroundService.foregroundTypes(Build.VERSION.SDK_INT).any { type ->
            runCatching {
                // A chamada da plataforma, direto (o ServiceCompat não conhece o mediaProcessing; ver o export).
                if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION_ID, notification(this), type)
                else startForeground(NOTIFICATION_ID, notification(this))
            }.onFailure { Log.w(TAG, "foreground type $type refused", it) }.isSuccess
        }
        if (!started) {
            // Recusado (segundo plano sem isenção, cota do dia esgotada): sem serviço o
            // processo congela; o relay põe as chamadas na fila e o push tenta de novo.
            leave()
            return START_NOT_STICKY
        }
        if (!AureaMcp.keepWorkingIn(this)) {
            leave()
            return START_NOT_STICKY
        }
        McpAgentHub.markServiceRunning(true)
        ensureClient(application)
        return START_NOT_STICKY
    }

    /** Android 15+: a cota diária do tipo acabou. Sai já (o sistema exige) e avisa. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "agent service timeout (type $fgsType)")
        runCatching {
            if (canNotify(this)) NotificationManagerCompat.from(this).notify(TIMEOUT_ID, infoNotification(this,
                AppText.get(this, R.string.mcp_bg_timeout_title), AppText.get(this, R.string.mcp_bg_timeout_text)))
        }
        leave()
    }

    override fun onDestroy() {
        releaseWake()
        McpAgentHub.markServiceRunning(false)
        super.onDestroy()
    }

    private fun leave() {
        runCatching { ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE) }
        stopSelf()
        McpAgentHub.markServiceRunning(false)
    }

    companion object {
        private const val TAG = "AureaMcpService"
        private const val CHANNEL = "aurea_mcp_agent"
        private const val CHANNEL_CONFIRM = "aurea_mcp_confirm"
        private const val NOTIFICATION_ID = 0x4D01
        private const val TIMEOUT_ID = 0x4D02
        private const val WAKE_ID = 0x4D03
        private const val CONFIRM_BASE = 0x4E00
        const val ACTION_STOP = "com.aurea.aurea.mcp.STOP_SERVICE"
        /** Teto do wake lock de uma chamada (o relay desiste em 45–95 s; folga para export/import longos). */
        private const val WAKE_MAX_MS = 10L * 60 * 1000

        private var wake: PowerManager.WakeLock? = null
        private var calls = 0

        /** Liga (app na frente, ou o push do FCM). Falha em silêncio: o relay continua com a fila. */
        fun start(context: Context): Boolean = runCatching {
            ContextCompat.startForegroundService(context.applicationContext, Intent(context.applicationContext, McpAgentService::class.java))
            true
        }.getOrElse { Log.w(TAG, "could not start agent service", it); false }

        fun stop(context: Context) {
            val app = context.applicationContext
            if (!McpAgentHub.serviceRunning) return
            runCatching { app.startService(Intent(app, McpAgentService::class.java).setAction(ACTION_STOP)) }
                .onFailure { runCatching { app.stopService(Intent(app, McpAgentService::class.java)) } }
        }

        /** O editor (e com ele o AureaMCP) existe e está conectando, mesmo sem tela. */
        fun ensureClient(app: Application) {
            val store = (app as? AureaApplication)?.editorStore() ?: return
            val mcp = store.mcp
            if (!McpAgentHub.foreground) store.agentTouch()
            mcp.start()
        }

        @Synchronized fun callStarted(context: Context) {
            calls++
            if (wake?.isHeld == true) return
            runCatching {
                wake = (context.getSystemService(Context.POWER_SERVICE) as PowerManager)
                    .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "aurea:mcp-call").apply {
                        setReferenceCounted(false)
                        acquire(WAKE_MAX_MS)
                    }
            }.onFailure { Log.w(TAG, "wake lock unavailable", it) }
        }

        @Synchronized fun callEnded() {
            calls = (calls - 1).coerceAtLeast(0)
            if (calls == 0) releaseWake()
        }

        @Synchronized private fun releaseWake() {
            runCatching { wake?.let { if (it.isHeld) it.release() } }
            wake = null
            calls = 0
        }

        fun canNotify(context: Context): Boolean =
            NotificationManagerCompat.from(context).areNotificationsEnabled() &&
                (Build.VERSION.SDK_INT < 33 || ContextCompat.checkSelfPermission(context, "android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED)

        private fun ensureChannels(context: Context) {
            if (Build.VERSION.SDK_INT < 26) return
            val m = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            if (m.getNotificationChannel(CHANNEL) == null) m.createNotificationChannel(
                NotificationChannel(CHANNEL, AppText.get(context, R.string.mcp_bg_channel), NotificationManager.IMPORTANCE_LOW))
            if (m.getNotificationChannel(CHANNEL_CONFIRM) == null) m.createNotificationChannel(
                NotificationChannel(CHANNEL_CONFIRM, AppText.get(context, R.string.mcp_bg_channel_confirm), NotificationManager.IMPORTANCE_HIGH))
        }

        private fun openApp(context: Context, request: Int = 0): PendingIntent = PendingIntent.getActivity(context, request,
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

        private fun action(context: Context, action: String, request: Int, id: Long = 0): PendingIntent = PendingIntent.getBroadcast(context, request,
            Intent(context, McpActionReceiver::class.java).setAction(action)
                .setData(android.net.Uri.parse("aurea-mcp-action://request/$id"))
                .putExtra(McpActionReceiver.EXTRA_ID, id),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

        private fun notification(context: Context): Notification {
            ensureChannels(context)
            val text = AppText.get(context, R.string.mcp_bg_notification_text)
            return NotificationCompat.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(AppText.get(context, R.string.mcp_bg_notification_title))
                .setContentText(text)
                .setStyle(NotificationCompat.BigTextStyle().bigText(text))
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setSilent(true)
                .setContentIntent(openApp(context))
                .addAction(0, AppText.get(context, R.string.mcp_bg_action_pause), action(context, McpActionReceiver.ACTION_PAUSE, 1))
                .addAction(0, AppText.get(context, R.string.mcp_bg_action_stop), action(context, McpActionReceiver.ACTION_STOP, 2))
                .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
                .build()
        }

        private fun infoNotification(context: Context, title: String, text: String): Notification {
            ensureChannels(context)
            return NotificationCompat.Builder(context, CHANNEL_CONFIRM)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(NotificationCompat.BigTextStyle().bigText(text))
                .setAutoCancel(true)
                .setContentIntent(openApp(context, 3))
                .build()
        }

        /** O push chegou mas o Android não deixou subir o serviço: um aviso que abre o app (como no iOS). */
        fun notifyWakeFallback(context: Context) {
            if (!canNotify(context)) return
            runCatching {
                NotificationManagerCompat.from(context).notify(WAKE_ID, infoNotification(context,
                    AppText.get(context, R.string.mcp_bg_wake_title), AppText.get(context, R.string.mcp_bg_wake_text)))
            }
        }

        fun showConfirm(context: Context, id: Long, tool: String, agent: String, detail: String): Boolean {
            if (!canNotify(context)) return false
            ensureChannels(context)
            val body = if (tool == "delete_project") AppText.get(context, R.string.mcp_confirm_delete, detail)
                else AppText.get(context, R.string.mcp_confirm_edit, tool) + if (detail.isNotBlank()) "\n" + detail.take(200) else ""
            val n = NotificationCompat.Builder(context, CHANNEL_CONFIRM)
                .setSmallIcon(R.drawable.ic_launcher_monochrome)
                .setContentTitle(AppText.get(context, R.string.mcp_confirm_title, agent))
                .setContentText(body)
                .setStyle(NotificationCompat.BigTextStyle().bigText(body))
                .setCategory(NotificationCompat.CATEGORY_REMINDER)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setTimeoutAfter(90_000)
                .setContentIntent(openApp(context, 4))
                .addAction(0, AppText.get(context, R.string.mcp_approve), action(context, McpActionReceiver.ACTION_APPROVE, (id * 2).toInt(), id))
                .addAction(0, AppText.get(context, R.string.mcp_deny), action(context, McpActionReceiver.ACTION_DENY, (id * 2 + 1).toInt(), id))
                .build()
            return runCatching { NotificationManagerCompat.from(context).notify("mcp-confirm-$id", CONFIRM_BASE, n); true }.getOrDefault(false)
        }

        fun clearConfirm(context: Context, id: Long) {
            runCatching { NotificationManagerCompat.from(context).cancel("mcp-confirm-$id", CONFIRM_BASE) }
        }
    }
}

/** As ações das notificações do AureaMCP (Aprovar/Recusar, Pausar, Parar). */
class McpActionReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val id = intent.getLongExtra(EXTRA_ID, 0)
        when (intent.action) {
            ACTION_APPROVE -> { McpAgentHub.answerConfirm(id, true); McpAgentService.clearConfirm(context, id) }
            ACTION_DENY -> { McpAgentHub.answerConfirm(id, false); McpAgentService.clearConfirm(context, id) }
            ACTION_PAUSE -> McpAgentHub.pauseFromNotification()
            ACTION_STOP -> McpAgentHub.stopFromNotification()
        }
    }

    companion object {
        const val EXTRA_ID = "id"
        const val ACTION_APPROVE = "com.aurea.aurea.mcp.APPROVE"
        const val ACTION_DENY = "com.aurea.aurea.mcp.DENY"
        const val ACTION_PAUSE = "com.aurea.aurea.mcp.PAUSE"
        const val ACTION_STOP = "com.aurea.aurea.mcp.STOP"
    }
}

/** Os ganchos do Android para o [McpAgentHub] (registrados no AureaApplication). */
class AndroidMcpPlatform(private val app: Application) : McpAgentHub.Platform {
    private val main = Handler(Looper.getMainLooper())
    override fun startService() { McpAgentService.start(app) }
    override fun stopService() { McpAgentService.stop(app) }
    override fun callStarted() = McpAgentService.callStarted(app)
    override fun callEnded() = McpAgentService.callEnded()
    override fun showConfirm(id: Long, tool: String, agent: String, detail: String) = McpAgentService.showConfirm(app, id, tool, agent, detail)
    override fun clearConfirm(id: Long) = McpAgentService.clearConfirm(app, id)
    override fun requestPushToken(onToken: (String?) -> Unit) = McpPushService.requestToken(app) { t -> main.post { onToken(t) } }
    override fun deletePushToken() = McpPushService.deleteToken()
    override fun notificationPermission(): String? = if (Build.VERSION.SDK_INT >= 33) "android.permission.POST_NOTIFICATIONS" else null
    override fun notificationsAllowed(): Boolean = McpAgentService.canNotify(app)
    /** Sem tela e sem serviço: o editor sai da memória (salva e desliga o motor, como fechar o app). */
    override fun serviceStopped() { main.post { (app as? AureaApplication)?.releaseEditorIfIdle() } }
}
