package com.aurea.aurea

import android.app.Application
import android.content.Context
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import com.aurea.aurea.diagnostics.CrashReporter
import com.aurea.aurea.mcp.McpAgentHub
import com.aurea.aurea.state.EditorStore

/**
 * 18/11/2026 (AureaMCP fora do app): o [EditorStore] mora no nível do PROCESSO,
 * não da Activity. Assim o agente de IA continua editando com a pessoa fora do
 * app (o McpAgentService segura o processo) e, aberto por um push com o app
 * fechado, o editor sobe sem tela. A Activity pega o mesmo editor ao voltar.
 * Sem o serviço, fechar a Activity solta o editor como antes (onCleared →
 * salva e desliga o motor) — ver [releaseEditorIfIdle].
 */
class AureaApplication : Application(), ViewModelStoreOwner {
    override val viewModelStore = ViewModelStore()

    /** Activities vivas (criadas e não destruídas). */
    private var activities = 0

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)
        // Install before providers and activity startup, in every app process.
        CrashReporter.instalar(this)
    }

    override fun onCreate() {
        super.onCreate()
        // Classe de memória LOW (até ~4 GB): caches de bitmap e limiar de pressão.
        com.aurea.aurea.engine.DeviceMemoryClass.init(this)
        // AureaMCP fora do app: serviço, notificações e push (o PC não registra nada).
        McpAgentHub.platform = com.aurea.aurea.mcp.AndroidMcpPlatform(this)
        // O FCM precisa do app PADRÃO do Firebase (o login usa um app com nome).
        com.aurea.aurea.mcp.McpPushService.initDefault(this)
    }

    /** O editor do processo (o mesmo para a Activity e para o serviço do agente). Thread principal. */
    fun editorStore(): EditorStore =
        ViewModelProvider(this, ViewModelProvider.AndroidViewModelFactory.getInstance(this))[EditorStore::class.java]

    fun activityCreated() { activities++ }

    /** `changingConfig`: rotação/idioma recriam a Activity e o editor fica. */
    fun activityDestroyed(changingConfig: Boolean) {
        activities = (activities - 1).coerceAtLeast(0)
        if (!changingConfig) releaseEditorIfIdle()
    }

    /** Sem tela e sem o serviço do agente: o editor sai (onCleared grava e desliga o motor). */
    fun releaseEditorIfIdle() {
        if (activities == 0 && !McpAgentHub.serviceRunning) viewModelStore.clear()
    }
}
