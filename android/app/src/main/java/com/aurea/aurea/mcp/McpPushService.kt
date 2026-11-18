package com.aurea.aurea.mcp

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.aurea.aurea.BuildConfig
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.messaging.FirebaseMessaging
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage

/**
 * AureaMCP fora do app (18/11/2026): o push que acorda o Aurea FECHADO.
 *
 * O Worker (discovery/mcp_push.js) manda uma mensagem de DADOS de prioridade
 * alta {t:"mcp_wake"} quando um agente pede algo e nenhum app está conectado.
 * Aqui: sobe o [McpAgentService] (o Android isenta o push de prioridade alta da
 * regra de não começar serviço em primeiro plano do segundo plano), que cria o
 * editor sem tela, conecta e o relay reenvia a fila. Sem conseguir subir, uma
 * notificação simples leva a pessoa ao app.
 *
 * O token vai ao relay no "hello" (só com "Continuar fora do app" ligado).
 *
 * Firebase: o app inicia o Firebase à mão (sem o plugin google-services; ver
 * conta/GoogleLogin.kt). O FCM exige o app PADRÃO com o sender id — [initDefault],
 * chamado no AureaApplication. O sender id é o número do projeto: vem de
 * `messagingSenderId` em android/firebase.properties ou do appId (1:<número>:android:…).
 *
 * TODO(dono): no Worker, `npx wrangler secret put FCM_SERVICE_ACCOUNT` com o JSON
 * de uma conta de serviço do projeto Firebase (papel "Firebase Cloud Messaging API
 * Admin"). Nenhuma chave de servidor fica no app.
 *
 * Só Android (o PC não compila este arquivo).
 */
class McpPushService : FirebaseMessagingService() {

    override fun onMessageReceived(message: RemoteMessage) {
        if (message.data["t"] != "mcp_wake") return
        if (!AureaMcp.keepWorkingIn(this)) return
        // Já de pé (serviço rodando ou app na frente): a conexão está viva ou reconectando.
        if (McpAgentHub.serviceRunning) { Handler(Looper.getMainLooper()).post { McpAgentHub.current?.start() }; return }
        if (!McpAgentService.start(this)) McpAgentService.notifyWakeFallback(this)
    }

    override fun onNewToken(token: String) {
        if (!AureaMcp.keepWorkingIn(this)) return
        AureaMcp.storePushToken(this, token)
        Handler(Looper.getMainLooper()).post { McpAgentHub.onPushToken(token) }
    }

    companion object {
        private const val TAG = "AureaMcpPush"

        /** Há configuração do Firebase para o FCM neste build. */
        val configured: Boolean
            get() = BuildConfig.FIREBASE_PROJECT_ID.isNotEmpty() && BuildConfig.FIREBASE_APP_ID.isNotEmpty() &&
                BuildConfig.FIREBASE_API_KEY.isNotEmpty() && senderId().isNotEmpty()

        private fun senderId(): String = BuildConfig.FIREBASE_SENDER_ID.ifEmpty {
            BuildConfig.FIREBASE_APP_ID.split(':').getOrNull(1)?.takeIf { it.all(Char::isDigit) }.orEmpty()
        }

        /** O app padrão do Firebase (o FCM só usa ele). Seguro chamar mais de uma vez. */
        fun initDefault(context: Context) {
            if (!configured) return
            runCatching {
                if (FirebaseApp.getApps(context).none { it.name == FirebaseApp.DEFAULT_APP_NAME }) {
                    FirebaseApp.initializeApp(context.applicationContext, FirebaseOptions.Builder()
                        .setProjectId(BuildConfig.FIREBASE_PROJECT_ID)
                        .setApplicationId(BuildConfig.FIREBASE_APP_ID)
                        .setApiKey(BuildConfig.FIREBASE_API_KEY)
                        .setGcmSenderId(senderId())
                        .build())
                }
            }.onFailure { Log.w(TAG, "firebase default app unavailable", it) }
        }

        /** Pede o token (a pessoa ligou o modo fora do app). null = sem FCM aqui. */
        fun requestToken(context: Context, onToken: (String?) -> Unit) {
            if (!configured) { onToken(null); return }
            runCatching {
                initDefault(context)
                val fm = FirebaseMessaging.getInstance()
                fm.isAutoInitEnabled = true
                fm.token.addOnCompleteListener { t -> onToken(if (t.isSuccessful) t.result else null) }
            }.onFailure { Log.w(TAG, "fcm token unavailable", it); onToken(null) }
        }

        /** Desligado: apaga o token no aparelho e não gera outro sozinho. */
        fun deleteToken() {
            if (!configured) return
            runCatching {
                val fm = FirebaseMessaging.getInstance()
                fm.isAutoInitEnabled = false
                fm.deleteToken()
            }.onFailure { Log.w(TAG, "fcm delete failed", it) }
        }
    }
}
