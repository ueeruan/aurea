package com.aurea.aurea

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.snapshotFlow
import androidx.lifecycle.lifecycleScope
import com.aurea.aurea.ads.AureaAds
import com.aurea.aurea.ads.AureaAdsManager
import com.aurea.aurea.conta.ContaEstado
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.diagnostics.CrashReporter
import com.aurea.aurea.state.Screen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.AureaApp
import com.aurea.aurea.ui.i18n.AppLanguage

/**
 * A única Activity. O [EditorStore] é um ViewModel de verdade: sobrevive a
 * mudanças de configuração e o motor é desligado em `onCleared` do ciclo do
 * ViewModel, não no `onDestroy` de uma rotação.
 */
class MainActivity : ComponentActivity() {

    private val store: EditorStore by viewModels()

    /** A conta obrigatória: sem ela a UI inteira é a tela de cadastro/entrada. */
    private val conta: ContaViewModel by viewModels()

    /**
     * O idioma escolhido entra AQUI — antes de qualquer recurso ser lido.
     *
     * É o gancho mais cedo que existe: a partir daqui `stringResource`,
     * `getString` e a direção de layout do app inteiro resolvem no idioma
     * certo. Trocar o idioma nos Ajustes recria a Activity, e a nova passa por
     * aqui de novo.
     */
    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(AppLanguage.wrap(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Antes de tudo: a pilha de um crash Java precisa ser gravada mesmo que
        // ele aconteça no primeiro quadro.
        CrashReporter.instalar(this)
        super.onCreate(savedInstanceState)
        // Barras do sistema escuras (o app antigo tinha o véu #0B0F13 sobre o fundo).
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(Color.BLACK),
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        com.aurea.aurea.home.HomeViewModel.loadTheme(this)
        setContent { AureaApp(store, conta) }

        if (savedInstanceState == null) {
            // Número de cadastrados + revalidação da sessão (com rede; offline, segue dentro).
            conta.aoAbrir()
            // Crash: coleta o que sobrou da sessão anterior; envia quando houver
            // conta (o cadastro avisa do envio antes). Longe da subida do motor.
            lifecycleScope.launch {
                kotlinx.coroutines.delay(3000)
                withContext(Dispatchers.IO) { CrashReporter.coletar(applicationContext) }
                snapshotFlow { conta.estado }.collect { estado ->
                    if (estado is ContaEstado.Dentro) {
                        withContext(Dispatchers.IO) { CrashReporter.enviar(applicationContext, conta.sessao()) }
                    }
                }
            }
        }

        // Debug-only, locally supplied media. No test intent is accepted by release builds.
        if (BuildConfig.DEBUG && intent.hasExtra("aureaPlaybackTest")) {
            lifecycleScope.launch {
                snapshotFlow { store.engineReady }.first { it }
                val name = intent.getStringExtra("aureaPlaybackTest") ?: return@launch
                val root = java.io.File(filesDir, "playback").canonicalFile
                val media = java.io.File(root, name).canonicalFile
                if (media.parentFile != root || !media.isFile) return@launch
                val fps = intent.getIntExtra("testFps", 30)
                store.newProject(1920, 1080, fps.toFloat(), "Playback benchmark")
                snapshotFlow { store.screen }.first { it == Screen.Editor }
                val engine = store.engineForStress
                val id = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    engine.importVideo(media.absolutePath, name)
                }
                if (id < 0) return@launch
                val report = java.io.File(root, "$name-report.jsonl")
                report.writeText("")
                var heartbeatMaxMs = 0L
                val heartbeat = launch {
                    var previous = android.os.SystemClock.elapsedRealtime()
                    while (true) {
                        kotlinx.coroutines.delay(50)
                        val now = android.os.SystemClock.elapsedRealtime()
                        heartbeatMaxMs = maxOf(heartbeatMaxMs, now - previous)
                        previous = now
                    }
                }
                suspend fun record(phase: String, samples: Int) {
                    repeat(samples) {
                        kotlinx.coroutines.delay(1000)
                        val row = org.json.JSONObject()
                            .put("phase", phase).put("elapsedMs", android.os.SystemClock.elapsedRealtime())
                            .put("heartbeatMaxMs", heartbeatMaxMs).put("pssKB", android.os.Debug.getPss())
                            .put("playback", engine.playbackReport())
                        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { report.appendText(row.toString()+"\n") }
                    }
                }
                kotlinx.coroutines.delay(1500)
                store.toggleRawPlayback()
                store.play()
                record("raw", 12)
                store.seek(3 * fps)
                record("raw-seek", 3)
                store.pause()
                record("raw-paused", 1)
                store.play()
                record("raw-resumed", 3)
                store.pause()
                store.toggleRawPlayback()
                store.setPreviewScale(false, 1, 1)
                store.seek(0)
                store.play()
                record("compositor", 12)
                store.pause()
                heartbeat.cancel()
                report.appendText(org.json.JSONObject().put("complete", true).toString()+"\n")
                android.util.Log.i("AureaPlaybackTest", "COMPLETE $name")
            }
        }

        // Anúncios: consentimento + SDK fora do caminho do app (nada aqui espera).
        AureaAds.initialize(this)
        if (savedInstanceState == null) {
            // Abertura fria: o App Open só tem chance no instante em que o Aurea
            // termina de carregar. Se o anúncio não estiver pronto nessa hora, a
            // Home segue e ele não aparece depois.
            lifecycleScope.launch {
                snapshotFlow { store.engineReady }.first { it }
                AureaAdsManager.onAppLoaded(this@MainActivity, trabalhando())
            }
        }
    }

    /** No editor, exportando ou na tela de conta: nenhum App Open. */
    private fun trabalhando(): Boolean = store.screen == Screen.Editor || store.exporter.busy || !conta.logado

    override fun onStart() {
        super.onStart()
        CrashReporter.primeiroPlano(this, true)
        conta.revalidarSeVencido()
        store.onEnterForeground()
        AureaAdsManager.onForeground(this, trabalhando())
    }

    override fun onResume() {
        super.onResume()
        AureaAdsManager.attach(this)
    }

    override fun onPause() {
        AureaAdsManager.detach(this)
        super.onPause()
    }

    override fun onStop() {
        store.onEnterBackground()
        AureaAdsManager.onBackground()
        CrashReporter.primeiroPlano(this, false)
        super.onStop()
    }

    /**
     * Pressão de memória do sistema (Fase 8B §13): o nível vai para o motor,
     * que solta cache na ordem do spec. Nunca o projeto.
     */
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        store.onTrimMemory(level)
    }

    @Deprecated("Android < 14 ainda chama; equivale a TRIM_MEMORY_COMPLETE")
    override fun onLowMemory() {
        @Suppress("DEPRECATION")
        super.onLowMemory()
        store.onTrimMemory(EditorStore.TRIM_COMPLETE)
    }
}
