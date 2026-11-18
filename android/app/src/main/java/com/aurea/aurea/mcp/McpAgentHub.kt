package com.aurea.aurea.mcp

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * AureaMCP fora do app (18/11/2026): o dono pediu que o agente continue
 * trabalhando com a pessoa fora do Aurea. Este objeto é o dono da conexão no
 * nível do PROCESSO (não da tela):
 *
 * - [scope]: onde o [AureaMcp] roda (antes era o viewModelScope do editor, que
 *   não sobrevivia ao app sair da frente).
 * - [platform]: os ganchos do Android (serviço em primeiro plano, wake lock,
 *   notificações de aprovação, token do FCM), registrados pelo AureaApplication.
 *   No PC fica null: a janela do Aurea já é o processo inteiro.
 * - [foreground] / [serviceRunning]: o que o relay recebe como `background` e
 *   `serviced` (o Worker só manda chamadas a quem está na frente ou servido).
 *
 * Sem tipos do Android: compila no Aurea PC (desktop/) como o resto do pacote.
 * Par do iOS: AureaMcpBackground em engine/platform/ios/app/AureaMcp.swift.
 */
object McpAgentHub {
    /** O escopo do processo (thread principal). Nunca é cancelado. */
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    /** O que só o Android sabe fazer. Toda função é chamada na thread principal. */
    interface Platform {
        /** Liga o serviço do agente (só com o app na frente: regra do Android 12+). */
        fun startService()
        fun stopService()
        /** Uma chamada começou/terminou: segura a CPU (wake lock parcial) só enquanto há chamada. */
        fun callStarted()
        fun callEnded()
        /** Notificação com Aprovar/Recusar. false = não dá para notificar (sem permissão). */
        fun showConfirm(id: Long, tool: String, agent: String, detail: String): Boolean
        fun clearConfirm(id: Long)
        /** Token do push (FCM). null = indisponível (sem Firebase configurado / sem Play Services). */
        fun requestPushToken(onToken: (String?) -> Unit)
        fun deletePushToken()
        /** A permissão de notificação a pedir ao ligar o modo fora do app (Android 13+), ou null. */
        fun notificationPermission(): String? = null
        /** O app pode mostrar notificações (aprovações fora da tela dependem disso). */
        fun notificationsAllowed(): Boolean = true
        /** O serviço saiu: sem tela, o editor pode ser solto da memória. */
        fun serviceStopped() {}
    }

    @Volatile var platform: Platform? = null

    /** A instância ligada (a do editor). O serviço e as ações da notificação falam com ela. */
    @Volatile var current: AureaMcp? = null
        private set

    /** A Activity está na frente (onStart..onStop). */
    var foreground by mutableStateOf(false)
        private set

    /** O serviço em primeiro plano do agente está de pé (o processo sobrevive fora da tela). */
    var serviceRunning by mutableStateOf(false)
        private set

    /** O serviço segura o processo: o editor não suspende o motor ao sair (só depois de ocioso). */
    val keepEngine: Boolean get() = serviceRunning

    internal fun attach(mcp: AureaMcp) {
        current = mcp
        ensureSnapshotNotifications()
    }

    private val snapshotPump = java.util.concurrent.atomic.AtomicBoolean(false)
    private val snapshotPending = java.util.concurrent.atomic.AtomicBoolean(false)

    /**
     * As ferramentas de host esperam o estado do editor com `snapshotFlow`, que só
     * anda quando alguém publica as escritas do snapshot global — o Compose faz isso
     * quando há tela. Aberto por um push SEM tela, ninguém fazia: abrir/criar
     * projeto esperava até o prazo. Este é o mesmo laço do GlobalSnapshotManager do
     * Compose (publicar de novo com ele também ativo não muda nada).
     */
    private fun ensureSnapshotNotifications() {
        if (!snapshotPump.compareAndSet(false, true)) return
        androidx.compose.runtime.snapshots.Snapshot.registerGlobalWriteObserver {
            if (snapshotPending.compareAndSet(false, true)) scope.launch {
                snapshotPending.set(false)
                androidx.compose.runtime.snapshots.Snapshot.sendApplyNotifications()
            }
        }
    }
    internal fun detach(mcp: AureaMcp) { if (current === mcp) current = null }

    /** MainActivity.onStart / onStop. */
    fun markForeground(value: Boolean) {
        if (foreground == value) return
        foreground = value
        current?.onLifecycleChanged()
    }

    /** McpAgentService: aceito em primeiro plano / encerrado. */
    fun markServiceRunning(value: Boolean) {
        if (serviceRunning == value) return
        serviceRunning = value
        current?.onLifecycleChanged()
        if (!value) platform?.serviceStopped()
    }

    /** Ação da notificação (Aprovar/Recusar) ou o diálogo do app. */
    fun answerConfirm(id: Long, approved: Boolean) { current?.answerConfirm(approved, id) }

    /** Ação "Pausar" da notificação do serviço. */
    fun pauseFromNotification() { current?.pause() }

    /** Ação "Parar" da notificação do serviço: desliga o modo fora do app até a pessoa voltar. */
    fun stopFromNotification() {
        val mcp = current
        if (mcp == null) { platform?.stopService(); return }
        mcp.stopBackgroundSession()
    }

    /** O FCM trocou o token (McpPushService.onNewToken). */
    fun onPushToken(token: String?) { current?.onPushToken(token) }
}
