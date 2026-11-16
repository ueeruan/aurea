package com.aurea.aurea

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewmodel.CreationExtras
import com.aurea.aurea.conta.ContaEstado
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.diagnostics.CrashReporter
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.AureaApp
import com.aurea.aurea.ui.i18n.AppLanguage
import com.aurea.aurea.ui.i18n.KeepAppLanguage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.reflect.KClass

/**
 * A "Activity" do Aurea PC: a mesma abertura da MainActivity do Android
 * (idioma → privacidade/termos → criador → app), sem anúncios e sem as rotas
 * de depuração do emulador. O [EditorStore] e a conta vivem enquanto a janela.
 */
class MainActivity : ComponentActivity(), ViewModelStoreOwner, androidx.lifecycle.HasDefaultViewModelProviderFactory {

    override val viewModelStore = ViewModelStore()

    /** `viewModel()` do Compose cria os ViewModels do app com o Application, como no Android. */
    override val defaultViewModelProviderFactory: ViewModelProvider.Factory = object : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: KClass<T>, extras: CreationExtras): T {
            val cls = modelClass.java
            val ctor = cls.constructors.firstOrNull { it.parameterTypes.size == 1 && it.parameterTypes[0].isAssignableFrom(android.app.Application::class.java) }
            @Suppress("UNCHECKED_CAST")
            return (ctor?.newInstance(com.aurea.pc.PcApp.application) ?: cls.getDeclaredConstructor().newInstance()) as T
        }
    }

    private val provider by lazy { ViewModelProvider.create(viewModelStore, defaultViewModelProviderFactory) }
    val store: EditorStore by lazy { provider[EditorStore::class] }
    val conta: ContaViewModel by lazy { provider[ContaViewModel::class] }

    override val applicationContext: Context get() = com.aurea.pc.PcApp.context

    /** O equivalente do onCreate: serviços opcionais e o motor. */
    fun create() {
        // O EditorStore copia as fontes embarcadas para o cache com renameTo, que
        // no Windows não substitui um arquivo existente: as cópias da sessão
        // anterior saem antes (no Android o rename já substitui).
        for (f in listOf("Roboto-Regular.ttf", "NotoNaskhArabic-Regular.ttf", "NotoSansJP-Regular.otf", "NotoSansKR-Hangul.otf"))
            java.io.File(java.io.File(cacheDir, "motor"), f).delete()
        CrashReporter.instalar(this)
        opcional("tema") { com.aurea.aurea.home.HomeViewModel.loadTheme(this) }
        opcional("config") { com.aurea.aurea.config.AppConfigStore.init(this) }
        opcional("hyper") { com.aurea.aurea.hyper.HyperEntitlements.init(this) }
        // AureaHyper liberado no PC (pedido do dono, 11/11/2026).
        com.aurea.aurea.hyper.HyperEntitlements.platformUnlocked = true
        opcional("config") { com.aurea.aurea.config.AppConfigStore.refresh(force = true) }
        com.aurea.pc.PcEngine.bind(store.engineForStress)
        opcional("conta") { conta.aoAbrir() }
        com.aurea.pc.PcApp.scope.launch {
            kotlinx.coroutines.delay(3000)
            withContext(Dispatchers.IO) { runCatching { CrashReporter.coletar(applicationContext) } }
            snapshotFlow { conta.estado }.collect { estado ->
                if (estado is ContaEstado.Dentro) {
                    withContext(Dispatchers.IO) { runCatching { CrashReporter.enviar(applicationContext, conta.sessao()) } }
                }
            }
        }
        store.syncDefaultNames()
        // "Abrir com" um projeto (argumento da linha de comando).
        com.aurea.pc.PcApp.launchIntent.data?.let { uri -> importProject(uri) }
    }

    fun importProject(uri: Uri) {
        com.aurea.pc.PcApp.scope.launch {
            snapshotFlow { store.engineReady }.first { it }
            store.importProjectFile(uri)
        }
    }

    fun onStart() {
        opcional("conta") { conta.revalidarSeVencido() }
        opcional("config") { com.aurea.aurea.config.AppConfigStore.refresh() }
        opcional("hyper") { com.aurea.aurea.hyper.HyperEntitlements.recheck() }
        store.onEnterForeground()
    }

    /** Minimizar ou perder o foco não suspende nada no PC; só grava. */
    fun onLeaving() = store.onLeaving()

    fun onStop() {
        store.onLeaving()
        store.onEnterBackground()
    }

    fun destroy() {
        com.aurea.pc.PcEngine.handle = 0L
        viewModelStore.clear()
    }

    @Composable
    fun Content() {
        // Por enquanto (pedido de 11/11/2026) o PC abre sem a conta obrigatória:
        // `conta = null` é o mesmo caminho do build de depuração no emulador.
        // A conta continua opcional nos Ajustes/Comunidade.
        KeepAppLanguage { LanguageWelcome { CreatorWelcome { AureaApp(store, if (CONTA_OBRIGATORIA) conta else null) } } }
    }

    private inline fun opcional(nome: String, bloco: () -> Unit) {
        try { bloco() } catch (e: Exception) { android.util.Log.w("AureaStart", "serviço opcional falhou ($nome)", e) }
    }
}

