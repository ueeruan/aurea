package com.aurea.aurea

import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterShellArgs
import android.util.Log
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.view.TextureRegistry
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread

class MainActivity : FlutterActivity() {

    private val encoder = VideoEncoder()

    /**
     * O PREVIEW VULKAN (V1).
     *
     * O `SurfaceProducer` e quem entrega uma janela nativa com o caminho
     * RECOMENDADO do Flutter: no Android novo ele usa um `ImageReader`,
     * que funciona direto nos backends graficos atuais (incluindo o Vulkan
     * do Impeller). O caminho antigo (`createSurfaceTexture`) nao serve —
     * ele passa por um `SurfaceTexture` de GLES.
     *
     * OS DOIS CALLBACKS SAO O LIFECYCLE, e nao enfeite:
     *   onSurfaceCreated  a janela existe (primeira vez, volta do segundo
     *                     plano, rotacao). E aqui que a swapchain nasce.
     *   onSurfaceDestroyed a janela VAI SUMIR. Soltar a swapchain ANTES e
     *                     obrigatorio: o Vulkan fica com uma referencia a
     *                     uma janela morta e o proximo quadro e um erro de
     *                     driver (ou pior, memoria de GPU presa).
     */
    private var produtor: TextureRegistry.SurfaceProducer? = null

    /**
     * O TAMANHO PEDIDO PELO DART, guardado.
     *
     * `onSurfaceCreated` pode chegar com `p.width == 0`: a janela nasce
     * ANTES do layout do `Texture` na arvore Flutter, e e o layout que da
     * tamanho a ela. Preso a zero, o Vulkan recusa a swapchain (extensao
     * 0x0) e o preview nunca aparece — foi assim que o PRIMEIRO
     * `SurfaceProducer` do processo deixava de apresentar. O tamanho
     * pedido e o melhor palpite que existe nesse instante, e a swapchain
     * se refaz sozinha quando o tamanho de verdade chega.
     */
    private var larguraPedida = 0
    private var alturaPedida = 0

    /** Se o callback ja anexou; a rede de seguranca no `criar` le isto. */
    private var anexadoPeloCallback = false

    private val callbackDaSuperficie = object : TextureRegistry.SurfaceProducer.Callback {
        override fun onSurfaceCreated() {
            val p = produtor ?: return
            val l = if (p.width > 0) p.width else larguraPedida
            val a = if (p.height > 0) p.height else alturaPedida
            anexadoPeloCallback = true
            val ok = RenderNativo.anexar(p.surface, l, a)
            Log.i("AureaVulkan", "superficie criada ${p.width}x${p.height} (pedido $l x $a) -> $ok")
        }

        override fun onSurfaceDestroyed() {
            // SOLTA ANTES DE A JANELA MORRER. Inverter esta ordem e o erro
            // que so aparece quando a pessoa troca de app no meio do play.
            anexadoPeloCallback = false
            RenderNativo.desanexar()
            Log.i("AureaVulkan", "superficie destruida")
        }
    }

