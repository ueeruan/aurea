package com.aurea.aurea

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
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
        super.onDestroy()
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