@Composable
private fun MainActivity.LanguageWelcome(content: @Composable () -> Unit) {
    if (!AppLanguage.needsChoice(this)) { content(); return }
    com.aurea.aurea.ui.theme.AureaTheme {
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.verticalScroll(rememberScrollState()).padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("AUREA", style = MaterialTheme.typography.headlineMedium)
                Text("Choose your language · Escolha seu idioma", style = MaterialTheme.typography.titleLarge)
                AppLanguage.entries.forEach { language ->
                    OutlinedButton(onClick = {
                        AppLanguage.select(this@LanguageWelcome, language)
                        recreate()
                    }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                        Text(if (language == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else language.display)
                    }
                }
            }
        }
    }
}

/** Liga de novo a tela de conta obrigatória do Aurea PC (desligada por enquanto). */
private const val CONTA_OBRIGATORIA = false

/** Versão do texto de privacidade/termos aceito (o mesmo número do Android). */
private const val CONSENT_VERSION = 2181

@Composable
private fun MainActivity.CreatorWelcome(content: @Composable () -> Unit) {
    val prefs = remember { getSharedPreferences("creator_welcome", Context.MODE_PRIVATE) }
    var consent by remember { mutableIntStateOf(prefs.getInt("consent_version", 0)) }
    var opened by remember { mutableIntStateOf(prefs.getInt("opened", 0)) }
    var error by remember { mutableStateOf(false) }
    if (consent < CONSENT_VERSION) {
        MaterialTheme(colorScheme = darkColorScheme()) {
            Surface(Modifier.fillMaxSize()) {
                Column(Modifier.padding(24.dp)) {
                    Column(Modifier.weight(1f).verticalScroll(rememberScrollState())) {
                        com.aurea.aurea.home.PrivacyMascot(Modifier.align(Alignment.CenterHorizontally))
                        Spacer(Modifier.height(16.dp))
                        Text(stringResource(R.string.privacy_title), style = MaterialTheme.typography.headlineSmall)
                        Spacer(Modifier.height(8.dp))
                        Text(stringResource(R.string.privacy_body), style = MaterialTheme.typography.bodyLarge)
                        Spacer(Modifier.height(24.dp))
                        Text(stringResource(R.string.consent_title), style = MaterialTheme.typography.titleLarge)
                        Spacer(Modifier.height(8.dp))
                        Text(stringResource(R.string.consent_body), style = MaterialTheme.typography.bodyLarge)
                        Spacer(Modifier.height(12.dp))
                        Text(stringResource(R.string.terms_liability), style = MaterialTheme.typography.bodyLarge)
                    }
                    Spacer(Modifier.height(16.dp))
                    Button(onClick = {
                        prefs.edit().putInt("consent_version", CONSENT_VERSION).apply()
                        consent = CONSENT_VERSION
                    }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text(stringResource(R.string.consent_accept)) }
                }
            }
        }
        return
    }
    // Igual ao APK de sideload: os perfis do criador antes da primeira abertura.
    if (opened == 3) { content(); return }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.Center) {
                Text(stringResource(R.string.social_title), style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(16.dp))
                Text(stringResource(R.string.social_body))
                Spacer(Modifier.height(24.dp))
                listOf("TikTok @ruanzitwo" to "https://www.tiktok.com/@ruanzitwo", "Instagram @ofruanzitwo" to "https://www.instagram.com/ofruanzitwo/").forEachIndexed { i, profile ->
                    val bit = 1 shl i
                    Button(onClick = {
                        try {
                            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(profile.second)))
                            opened = opened or bit
                            prefs.edit().putInt("opened", opened).apply()
                            error = false
                        } catch (_: android.content.ActivityNotFoundException) { error = true }
                    }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp), enabled = opened and bit == 0) {
                        Text(profile.first)
                    }
                    Spacer(Modifier.height(12.dp))
                }
                if (error) Text(stringResource(R.string.social_open_error), color = MaterialTheme.colorScheme.error)
            }
        }
    }
}