    /**
     * A API DE DESENHO E ESCOLHIDA AQUI, antes de o motor subir.
     *
     * Por padrao o Impeller usa Vulkan onde o aparelho diz que tem. Em
     * algumas GPUs Mali (MediaTek) e nele que o preview sai com cores
     * erradas — e a exportacao, que captura pela mesma GPU, tambem. A
     * pessoa liga "OpenGL ES" em Ajustes; a chave e a do
     * shared_preferences (arquivo FlutterSharedPreferences, prefixo
     * "flutter."), lida aqui porque o Dart ainda nao existe.
     *
     * A MIGALHA: gravamos "tentando" antes de subir em OpenGL; o Dart
     * apaga no primeiro quadro. Se ainda estiver la na proxima abertura,
     * a sessao anterior nao voltou — a escolha e desfeita sozinha, para
     * a pessoa conseguir abrir o app e chegar em Ajustes.
     */
    override fun getFlutterShellArgs(): FlutterShellArgs {
        val args = super.getFlutterShellArgs()
        val prefs = getSharedPreferences("FlutterSharedPreferences", Context.MODE_PRIVATE)
        if (!prefs.getBoolean("flutter.grafico_opengl", false)) return args
        if (prefs.getBoolean("flutter.grafico_tentando", false)) {
            prefs.edit()
                .putBoolean("flutter.grafico_opengl", false)
                .putBoolean("flutter.grafico_caiu", true)
                .putBoolean("flutter.grafico_tentando", false)
                .commit()
            return args
        }
        prefs.edit().putBoolean("flutter.grafico_tentando", true).commit()
        args.add("--impeller-backend=opengles")
        return args
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/atualizacao"
        ).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    // A VERSAO INSTALADA, do proprio sistema. E daqui que
                    // sai o numero que o app compara com o do servidor —
                    // ler do pubspec seria ler o que foi COMPILADO, e nao
                    // o que esta instalado, e depois de uma atualizacao
                    // recusada os dois discordam.
                    "versao" -> result.success(versaoInstalada())
                    "podeInstalar" -> result.success(podeInstalar())
                    "instalar" -> result.success(
                        instalar(call.argument<String>("caminho"))
                    )
                    else -> result.notImplemented()
                }
            } catch (e: Exception) {
                result.error("atualizacao", e.message ?: "$e", null)
            }
        }

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/render"
        ).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    // CRIA O PRODUTOR E DEVOLVE O ID DA TEXTURA. O Dart
                    // desenha `Texture(textureId: id)` e o conteudo passa a
                    // ser o que o C++ apresentar.
                    "criar" -> {
                        val largura = call.argument<Int>("largura") ?: 0
                        val altura = call.argument<Int>("altura") ?: 0
                        if (largura <= 0 || altura <= 0) {
                            result.error("render", "tamanho invalido", null)
                            return@setMethodCallHandler
                        }
                        liberarProdutor()
                        larguraPedida = largura
                        alturaPedida = altura
                        val p = flutterEngine.renderer.createSurfaceProducer()
                        // A ORDEM AQUI E O DEFEITO QUE O V1 DEIXOU ABERTO.
                        //
                        // Antes: setSize -> setCallback -> produtor = p.
                        // O `setSize` dispara `onSurfaceCreated` na hora, e
                        // o callback lia `produtor`, que ainda era NULO —
                        // entao a primeira superficie do processo nunca era
                        // anexada pelo caminho certo. `produtor` tem de
                        // estar posto ANTES de qualquer coisa que possa
                        // chamar o callback.
                        produtor = p
                        anexadoPeloCallback = false
                        p.setCallback(callbackDaSuperficie)
                        p.setSize(largura, altura)
                        // REDE DE SEGURANCA: em aparelho onde a superficie
                        // ja existe, o `setSize` nao transiciona e o
                        // callback nao vem. Anexar aqui so quando ele NAO
                        // veio evita anexar duas vezes a mesma janela.
                        val ok = if (anexadoPeloCallback) {
                            true
                        } else {
                            RenderNativo.anexar(p.surface, largura, altura)
                        }
                        result.success(
                            mapOf("id" to p.id(), "ok" to ok,
                                  "estado" to RenderNativo.estado())
                        )
                    }
                    "liberar" -> {
                        liberarProdutor()
                        result.success(null)
                    }
                    "estado" -> result.success(RenderNativo.estado())
                    else -> result.notImplemented()
                }
            } catch (e: Exception) {
                result.error("render", e.message ?: "$e", null)
            }
        }

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/seletor"
        ).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    "escolher" -> abrirSeletor(
                        call.argument<List<String>>("mimes") ?: emptyList(),
                        call.argument<String>("uriInicial"),
                        result
                    )
                    else -> result.notImplemented()
                }
            } catch (e: Exception) {
                seletorPendente = null
                result.error("seletor", e.message ?: "$e", null)
            }
        }

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/galeria"
        ).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    "disponivel" -> result.success(true)
                    "publicarVideo" -> publicarVideo(
                        call.argument<String>("caminho"),
                        call.argument<String>("nome"),
                        call.argument<String>("mime") ?: "video/mp4",
                        call.argument<String>("album") ?: "Aurea",
                        result
                    )
                    "abrir" -> result.success(
                        abrirNaGaleria(
                            call.argument<String>("uri"),
                            call.argument<String>("mime") ?: "video/mp4"
                        )
                    )
                    "compartilhar" -> result.success(
                        compartilharDaGaleria(
                            call.argument<String>("uri"),
                            call.argument<String>("mime") ?: "video/mp4"
                        )
                    )
                    else -> result.notImplemented()
                }
            } catch (e: Exception) {
                galeriaPendente = null
                result.error("galeria", e.message ?: "$e", null)
            }
        }

        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "aurea/encoder"
        ).setMethodCallHandler { call, result ->
            // Codificar bloqueia; a thread da interface nao pode parar.
            thread {
                try {
                    val value = handle(call.method, call)
                    runOnUiThread { result.success(value) }
                } catch (e: Exception) {
                    runOnUiThread {
                        result.error("encoder", e.message ?: "$e", null)
                    }
                }
            }
        }
    }

    /**
     * SOLTA O PRODUTOR E A SUPERFICIE. Chamado ao trocar de preview e no
     * `onDestroy` — um `SurfaceProducer` nao liberado mantem uma janela
     * nativa viva e a GPU presa a ela.
     */
    private fun liberarProdutor() {
        RenderNativo.desanexar()
        produtor?.let {
            it.setCallback(null)
            it.release()
        }
        produtor = null
        anexadoPeloCallback = false
        larguraPedida = 0
        alturaPedida = 0
    }

    override fun onDestroy() {
        liberarProdutor()
        // O Dart nao pode ficar esperando um seletor que nao volta mais.
        seletorPendente?.let {
            seletorPendente = null
            try {
                it.error("seletor", "a tela fechou com o seletor aberto", null)
            } catch (_: Exception) {
            }
        }
        // Nem uma publicacao presa no pedido de permissao.
        galeriaPendente?.let {
            galeriaPendente = null
            galeriaArgumentos = null
            try {
                it.error("galeria", "a tela fechou durante o pedido de permissao", null)
            } catch (_: Exception) {
            }
        }
        super.onDestroy()
    }

    // =============================================================== seletor

    /**
     * O SELETOR DE DOCUMENTOS QUE ABRE ONDE A PESSOA ESTAVA.
     *
     * O plugin `file_picker` monta o mesmo `ACTION_OPEN_DOCUMENT`, mas sem
     * lugar inicial: o `initialDirectory` dele so vale para salvar. Aqui a
     * dica vai no `EXTRA_INITIAL_URI` (API 26+). Ela aceita a URI de um
     * ARQUIVO — o navegador abre na pasta que o contem — entao o Dart so
     * guarda a URI do ultimo escolhido, por tipo de midia.
     *
     * E SO UMA DICA: URI que sumiu, provedor que nao a acha ou Android
     * antigo abrem no lugar de sempre, sem erro. Qualquer excecao aqui vira
     * `result.error`, e o Dart cai no `file_picker` de antes.
     */
    private var seletorPendente: MethodChannel.Result? = null
    private var seletorUriAnterior: String? = null

    private companion object {
        /** Codigo do pedido: longe dos que os plugins de seletor usam. */
        const val PEDIDO_DO_SELETOR = 0xA17E

        /** Permissao de escrita para publicar na galeria ate a API 28. */
        const val PEDIDO_DA_GALERIA = 0xA17F
    }

    private fun abrirSeletor(
        mimes: List<String>,
        uriInicial: String?,
        result: MethodChannel.Result
    ) {
        if (seletorPendente != null) {
            result.error("seletor", "ja existe um seletor aberto", null)
            return
        }
        val tipos = mimes.filter { it.isNotBlank() }
        val intent = android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(android.content.Intent.CATEGORY_OPENABLE)
            .addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            .addFlags(android.content.Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        if (tipos.size == 1) {
            intent.type = tipos[0]
        } else {
            // Com mais de um tipo o `type` tem de ser o curinga, e a lista
            // vai no extra — e assim que "foto OU video" se pede.
            intent.type = "*/*"
            if (tipos.isNotEmpty() && !tipos.contains("*/*")) {
                intent.putExtra(
                    android.content.Intent.EXTRA_MIME_TYPES, tipos.toTypedArray()
                )
            }
        }
        if (android.os.Build.VERSION.SDK_INT >= 26 && !uriInicial.isNullOrBlank()) {
            try {
                intent.putExtra(
                    android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                    android.net.Uri.parse(uriInicial)
                )
            } catch (_: Exception) {
            }
        }
        seletorPendente = result
        seletorUriAnterior = uriInicial
        // Se nao houver navegador de documentos, a excecao sobe ate o
        // `setMethodCallHandler`, que solta o pendente e responde com erro.
        startActivityForResult(intent, PEDIDO_DO_SELETOR)
    }

    override fun onActivityResult(
        requestCode: Int,
        resultCode: Int,
        data: android.content.Intent?
    ) {
        if (requestCode != PEDIDO_DO_SELETOR) {
            // Os plugins (file_picker, image_picker) recebem o resultado
            // deles por aqui; engolir isto quebraria todos.
            super.onActivityResult(requestCode, resultCode, data)
            return
        }
        val result = seletorPendente ?: return
        seletorPendente = null
        val anterior = seletorUriAnterior
        seletorUriAnterior = null
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) {
            // CANCELAR NAO E ERRO: nulo, e o Dart nao abre outro seletor.
            result.success(null)
            return
        }
        // Copiar um video de centenas de MB bloqueia; fora da thread da UI.
        thread {
            try {
                val mapa = copiarDoSeletor(uri, anterior)
                runOnUiThread { result.success(mapa) }
            } catch (e: Exception) {
                runOnUiThread { result.error("seletor", e.message ?: "$e", null) }
            }
        }
    }

    /**
     * COPIA O ESCOLHIDO PARA O CACHE e devolve caminho, nome e URI.
     *
     * O Dart trabalha com caminho de arquivo (FFmpeg, `Image.file`), e nao
     * com `content://`. A copia daqui e descartavel: quem guarda a midia
     * passa pelo `persist`, que a leva para `imported_media`.
     */
    private fun copiarDoSeletor(uri: android.net.Uri, anterior: String?): Map<String, Any?> {
        // A PERMISSAO PERSISTENTE mantem a URI valida entre sessoes, para a
        // dica da proxima vez. O Android limita quantas um app segura, por
        // isso a do mesmo tipo, que esta sendo trocada, e devolvida. Nem
        // todo provedor oferece a permissao — sem ela a dica costuma valer
        // do mesmo jeito, e a copia abaixo nao depende dela.
        try {
            contentResolver.takePersistableUriPermission(
                uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
            if (!anterior.isNullOrBlank() && anterior != uri.toString()) {
                try {
                    contentResolver.releasePersistableUriPermission(
                        android.net.Uri.parse(anterior),
                        android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
                    )
                } catch (_: Exception) {
                }
            }
        } catch (_: Exception) {
        }

        var nome: String? = null
        try {
            contentResolver.query(
                uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME),
                null, null, null
            )?.use { c ->
                if (c.moveToFirst() && !c.isNull(0)) nome = c.getString(0)
            }
        } catch (_: Exception) {
        }
        val limpo = (nome ?: uri.lastPathSegment ?: "arquivo")
            .substringAfterLast('/')
            .replace(Regex("[\\\\/:*?\"<>|]"), "_")
            .ifBlank { "arquivo" }

        val raiz = File(cacheDir, "aurea_seletor")
        // As copias de ontem ja foram para `imported_media` (ou nao serviram
        // para nada): nao acumular video em cache a cada importacao.
        val limite = System.currentTimeMillis() - 24L * 60 * 60 * 1000
        raiz.listFiles()?.forEach {
            if (it.lastModified() < limite) it.deleteRecursively()
        }
        val pasta = File(raiz, System.currentTimeMillis().toString())
        pasta.mkdirs()
        val alvo = File(pasta, limpo)
        try {
            val entrada = contentResolver.openInputStream(uri)
                ?: throw java.io.IOException("o provedor nao abriu o arquivo")
            entrada.use { e ->
                alvo.outputStream().use { s -> e.copyTo(s, 1 shl 16) }
            }
            if (alvo.length() == 0L) throw java.io.IOException("arquivo vazio")
        } catch (e: Exception) {
            pasta.deleteRecursively()
            throw e
        }
        return mapOf(
            "caminho" to alvo.absolutePath,
            "nome" to limpo,
            "uri" to uri.toString()
        )
    }

    // =============================================================== galeria

    /**
     * PUBLICAR O VIDEO EXPORTADO NA GALERIA DO APARELHO.
     *
     * O relato era "exporto e o video nao aparece na galeria", e a causa
     * nao estava no codificador: o arquivo nascia em
     * `/data/user/0/<pacote>/app_flutter/exports`, a pasta PRIVADA do
     * app. Nenhum indexador entra la — nem o Google Fotos, nem a Galeria
     * do fabricante, nem o seletor de midia de outro aplicativo.
     *
     * Ha DOIS mundos, e os dois estao aqui:
     *
     *   API 29+ (Android 10, armazenamento com escopo)
     *       Quem cria o arquivo publico e o `MediaStore`. O app nunca
     *       toca no caminho: insere uma linha com `RELATIVE_PATH`
     *       (Movies/Aurea), `DISPLAY_NAME` e `MIME_TYPE`, escreve pelo
     *       `ContentResolver` e so entao baixa o `IS_PENDING`. O
     *       PENDENTE e o detalhe que decide tudo: sem ele, o indexador
     *       pode ler o arquivo no meio da copia e registrar um video
     *       truncado — que aparece na galeria quebrado, ou nao aparece.
     *
     *   API 24..28 (o caminho antigo)
     *       Escrever direto em `Movies/Aurea` com
     *       WRITE_EXTERNAL_STORAGE, que ali ainda e uma permissao de
     *       execucao e precisa ser PEDIDA, e depois avisar o indexador
     *       pelo `MediaScannerConnection` — que e quem devolve a URI de
     *       conteudo. Sem o scan o arquivo existe no cartao e continua
     *       invisivel, que e o mesmo sintoma por outro motivo.
     *
     * Nos dois casos a resposta ao Dart e a MESMA: `uri`, `bytes` e
     * `caminho`. A tela so diz "Exportado com sucesso" com a URI na mao.
     */
    private var galeriaPendente: MethodChannel.Result? = null
    private var galeriaArgumentos: Array<String>? = null

    private fun publicarVideo(
        caminho: String?,
        nome: String?,
        mime: String,
        album: String,
        result: MethodChannel.Result
    ) {
        if (caminho.isNullOrBlank()) {
            result.error("galeria", "caminho vazio", null)
            return
        }
        val origem = File(caminho)
        if (!origem.exists()) {
            result.error("galeria", "o arquivo exportado nao esta em $caminho", null)
            return
        }
        if (origem.length() <= 0L) {
            result.error("galeria", "o arquivo exportado esta vazio", null)
            return
        }
        val limpo = (nome ?: origem.name)
            .substringAfterLast('/')
            .replace(Regex("[\\\\/:*?\"<>|]"), "_")
            .ifBlank { "aurea.mp4" }

        // O CAMINHO ANTIGO PRECISA DA PERMISSAO ANTES DE ESCREVER. Pedir
        // e assincrono: o pedido fica guardado e `onRequestPermissionsResult`
        // retoma daqui de cima.
        if (Build.VERSION.SDK_INT in 23..28 && !temPermissaoDeEscrita()) {
            if (galeriaPendente != null) {
                result.error("galeria", "ja existe um pedido de permissao aberto", null)
                return
            }
            galeriaPendente = result
            galeriaArgumentos = arrayOf(caminho, limpo, mime, album)
            // Pelo `ActivityCompat`: `Activity.requestPermissions` so
            // existe da API 23 em diante, e o app ainda instala abaixo
            // disso.
            androidx.core.app.ActivityCompat.requestPermissions(
                this,
                arrayOf(android.Manifest.permission.WRITE_EXTERNAL_STORAGE),
                PEDIDO_DA_GALERIA
            )
            return
        }

        publicarEmSegundoPlano(origem, limpo, mime, album, result)
    }

    /** Copiar centenas de MB bloqueia; nunca na thread da interface. */
    private fun publicarEmSegundoPlano(
        origem: File,
        nome: String,
        mime: String,
        album: String,
        result: MethodChannel.Result
    ) {
        thread {
            try {
                val mapa = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    publicarPeloMediaStore(origem, nome, mime, album)
                } else {
                    publicarNoCaminhoAntigo(origem, nome, mime, album)
                }
                runOnUiThread { result.success(mapa) }
            } catch (e: Exception) {
                runOnUiThread { result.error("galeria", e.message ?: "$e", null) }
            }
        }
    }

    private fun temPermissaoDeEscrita(): Boolean =
        androidx.core.content.ContextCompat.checkSelfPermission(
            this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE
        ) == android.content.pm.PackageManager.PERMISSION_GRANTED

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        if (requestCode != PEDIDO_DA_GALERIA) {
            // Os plugins recebem os pedidos deles por aqui.
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
            return
        }
        val result = galeriaPendente ?: return
        val args = galeriaArgumentos
        galeriaPendente = null
        galeriaArgumentos = null
        val liberou = grantResults.isNotEmpty() &&
            grantResults[0] == android.content.pm.PackageManager.PERMISSION_GRANTED
        if (!liberou || args == null) {
            result.error("galeria", "sem permissao para escrever na galeria", null)
            return
        }
        publicarEmSegundoPlano(File(args[0]), args[1], args[2], args[3], result)
    }

    @androidx.annotation.RequiresApi(Build.VERSION_CODES.Q)
    private fun publicarPeloMediaStore(
        origem: File,
        nome: String,
        mime: String,
        album: String
    ): Map<String, Any?> {
        val relativo = "${Environment.DIRECTORY_MOVIES}/$album"
        val agora = System.currentTimeMillis()
        val valores = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, nome)
            put(MediaStore.Video.Media.MIME_TYPE, mime)
            put(MediaStore.Video.Media.RELATIVE_PATH, relativo)
            put(MediaStore.Video.Media.DATE_ADDED, agora / 1000)
            put(MediaStore.Video.Media.DATE_MODIFIED, agora / 1000)
            put(MediaStore.Video.Media.DATE_TAKEN, agora)
            // PENDENTE ATE O FIM DA COPIA: enquanto valer 1, o arquivo
            // nao aparece para mais ninguem — nem meio escrito.
            put(MediaStore.Video.Media.IS_PENDING, 1)
        }
        val colecao = MediaStore.Video.Media.getContentUri(
            MediaStore.VOLUME_EXTERNAL_PRIMARY
        )
        val uri = contentResolver.insert(colecao, valores)
            ?: throw java.io.IOException("o sistema recusou o registro na galeria")

        try {
            contentResolver.openOutputStream(uri, "w").use { saida ->
                if (saida == null) {
                    throw java.io.IOException("a galeria nao abriu o arquivo para escrita")
                }
                origem.inputStream().use { entrada -> entrada.copyTo(saida, 1 shl 16) }
                saida.flush()
            }
            val gravados = tamanhoDe(uri)
            if (gravados <= 0L) {
                throw java.io.IOException("o registro na galeria ficou com 0 bytes")
            }
            val fim = ContentValues().apply {
                put(MediaStore.Video.Media.IS_PENDING, 0)
            }
            contentResolver.update(uri, fim, null, null)
            return mapOf(
                "uri" to uri.toString(),
                "bytes" to gravados,
                "nome" to nome,
                "caminho" to "$relativo/$nome"
            )
        } catch (e: Exception) {
            // UM PENDENTE ABANDONADO FICA PARA SEMPRE. Apagar a linha e
            // parte de falhar direito.
            try {
                contentResolver.delete(uri, null, null)
            } catch (_: Exception) {
            }
            throw e
        }
    }

    private fun tamanhoDe(uri: Uri): Long = try {
        contentResolver.openFileDescriptor(uri, "r")?.use { it.statSize } ?: 0L
    } catch (_: Exception) {
        0L
    }

    /**
     * ANDROID 9 E ANTERIORES: arquivo publico + scan.
     *
     * Aqui `Movies/Aurea` e uma pasta de verdade e o app escreve nela
     * com WRITE_EXTERNAL_STORAGE. Quem transforma o arquivo em item da
     * galeria e o indexador; ele devolve a URI de conteudo pelo
     * callback, e e essa URI que "Abrir" e "Compartilhar" usam.
     */
    @Suppress("DEPRECATION")
    private fun publicarNoCaminhoAntigo(
        origem: File,
        nome: String,
        mime: String,
        album: String
    ): Map<String, Any?> {
        val pasta = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES),
            album
        )
        if (!pasta.exists() && !pasta.mkdirs()) {
            throw java.io.IOException("nao deu para criar a pasta ${pasta.absolutePath}")
        }
        var alvo = File(pasta, nome)
        // Nao sobrescrever o que a pessoa ja exportou antes.
        if (alvo.exists()) {
            val base = nome.substringBeforeLast('.', nome)
            val ext = nome.substringAfterLast('.', "mp4")
            alvo = File(pasta, "${base}_${System.currentTimeMillis()}.$ext")
        }
        origem.inputStream().use { entrada ->
            alvo.outputStream().use { saida -> entrada.copyTo(saida, 1 shl 16) }
        }
        if (alvo.length() <= 0L) {
            alvo.delete()
            throw java.io.IOException("a copia para a galeria ficou com 0 bytes")
        }

        val trava = java.util.concurrent.CountDownLatch(1)
        var uri: Uri? = null
        MediaScannerConnection.scanFile(
            this, arrayOf(alvo.absolutePath), arrayOf(mime)
        ) { _, devolvida ->
            uri = devolvida
            trava.countDown()
        }
        trava.await(15, java.util.concurrent.TimeUnit.SECONDS)
        val achada = uri
            ?: throw java.io.IOException(
                "o indexador nao registrou ${alvo.absolutePath}"
            )
        return mapOf(
            "uri" to achada.toString(),
            "bytes" to alvo.length(),
            "nome" to alvo.name,
            "caminho" to alvo.absolutePath
        )
    }

    private fun abrirNaGaleria(uri: String?, mime: String): Boolean {
        if (uri.isNullOrBlank()) return false
        return try {
            startActivity(
                Intent(Intent.ACTION_VIEW)
                    .setDataAndType(Uri.parse(uri), mime)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            true
        } catch (_: Exception) {
            false
        }
    }

    private fun compartilharDaGaleria(uri: String?, mime: String): Boolean {
        if (uri.isNullOrBlank()) return false
        return try {
            val envio = Intent(Intent.ACTION_SEND)
                .setType(mime)
                .putExtra(Intent.EXTRA_STREAM, Uri.parse(uri))
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            startActivity(
                Intent.createChooser(envio, null)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            true
        } catch (_: Exception) {
            false
        }
    }

    // =========================================================== atualizacao

    private fun versaoInstalada(): Map<String, Any> {
        val info = packageManager.getPackageInfo(packageName, 0)
        // `longVersionCode` existe desde a API 28 e e o numero que o
        // Android usa para decidir se uma instalacao e mais NOVA. O
        // `versionCode` antigo e um Int e estoura em apps grandes.
        val codigo = if (android.os.Build.VERSION.SDK_INT >= 28) {
            info.longVersionCode
        } else {
            @Suppress("DEPRECATION")
            info.versionCode.toLong()
        }
        return mapOf("codigo" to codigo, "nome" to (info.versionName ?: ""))
    }

    /// O Android so deixa instalar pacote de fora da loja com esta
    /// permissao ligada, e ela e POR APLICATIVO: quem decide nao e o app,
    /// e a pessoa, numa tela de Ajustes. Sem esta pergunta o app tentaria
    /// instalar e o sistema recusaria em silencio.
    private fun podeInstalar(): Boolean =
        if (android.os.Build.VERSION.SDK_INT >= 26) {
            packageManager.canRequestPackageInstalls()
        } else {
            true
        }

    /**
     * ABRE O INSTALADOR DO SISTEMA para o APK em [caminho].
     *
     * O ARQUIVO NAO VAI CRU. Desde o Android 7 o instalador so aceita um
     * `content://` que o proprio app autoriza; um `file://` faz o sistema
     * recusar a instalacao com um erro que nao explica nada. Quem converte
     * e o `FileProvider`, e o `caminhos_do_apk.xml` diz qual pasta ele
     * alcanca.
     *
     * Devolve "abriu", "permissao" (falta ligar o ajuste) ou "erro: ...".
     */
    private fun instalar(caminho: String?): String {
        if (caminho == null || caminho.isBlank()) return "erro: caminho vazio"
        val arquivo = File(caminho)
        if (!arquivo.exists() || arquivo.length() == 0L) {
            return "erro: o arquivo baixado nao esta la"
        }
        if (!podeInstalar()) {
            // LEVA A PESSOA ATE O AJUSTE. Um aviso dizendo "ligue a
            // permissao" sem dizer onde deixa o app sem saida.
            val intent = android.content.Intent(
                android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                android.net.Uri.parse("package:$packageName")
            ).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
            startActivity(intent)
            return "permissao"
        }
        val uri = androidx.core.content.FileProvider.getUriForFile(
            this, "$packageName.arquivos", arquivo
        )
        val intent = android.content.Intent(android.content.Intent.ACTION_VIEW)
            .setDataAndType(
                uri, "application/vnd.android.package-archive"
            )
            .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
            .addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
        startActivity(intent)
        return "abriu"
    }

    private fun handle(method: String, call: io.flutter.plugin.common.MethodCall): Any? =
        when (method) {
            // MEMORIA E TERMICO: o que o controlador de qualidade 3D le a
            // cada dois segundos para descer a escada antes de o sistema
            // matar o app.
            "memoria" -> {
                val am = getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
                val info = android.app.ActivityManager.MemoryInfo()
                am.getMemoryInfo(info)
                mapOf(
                    "total" to info.totalMem,
                    "disponivel" to info.availMem,
                    "baixa" to info.lowMemory
                )
            }
            "termico" -> {
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                    val pm = getSystemService(Context.POWER_SERVICE) as android.os.PowerManager
                    when (pm.currentThermalStatus) {
                        android.os.PowerManager.THERMAL_STATUS_NONE,
                        android.os.PowerManager.THERMAL_STATUS_LIGHT -> 0
                        android.os.PowerManager.THERMAL_STATUS_MODERATE -> 1
                        android.os.PowerManager.THERMAL_STATUS_SEVERE -> 2
                        else -> 3
                    }
                } else {
                    0
                }
            }
            "available" -> true

            "freeBytes" -> {
                val path = call.argument<String>("path")!!
                val stat = android.os.StatFs(path)
                stat.availableBytes
            }

            "start" -> {
                encoder.start(
                    call.argument<String>("path")!!,
                    call.argument<Int>("width")!!,
                    call.argument<Int>("height")!!,
                    call.argument<Int>("fps")!!,
                    call.argument<Int>("bitrate")!!,
                    call.argument<Boolean>("hevc") ?: false
                )
                true
            }

            "frameRgba" -> {
                encoder.encodeFrameRgba(
                    call.argument<ByteArray>("bytes")!!,
                    call.argument<Int>("width")!!,
                    call.argument<Int>("height")!!
                )
                true
            }

            "frame" -> {
                encoder.encodeFrameFile(call.argument<String>("path")!!)
                true
            }

            /** Codifica uma sequencia inteira sem voltar ao Dart por quadro. */
            "frames" -> {
                val paths = call.argument<List<String>>("paths")!!
                for (p in paths) encoder.encodeFrameFile(p)
                paths.size
            }

            "finish" -> encoder.finish()

            "cancel" -> {
                encoder.stop(discard = true)
                true
            }

            /**
             * REMUX: copia trilhas de um MP4 para outro sem recodificar.
             * Corte puro deixa de custar um render inteiro.
             */
            "remux" -> remux(
                call.argument<String>("source")!!,
                call.argument<String>("target")!!,
                (call.argument<Number>("startUs") ?: 0L).toLong(),
                (call.argument<Number>("endUs") ?: 0L).toLong()
            )

            else -> null
        }

    private fun remux(source: String, target: String, startUs: Long, endUs: Long): Boolean {
        val extractor = MediaExtractor()
        extractor.setDataSource(source)
        File(target).parentFile?.mkdirs()
        val muxer = MediaMuxer(target, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

        val indexMap = HashMap<Int, Int>()
        var maxBuffer = 1 shl 20
        for (i in 0 until extractor.trackCount) {
            val format = extractor.getTrackFormat(i)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: continue
            if (!mime.startsWith("video/") && !mime.startsWith("audio/")) continue
            extractor.selectTrack(i)
            if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) {
                val s = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE)
                if (s > maxBuffer) maxBuffer = s
            }
            indexMap[i] = muxer.addTrack(format)
        }
        if (indexMap.isEmpty()) {
            extractor.release()
            muxer.release()
            return false
        }

        muxer.start()
        val buffer = ByteBuffer.allocate(maxBuffer)
        val info = android.media.MediaCodec.BufferInfo()
        if (startUs > 0) {
            extractor.seekTo(startUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
        }

        while (true) {
            buffer.clear()
            val size = extractor.readSampleData(buffer, 0)
            if (size < 0) break
            val pts = extractor.sampleTime
            if (endUs > 0 && pts > endUs) break

            info.offset = 0
            info.size = size
            info.presentationTimeUs = (pts - startUs).coerceAtLeast(0)
            info.flags = extractor.sampleFlags
            val track = indexMap[extractor.sampleTrackIndex]
            if (track != null) muxer.writeSampleData(track, buffer, info)
            extractor.advance()
        }

        muxer.stop()
        muxer.release()
        extractor.release()
        return true
    }
}
