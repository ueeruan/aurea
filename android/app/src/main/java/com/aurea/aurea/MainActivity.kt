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
import androidx.compose.runtime.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.res.stringResource
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.BackHandler
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
import com.aurea.aurea.ui.i18n.KeepAppLanguage

/**
 * A única Activity. O [EditorStore] é um ViewModel de verdade: sobrevive a
 * mudanças de configuração e o motor é desligado em `onCleared` do ciclo do
 * ViewModel, não no `onDestroy` de uma rotação.
 */
class MainActivity : ComponentActivity() {

    private val store: EditorStore by viewModels()

    /** A conta opcional, compartilhada com a Home. */
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

    /**
     * Rotação, uiMode, densidade… (`configChanges` do manifesto): a Activity não
     * é recriada e o sistema entrega a configuração DELE, sem o idioma do app.
     * Reaplica o idioma escolhido nos recursos da Activity antes de repassar;
     * o Compose faz o mesmo na raiz (`KeepAppLanguage`).
     */
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        val fixed = AppLanguage.override(this, newConfig)
        if (fixed != null) {
            // O sistema também volta o Locale padrão do processo para o dele:
            // sem repor, "%.1f".format e o tracking do árabe (AureaType)
            // mudariam no meio da sessão.
            java.util.Locale.setDefault(fixed.locales[0])
            @Suppress("DEPRECATION")
            resources.updateConfiguration(fixed, resources.displayMetrics)
        }
        super.onConfigurationChanged(fixed ?: newConfig)
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
        // Bug #2 (abre e fecha): serviços opcionais (tema salvo, config remota,
        // portão do Hyper, conta) nunca derrubam a abertura — sem eles o app
        // segue com os padrões.
        opcional("tema") { com.aurea.aurea.home.HomeViewModel.loadTheme(this) }
        // 0.3.2: config remota assinada (atualização obrigatória, política do AureaHyper, aviso).
        opcional("config") { com.aurea.aurea.config.AppConfigStore.init(this) }
        opcional("hyper") { com.aurea.aurea.hyper.HyperEntitlements.init(this) }
        if (savedInstanceState == null) opcional("config") { com.aurea.aurea.config.AppConfigStore.refresh(force = true) }
        // Debug-only: mockup da nova Cena 3D (editor/scene3d), sem motor e sem projeto.
        if (BuildConfig.DEBUG && intent.getStringExtra("aureaMockup") == "scene3d") {
            setContent { com.aurea.aurea.ui.theme.AureaTheme { com.aurea.aurea.editor.scene3d.Scene3DStudioMockup(onClose = ::finish) } }
            return
        }
        // Debug (0.4.6): a cena demo — e QUALQUER teste no emulador — abre direto,
        // sem as telas de boas-vindas, do criador e da conta. Só no build de
        // debug rodando em emulador; release e aparelho de verdade não mudam.
        if (BuildConfig.DEBUG && (intent.getStringExtra("aureaDemo") != null || runningOnEmulator()))
            setContent { KeepAppLanguage { AureaApp(store, null) } }   // sem a tela de conta (só debug)
        else
            setContent { KeepAppLanguage { LanguageWelcome { CreatorWelcome { AureaApp(store, conta) } } } }

        if (savedInstanceState == null) {
            // Número de cadastrados + revalidação da sessão (com rede; offline, segue dentro).
            opcional("conta") { conta.aoAbrir() }
            // Crash: coleta o que sobrou da sessão anterior; envia quando houver
            // conta (o cadastro avisa do envio antes). Longe da subida do motor.
            lifecycleScope.launch {
                kotlinx.coroutines.delay(3000)
                // Coleta/envio nunca derrubam a abertura: um relatório ou
                // tombstone estranho que lançasse aqui viraria um crash a cada
                // abertura (o histórico do Android sobrevive a limpar o cache).
                withContext(Dispatchers.IO) {
                    runCatching { CrashReporter.coletar(applicationContext) }
                        .onFailure { android.util.Log.w("AureaCrash", "coleta de crash falhou", it) }
                }
                snapshotFlow { conta.estado }.collect { estado ->
                    if (estado is ContaEstado.Dentro) {
                        withContext(Dispatchers.IO) { runCatching { CrashReporter.enviar(applicationContext, conta.sessao()) } }
                    }
                }
            }
        }

