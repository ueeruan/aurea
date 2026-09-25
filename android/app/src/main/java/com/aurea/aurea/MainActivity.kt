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
import com.aurea.aurea.state.Screen
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
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
        super.onCreate(savedInstanceState)
        // Barras do sistema escuras (o app antigo tinha o véu #0B0F13 sobre o fundo).
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(Color.BLACK),
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        com.aurea.aurea.home.HomeViewModel.loadTheme(this)
        setContent { AureaApp(store) }

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

    /** No editor ou exportando: nenhum App Open. */
    private fun trabalhando(): Boolean = store.screen == Screen.Editor || store.exporter.busy

    override fun onStart() {
        super.onStart()
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