        // Debug-only (0.4.6): cena 3D de demonstração no motor real — formas
        // encostadas (oclusão ambiente), sol com sombra, luz pontual e chão.
        store.syncDefaultNames()   // trocar o idioma recria a Activity; o motor continua o mesmo
        if (BuildConfig.DEBUG && intent.getStringExtra("aureaDemo") == "scene3d") {
            com.aurea.aurea.ui.i18n.AppLanguage.select(this, com.aurea.aurea.ui.i18n.AppLanguage.PT_BR)
            lifecycleScope.launch {
                snapshotFlow { store.engineReady }.first { it }
                store.newProject(1920, 1080, 30f, "Cena 3D demo")
                snapshotFlow { store.screen }.first { it == Screen.Editor }
                kotlinx.coroutines.delay(300)
                fun shape(kind: Int, x: Float, y: Float, z: Float, s: Float, rx: Float, ry: Float) {
                    val id = store.addShape3D(kind)
                    if (id < 0) return
                    store.setTransform(com.aurea.aurea.engine.TrackProperty.POSITION_X, x, id)
                    store.setTransform(com.aurea.aurea.engine.TrackProperty.POSITION_Y, y, id)
                    store.setTransform(com.aurea.aurea.engine.TrackProperty.POSITION_Z, z, id)
                    for (p in listOf(com.aurea.aurea.engine.TrackProperty.SCALE_X, com.aurea.aurea.engine.TrackProperty.SCALE_Y, com.aurea.aurea.engine.TrackProperty.SCALE_Z)) store.setTransform(p, s, id)
                    store.setTransform(com.aurea.aurea.engine.TrackProperty.ROTATION_X, rx, id)
                    store.setTransform(com.aurea.aurea.engine.TrackProperty.ROTATION_Y, ry, id)
                }
                // Shape3DKind: 0 cubo, 1 esfera, 2 cilindro, 5 toro.
                // Espalhadas pela composição, encostando umas nas outras (AO nos contatos).
                shape(0, 960f, 560f, 0f, 1.0f, -15f, 35f)
                shape(1, 560f, 640f, -40f, 0.75f, 0f, 0f)
                shape(2, 1380f, 520f, 80f, 0.8f, 0f, 20f)
                shape(5, 1190f, 760f, -160f, 0.7f, 70f, 15f)
                store.addLight(0)   // sol (direcional, com sombra)
                store.addLight(1)   // luz pontual
                store.addReflectiveFloor()
                store.enterSceneEditor()
                store.updateSceneView(-28f, 16f, 1.7f)
            }
        }

        // Debug-only (0.4.6): a cena da floresta com os modelos que a pessoa pôs em
        // <externo>/files/demo (tank_t-55a.glb, pine_forest.glb, old_bicycle.glb),
        // com sol, oclusão ambiente e neblina com raios de luz.
        if (BuildConfig.DEBUG && intent.getStringExtra("aureaDemo") == "floresta") {
            com.aurea.aurea.ui.i18n.AppLanguage.select(this, com.aurea.aurea.ui.i18n.AppLanguage.PT_BR)
            lifecycleScope.launch {
                snapshotFlow { store.engineReady }.first { it }
                store.newProject(1920, 1080, 30f, "Floresta")
                snapshotFlow { store.screen }.first { it == Screen.Editor }
                kotlinx.coroutines.delay(300)
                val dir = getExternalFilesDir("demo") ?: return@launch
                val engine = store.engineForStress
                val memory = com.aurea.aurea.engine.ModelPlan.memoryNow(this@MainActivity)
                suspend fun model(file: String, name: String, x: Float, y: Float, z: Float, s: Float, ry: Float,
                                  hideMaterials: List<Int> = emptyList()) {
                    val path = java.io.File(dir, file)
                    if (!path.isFile) { android.util.Log.w("AureaDemo", "falta ${path.absolutePath}"); return }
                    val detail = arrayOfNulls<String>(1)
                    val id = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                        engine.importModel(path.absolutePath, name, detail, com.aurea.aurea.engine.MODEL_QUALITY_ORIGINAL, memory)
                    }
                    android.util.Log.i("AureaDemo", "$file -> $id ${detail[0] ?: ""}")
                    if (id < 0) return
                    store.select(id, openOptions = false)
                    val t = com.aurea.aurea.engine.TrackProperty
                    store.setTransform(t.POSITION_X, x, id)
                    store.setTransform(t.POSITION_Y, y, id)
                    store.setTransform(t.POSITION_Z, z, id)
                    for (p in listOf(t.SCALE_X, t.SCALE_Y, t.SCALE_Z)) store.setTransform(p, s, id)
                    store.setTransform(t.ROTATION_Y, ry, id)
                    // Placas falsas do arquivo (sombra/fumaça pintadas): opacidade 0.
                    for (m in hideMaterials) engine.setMaterialParam(id, m, 3, 0f)
                }
                // Escala do mundo: 1 m = 33 px (com 100 px a câmera ficava dentro da
                // floresta e as texturas do chão e dos pinheiros esticavam em riscos);
                // chão em y ≈ 607. Antes: 1 m = 100 px; chão em y ≈ 740 ("para cima" = −Y),
                // 2 m abaixo do alvo da órbita (o centro da composição). O motor
                // normaliza cada modelo (maior medida = 594 px, pivô no centro da
                // caixa); as escalas vêm das caixas dos arquivos:
                // - pine_forest: pinheiros (10 un.) + disco de chão + céu (esfera de
                //   raio 70,6, centro no chão) → 28,5 = pinheiros de ~12 m;
                // - tanque: 14,2 × 3,1 m com a placa de sombra → 2,4; pivô 1,5 m acima da base;
                // - bicicleta: 16,7 un. para 1,7 m → 0,29; pivô 0,46 m acima da base.
                // Hoje tudo ÷ 3 (9,5 / 0,8 / 0,097; distâncias ao centro também).
                model("pine_forest.glb", "Floresta", 960f, 617f, 0f, 9.5f, 0f)
                model("tank_t-55a.glb", "Tanque", 960f, 556f, 100f, 0.8f, -32f, hideMaterials = listOf(2, 6, 10))
                model("old_bicycle.glb", "Bicicleta", 857f, 591f, -50f, 0.097f, 55f)
                store.addLight(0)                 // sol (direcional com sombra: os feixes)
                store.setSceneSetting(0, 3f)      // luz do dia do estúdio "Céu com sol" (o fundo é o panorama da floresta)
                store.setSceneSetting(9, 1f)      // neblina com raios de luz
                store.setSceneSetting(10, 0.00036f)
                store.setSceneSetting(11, 1.6f)
                if (intent.getStringExtra("ssr") == "0") store.setSceneSetting(12, 0f)
                kotlinx.coroutines.delay(600)   // o import termina de abrir os painéis antes
                store.enterSceneEditor()
                store.updateSceneView(-14f, 12f, 1.05f)
            }
        }

        // Debug-only: exporta um vídeo local (filesDir/playback) e mede o tempo,
        // quadro a quadro, para achar exports lentos. Release não aceita o intent.
        if (BuildConfig.DEBUG && intent.hasExtra("aureaExportTest")) {
            lifecycleScope.launch {
                snapshotFlow { store.engineReady }.first { it }
                val name = intent.getStringExtra("aureaExportTest") ?: return@launch
                val root = java.io.File(filesDir, "playback").canonicalFile
                val media = java.io.File(root, name).canonicalFile
                if (media.parentFile != root || !media.isFile) return@launch
                val side = intent.getIntExtra("testSide", 720)
                store.newProject(1920, 1080, 30f, "Export benchmark")
                snapshotFlow { store.screen }.first { it == Screen.Editor }
                val engine = store.engineForStress
                val id = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    engine.importVideo(media.absolutePath, name)
                }
                if (id < 0) { android.util.Log.e("AureaExportTest", "import failed"); return@launch }
                // testScene=transition: o caso dos relatórios — 2 clipes no corte
                // com transição e shake nos dois.
                if (intent.getStringExtra("testScene") == "transition") {
                    val second = intent.getStringExtra("testSecond") ?: name
                    val media2 = java.io.File(root, second).canonicalFile
                    if (media2.parentFile != root || !media2.isFile) return@launch
                    val id2 = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                        engine.importVideo(media2.absolutePath, second)
                    }
                    kotlinx.coroutines.delay(800)
                    engine.moveClipToMainTrack(id2)
                    val transition = engine.setCutTransition(id, id2, intent.getIntExtra("testTransition", 1), 15)
                    store.addEffect(com.aurea.aurea.editor.panels.effectTypeId("aurea.distort.shake"), listOf(id, id2))
                    android.util.Log.i("AureaExportTest", "scene: clips $id $id2 transition=$transition " +
                        "duration=${engine.exportDuration(true)}")
                }
                kotlinx.coroutines.delay(1500)
                val out = java.io.File(root, "$name-export.mp4"); out.delete()
                val t0 = android.os.SystemClock.elapsedRealtime()
                val code = engine.startExport(out.absolutePath, side, 0.0, 0, 0)
                android.util.Log.i("AureaExportTest", "start code=$code")
                val buffer = java.nio.ByteBuffer.allocateDirect(4096).order(java.nio.ByteOrder.nativeOrder())
                val prog = com.aurea.aurea.engine.ExportProgress()
                while (code == 0) {
                    kotlinx.coroutines.delay(1000)
                    if (!engine.exportProgress(buffer)) continue
                    prog.readFrom(buffer)
                    android.util.Log.i("AureaExportTest", "t=${(android.os.SystemClock.elapsedRealtime() - t0) / 1000.0}s " +
                        "${prog.framesDone}/${prog.framesTotal} fps=${prog.fps} flags=0x${Integer.toHexString(prog.flags)}")
                    if (prog.finished) break
                }
                android.util.Log.i("AureaExportTest", "COMPLETE result=${prog.result} ${(android.os.SystemClock.elapsedRealtime() - t0) / 1000.0}s ${out.length()} bytes ${prog.message}")
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
        // Bugs 0.3.4 (importar projeto): aberto por um arquivo do projeto.
        if (savedInstanceState == null) importProjectFileFrom(intent)
    }

    // Bugs 0.3.4 (importar projeto): com o app já aberto (singleTask), o
    // arquivo chega aqui.
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        importProjectFileFrom(intent)
    }

    /**
     * Bugs 0.3.4 (importar projeto): "Abrir com" / "Compartilhar" um arquivo do
     * projeto (.aureaproj, .aurea, .pjf) entra como projeto NOVO, pelo mesmo
     * caminho de "Importar arquivo do projeto" (o motor diz se é projeto).
     * Antes o filtro do manifesto só conhecia um MIME que ninguém manda e a
     * Activity nem olhava a URI: o arquivo "não importava".
     */
    private fun importProjectFileFrom(intent: Intent?) {
        val uri = projectFileUri(intent) ?: return
        lifecycleScope.launch {
            snapshotFlow { store.engineReady }.first { it }
            store.importProjectFile(uri)
        }
    }

    private fun projectFileUri(intent: Intent?): Uri? {
        if (intent == null) return null
        val uri = when (intent.action) {
            Intent.ACTION_VIEW -> intent.data
            Intent.ACTION_SEND ->
                if (android.os.Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
                else @Suppress("DEPRECATION") intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
            else -> null
        } ?: return null
        return uri.takeIf { it.scheme == android.content.ContentResolver.SCHEME_CONTENT }
    }

    /** No editor, exportando ou na tela de conta: nenhum App Open. */
    private fun trabalhando(): Boolean = store.screen == Screen.Editor || store.exporter.busy || !conta.logado

    override fun onStart() {
        super.onStart()
        CrashReporter.primeiroPlano(this, true)
        opcional("conta") { conta.revalidarSeVencido() }
        // No máximo a cada 30 min (AppConfigStore.MIN_INTERVAL_MS); o prazo do Hyper "free" é recalculado.
        opcional("config") { com.aurea.aurea.config.AppConfigStore.refresh() }
        opcional("hyper") { com.aurea.aurea.hyper.HyperEntitlements.recheck() }
        store.onEnterForeground()
        opcional("ads") { AureaAdsManager.onForeground(this, trabalhando()) }
    }

    /**
     * Bug #2 (abre e fecha): um serviço OPCIONAL (config remota, conta, Hyper,
     * anúncios) que falhe fica no log; o app continua aberto sem ele.
     */
    private inline fun opcional(nome: String, bloco: () -> Unit) {
        try {
            bloco()
        } catch (e: Exception) {
            android.util.Log.w("AureaStart", "serviço opcional falhou ($nome)", e)
        }
    }

    override fun onResume() {
        super.onResume()
        AureaAdsManager.attach(this)
        // O WebView do anúncio pode ter gravado o resumo de estado do processo
        // por cima da nossa etapa enquanto estava na frente.
        com.aurea.aurea.diagnostics.ExitDiagnostics.reassert(this)
    }

    override fun onPause() {
        // Grava já no onPause (fora da main): arrastar o app para fora dos
        // recentes mata o processo sem esperar pelo onStop.
        store.onLeaving()
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

@Composable
private fun MainActivity.LanguageWelcome(content: @Composable () -> Unit) {
    if (!AppLanguage.needsChoice(this)) { content(); return }
    BackHandler { finish() }
    com.aurea.aurea.ui.theme.AureaTheme {
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.safeDrawingPadding().verticalScroll(rememberScrollState()).padding(24.dp),
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

/** Versão do texto de privacidade/termos aceito (o build em que mudou). */
private const val CONSENT_VERSION = 2181

@Composable
private fun MainActivity.CreatorWelcome(content: @Composable () -> Unit) {
    val prefs = remember { getSharedPreferences("creator_welcome", Context.MODE_PRIVATE) }
    var consent by remember { mutableIntStateOf(prefs.getInt("consent_version", 0)) }
    var opened by remember { mutableIntStateOf(prefs.getInt("opened", 0)) }
    var error by remember { mutableStateOf(false) }
    // 0.1.7 BETA 3: privacidade + termos com o bonequinho, mostrados uma vez
    // a todos (quem aceitou a versão 2125 vê o texto novo uma vez).
    if (consent < CONSENT_VERSION) {
        BackHandler { finish() }
        MaterialTheme(colorScheme = darkColorScheme()) {
            Surface(Modifier.fillMaxSize()) {
                Column(Modifier.safeDrawingPadding().padding(24.dp)) {
                    Column(Modifier.weight(1f).verticalScroll(rememberScrollState())) {
                        com.aurea.aurea.home.PrivacyMascot(Modifier.align(androidx.compose.ui.Alignment.CenterHorizontally))
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
    // Google Play: abrir os perfis do criador não pode ser condição para usar o
    // app (e travaria a revisão). O APK de sideload mantém a tela.
    if (opened == 3 || BuildConfig.PLAY_STORE) { content(); return }
    BackHandler { finish() }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.safeDrawingPadding().padding(24.dp), verticalArrangement = Arrangement.Center) {
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

/** Debug: o app roda num emulador do Android (pula as telas de entrada nos testes). */
private fun runningOnEmulator(): Boolean {
    val b = android.os.Build.FINGERPRINT + " " + android.os.Build.MODEL + " " + android.os.Build.HARDWARE + " " + android.os.Build.PRODUCT
    return b.contains("generic", ignoreCase = true) || b.contains("emulator", ignoreCase = true) ||
        b.contains("ranchu", ignoreCase = true) || b.contains("goldfish", ignoreCase = true) ||
        b.contains("sdk_gphone", ignoreCase = true) || b.contains("google_sdk", ignoreCase = true)
}
