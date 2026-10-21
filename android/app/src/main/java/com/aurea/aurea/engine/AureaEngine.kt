package com.aurea.aurea.engine

import android.content.Context
import android.net.Uri
import android.view.Surface
import java.nio.ByteBuffer

/**
 * A fronteira com o motor C++.
 *
 * REGRA DESTA CLASSE: ela é a ÚNICA porta para o motor. Nenhum outro arquivo
 * Kotlin declara `external fun`.
 *
 * O PREVIEW NÃO PASSA POR AQUI. O motor tem a sua própria thread de render,
 * pacificada pelo vsync do swapchain Vulkan, que desenha direto no `Surface`
 * do `SurfaceView`. A UI só:
 *
 *   1. envia comandos POD em lote ([submitCommands]);
 *   2. lê o estado condensado ([readStatus]) e, no painel DEV, as métricas
 *      ([readPerf]);
 *   3. consulta listas (camadas, efeitos, parâmetros) em buffers diretos.
 *
 * Recomposição do Compose nunca bloqueia o preview, e um preview pesado nunca
 * trava a interface.
 */
class AureaEngine private constructor() {

    companion object {
        /** [saveProjectIfDirty]: o projeto já estava gravado, nada foi escrito. */
        const val SAVE_CLEAN = -1

        /**
         * Pasta padrão dos plugins de efeito: `files/plugins`, irmã da pasta de
         * projetos (`files/projetos`) que o app passa como `documentsDir`.
         */
        @JvmStatic
        fun defaultPluginDir(documentsDir: String): String {
            val docs = java.io.File(documentsDir)
            return java.io.File(docs.parentFile ?: docs, "plugins").absolutePath
        }

        /** Códigos de `Errc` (core/Result.hpp, com `static_assert` lá). */
        const val ERR_NOT_IMPLEMENTED = 7
        const val ERR_SHUTTING_DOWN = 27
        const val ERR_BUSY = 32

        /** Tamanho de um `Command` no C++ (`static_assert` do lado nativo). */
        const val COMMAND_SIZE_BYTES = 128

        /** `MemoryClass::_Count` do C++ (MemoryManager.hpp). */
        const val MEMORY_CLASSES = 11
        /** Índices de MemoryClass usados na tela Armazenamento. */
        const val MEM_THUMBNAILS = 0
        const val MEM_WAVEFORMS = 1
        const val MEM_DECODED_FRAMES = 3

        /** Capacidade do lote. 4096 comandos = 512 KB, o pior caso de um gesto. */
        const val MAX_COMMANDS_PER_FRAME = 4096

        /** Blob de strings por lote (nomes, conteúdo de texto). */
        const val STRING_BLOB_BYTES = 64 * 1024

        @Volatile
        private var appContext: Context? = null

        init {
            System.loadLibrary("aurea")
        }

        fun create(context: Context): AureaEngine {
            appContext = context.applicationContext
            return AureaEngine()
        }

        /**
         * Chamado pelo motor (threads nativas) para abrir uma URI `content://`
         * de vídeo. Devolve um descritor que passa a ser do motor, ou -1.
         *
         * É o que permite reabrir o vídeo de um projeto salvo: o asset guarda a
         * URI, e cada decoder pede um descritor novo quando precisa.
         */
        @JvmStatic
        fun openContentFd(uri: String): Int = try {
            appContext?.contentResolver
                ?.openFileDescriptor(Uri.parse(uri), "r")
                ?.detachFd() ?: -1
        } catch (e: Throwable) {
            // Mídia apagada ou permissão revogada: o motor mostra o espaço vazio
            // (placeholder); o motivo fica no log, sem a URI (dado do usuário).
            android.util.Log.w("AureaEngine", "openContentFd falhou: ${e.javaClass.simpleName}")
            -1
        }

        /**
         * Chamado pelo motor ao reabrir um projeto com imagens: decodifica a
         * origem (URI `content://` ou caminho) em RGBA8 com alfa reto. Resposta:
         * 12 bytes (largura, altura, orientação EXIF em u32 little-endian) +
         * pixels CRUS (quem gira é o motor); null se falhar.
         */
        @JvmStatic
        fun decodeImage(source: String): ByteArray? {
            val ctx = appContext ?: return null
            // Caminho solto (mídia restaurada de um arquivo do projeto) vira file://.
            val uri = if (source.startsWith("/")) Uri.fromFile(java.io.File(source)) else Uri.parse(source)
            val orientation = exifOrientation(ctx, uri)
            val bmp = decodeBitmapRgba(ctx, uri) ?: return null
            return try {
                val w = bmp.width
                val h = bmp.height
                val out = ByteArray(12 + w * h * 4)
                val header = java.nio.ByteBuffer.wrap(out, 0, 12).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                header.putInt(w).putInt(h).putInt(orientation)
                bmp.copyPixelsToBuffer(java.nio.ByteBuffer.wrap(out, 12, w * h * 4))
                out
            } catch (_: Exception) {
                null
            } catch (_: OutOfMemoryError) {
                null
            } finally { bmp.recycle() }
        }

        /**
         * Orientação EXIF 1..8 do arquivo (1 = normal ou sem EXIF). O
         * BitmapFactory entrega os pixels crus; sem isto o retrato do celular
         * entrava deitado. PNG/WebP/HEIF sem EXIF dão 1.
         */
        fun exifOrientation(ctx: Context, uri: Uri): Int = try {
            ctx.contentResolver.openInputStream(uri)?.use {
                android.media.ExifInterface(it).getAttributeInt(
                    android.media.ExifInterface.TAG_ORIENTATION, android.media.ExifInterface.ORIENTATION_NORMAL)
            }?.takeIf { it in 1..8 } ?: 1
        } catch (_: Throwable) {
            1
        }

        /** Stable straight-alpha asset dimensions; reject if temporary copies cannot fit safely. */
        fun decodeBitmapRgba(ctx: Context, uri: Uri, maxDimension: Int = 4096): android.graphics.Bitmap? = try {
            val cr = ctx.contentResolver
            val bounds = android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds = true }
            cr.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it, null, bounds) }
            val sample = ImageMemoryPolicy.sourceSampleSize(bounds.outWidth, bounds.outHeight,
                maxDimension.coerceAtMost(4096), ImageMemoryPolicy.availablePixels())
            if (sample == null) null else {
                val opts = android.graphics.BitmapFactory.Options().apply {
                    inSampleSize = sample
                    inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888
                    inPremultiplied = false
                    // O motor quer sRGB: sem isto uma foto Display P3 / Adobe RGB
                    // (ICC) chegava com os números do outro espaço e desbotava.
                    inPreferredColorSpace = android.graphics.ColorSpace.get(android.graphics.ColorSpace.Named.SRGB)
                }
                val decoded = cr.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it, null, opts) }
                // HEIF 10 bits (RGBA_1010102), PNG 16 bits/HDR (RGBA_F16) ou
                // HARDWARE ignoram o pedido de ARGB_8888 em alguns aparelhos: o
                // copyPixelsToBuffer de w*h*4 estourava ou entregava cor errada.
                if (decoded == null || decoded.config == android.graphics.Bitmap.Config.ARGB_8888) decoded
                else decoded.copy(android.graphics.Bitmap.Config.ARGB_8888, false).also { decoded.recycle() }
            }
        } catch (e: Throwable) {
            android.util.Log.w("AureaEngine", "decodeBitmapRgba falhou: ${e.javaClass.simpleName}")
            null
        }

        @JvmStatic external fun nativeCreate(): Long
        /** Shared limits for a proportional 2D/3D pinch; no render/model lock. */
        @JvmStatic external fun clampPinchFactor(factor: Float, x: Float, y: Float, z: Float, threeD: Boolean): Float
        /** Pinça 3D: limite com a regra de profundidade do motor (Z de conteúdo acompanha X). */
        @JvmStatic external fun clampPinchFactor3D(kind: Int, factor: Float, x: Float, y: Float, z: Float): Float
        /** Escala 3D de gesto já no formato gravado; [axis] 0..2 eixo, 3 uniforme, 4 ajustar (GestureMath.hpp). */
        @JvmStatic external fun gestureScale3D(kind: Int, x: Float, y: Float, z: Float, axis: Int, factor: Float): FloatArray
        /** Trackball (core/Trackball.hpp): 27 argumentos → Euler XYZ + Q acumulado (7), ou vazio. */
        @JvmStatic external fun trackballDrag(args: FloatArray): FloatArray
        /** Parte do trackball sob o dedo: 0..2 anel X/Y/Z, 3 anel da vista, 4 esfera, −1 nada. */
        @JvmStatic external fun trackballHit(axes: FloatArray, x: Float, y: Float, radius: Float, tolerance: Float): Int
        @JvmStatic external fun fitCanvas(values: FloatArray, fill: Boolean): FloatArray
        @JvmStatic external fun previewGestureValue(basis: FloatArray, dx: Float, dy: Float, rotate: Boolean): FloatArray
        @JvmStatic external fun nativeDestroy(handle: Long)

        /**
         * O trecho de curva amostrado pelo MOTOR (`sample_keyframe_ease`,
         * animation/Curve.hpp): [out] recebe valores em t = i/(n−1). É o que o
         * editor de curva desenha. Devolve quantos escreveu (0 = inválido).
         */
        @JvmStatic external fun nativeSampleEase(interp: Int, x1: Float, y1: Float, x2: Float, y2: Float, power: Int, out: FloatArray): Int
        /** Taxa de vídeo (bps) que o export usaria: a regra do motor (BitratePolicy). `quality` 0/1/2. */
        @JvmStatic external fun nativeExportBitrateBps(width: Int, height: Int, fps: Double, codec: Int, quality: Int, customMbps: Int): Long
        /**
         * O degrau do encoder de vídeo que o último export usou (escada do motor:
         * "surface:…", "buffer:…", "legacy:…", "software:…", com os que falharam
         * antes). Para o diagnóstico do relato de bug; vazio = nenhum export ainda.
         */
        @JvmStatic external fun nativeLastExportEncoderPath(): String
        /**
         * A escada de tentativas do export (export/ExportAttempts.hpp no motor):
         * o próximo degrau depois de uma falha. 0 = nada mais ajuda;
         * [EXPORT_ATTEMPT_WAIT] = esperar e refazer o mesmo degrau.
         */
        @JvmStatic external fun nativeExportNextAttempt(attempt: Int, code: Int, failure: Int, stalled: Boolean, tooLong: Boolean,
                                                        shortSide: Int, fps: Double, hevc: Boolean, upscale: Boolean): Int
        /** O lado menor que o degrau exporta (`requested` 0 = o da composição). */
        @JvmStatic external fun nativeExportAttemptShortSide(attempt: Int, requested: Int): Int
        /** Teto de motion blur do degrau (0 = o do projeto; 1 = sem desfoque). */
        @JvmStatic external fun nativeExportAttemptBlurCap(attempt: Int): Int
        /** Teto de fps do degrau (0 = o pedido). */
        @JvmStatic external fun nativeExportAttemptFpsCap(attempt: Int): Double
        /** kExportAttemptWait: refazer o mesmo degrau depois de esperar. */
        const val EXPORT_ATTEMPT_WAIT = 0x100
        /** kExportAttemptBusyRetries. */
        const val EXPORT_ATTEMPT_BUSY_RETRIES = 2
    }

    /** Ponteiro para o contexto nativo. 0 = destruído. */
    private var nativeHandle: Long = nativeCreate()
    private val nativeWork = NativeWorkGate()

    private val commandBuffer: ByteBuffer = directBuffer(MAX_COMMANDS_PER_FRAME * COMMAND_SIZE_BYTES)
    private val stringBuffer: ByteBuffer = directBuffer(STRING_BLOB_BYTES)

    private var stringWriteOffset = 0
    private var commandCount = 0

    // =========================================================================
    // Ciclo de vida
    // =========================================================================

    /**
     * Sobe GPU, renderer e a thread de render. NÃO precisa de superfície: ela
     * chega depois, por [attachSurface]. Pesado (dezenas a centenas de ms na
     * primeira vez, antes do cache de pipeline existir) — fora da thread da UI.
     */
    fun initialize(
        refreshRate: Float,
        cacheDir: String,
        documentsDir: String,
        debug: Boolean,
        probe: LongArray? = null,
        codecs: IntArray? = null,
        /** Pasta dos plugins de efeito. Padrão: `files/plugins` (irmã de `files/projetos`). */
        pluginDir: String = defaultPluginDir(documentsDir),
    ): Boolean = nativeInitialize(nativeHandle, refreshRate, cacheDir, documentsDir, debug, probe, codecs, pluginDir)

    // -------------------------------------------------------------------------
    // Plugins de efeito (docs/plugins/README.md). Um plugin acrescenta um efeito
    // à categoria "Plugins" do catálogo (chave `plugin.<id>`). Instalar compila
    // e desenha um quadro de teste: chamar FORA da thread da UI.
    // -------------------------------------------------------------------------

    /** Resultado de [installPlugin]: o id instalado, ou a mensagem de recusa. */
    data class PluginInstallResult(val ok: Boolean, val id: String, val error: String)

    /**
     * Instala um `.aureaplugin` (ZIP com plugin.json) ou um `.fs` (ISF 2.0) do
     * caminho dado (o app copia o arquivo escolhido para o sandbox antes).
     * Mesmo id com versão igual ou mais nova substitui a instalada.
     */
    fun installPlugin(path: String): PluginInstallResult {
        val raw = nativeWork.run("error:motor encerrado") { nativeInstallPlugin(nativeHandle, path) }
        return when {
            raw.startsWith("ok:") -> PluginInstallResult(true, raw.substring(3), "")
            raw.startsWith("error:") -> PluginInstallResult(false, "", raw.substring(6))
            else -> PluginInstallResult(false, "", raw)
        }
    }

    /** O texto cru da ponte: `ok:<id>` ou `error:<mensagem>`. */
    fun installPluginRaw(path: String): String =
        nativeWork.run("error:motor encerrado") { nativeInstallPlugin(nativeHandle, path) }

    /**
     * Plugins instalados em JSON (lista de objetos: id, version, author, kind,
     * category, name/description por idioma, effectKey, effectTypeId, ok,
     * errorCode, error, directory, passCount, testRenderMs, params).
     */
    fun pluginsJson(): String = nativeWork.run("[]") { nativeListPlugins(nativeHandle) }

    /** Desinstala; o efeito sai do catálogo (projetos guardam os valores). */
    fun removePlugin(id: String): Boolean = nativeWork.run(false) { nativeRemovePlugin(nativeHandle, id) }

    /** A pasta de plugins em uso pelo motor. */
    fun pluginDirectory(): String = nativeWork.run("") { nativePluginDirectory(nativeHandle) }

    fun startupError(): String = nativeStartupError(nativeHandle)
    private external fun nativeStartupError(handle: Long): String

    /** Static, versioned native metadata; values still use the existing layer queries. */
    fun builtinPropertySchemaJson(): String = nativeBuiltinPropertySchema()
    private external fun nativeBuiltinPropertySchema(): String

    /** Modo seguro de vídeo: planos YUV pela CPU, sem zero-copy na GPU (decoders abertos depois). */
    fun useReadableVideoPlanes() = nativeUseReadableVideoPlanes(nativeHandle)
    private external fun nativeUseReadableVideoPlanes(handle: Long)

    /**
     * O que o motor decidiu para ESTE aparelho, já em números.
     *
     * É o que a tela de Ajustes mostra e o que a folha "Novo projeto" usa para
     * não oferecer uma resolução que o aparelho não exporta. Nulo = motor fora
     * do ar.
     */
    fun deviceReport(): DeviceReport? {
        val out = LongArray(DeviceReport.SLOTS)
        if (!nativeDeviceReport(nativeHandle, out)) return null
        return DeviceReport(out)
    }

    /** "Adreno (TM) 740 · driver 0x…" — o aparelho, não um rótulo genérico. */
    fun deviceSummary(): String = nativeDeviceSummary(nativeHandle) ?: ""

    // --- Proxy de edição ("Otimizar vídeos pesados", 0.2.5) -----------------
    // Política, fila e cache são do motor (PreviewProxyService). Daqui só o
    // modo escolhido, o progresso e a limpeza. Par do AureaEngine.mm.

    /** [ProxyMode]: 0 Automático · 1 Sempre · 2 Desligado. */
    fun setProxyMode(mode: Int) = nativeWork.run(Unit) { nativeSetProxyMode(nativeHandle, mode) }
    fun proxyMode(): Int = nativeWork.run(0) { nativeProxyMode(nativeHandle) }
    /** Pedidos desta sessão de projeto. Nulo = motor fora do ar. */
    fun proxySummary(): ProxySummary? = nativeWork.run<ProxySummary?>(null) {
        val out = LongArray(ProxySummary.SLOTS)
        if (nativeProxySummary(nativeHandle, out)) ProxySummary.decode(out) else null
    }
    /** Proxy do vídeo da camada [layer]. */
    fun proxyStatus(layer: Long): ProxyClipStatus =
        nativeWork.run(ProxyClipStatus.NONE) { ProxyClipStatus.decode(nativeProxyStatus(nativeHandle, layer)) }
    fun regenerateProxy(layer: Long): Boolean = nativeWork.run(false) { nativeRegenerateProxy(nativeHandle, layer) }
    fun cancelProxy(layer: Long): Boolean = nativeWork.run(false) { nativeCancelProxy(nativeHandle, layer) }
    /** Bytes da mídia otimizada (percorre a pasta: chamar fora da main thread). */
    fun proxyCacheBytes(): Long = nativeWork.run(0L) { nativeProxyCacheBytes(nativeHandle) }
    /** Apaga a mídia otimizada; devolve os bytes liberados. O export segue no original. */
    fun clearProxyCache(): Long = nativeWork.run(0L) { nativeClearProxyCache(nativeHandle) }
    private external fun nativeSetProxyMode(handle: Long, mode: Int)
    private external fun nativeProxyMode(handle: Long): Int
    private external fun nativeProxySummary(handle: Long, out: LongArray): Boolean
    private external fun nativeProxyStatus(handle: Long, layer: Long): Int
    private external fun nativeRegenerateProxy(handle: Long, layer: Long): Boolean
    private external fun nativeCancelProxy(handle: Long, layer: Long): Boolean
    private external fun nativeProxyCacheBytes(handle: Long): Long
    private external fun nativeClearProxyCache(handle: Long): Long

    /** Foto de base das prévias de efeito (RGBA8, alfa reto). */
    fun setEffectPreviewSource(rgba: ByteArray, width: Int, height: Int): Boolean =
        nativeWork.run(false) { nativeSetEffectPreviewSource(nativeHandle, rgba, width, height) }

    fun shutdown() = nativeWork.close(cancelOngoing = {
        if (nativeHandle != 0L) {
            nativeCancelModelImport(nativeHandle)
            nativeCancelExport(nativeHandle)
            nativeCancelPreRender(nativeHandle)
            nativeCaptionProgress(nativeHandle, true)
        }
    }) { if (nativeHandle != 0L) nativeShutdown(nativeHandle) }

    fun destroy() = nativeWork.close {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    /** App em segundo plano: pausa, devolve os decoders, grava o cache de pipeline. */
    fun suspend() = nativeSuspend(nativeHandle)

    fun resume() = nativeResume(nativeHandle)

    /**
     * Pressão de memória do sistema (Fase 8B): o nível de
     * ComponentCallbacks2.onTrimMemory. O motor solta caches na ordem do spec
     * (miniaturas fora da tela → waveform antiga → quadros sem uso → cache de
     * render → assets 3D sem uso → temporários); o projeto nunca. Devolve os
     * bytes liberados.
     */
    fun trimMemory(level: Int): Long = nativeWork.run(0L) { if (nativeHandle != 0L) nativeTrimMemory(nativeHandle, level) else 0L }

    /**
     * Uso e orçamento de memória do motor por categoria (MemoryClass):
     * [usado, orçamento] intercalados. null = motor ainda não subiu.
     */
    fun memoryReport(): LongArray? {
        if (nativeHandle == 0L) return null
        val out = LongArray(MEMORY_CLASSES * 2)
        return if (nativeMemoryReport(nativeHandle, out) > 0) out else null
    }
    /** Redesenha o preview mesmo sem mudança (a janela voltou a aparecer). */
    fun invalidate() = nativeInvalidate(nativeHandle)

    // =========================================================================
    // Superfície
    // =========================================================================

    fun attachSurface(surface: Surface, widthPx: Int, heightPx: Int): Boolean =
        nativeAttachSurface(nativeHandle, surface, widthPx, heightPx)

    /** Só volta quando a GPU largou a janela (o Android a destrói logo depois). */
    fun detachSurface() = nativeDetachSurface(nativeHandle)

    fun resizeSurface(widthPx: Int, heightPx: Int) = nativeResizeSurface(nativeHandle, widthPx, heightPx)

    // =========================================================================
    // Comandos
    // =========================================================================

    fun beginCommandBatch() {
        commandCount = 0
        stringWriteOffset = 0
    }

    /** Escreve uma string no blob do lote e devolve o offset, ou -1 se não couber. */
    fun writeString(text: String): Int {
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (stringWriteOffset + bytes.size > STRING_BLOB_BYTES) return -1
        val offset = stringWriteOffset
        stringBuffer.position(offset)
        stringBuffer.put(bytes)
        stringWriteOffset += bytes.size
        return offset
    }

    /** Slot do próximo comando, ou `null` com o lote cheio. Só o [CommandBatch] escreve nele. */
    internal fun reserveCommandSlot(): ByteBuffer? {
        if (commandCount >= MAX_COMMANDS_PER_FRAME) return null
        commandBuffer.limit(commandBuffer.capacity())
        commandBuffer.position(commandCount * COMMAND_SIZE_BYTES)
        return commandBuffer.slice().order(java.nio.ByteOrder.nativeOrder())
    }

    internal fun endCommand() {
        commandCount++
    }

    fun pendingCommandCount(): Int = commandCount

    /** Envia o lote (fila sem trava: nunca bloqueia) e acorda o render. */
    fun submitCommands(): Int {
        if (commandCount == 0) return 0
        val accepted = nativeWork.run(0) { nativeSubmitCommands(
            nativeHandle, commandBuffer, commandCount,
            if (stringWriteOffset > 0) stringBuffer else null, stringWriteOffset,
        ) }
        commandCount = 0
        stringWriteOffset = 0
        return accepted
    }

    // =========================================================================
    // Estado
    // =========================================================================

    fun readStatus(out: ByteBuffer): Boolean = nativeWork.run(false) { nativeReadStatus(nativeHandle, out) }
    fun readTelemetry(out: ByteBuffer): Boolean = nativeReadTelemetry(nativeHandle, out)
    fun readPerf(out: ByteBuffer): Boolean = nativeReadPerf(nativeHandle, out)

    // =========================================================================
    // Consultas
    // =========================================================================

    fun queryLayers(outBuffer: ByteBuffer, capacity: Int, nameBlob: ByteBuffer): Int =
        nativeQueryLayers(nativeHandle, outBuffer, capacity, nameBlob, nameBlob.capacity())

    fun queryKeyframes(layer: Long, outBuffer: ByteBuffer, capacity: Int): Int =
        nativeQueryKeyframes(nativeHandle, layer, outBuffer, capacity)

    /**
     * Keyframes de todas as camadas numa consulta só: `index` recebe 16 bytes
     * por camada (id u64, quantidade u32, reservado) e `rows` as linhas
     * concatenadas. Devolve `(camadas shl 32) or total`; se não coube, nada
     * foi escrito e quem chama cresce os buffers.
     */
    /** [x1, y1, x2, y2, força 1..3] do trecho que sai do keyframe. */
    fun queryKeyframeEasing(layer: Long, property: Int, effect: Int, param: Int, time: Int): FloatArray? {
        val handles = floatArrayOf(0f, 0f, 0f, 0f, 1f)
        return if (nativeQueryKeyframeEasing(nativeHandle, layer, property, effect, param, time, handles)) handles else null
    }

    fun queryAllKeyframes(index: ByteBuffer, layerCapacity: Int, rows: ByteBuffer, capacity: Int): Long =
        nativeQueryAllKeyframes(nativeHandle, index, layerCapacity, rows, capacity)

    fun queryTrackCurve(layer: Long, property: Int, effect: Int, param: Int, from: Int, to: Int): FloatArray {
        val values = FloatArray(160)
        val count = nativeQueryTrackCurve(nativeHandle, layer, property, effect, param, from, to, values)
        return if (count == values.size) values else values.copyOf(count.coerceIn(0, values.size))
    }

    fun queryCurve(layer: Long, property: Int, from: Int, to: Int, out: FloatArray): Int =
        nativeQueryCurve(nativeHandle, layer, property, from, to, out, out.size)

    fun queryEffectCatalog(rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectCatalog(nativeHandle, rows, capacity, blob)

    fun queryLayerEffects(layer: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryLayerEffects(nativeHandle, layer, rows, capacity, blob)

    fun queryEffectParams(layer: Long, effectId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectParams(nativeHandle, layer, effectId, rows, capacity, blob)

    fun effectCurve(layer: Long, effect: Int, param: Int, channel: Int, samples: Boolean = false): FloatArray {
        val out = FloatArray(if (samples) 256 else 128)
        val count = nativeQueryEffectCurve(nativeHandle, layer, effect, param, channel, samples, out)
        return out.copyOf(count.coerceIn(0, out.size))
    }
    fun editEffectCurve(layer: Long, effect: Int, param: Int, channel: Int, action: Int, point: Int, x: Float, y: Float): Int =
        nativeWork.run(-1) { nativeEditEffectCurve(nativeHandle, layer, effect, param, channel, action, point, x, y) }

    /** Abre/recolhe a seção [section] (ordinal 1..N) do efeito no painel; sem passo de desfazer. */
    fun setEffectSectionOpen(layer: Long, effect: Int, section: Int, open: Boolean): Boolean =
        nativeWork.run(false) { nativeSetEffectSectionOpen(nativeHandle, layer, effect, section, open) }

    /** Declaração dos parâmetros de um TIPO de efeito (a ficha do catálogo). */
    fun queryEffectSpecs(typeId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectSpecs(nativeHandle, typeId, rows, capacity, blob)

    fun queryLayerDetail(layer: Long, out: ByteBuffer): Boolean = nativeQueryLayerDetail(nativeHandle, layer, out)

    /** Composição atual: devolve o id (0 = nenhuma); `out` = [w, h, fps, duração, r, g, b, a]. */
    fun queryComposition(out: DoubleArray): Long = nativeQueryComposition(nativeHandle, out)
    fun frameTimeNs(frame: Long): Long = nativeFrameTimeNs(nativeHandle, frame)

    /**
     * Medida do último quadro renderizado FORA da tela — é o que a captura de
     * quadro usa. Fora do editor não há prévia viva e o `PerfPOD` fica zerado,
     * mas a captura renderiza de verdade: este é o número que sobra, e é ele que
     * diz se o tempo foi para a CPU, para o decoder ou para a GPU.
     */
    fun readOffscreenMeasure(out: DoubleArray): Boolean = nativeWork.run(false) { nativeReadOffscreenMeasure(nativeHandle, out) }

    /// Liga as timestamp queries do render fora da tela (só a medição usa).
    fun setOffscreenTimers(on: Boolean) = nativeOffscreenTimers(nativeHandle, on)

    /** RGBA8 da miniatura em `out`; 0 = ainda na fila (ver `thumbnailGeneration`). */
    fun queryThumbnail(layer: Long, frame: Int, height: Int, out: ByteBuffer, outWidth: IntArray): Int =
        nativeWork.run(0) { nativeQueryThumbnail(nativeHandle, layer, frame, height, out, outWidth) }

    /** Frame do playhead em RGBA8 sRGB (lado maior = `maxDim`); `outSize` recebe largura/altura. */
    fun captureFrame(maxDim: Int, out: ByteBuffer, outSize: IntArray): Int =
        nativeWork.run(0) { nativeCaptureFrame(nativeHandle, maxDim, out, outSize) }

    /** Capa da Home: escala de miniatura e políticas da prévia, sem render final. */
    fun capturePreviewFrame(maxDim: Int, out: ByteBuffer, outSize: IntArray): Int =
        nativeWork.run(0) { nativeCapturePreviewFrame(nativeHandle, maxDim, out, outSize) }

    /**
     * A prévia de um efeito: o efeito de verdade rodando sobre a cartela de
     * demonstração do motor, em RGBA8 (pré-multiplicado não: alfa reto).
     * `outSize` recebe largura/altura de verdade. Falso = não deu para
     * pré-visualizar (efeito temporal, sem GPU, tipo desconhecido).
     */
    fun renderEffectPreview(typeId: Int, width: Int, height: Int, out: ByteBuffer, outSize: IntArray): Boolean =
        nativeWork.run(false) { nativeRenderEffectPreview(nativeHandle, typeId, width, height, out, outSize) }

    /**
     * A prévia ANIMADA de um efeito: `frames` (12..16) quadros antes/depois,
     * RGBA8 de alfa reto, um atrás do outro em [out] (capacidade para
     * `frames × width × height × 4`). `scene`: 0 foto, 1 cartela, 2 formas,
     * 3 baixa luz. `outSize` recebe largura, altura e quadros de verdade.
     * Devolve 0 (ok) ou o código de erro do motor ([ERR_BUSY] = o preview do
     * editor está com a GPU: tente de novo mais tarde).
     */
    fun renderEffectPreviewSequence(
        typeId: Int, width: Int, height: Int, frames: Int, scene: Int, out: ByteBuffer, outSize: IntArray,
    ): Int = nativeWork.run(ERR_SHUTTING_DOWN) {
        nativeRenderEffectPreviewSequence(nativeHandle, typeId, width, height, frames, scene, out, outSize)
    }

    fun setSelection(layers: LongArray) = nativeSetSelection(nativeHandle, layers)
    fun clearSelection() = nativeClearSelection(nativeHandle)

    // =========================================================================
    // Importação e projeto
    // =========================================================================

    /** Id da layer criada (≥ 0), ou `-código` do erro. */
    fun importVideo(source: String, displayName: String): Long =
        nativeWork.run(-5L) { nativeImportVideo(nativeHandle, source, displayName) }

    /** Arquivo de áudio: camada de áudio no topo. Id ≥ 0 ou −Errc. */
    fun importAudio(source: String, displayName: String): Long =
        nativeWork.run(-5L) { nativeImportAudio(nativeHandle, source, displayName) }

    /**
     * Sequência de imagens (PNG/JPG/TGA/BMP/EXR numerados, ou um .zip deles): sondagem.
     * `absolutePaths` = arquivos JÁ copiados para o armazenamento do app (os quadros,
     * um quadro só, a pasta ou o .zip). Devolve o JSON do motor:
     * `{"ok":true,"pattern","directory","first","last","frameCount","presentCount",
     * "missingCount","missing":[…],"width","height","bitDepth","hasAlpha","linear",
     * "suggestedFps","suggestedAlphaMode","suggestedColorMode",…}` ou
     * `{"ok":false,"error":<Errc>,"detail"}`. Nulo = motor encerrando. Fora da thread da UI.
     */
    fun probeImageSequence(absolutePaths: Array<String>): String? =
        nativeWork.run<String?>(null) { nativeProbeImageSequence(nativeHandle, absolutePaths) }

    /**
     * Importa a sequência como clipe de vídeo. `fps` 0 = o sugerido; `alphaMode`
     * 0 auto, 1 reto, 2 pré-multiplicado, 3 ignorar; `colorMode` 0 auto, 1 sRGB,
     * 2 linear. Id da camada ≥ 0 ou −Errc.
     */
    fun importImageSequence(absolutePaths: Array<String>, fps: Double, alphaMode: Int, colorMode: Int): Long =
        nativeWork.run(-5L) { nativeImportImageSequence(nativeHandle, absolutePaths, fps, alphaMode, colorMode) }

    /** O som do vídeo vira camada própria; o vídeo fica mudo. Id ≥ 0 ou −Errc. */
    fun extractAudio(layer: Long): Long = nativeExtractAudio(nativeHandle, layer)

    /** Nova camada de texto no centro. Id ≥ 0 ou −Errc. */
    fun addText(content: String): Long = nativeAddText(nativeHandle, content)

    /** Texto da camada: conteúdo (nulo = não é texto) e 13 floats (ver TextDetail). */
    fun queryText(layer: Long, out: FloatArray): String? = nativeQueryText(nativeHandle, layer, out)

    /** Nova forma (ladrilho `preset` da aba Forma) no centro. Id ≥ 0 ou −Errc. */
    fun addShape(preset: Int): Long = nativeAddShape(nativeHandle, preset)

    // Copiar e colar (área de transferência do motor).
    fun copyLayers(ids: LongArray): Int = nativeCopyLayers(nativeHandle, ids)
    fun pasteLayers(frame: Long): Int = nativePasteLayers(nativeHandle, frame)
    fun copyStyle(layer: Long): Boolean = nativeCopyStyle(nativeHandle, layer)
    fun pasteStyle(ids: LongArray): Int = nativePasteStyle(nativeHandle, ids)
    fun copyTransform(layer: Long): Boolean = nativeCopyTransform(nativeHandle, layer)
    fun pasteTransform(ids: LongArray): Int = nativePasteTransform(nativeHandle, ids)
    fun copyEffects(layer: Long, effect: Int = -1): Int = nativeCopyEffects(nativeHandle, layer, effect)
    fun pasteEffects(ids: LongArray): Int = nativePasteEffects(nativeHandle, ids)
    fun copyKeyframes(layer: Long, frame: Long): Int = nativeCopyKeyframes(nativeHandle, layer, frame)
    fun copyKeyframeSelection(layer: Long, references: LongArray): Int = nativeKeyframeSelection(nativeHandle, layer, references, 0, 0)
    fun editKeyframeSelection(layer: Long, references: LongArray, delta: Int, remove: Boolean = false): Int =
        nativeKeyframeSelection(nativeHandle, layer, references, if (remove) 2 else 1, delta)
    fun pasteKeyframes(ids: LongArray, frame: Long): Int = nativePasteKeyframes(nativeHandle, ids, frame)
    /** Todos os keyframes da camada (tempo relativo ao primeiro); cola com [pasteKeyframes]. */
    fun copyAnimation(layer: Long): Int = nativeCopyAnimation(nativeHandle, layer)
    /** Tira keyframes redundantes (`property` < 0 = todas); tolerância = fração da amplitude. */
    fun optimizeKeyframes(layer: Long, property: Int, tolerance: Float): Int = nativeOptimizeKeyframes(nativeHandle, layer, property, tolerance)
    /** Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes. */
    fun clipboardState(): Int = nativeClipboardState(nativeHandle)

    // Gizmo 3D.
    fun queryGizmo(layer: Long, length: Float, out: FloatArray, localSpace: Boolean = false): Boolean = nativeQueryGizmo(nativeHandle, layer, length, out, localSpace)
    fun gizmoMoveLocal(layer: Long, axis: Int, amount: Float, out: FloatArray): Boolean = nativeGizmoMoveLocal(nativeHandle, layer, axis, amount, out)
    fun previewGestureBasis(layer: Long): FloatArray? = FloatArray(13).takeIf { nativePreviewGestureBasis(nativeHandle, layer, it) }
    private external fun nativePreviewGestureBasis(handle: Long, layer: Long, out: FloatArray): Boolean
    /** Trackball: origem (2), eixos na vista A (9), frame F (9), Rotação XYZ (3). */
    fun queryTrackball(layer: Long): FloatArray? = FloatArray(23).takeIf { nativeQueryTrackball(nativeHandle, layer, it) }
    private external fun nativeQueryTrackball(handle: Long, layer: Long, out: FloatArray): Boolean

    fun sceneSettings(): FloatArray = nativeSceneSettings(nativeHandle)
    fun setSceneSetting(parameter: Int, value: Float): Boolean = nativeSetSceneSetting(nativeHandle, parameter, value)
    private external fun nativeSceneSettings(handle: Long): FloatArray
    private external fun nativeSetSceneSetting(handle: Long, parameter: Int, value: Float): Boolean

    // Ambiente 3D (HDRI).
    fun importHdri(path: String): Long = nativeWork.run(-5L) { nativeImportHdri(nativeHandle, path) }
    fun clearHdri(): Boolean = nativeClearHdri(nativeHandle)
    fun setEnvironmentBackground(visible: Boolean): Boolean = nativeSetEnvironmentBackground(nativeHandle, visible)
    fun setEnvironmentBackgroundRange(start: Long, end: Long): Boolean = nativeSetEnvironmentBackgroundRange(nativeHandle, start, end)
    fun setEnvironment(intensity: Float, rotation: Float): Boolean = nativeSetEnvironment(nativeHandle, intensity, rotation)
    fun queryEnvironment(out: FloatArray): Boolean = nativeQueryEnvironment(nativeHandle, out)

    /** Ambiente PRÓPRIO de um objeto 3D (v22). `source` 0 = do projeto, 1 = dele. */
    fun setObjectEnvironment(layer: Long, source: Int, hdri: Long, intensity: Float, rotation: Float, exposure: Float): Boolean =
        nativeSetObjectEnvironment(nativeHandle, layer, source, hdri, intensity, rotation, exposure)

    /** {fonte, asset, intensidade, giro, exposição}. */
    fun queryObjectEnvironment(layer: Long, out: FloatArray): Boolean =
        nativeQueryObjectEnvironment(nativeHandle, layer, out)

    fun queryMaterials(layer: Long): FloatArray = nativeQueryMaterials(nativeHandle, layer)
    fun setMaterialParam(layer: Long, material: Int, param: Int, value: Float): Boolean =
        nativeSetMaterialParam(nativeHandle, layer, material, param, value)

    // Pré-composição.
    fun precompose(ids: LongArray): Long = nativePrecompose(nativeHandle, ids)
    fun groupAndMask(ids: LongArray, exclude: Boolean, name: String? = null): Long = nativeGroupAndMask(nativeHandle, ids, exclude, name)
    /** A mesma regra de [groupAndMask], sem mudar nada (habilita os botões da seleção). */
    fun canGroupAndMask(ids: LongArray): Boolean = nativeWork.run(false) { nativeCanGroupAndMask(nativeHandle, ids) }
    fun queryKeyframeNavigation(layer: Long, property: Int, effect: Int, param: Int, compositionFrame: Int): KeyframeNavigation? {
        val out = LongArray(7)
        if (!nativeQueryKeyframeNavigation(nativeHandle, layer, property, effect, param, compositionFrame.toLong(), out)) return null
        return KeyframeNavigation(out[0].toInt(), out[1].toInt(), out[2].toInt(), out[3].toInt(), out[4].toInt(), out[5].toInt(), out[6].toInt())
    }
    /** Null means no marker/selected-layer key lies strictly in that direction. */
    fun queryTimelineNavigation(layer: Long?, compositionFrame: Int, direction: Int): Int? =
        nativeQueryTimelineNavigation(nativeHandle, layer ?: 0L, compositionFrame.toLong(), direction)
            .takeIf { it in 0..Int.MAX_VALUE.toLong() }?.toInt()
    fun seekTimelineEvent(compositionFrame: Int): Boolean = nativeSeekTimelineEvent(nativeHandle, compositionFrame.toLong())
    fun openPrecomp(layer: Long): Boolean = nativeOpenPrecomp(nativeHandle, layer)
    /** null = desagrupou; senão o motivo da recusa. */
    fun ungroupPrecomp(layer: Long): String? = nativeUngroupPrecomp(nativeHandle, layer)
    fun closePrecomp(): Boolean = nativeClosePrecomp(nativeHandle)
    fun precompDepth(): Int = nativePrecompDepth(nativeHandle)
    fun compositionName(): String = nativeCompositionName(nativeHandle) ?: ""

    /** Rastreio/estabilização (síncrono, fora da UI). Id da camada com os keyframes ou −Errc. */
    fun trackPoint(layer: Long, x: Float, y: Float, stabilize: Boolean, tracked: IntArray): Long =
        nativeTrackPoint(nativeHandle, layer, x, y, stabilize, tracked)

    // Máscaras (roto) e track matte. Pontos = 6 floats cada (x, y, entrada x/y,
    // saída x/y — px da camada).
    fun addMask(layer: Long, pts: FloatArray?, count: Int, closed: Boolean): Int = nativeAddMask(nativeHandle, layer, pts, count, closed)
    fun removeMask(layer: Long, mask: Int): Boolean = nativeRemoveMask(nativeHandle, layer, mask)
    fun setMaskPath(layer: Long, mask: Int, pts: FloatArray?, count: Int, closed: Boolean, undo: Boolean): Boolean =
        nativeSetMaskPath(nativeHandle, layer, mask, pts, count, closed, undo)
    fun setMaskProps(layer: Long, mask: Int, op: Int, inverted: Boolean, feather: Float, expansion: Float, opacity: Float): Boolean =
        nativeSetMaskProps(nativeHandle, layer, mask, op, inverted, feather, expansion, opacity)
    /** 1 = ficou com key no cabeçote, 0 = tirou, −1 = falhou. */
    fun toggleMaskPathKey(layer: Long, mask: Int): Int = nativeToggleMaskPathKey(nativeHandle, layer, mask)
    fun setMaskParam(layer: Long, mask: Int, param: Int, value: Float): Boolean = nativeSetMaskParam(nativeHandle, layer, mask, param, value)
    fun toggleMaskParamKey(layer: Long, mask: Int, param: Int): Boolean = nativeToggleMaskParamKey(nativeHandle, layer, mask, param)
    /** Floats necessários (Engine::query_masks); só escreve se couber em [out]. */
    fun queryMasks(layer: Long, out: FloatArray): Int = nativeQueryMasks(nativeHandle, layer, out)
    /** Síncrono (decodifica): fora da UI. Quadros rastreados, ou −Errc. */
    fun trackMask(layer: Long, mask: Int, mode: Int): Int = nativeWork.run(-5) { nativeTrackMask(nativeHandle, layer, mask, mode) }

    // Rig 2D (camada de imagem): juntas em px da COMPOSIÇÃO (Engine::query_rig).
    /** Floats necessários (5 por junta: id, pai ou −1, x, y, key no playhead); só escreve se couber. */
    fun queryRig(layer: Long, bind: Boolean, out: FloatArray): Int = nativeQueryRig(nativeHandle, layer, bind, out)
    /** Id da junta nova (−1 = falhou). [parent] −1 = raiz solta. */
    fun rigAddJoint(layer: Long, parent: Int, x: Float, y: Float): Int = nativeRigAddJoint(nativeHandle, layer, parent, x, y)
    fun rigMoveJoint(layer: Long, joint: Int, x: Float, y: Float, continuing: Boolean): Boolean =
        nativeRigMoveJoint(nativeHandle, layer, joint, x, y, continuing)
    fun rigRemoveJoint(layer: Long, joint: Int): Boolean = nativeRigRemoveJoint(nativeHandle, layer, joint)
    fun rigClear(layer: Long): Boolean = nativeRigClear(nativeHandle, layer)
    fun rigAutoHumanoid(layer: Long): Int = nativeRigAutoHumanoid(nativeHandle, layer)
    /** Animar: leva a junta até (x, y) e grava keyframe no playhead (IK na ponta, FK no resto). */
    fun rigPoseJoint(layer: Long, joint: Int, x: Float, y: Float, continuing: Boolean): Boolean =
        nativeRigPoseJoint(nativeHandle, layer, joint, x, y, continuing)
    /** Montagem aberta nesta camada (a prévia sem deformação); 0 = nenhuma. */
    fun setRigSetupLayer(layer: Long) = nativeSetRigSetupLayer(nativeHandle, layer)
    // Malha de deformação (Engine::query_mesh_warp): 4 floats de cabeçalho
    // {linhas, colunas, key no cabeçote, animada} + 10 por vértice, normalizados à camada.
    fun queryMeshWarp(layer: Long, effect: Int, out: FloatArray): Int = nativeQueryMeshWarp(nativeHandle, layer, effect, out)
    /** [grip] 0 = vértice (alças juntas), 1..4 = alça esquerda, direita, cima, baixo; (u, v) normalizado. */
    fun meshWarpDrag(layer: Long, effect: Int, vertex: Int, grip: Int, u: Float, v: Float, autoKey: Boolean, continuing: Boolean): Boolean =
        nativeMeshWarpDrag(nativeHandle, layer, effect, vertex, grip, u, v, autoKey, continuing)
    fun meshWarpReset(layer: Long, effect: Int): Boolean = nativeMeshWarpReset(nativeHandle, layer, effect)
    fun meshWarpToggleKey(layer: Long, effect: Int): Boolean = nativeMeshWarpToggleKey(nativeHandle, layer, effect)
    // Fantoche (Engine::query_puppet): 4 floats por pino {índice, u, v, key no cabeçote};
    // a malha deformada: 6 floats por triângulo (u, v). u, v = fração da camada.
    fun queryPuppet(layer: Long, effect: Int, out: FloatArray): Int = nativeQueryPuppet(nativeHandle, layer, effect, out)
    fun queryPuppetMesh(layer: Long, effect: Int, out: FloatArray): Int = nativeQueryPuppetMesh(nativeHandle, layer, effect, out)
    /** Pino novo (−1 = cheio). */
    fun puppetAddPin(layer: Long, effect: Int, u: Float, v: Float): Int = nativePuppetAddPin(nativeHandle, layer, effect, u, v)
    fun puppetMovePin(layer: Long, effect: Int, pin: Int, u: Float, v: Float, autoKey: Boolean, continuing: Boolean): Boolean =
        nativePuppetMovePin(nativeHandle, layer, effect, pin, u, v, autoKey, continuing)
    fun puppetRemovePin(layer: Long, effect: Int, pin: Int): Boolean = nativePuppetRemovePin(nativeHandle, layer, effect, pin)
    fun setTrackMatte(layer: Long, matte: Long, mode: Int): Boolean = nativeSetTrackMatte(nativeHandle, layer, matte, mode)
    /** {matte, modo} (matte 0 = nenhuma). */
    fun queryTrackMatte(layer: Long, out: LongArray): Boolean = nativeQueryTrackMatte(nativeHandle, layer, out)

    // Eco e RGB no tempo.
    fun setEcho(layer: Long, count: Int, delay: Float, decay: Float): Boolean = nativeSetEcho(nativeHandle, layer, count, delay, decay)
    fun setRgbTime(layer: Long, delay: Float): Boolean = nativeSetRgbTime(nativeHandle, layer, delay)
    fun queryEcho(layer: Long, out: FloatArray): Boolean = nativeQueryEcho(nativeHandle, layer, out)

    /** Transição de entrada/saída (tipo 0..5, duração em quadros). */
    fun setTransition(layer: Long, out: Boolean, type: Int, frames: Int): Boolean = nativeSetTransition(nativeHandle, layer, out, type, frames)

    // Partículas.
    fun addParticles(preset: Int): Long = nativeAddParticles(nativeHandle, preset)

    // Texto 3D.
    fun addText3d(content: String, fields: FloatArray, fontPath: String = ""): Long = nativeAddText3d(nativeHandle, content, fields, fontPath)
    fun setText3d(layer: Long, content: String, fields: FloatArray, fontPath: String = ""): Boolean =
        nativeSetText3d(nativeHandle, layer, content, fields, fontPath)
    fun queryText3dFont(layer: Long): String = nativeQueryText3dFont(nativeHandle, layer) ?: ""
    fun queryText3dTexture(layer: Long): String = nativeQueryText3dTexture(nativeHandle, layer) ?: ""
    fun setText3dTexture(layer: Long, path: String): Boolean = nativeSetText3dTexture(nativeHandle, layer, path)
    fun queryText3d(layer: Long, out: FloatArray): String? = nativeQueryText3d(nativeHandle, layer, out)
    fun applyText3dPreset(layer: Long, preset: Int): Boolean = nativeApplyText3dPreset(nativeHandle, layer, preset)

    /** Real Camera: andar (m/s, + frente, 0 parado) e profundidade (× do frente/trás medido). */
    fun realCameraWalk(speed: Float, depth: Float): Boolean = nativeWork.run(false) { nativeRealCameraWalk(nativeHandle, speed, depth) }
    private external fun nativeRealCameraWalk(handle: Long, speed: Float, depth: Float): Boolean

    /** Sombras do objeto 3D: projeta / recebe. */
    fun setModelShadows(layer: Long, cast: Boolean, receive: Boolean): Boolean =
        nativeSetModelShadows(nativeHandle, layer, cast, receive)
    fun queryModelShadows(layer: Long, out: FloatArray): Boolean = nativeQueryModelShadows(nativeHandle, layer, out)

    /** Mostrar interior (dupla face) do objeto 3D: um passo de desfazer. */
    fun setModelInterior(layer: Long, on: Boolean): Boolean = nativeSetModelInterior(nativeHandle, layer, on)
    /** 1 = mostra o interior, 0 = não, −1 = não é objeto 3D. */
    fun queryModelInterior(layer: Long): Int = nativeQueryModelInterior(nativeHandle, layer)

    /**
     * Miniatura (bola de estúdio) do material [material] da camada 3D, [size]×[size]
     * em ARGB (o formato de `Bitmap.createBitmap(int[])`). Nulo = sem material.
     * Pode rodar fora da thread principal (a bola é calculada na CPU, com cache).
     */
    fun materialPreview(layer: Long, material: Int, size: Int): IntArray? =
        nativeWork.run<IntArray?>(null) { nativeMaterialPreview(nativeHandle, layer, material, size) }
    /** A bola de um material pronto do texto 3D (0..6). */
    fun text3dPresetPreview(preset: Int, size: Int): IntArray? =
        nativeWork.run<IntArray?>(null) { nativeText3DPresetPreview(nativeHandle, preset, size) }

    // --- Formas 3D (Engine::add_shape3d e família) -----------------------------------------
    /** Nova camada de forma 3D ([kind] = Shape3DKind 0..9). Id da camada, ou −Errc. */
    fun addShape3d(kind: Int, name: String): Long = nativeAddShape3d(nativeHandle, kind, name)
    /** Receita: out[0] forma, out[1] partes, 5 por parte (RGBA sRGB, tem imagem). −1 = não é forma. */
    fun queryShape3d(layer: Long, out: FloatArray): Int = nativeQueryShape3d(nativeHandle, layer, out)
    /** Cor ([rgba] nula = mantém) e imagem ([image] nula = mantém, "" = tira) da parte (−1 = todas). */
    fun setShape3dPartStyle(layer: Long, part: Int, rgba: FloatArray?, image: String?): Boolean =
        nativeWork.run(false) { nativeSetShape3dPartStyle(nativeHandle, layer, part, rgba, image) }
    /** Partes no cabeçote (Engine::kShapePartFloats cada). Devolve os floats precisos. */
    fun queryShape3dParts(layer: Long, out: FloatArray): Int = nativeQueryShape3dParts(nativeHandle, layer, out)
    fun setShape3dPart(layer: Long, part: Int, values: FloatArray, mask: Int, continuing: Boolean): Boolean =
        nativeSetShape3dPart(nativeHandle, layer, part, values, mask, continuing)
    fun toggleShape3dPartKey(layer: Long, part: Int): Int = nativeToggleShape3dPartKey(nativeHandle, layer, part)
    fun resetShape3dPart(layer: Long, part: Int): Boolean = nativeResetShape3dPart(nativeHandle, layer, part)
    fun queryShape3dPartGizmo(layer: Long, part: Int, length: Float, out: FloatArray, localSpace: Boolean): Boolean =
        nativeQueryShape3dPartGizmo(nativeHandle, layer, part, length, out, localSpace)
    fun shape3dPartMove(layer: Long, part: Int, axis: Int, amount: Float, out: FloatArray): Boolean =
        nativeShape3dPartMove(nativeHandle, layer, part, axis, amount, out)
    /**
     * Divide o cubo em [count] fatias (2..16) no eixo [axis] (0 X, 1 Y, 2 Z): a camada vira
     * um nulo 3D com as fatias filhas (Engine::split_shape3d). Id do nulo, ou −Errc.
     */
    fun splitShape3d(layer: Long, axis: Int, count: Int): Long = nativeSplitShape3d(nativeHandle, layer, axis, count)
    fun applyParticlePreset(layer: Long, preset: Int): Boolean = nativeApplyParticlePreset(nativeHandle, layer, preset)
    fun setParticleParam(layer: Long, param: Int, value: Float): Boolean = nativeSetParticleParam(nativeHandle, layer, param, value)
    fun queryParticles(layer: Long, out: FloatArray): Boolean = nativeQueryParticles(nativeHandle, layer, out)
    /** 8.2: camada que emite (Camada/Texto/Caminho/Malha); 0 = nenhuma. */
    fun setParticleSource(layer: Long, source: Long): Boolean = nativeSetParticleSource(nativeHandle, layer, source)
    /** 8.2: imagem da partícula — camada de imagem (ou asset de imagem); 0 = nenhuma. */
    fun setParticleTexture(layer: Long, image: Long): Boolean = nativeSetParticleTexture(nativeHandle, layer, image)
    /** 8.2: modelo 3D da partícula de malha (camada de modelo); 0 = nenhum. */
    fun setParticleMesh(layer: Long, model: Long): Boolean = nativeSetParticleMesh(nativeHandle, layer, model)
    /** 8.2: curva ao longo da vida (0 cor: pos,r,g,b; 1 tamanho e 2 opacidade: pos,valor). */
    fun setParticleLifeCurve(layer: Long, kind: Int, values: FloatArray, count: Int): Boolean =
        nativeSetParticleLifeCurve(nativeHandle, layer, kind, values, count)
    /** {fonte, camada da textura, malha, asset da textura}. */
    fun queryParticleLinks(layer: Long, out: LongArray): Boolean = nativeQueryParticleLinks(nativeHandle, layer, out)
    /** Pontos da curva `kind` em `out`; devolve quantos. */
    fun queryParticleCurve(layer: Long, kind: Int, out: FloatArray): Int = nativeQueryParticleCurve(nativeHandle, layer, kind, out)

    // Remapeamento de tempo / rampas.
    fun setTimeRemap(layer: Long, on: Boolean): Boolean = nativeSetTimeRemap(nativeHandle, layer, on)
    fun setTimeRemapValue(layer: Long, frame: Long, value: Float): Boolean = nativeSetTimeRemapValue(nativeHandle, layer, frame, value)
    fun applySpeedRamp(layer: Long, preset: Int): Boolean = nativeApplySpeedRamp(nativeHandle, layer, preset)

    // Desfoque de movimento.
    fun setMotionBlur(layer: Long, on: Boolean): Boolean = nativeSetMotionBlur(nativeHandle, layer, on)

    // Organização e papel da camada (7H).
    fun setLayerAdjustment(layer: Long, on: Boolean): Boolean = nativeSetLayerAdjustment(nativeHandle, layer, on)
    fun setLayerGuide(layer: Long, on: Boolean): Boolean = nativeSetLayerGuide(nativeHandle, layer, on)
    fun setLayerLabel(layer: Long, label: Int): Boolean = nativeSetLayerLabel(nativeHandle, layer, label)
    /**
     * Máscara de recorte: a camada só aparece onde a de baixo (ordem de desenho)
     * tem alfa. Criar exige `LayerRow.clipCandidate`; liberar solta também a
     * cadeia recortada logo acima. Um passo de desfazer.
     */
    fun setLayerClipToBelow(layer: Long, on: Boolean): Boolean = nativeSetLayerClipToBelow(nativeHandle, layer, on)
    fun setLayerSolo(layer: Long, on: Boolean): Boolean = nativeSetLayerSolo(nativeHandle, layer, on)
    /** Camadas cujo nome/texto contém [query] (sem maiúscula nem acento), da frente para o fundo. */
    fun searchLayers(query: String): LongArray = nativeSearchLayers(nativeHandle, query)
    fun setFrameBlend(layer: Long, mode: Int): Boolean = nativeSetFrameBlend(nativeHandle, layer, mode)
    fun setVectorBlur(layer: Long, amount: Float): Boolean = nativeSetVectorBlur(nativeHandle, layer, amount)

    // Fontes.
    fun listFonts(): String? = nativeWork.run(null) { nativeListFonts(nativeHandle) }
    fun importFont(path: String): String? = nativeWork.run(null) { nativeImportFont(nativeHandle, path) }
    fun importColorLut(layer: Long, effect: Int, path: String): Int = nativeWork.run(5) { nativeImportColorLut(nativeHandle, layer, effect, path) }
    fun colorLutName(layer: Long, effect: Int): String = nativeColorLutName(nativeHandle, layer, effect)
    /** Efeito de parte 3D já apontando para a parte `part` (0 = a 1ª). −1 = não entrou. */
    fun addPartEffect(layer: Long, type: Int, part: Int): Int = nativeAddPartEffect(nativeHandle, layer, type, part)
    /** Imagem do "Trocar textura 3D" (arquivo já copiado para o app). */
    fun setEffectImage(layer: Long, effect: Int, path: String): Boolean = nativeWork.run(false) { nativeSetEffectImage(nativeHandle, layer, effect, path) }
    fun effectImageName(layer: Long, effect: Int): String = nativeEffectImageName(nativeHandle, layer, effect)
    /** Nome de cada parte da camada 3D (vazio = sem nome no arquivo). */
    fun modelPartNames(layer: Long): Array<String> = nativeModelPartNames(nativeHandle, layer) ?: emptyArray()
    fun setTextFont(layer: Long, family: String, weight: Int, italic: Boolean, path: String): Boolean =
        nativeSetTextFont(nativeHandle, layer, family, weight, italic, path)
    fun textFont(layer: Long): String? = nativeTextFont(nativeHandle, layer)
    fun setTextStyle(layer: Long, v: FloatArray): Boolean = nativeSetTextStyle(nativeHandle, layer, v)
    fun queryTextStyle(layer: Long, out: FloatArray): Boolean = nativeQueryTextStyle(nativeHandle, layer, out)
    fun setTextSpan(layer: Long, start: Int, end: Int, hasColor: Boolean, r: Float, g: Float, b: Float, weight: Int, scale: Float): Boolean =
        nativeSetTextSpan(nativeHandle, layer, start, end, hasColor, r, g, b, weight, scale)
    fun clearTextSpans(layer: Long, start: Int, end: Int): Boolean = nativeClearTextSpans(nativeHandle, layer, start, end)
    fun queryTextAnimators(layer: Long): FloatArray? = nativeQueryTextAnimators(nativeHandle, layer)
    fun layerMediaPath(layer: Long): String? = nativeLayerMediaPath(nativeHandle, layer)

    /** "Substituir mídia": troca a fonte da camada de vídeo/imagem por um vídeo. id ou −código. */
    fun replaceLayerVideo(layer: Long, source: String, name: String): Long =
        nativeWork.run(-5L) { nativeReplaceLayerVideo(nativeHandle, layer, source, name) }
    /** "Substituir mídia" por uma foto (RGBA8 em buffer direto, como [importImage]). */
    fun replaceLayerImage(layer: Long, rgba: ByteBuffer, width: Int, height: Int, name: String, source: String,
                          orientation: Int = 1): Long =
        nativeWork.run(-5L) { nativeReplaceLayerImage(nativeHandle, layer, rgba, width, height, name, source, orientation) }
    /** Arquivo de origem da camada de vídeo, áudio ou imagem (caminho ou URI); null = nenhum. */
    fun layerSourcePath(layer: Long): String? = nativeWork.run(null) { nativeLayerSourcePath(nativeHandle, layer) }
    /** Mídias de um `.aurea` fechado: 4 strings por mídia (gravado, legível, nome, tipo). null = não abre. */
    fun projectFileMedia(path: String): Array<String>? = nativeWork.run(null) { nativeProjectFileMedia(nativeHandle, path) }
    /** Arquivo do projeto: [erro (0 = ok), incluídas, puladas]. `media` = 3 strings por mídia. */
    fun exportProjectPackage(project: String, out: String, title: String, appVersion: String, media: Array<String>): IntArray =
        nativeExportProjectPackage(project, out, title, appVersion, media) ?: intArrayOf(10, 0, 0)   // 10 = Errc::IoError
    /** [erro (0 = ok), título, versão do app, religadas, ausentes]. */
    fun importProjectPackage(pkg: String, projectOut: String, mediaDir: String): Array<String> =
        nativeImportProjectPackage(pkg, projectOut, mediaDir) ?: arrayOf("10", "", "", "0", "0")
    fun extractModelArchive(archive: String, directory: String): Array<String>? = nativeExtractModelArchive(archive, directory)
    fun createCaptions(layer: Long, texts: Array<String>, times: DoubleArray, ints: IntArray, floats: FloatArray): Int =
        nativeWork.run(-5) { nativeCreateCaptions(nativeHandle, layer, texts, times, ints, floats) }
    fun removeCaptions(layer: Long): Int = nativeRemoveCaptions(nativeHandle, layer)
    fun captionCount(layer: Long): Int = nativeCaptionCount(nativeHandle, layer)
    fun parseSrt(srt: String): String? = nativeParseSrt(srt)
    fun transcribeLocal(layer: Long, model: String, language: String, translateEnglish: Boolean = false): String =
        nativeWork.run("") { nativeTranscribeLocal(nativeHandle, layer, model.toByteArray(Charsets.UTF_8), language.toByteArray(Charsets.UTF_8), translateEnglish).toString(Charsets.UTF_8) }
    fun captionProgress(cancel: Boolean = false): Int = nativeWork.run(0) { nativeCaptionProgress(nativeHandle, cancel) }
    fun captionTracks(): String = nativeCaptionTracks(nativeHandle).toString(Charsets.UTF_8)
    fun saveCaptionBundle(layer: Long, name: String): String = nativeSaveCaptionBundle(nativeHandle, layer, name.toByteArray(Charsets.UTF_8)).toString(Charsets.UTF_8)
    fun applyCaptionBundle(layer: Long, data: String): Boolean = nativeApplyCaptionBundle(nativeHandle, layer, data.toByteArray(Charsets.UTF_8))
    private external fun nativeSaveCaptionBundle(handle: Long, layer: Long, name: ByteArray): ByteArray
    private external fun nativeApplyCaptionBundle(handle: Long, layer: Long, data: ByteArray): Boolean
    fun editCaptionTrack(layer: Long, command: String): Boolean = nativeEditCaptionTrack(nativeHandle, layer, command.toByteArray(Charsets.UTF_8))
    private external fun nativeCaptionTracks(handle: Long): ByteArray
    private external fun nativeEditCaptionTrack(handle: Long, layer: Long, command: ByteArray): Boolean
    private external fun nativeTranscribeLocal(handle: Long, layer: Long, model: ByteArray, language: ByteArray, translateEnglish: Boolean): ByteArray
    private external fun nativeCaptionProgress(handle: Long, cancel: Boolean): Int
    fun isFillerWord(word: String): Boolean = nativeIsFillerWord(word)
    fun addTextAnimator(layer: Long, props: Int): Int = nativeAddTextAnimator(nativeHandle, layer, props)
    fun removeTextAnimator(layer: Long, index: Int): Boolean = nativeRemoveTextAnimator(nativeHandle, layer, index)
    fun duplicateTextAnimator(layer: Long, index: Int): Int = nativeDuplicateTextAnimator(nativeHandle, layer, index)
    fun moveTextAnimator(layer: Long, from: Int, to: Int): Boolean = nativeMoveTextAnimator(nativeHandle, layer, from, to)
    fun setTextAnimator(layer: Long, index: Int, v: FloatArray): Boolean = nativeSetTextAnimator(nativeHandle, layer, index, v)
    fun setTextAnimParam(layer: Long, index: Int, param: Int, value: Float): Boolean = nativeSetTextAnimParam(nativeHandle, layer, index, param, value)
    fun toggleTextAnimKey(layer: Long, index: Int, param: Int): Boolean = nativeToggleTextAnimKey(nativeHandle, layer, index, param)
    fun applyTextPreset(layer: Long, preset: Int): Boolean = nativeApplyTextPreset(nativeHandle, layer, preset)

    /** Animadores de camada: 32 floats cada (ver Engine::kLayerAnimFloats). */
    fun queryLayerAnimators(layer: Long): FloatArray? = nativeQueryLayerAnimators(nativeHandle, layer)
    /** Animação de texto 3D: 3 modos (entrada, saída, loop) × 5 (preset, unidade, duração s, atraso ms, unidades). */
    fun queryText3dAnim(layer: Long): FloatArray? = nativeQueryText3dAnim(nativeHandle, layer)
    fun applyText3dAnim(layer: Long, preset: Int, mode: Int, unit: Int, durationSec: Float, staggerMs: Float): Boolean =
        nativeApplyText3dAnim(nativeHandle, layer, preset, mode, unit, durationSec, staggerMs)
    fun addLayerAnimator(layer: Long): Int = nativeAddLayerAnimator(nativeHandle, layer)
    fun removeLayerAnimator(layer: Long, index: Int): Boolean = nativeRemoveLayerAnimator(nativeHandle, layer, index)
    fun setLayerAnimator(layer: Long, index: Int, v: FloatArray): Boolean = nativeSetLayerAnimator(nativeHandle, layer, index, v)
    fun setLayerAnimParam(layer: Long, index: Int, param: Int, value: Float): Boolean = nativeSetLayerAnimParam(nativeHandle, layer, index, param, value)
    fun toggleLayerAnimKey(layer: Long, index: Int, param: Int): Boolean = nativeToggleLayerAnimKey(nativeHandle, layer, index, param)
    fun copyLayerAnimators(layer: Long): Int = nativeCopyLayerAnimators(nativeHandle, layer)
    fun pasteLayerAnimators(ids: LongArray): Int = nativePasteLayerAnimators(nativeHandle, ids)
    fun layerAnimatorClipboard(): Int = nativeLayerAnimatorClipboard(nativeHandle)
    fun setLayerMotionBlurLength(layer: Long, factor: Float): Boolean = nativeSetLayerMotionBlurLength(nativeHandle, layer, factor)
    fun queryLayerMotionBlurLength(layer: Long): Float = nativeQueryLayerMotionBlurLength(nativeHandle, layer)
    fun setAdjustmentScope(layer: Long, scope: Int): Boolean = nativeSetAdjustmentScope(nativeHandle, layer, scope)
    fun queryAdjustmentScope(layer: Long): Int = nativeQueryAdjustmentScope(nativeHandle, layer)
    /** Escopo 2 do ajuste: põe/tira a camada da lista (a lista marcada do app antigo). */
    fun setAdjustmentTarget(layer: Long, target: Long, on: Boolean): Boolean = nativeSetAdjustmentTarget(nativeHandle, layer, target, on)
    fun queryAdjustmentTargets(layer: Long): LongArray = nativeQueryAdjustmentTargets(nativeHandle, layer) ?: LongArray(0)
    /** Presets do tipo de efeito: pares (id estável, nome do motor). */
    fun effectPresets(typeId: Int): List<Pair<String, String>> =
        (nativeEffectPresets(nativeHandle, typeId) ?: emptyArray()).toList().chunked(2).filter { it.size == 2 }.map { it[0] to it[1] }
    fun applyEffectPreset(layer: Long, effectId: Int, preset: Int): Boolean = nativeApplyEffectPreset(nativeHandle, layer, effectId, preset)
    // Correção de cor no modo de edição (0.3.0): papéis 1 = Filtro, 2 = Ajustar.
    fun ensureColorGrade(layer: Long, role: Int): Int = nativeWork.run(-1) { nativeEnsureColorGrade(nativeHandle, layer, role) }
    fun applyColorLook(layer: Long, effectId: Int, look: Int): Boolean =
        nativeWork.run(false) { nativeApplyColorLook(nativeHandle, layer, effectId, look) }
    fun resetColorGrade(layer: Long, effectId: Int): Boolean = nativeWork.run(false) { nativeResetColorGrade(nativeHandle, layer, effectId) }
    /** Miniaturas dos looks em [out]; [size] = largura, altura, quantidade. */
    fun renderColorLooks(maxDim: Int, out: java.nio.ByteBuffer, size: IntArray): Boolean =
        nativeRenderColorLooks(nativeHandle, maxDim, out, size)
    fun computeScopes(maxDim: Int, histogram: IntArray, waveform: ShortArray, parade: ShortArray, vectorscope: ShortArray,
                      size: IntArray): Boolean = nativeComputeScopes(nativeHandle, maxDim, histogram, waveform, parade, vectorscope, size)
    // Roto Brush do Rotobrush IA: pontos em px da composição (x,y intercalados).
    fun rotoAddStroke(layer: Long, effectId: Int, background: Boolean, radius: Float, xy: FloatArray): Boolean =
        nativeRotoAddStroke(nativeHandle, layer, effectId, background, radius, xy)
    fun rotoUndoStroke(layer: Long, effectId: Int): Boolean = nativeRotoUndoStroke(nativeHandle, layer, effectId)
    fun rotoPropagate(layer: Long, effectId: Int): Boolean = nativeRotoPropagate(nativeHandle, layer, effectId)
    fun rotoCancel() = nativeRotoCancel(nativeHandle)
    fun rotoSetView(layer: Long, effectId: Int, mode: Int): Boolean = nativeRotoSetView(nativeHandle, layer, effectId, mode)
    /** [feitos, total, rodando, falhou, traços no quadro, traços no total] */
    /** [propagados, total, propagando, falhou, traços aqui, traços total, quadro atual pronto,
     *  quadros do clipe prontos, quadros do clipe, há traços] */
    fun rotoStatus(layer: Long, effectId: Int): LongArray =
        nativeRotoStatus(nativeHandle, layer, effectId)?.takeIf { it.size >= 10 } ?: LongArray(10)
    fun setGroupCameraPassThrough(layer: Long, on: Boolean): Boolean = nativeSetGroupCameraPassThrough(nativeHandle, layer, on)
    /** −1 = não é grupo. */
    fun queryGroupCameraPassThrough(layer: Long): Int = nativeQueryGroupCameraPassThrough(nativeHandle, layer)
    /** "Aceita luzes": a camada 2D no espaço 3D recebe as luzes da composição. */
    fun setLayerAcceptsLights(layer: Long, on: Boolean): Boolean = nativeSetLayerAcceptsLights(nativeHandle, layer, on)
    fun setContentBoundedPlayback(on: Boolean) = nativeSetContentBoundedPlayback(nativeHandle, on)
    fun queryNavigationEnd(): Long = nativeQueryNavigationEnd(nativeHandle)
    fun setLayer3D(layer: Long, on: Boolean): Boolean = nativeSetLayer3D(nativeHandle, layer, on)
    fun enableLayer3D(layer: Long): Boolean = nativeEnableLayer3D(nativeHandle, layer)
    /** −1 = camada sem a opção (câmera, luz, modelo 3D, áudio, nulo). */
    fun queryLayerAcceptsLights(layer: Long): Int = nativeQueryLayerAcceptsLights(nativeHandle, layer)
    /** Nulo = deu certo; senão o motivo da recusa. */
    fun addLayersToGroup(ids: LongArray, group: Long): String? = nativeAddLayersToGroup(nativeHandle, ids, group)
    fun removeLayerFromGroup(layer: Long): String? = nativeRemoveLayerFromGroup(nativeHandle, layer)

    // Presets (JSON do motor, formato em engine/include/aurea/project/Presets.hpp).
    /** kind 0 efeitos, 1 texto, 2 animação; parts (texto) 1 estilo, 2 animadores. Nulo = nada a salvar. */
    fun savePreset(layer: Long, kind: Int, name: String, parts: Int = 3): String? =
        nativeSavePreset(nativeHandle, layer, kind, name.toByteArray(Charsets.UTF_8), parts)?.toString(Charsets.UTF_8)
    /** Preset de efeitos com SÓ o efeito [effectId] (id da instância) e os keyframes dele. Nulo = não existe. */
    fun saveEffectPreset(layer: Long, effectId: Int, name: String): String? =
        nativeSaveEffectPreset(nativeHandle, layer, effectId, name.toByteArray(Charsets.UTF_8))?.toString(Charsets.UTF_8)
    /**
     * XML do Alight Motion (ou os bytes do pacote .zip/.amproj) → envelope JSON
     * {"preset", "name", "layer", "mapped", "skipped", "warnings", "error"}.
     * "preset" é o JSON de efeitos para [applyPreset]; vazio = nada aproveitável.
     */
    fun importAlightMotion(data: ByteArray): String =
        nativeImportAlightMotion(nativeHandle, data)?.toString(Charsets.UTF_8) ?: """{"preset":"","error":"motor indisponivel"}"""
    fun importAlightMotion(xml: String): String = importAlightMotion(xml.toByteArray(Charsets.UTF_8))
    /** Um passo de desfazer. Nulo = aplicado; senão, o motivo. `duration` > 0 estica a animação. */
    fun applyPreset(layer: Long, json: String, duration: Long = 0): String? =
        nativeApplyPreset(nativeHandle, layer, json.toByteArray(Charsets.UTF_8), duration)?.toString(Charsets.UTF_8)
    fun makeCaptionPreset(name: String, ints: IntArray, floats: FloatArray): String? =
        nativeMakeCaptionPreset(name.toByteArray(Charsets.UTF_8), ints, floats)?.toString(Charsets.UTF_8)
    fun parseCaptionPreset(json: String): FloatArray? = nativeParseCaptionPreset(json.toByteArray(Charsets.UTF_8))
    fun makeCurvePreset(name: String, interp: Int, x1: Float, y1: Float, x2: Float, y2: Float, power: Int = 1): String? =
        nativeMakeCurvePreset(name.toByteArray(Charsets.UTF_8), interp, x1, y1, x2, y2, power)?.toString(Charsets.UTF_8)
    /** [interp, x1, y1, x2, y2]; nulo = não é um preset de curva válido. */
    fun parseCurvePreset(json: String): FloatArray? = nativeParseCurvePreset(json.toByteArray(Charsets.UTF_8))
    // --- Expressões (motor: expr/Expression.hpp) ---------------------------------
    /**
     * Grava o MESMO texto em todas as [keys] num passo de desfazer (Posição =
     * X e Y); vazio remove. Nulo = camada/propriedade inválida.
     */
    fun setExpression(layer: Long, keys: List<TrackKey>, source: String): ExpressionDiag? =
        ExpressionDiag.decode(nativeSetExpression(nativeHandle, layer, packKeys(keys), source.toByteArray(Charsets.UTF_8)))
    fun setExpressionEnabled(layer: Long, keys: List<TrackKey>, enabled: Boolean): Boolean =
        nativeSetExpressionEnabled(nativeHandle, layer, packKeys(keys), enabled)
    private fun packKeys(keys: List<TrackKey>): IntArray =
        IntArray(keys.size * 3) { i -> keys[i / 3].let { k -> when (i % 3) { 0 -> k.property; 1 -> k.effectIndex; else -> k.paramIndex } } }
    /** Estado no playhead (avaliada agora: o erro de execução é o do instante visto). */
    fun queryExpression(layer: Long, key: TrackKey): ExpressionInfo? =
        ExpressionInfo.decode(nativeQueryExpression(nativeHandle, layer, key.property, key.effectIndex, key.paramIndex))
    fun queryExpressions(layer: Long): List<ExpressionRow> = ExpressionRow.decode(nativeQueryExpressions(nativeHandle, layer))
    /** Só a sintaxe, sem gravar (validação enquanto digita). */
    fun checkExpressionSyntax(source: String): ExpressionDiag =
        ExpressionDiag.decode(nativeCheckExpressionSyntax(source.toByteArray(Charsets.UTF_8))) ?: ExpressionDiag.OK
    /** PowerManager.THERMAL_STATUS_* → o preview reduz o que é caro sob calor. */
    fun setThermal(status: Int) = nativeSetThermal(nativeHandle, status)
    fun queryTimeRemap(layer: Long, out: FloatArray): Int = nativeQueryTimeRemap(nativeHandle, layer, out)
    fun timeRemapCurrentSource(layer: Long): Double = nativeWork.run(Double.NaN) { nativeQueryTimeRemapCurrentSource(nativeHandle, layer) }

    // Rastreio de câmera 3D.
    fun startMotionTrack(layer: Long, tool: Int, model: Int, backward: Boolean, points: FloatArray, feature: Float, search: Float): Boolean = nativeStartMotionTrack(nativeHandle, layer, tool, model, backward, points, feature, search)
    fun cancelMotionTrack() = nativeCancelMotionTrack(nativeHandle)
    fun restoreMotionTrack(layer: Long): Boolean = nativeRestoreMotionTrack(nativeHandle, layer)
    fun motionTrackSource(): Long = nativeMotionTrackSource(nativeHandle)
    fun motionTrackStatus(out: FloatArray): String = nativeMotionTrackStatus(nativeHandle, out) ?: ""
    fun applyMotionTrack(target: Long, apply: Int, lock: Boolean, smooth: Float, maxScale: Float, crop: Int): Long = nativeApplyMotionTrack(nativeHandle, target, apply, lock, smooth, maxScale, crop)
    private external fun nativeStartMotionTrack(handle: Long, layer: Long, tool: Int, model: Int, backward: Boolean, points: FloatArray, feature: Float, search: Float): Boolean
    private external fun nativeCancelMotionTrack(handle: Long)
    private external fun nativeRestoreMotionTrack(handle: Long, layer: Long): Boolean
    private external fun nativeMotionTrackStatus(handle: Long, out: FloatArray): String?
    private external fun nativeMotionTrackSource(handle: Long): Long
    private external fun nativeApplyMotionTrack(handle: Long, target: Long, apply: Int, lock: Boolean, smooth: Float, maxScale: Float, crop: Int): Long
    fun startCameraTrack(layer: Long, mode: Int): Boolean = nativeStartCameraTrack(nativeHandle, layer, mode)
    /** motion: 0 auto, 1 típico, 2 tripé, 3 cena quase plana; shotType: 0 FOV fixa, 1 zoom variável,
     *  2 FOV informada (knownFov > 0); lens: 1 = estimar a distorção radial (k1; k2 se estável). */
    fun startCameraTrack(layer: Long, mode: Int, motion: Int, knownFov: Float, shotType: Int, lens: Int): Boolean =
        nativeStartCameraTrackEx(nativeHandle, layer, mode, motion, knownFov, shotType, lens)
    fun refineCameraTrack(remove: Boolean, motion: Int, fov: Float, shotType: Int, lens: Int): Boolean =
        nativeRefineCameraTrackEx(nativeHandle, remove, motion, fov, shotType, lens)
    /** Por quadro: (FOV°, erro médio em px da fonte; −1 = sem pose). Devolve quantos quadros. */
    fun cameraTrackFrameInfo(out: FloatArray): Int = nativeCameraTrackFrameInfo(nativeHandle, out)
    /** "Distorção da lente" com o k1/k2 do solve: id do efeito, ou negativo (código do erro). */
    fun addCameraTrackLensEffect(layer: Long, redistort: Boolean): Long = nativeAddCameraTrackLensEffect(nativeHandle, layer, redistort)
    fun cancelCameraTrack() = nativeCancelCameraTrack(nativeHandle)
    fun refineCameraTrack(remove: Boolean, motion: Int, fov: Float): Boolean = nativeRefineCameraTrack(nativeHandle, remove, motion, fov)
    fun cameraTrackTarget(frame: Long, out: FloatArray): Int = nativeCameraTrackTarget(nativeHandle, frame, out)
    fun calibrateCameraScene(operation: Int, distance: Float): Boolean = nativeCalibrateCameraScene(nativeHandle, operation, distance)
    fun placeModelOnTrack(layer: Long): Boolean = nativePlaceModelOnTrack(nativeHandle, layer)
    private external fun nativeCameraTrackTarget(handle: Long, frame: Long, out: FloatArray): Int
    private external fun nativeCalibrateCameraScene(handle: Long, operation: Int, distance: Float): Boolean
    private external fun nativePlaceModelOnTrack(handle: Long, layer: Long): Boolean
    private external fun nativeRefineCameraTrack(handle: Long, remove: Boolean, motion: Int, fov: Float): Boolean
    /** out[0..10]: estado, progresso, quadros, resolvidos, rastros, pontos, erro px (análise), confiança, FOV°,
     *  só rotação, do cache. Com out.size >= 22, também out[11..21]: tipo de tomada, método, FOV inicial°,
     *  FOV final°, FOV mín°, FOV máx°, k1, k2, lente resolvida (0/1), k2 resolvido (0/1), erro médio em px da fonte. */
    fun cameraTrackStatus(out: FloatArray): String? = nativeCameraTrackStatus(nativeHandle, out)
    fun applyCameraTrack(frame: Long = -1, rect: FloatArray? = null): Long = nativeApplyCameraTrack(nativeHandle, frame, rect?.get(0) ?: 0f, rect?.get(1) ?: 0f, rect?.get(2) ?: 0f, rect?.get(3) ?: 0f)
    fun cameraTrackFeatures(frame: Long, out: FloatArray): Int = nativeCameraTrackFeatures(nativeHandle, frame, out)
    fun restoreCameraTrack(layer: Long): Boolean = nativeRestoreCameraTrack(nativeHandle, layer)
    fun cameraTrackDetails(frame: Long, out: FloatArray): Int = nativeCameraTrackDetails(nativeHandle, frame, out)
    fun selectCameraTrackPoints(ids: IntArray, operation: Int = 0): Int = nativeSelectCameraTrackPoints(nativeHandle, ids, operation)
    fun createCameraTrackObject(kind: Int): Long = nativeCreateCameraTrackObject(nativeHandle, kind)
    fun editTimeRemapKey(layer: Long, index: Int, frame: Long, value: Float, interp: Int): Int =
        nativeEditTimeRemapKey(nativeHandle, layer, index, frame, value, interp)
    fun removeTimeRemapKey(layer: Long, index: Int): Boolean = nativeRemoveTimeRemapKey(nativeHandle, layer, index)
    /** Gráfico de velocidade (`query_speed_curve`): cabeçalho de 8, amostras e chaves (tempo, ×, interpolação). */
    fun querySpeedCurve(layer: Long, samples: Int, out: FloatArray): Int = nativeQuerySpeedCurve(nativeHandle, layer, samples, out)
    /** Curva pronta: 0 plana, 1 montagem, 2 herói, 3 bala, 4 entrada rápida, 5 saída rápida, 6 pulo, 7 suavizar, 8 tirar. */
    fun applySpeedCurve(layer: Long, preset: Int): Boolean = nativeApplySpeedCurve(nativeHandle, layer, preset)
    fun editSpeedKey(layer: Long, index: Int, frame: Long, speed: Float, interp: Int): Int =
        nativeEditSpeedKey(nativeHandle, layer, index, frame, speed, interp)
    fun removeSpeedKey(layer: Long, index: Int): Boolean = nativeRemoveSpeedKey(nativeHandle, layer, index)
    /** Câmeras do arquivo do modelo 3D (glTF/FBX); a importação já as põe na timeline. */
    fun modelCameraCount(layer: Long): Int = nativeModelCameraCount(nativeHandle, layer)
    /** Ossos posáveis do modelo (`query_model_bones`, BONE_FLOATS por osso); array curto devolve o tamanho pedido. */
    fun queryModelBones(layer: Long, out: FloatArray?): Int = nativeQueryModelBones(nativeHandle, layer, out)
    /** Dedo na cena 3D: gira o osso como trackball (px da composição; twist em graus em volta do olhar). */
    fun dragBone(layer: Long, node: Int, dx: Float, dy: Float, twist: Float, continuing: Boolean): Boolean =
        nativeDragBone(nativeHandle, layer, node, dx, dy, twist, continuing)
    /** Keyframe do osso no cabeçote (node −1 = todos): 1 marcou, 0 tirou, −1 fora da camada. */
    fun toggleBoneKey(layer: Long, node: Int): Int = nativeToggleBoneKey(nativeHandle, layer, node)
    fun resetBonePose(layer: Long, node: Int): Boolean = nativeResetBonePose(nativeHandle, layer, node)
    fun addModelCameras(layer: Long): Int = nativeAddModelCameras(nativeHandle, layer)
    /** Ao contrário: espelha a curva de tempo no clipe (liga o remapeamento se preciso). */
    fun reverseTimeRemap(layer: Long): Boolean = nativeReverseTimeRemap(nativeHandle, layer)
    fun setKeepPitch(layer: Long, on: Boolean): Boolean = nativeSetKeepPitch(nativeHandle, layer, on)
    fun setCompositionMotionBlur(on: Boolean) = nativeSetCompositionMotionBlur(nativeHandle, on)
    fun setShutterAngle(degrees: Float) = nativeSetShutterAngle(nativeHandle, degrees)
    /** > 0 = ligado; |valor| − 1 = obturador em graus; 0 = sem projeto. */
    fun motionBlurState(): Float = nativeMotionBlurState(nativeHandle)
    fun queryMotionBlurSettings(): MotionBlurSettings? = nativeWork.run<MotionBlurSettings?>(null) {
        val values = FloatArray(6)
        if (nativeQueryMotionBlurSettings(nativeHandle, values)) MotionBlurSettings.fromNative(values) else null
    }
    fun setMotionBlurSettings(settings: MotionBlurSettings): Boolean = nativeWork.run(false) {
        nativeSetMotionBlurSettings(nativeHandle, settings.enabled, settings.angle, settings.phase,
            settings.samples, settings.adaptiveLimit)
    }

    /** Modo Edição (timeline magnética) da composição atual. */
    fun setEditMode(on: Boolean) = nativeSetEditMode(nativeHandle, on)
    fun editMode(): Boolean = nativeEditMode(nativeHandle)
    /** Interface do projeto (0.2.5): 0 = Composição (padrão), 1 = Edição. Ligar a Edição liga o ímã. */
    fun setProjectUiMode(mode: Int): Boolean = nativeSetProjectUiMode(nativeHandle, mode)
    fun projectUiMode(): Int = nativeProjectUiMode(nativeHandle)
    /** O trecho de mídia mais fundo: a linha dele é a trilha principal da Edição (0 = nenhum). */
    fun mainTrackAnchor(): Long = nativeMainTrackAnchor(nativeHandle)
    /** "Para camada": linha própria acima da trilha principal; [atFrame] >= 0 leva ao quadro. Um passo de desfazer. */
    fun moveClipToOverlay(layer: Long, atFrame: Long = -1): Boolean = nativeMoveClipToOverlay(nativeHandle, layer, atFrame)
    /** "Para trilha principal": entra no fim dela, magnético. Um passo de desfazer. */
    fun moveClipToMainTrack(layer: Long): Boolean = nativeMoveClipToMainTrack(nativeHandle, layer)
    /** Transição num corte: saída de [outgoing] e entrada de [incoming] (0 nenhuma … 5 girar). Um passo de desfazer. */
    fun setCutTransition(outgoing: Long, incoming: Long, type: Int, frames: Int): Boolean =
        nativeSetCutTransition(nativeHandle, outgoing, incoming, type, frames)
    fun cutTransition(incoming: Long): Int = nativeCutTransition(nativeHandle, incoming)
    /** Exclui e fecha só os buracos criados (um passo de desfazer). */
    fun rippleDelete(ids: LongArray): Boolean = nativeRippleDelete(nativeHandle, ids)
    /** Fecha todos os espaços vazios. Devolve os frames removidos. */
    fun removeGaps(): Long = nativeRemoveGaps(nativeHandle)
    /** 0/1 trim absoluto, 2 slip, 3/4 roll, 5 slide; 6 move ao quadro absoluto. */
    fun editClipTime(layer: Long, operation: Int, amount: Long, previous: Long = 0, next: Long = 0): Boolean =
        nativeEditClipTime(nativeHandle, layer, operation, amount, previous, next)
    fun queryClipTimeActions(layer: Long, frame: Long): Int = nativeQueryClipTimeActions(nativeHandle, layer, frame)
    fun trimComposition(frame: Long): Boolean = nativeTrimComposition(nativeHandle, frame)

    /**
     * LINHA MAGNÉTICA da camada: os cortes dela viram uma faixa de montagem de
     * vídeo (aparar e apagar puxam os vizinhos DA MESMA linha; quem está em
     * outra linha não anda). Nasce no estado do modo Edição da composição.
     */
    fun setLayerMagneticTrack(layer: Long, on: Boolean): Boolean = nativeSetLayerMagneticTrack(nativeHandle, layer, on)
    fun layerMagneticTrack(layer: Long): Boolean = nativeLayerMagneticTrack(nativeHandle, layer)
    /** Arrasta o trecho para outro ponto da mesma linha, com reordenação. */
    fun reorderClip(layer: Long, targetFrame: Long): Boolean = nativeReorderClip(nativeHandle, layer, targetFrame)
    /**
     * Arrasto vertical de UM trecho (só ele anda). [mode] 0 = fileira própria logo
     * acima da fileira de [anchor] (0 = no fundo); 1 = entrar na linha de [anchor]
     * se couber no tempo (senão fileira própria ali). Um passo de desfazer.
     */
    fun moveLayerToRow(layer: Long, anchor: Long, mode: Int): Boolean = nativeMoveLayerToRow(nativeHandle, layer, anchor, mode)

    /** Liga/desliga a marca no frame. true = ficou marcada. */
    fun toggleMarker(frame: Long): Boolean = nativeToggleMarker(nativeHandle, frame)
    /** Tap de batida tocando: marca o instante que soa, sem pausar nem alternar. Quadro ou -1. */
    fun markBeatLive(): Long = nativeMarkBeatLive(nativeHandle)
    fun moveMarker(from: Long, to: Long): Boolean = nativeMoveMarker(nativeHandle, from, to)
    fun editMarker(from: Long, to: Long, color: Int, label: String): Boolean =
        nativeEditMarker(nativeHandle, from, to, color, label.toByteArray(Charsets.UTF_8))
    fun deleteMarker(frame: Long): Boolean = nativeDeleteMarker(nativeHandle, frame)
    fun markerLabel(frame: Long): String = nativeMarkerLabel(nativeHandle, frame).toString(Charsets.UTF_8)
    /** Marcas: frame, cor, tipo (3 longs cada). Devolve o total. */
    fun queryMarkers(out: LongArray): Int = nativeQueryMarkers(nativeHandle, out)
    /** Síncrono (decodifica o som): fora da thread de UI. Nº de batidas ou −Errc. */
    fun detectBeats(layer: Long, bpm: DoubleArray): Long = nativeWork.run(-5L) { nativeDetectBeats(nativeHandle, layer, bpm) }

    /** Nulo 2D ou 3D no centro. Id ≥ 0 ou −Errc. */
    fun playbackReport(): String = nativePlaybackReport(nativeHandle)
    /** Position actually presented by the audio output, for playback diagnostics. */
    fun audioPositionNs(): Long = nativeAudioPositionNs(nativeHandle)
    private external fun nativeAudioPositionNs(handle: Long): Long
    private external fun nativePlaybackReport(handle: Long): String
    fun setRawPlayback(enabled: Boolean): Boolean = nativeSetRawPlayback(nativeHandle, enabled)
    private external fun nativeSetRawPlayback(handle: Long, enabled: Boolean): Boolean
    fun setSceneEditor(enabled: Boolean, yaw: Float, pitch: Float, distance: Float) = nativeSetSceneEditor(nativeHandle, enabled, yaw, pitch, distance)
    fun sceneGuides(output: FloatArray): Int = nativeSceneGuides(nativeHandle, output)
    /** Cena 3D: camada 3D sob o ponto (px da composição) pelo corpo real; 0 = nada. */
    fun scenePick(x: Float, y: Float, radius: Float): Long = nativeScenePick(nativeHandle, x, y, radius)
    fun layoutTransform(layer: Long, property: Int, value: Float): Boolean = nativeLayoutTransform(nativeHandle, layer, property, value)
    fun addLight(kind: Int): Long = nativeAddLight(nativeHandle, kind)
    fun lightInfo(layer: Long): FloatArray? = FloatArray(11).takeIf { nativeLightInfo(nativeHandle, layer, it) }
    fun setLightParam(layer: Long, param: Int, value: Float): Boolean = nativeSetLightParam(nativeHandle, layer, param, value)
    // Real Camera rules, recording state and key creation live in the shared engine.
    fun realCameraStart(layer: Long): Boolean = nativeWork.run(false) { nativeRealCameraStart(nativeHandle, layer) }
    fun realCameraRecord(record: Boolean): Boolean = nativeWork.run(false) { nativeRealCameraRecord(nativeHandle, record) }
    fun realCameraSample(pose: FloatArray, timestampNs: Long, positional: Boolean, sensitivity: Float, inverted: Boolean): Boolean =
        nativeWork.run(false) { nativeRealCameraSample(nativeHandle, pose, timestampNs, positional, sensitivity, inverted) }
    fun realCameraHold(): Boolean = nativeWork.run(false) { nativeRealCameraHold(nativeHandle) }
    fun realCameraRecenter(): Boolean = nativeWork.run(false) { nativeRealCameraRecenter(nativeHandle) }
    fun realCameraStop() = nativeWork.run(Unit) { nativeRealCameraStop(nativeHandle) }
    /** [active, recording, last local frame, positional] from the native session. */
    fun realCameraStatus(): LongArray = nativeWork.run(longArrayOf(0, 0, -1, 0)) { nativeRealCameraStatus(nativeHandle) }
    private external fun nativeRealCameraStart(handle: Long, layer: Long): Boolean
    private external fun nativeRealCameraRecord(handle: Long, record: Boolean): Boolean
    private external fun nativeRealCameraSample(handle: Long, pose: FloatArray, timestampNs: Long, positional: Boolean, sensitivity: Float, inverted: Boolean): Boolean
    private external fun nativeRealCameraHold(handle: Long): Boolean
    private external fun nativeRealCameraRecenter(handle: Long): Boolean
    private external fun nativeRealCameraStop(handle: Long)
    private external fun nativeRealCameraStatus(handle: Long): LongArray

    fun addCamera(): Long = nativeAddCamera(nativeHandle)
    /** Lente da câmera 3D (10 valores, ver `nativeQueryCameraLens`; [9] = afeta camadas abaixo); null se não é câmera. */
    fun cameraLens(layer: Long): FloatArray? = FloatArray(10).takeIf { nativeQueryCameraLens(nativeHandle, layer, it) }
    /** Pick Focus: distância no eixo ótico até o 3D sob o ponto (px da composição); < 0 = nada ali. */
    fun pickFocusDistance(layer: Long, compX: Float, compY: Float): Float = nativePickFocusDistance(nativeHandle, layer, compX, compY)
    /** LayerSetCameraParam: 0 mm, 1 DOF, 2 distância de foco, 3 f/, 4 desfoque ×. */
    fun setCameraParam(layer: Long, param: Int, value: Float): Boolean = nativeSetCameraParam(nativeHandle, layer, param, value)
    fun addNull(threeD: Boolean): Long = nativeAddNull(nativeHandle, threeD)
    /** "Vincular a novo nulo": nulo no centro das camadas, pai de todas (um desfazer). Id ≥ 0 ou −Errc. */
    fun parentToNewNull(ids: LongArray): Long = nativeParentToNewNull(nativeHandle, ids)
    /** "Escalonar": cascata de `stepFrames` na ordem de `ids` (a primeira fica). Camadas que andaram ≥ 0 ou −Errc. */
    fun staggerLayers(ids: LongArray, stepFrames: Int, keysOnly: Boolean): Int = nativeStaggerLayers(nativeHandle, ids, stepFrames, keysOnly)
    fun arrangeLayerTimes(ids: LongArray, mode: Int, playhead: Long): Int = nativeArrangeLayerTimes(nativeHandle, ids, mode, playhead)

    /** Congela o quadro do clipe no `frame` por `holdFrames`; o resto anda. Id ≥ 0 ou −Errc. */
    fun freezeFrame(layer: Long, frame: Int, holdFrames: Int): Long = nativeFreezeFrame(nativeHandle, layer, frame, holdFrames)

    /** Waveform: `count` baldes (u8, compansão raiz) a partir de `startFrame`. 0 = sem som. */
    fun queryWaveform(layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: ByteBuffer): Int =
        nativeWork.run(0) { nativeQueryWaveform(nativeHandle, layer, startFrame, framesPerBucket, count, out) }

    /**
     * RGBA8 sRGB (alfa reto) num buffer direto de `width * height * 4` bytes.
     * [orientation] = EXIF 1..8 dos pixels CRUS: o motor gira e reduz o que não
     * couber (ImageResample.hpp) em vez de recusar a foto.
     */
    fun importImage(rgba: ByteBuffer, width: Int, height: Int, name: String, source: String, orientation: Int = 1): Long =
        nativeWork.run(-5L) { nativeImportImage(nativeHandle, rgba, width, height, name, source, orientation) }

    fun newProject(width: Int, height: Int, fps: Float, title: String): Boolean =
        newProject(width, height, fps.toDouble(), title, null)

    /**
     * fps livre (1–240, decimais como 29,97; o motor encaixa a razão NTSC) e,
     * opcional, o fundo da composição em RGBA sRGB (nulo = preto).
     */
    fun newProject(width: Int, height: Int, fps: Double, title: String, background: FloatArray?): Boolean =
        nativeWork.run(false) { nativeNewProject(nativeHandle, width, height, fps, title, background) }

    fun loadProject(path: String): Int = nativeWork.run(5) { nativeLoadProject(nativeHandle, path) }
    fun saveProject(path: String): Int = nativeWork.run(5) { nativeSaveProject(nativeHandle, path) }
    fun autosaveProject(): Int = nativeWork.run(5) { nativeSaveProject(nativeHandle, null) }
    /** Sair do app: grava qualquer mudança (fila drenada). [SAVE_CLEAN] = nada a gravar. */
    fun saveProjectIfDirty(): Int = nativeWork.run(5) { nativeSaveProjectIfDirty(nativeHandle) }
    /**
     * O que a última abertura precisou fazer (Engine::LoadNotice): bits 0–15 =
     * 1 abriu da cópia (.bak/.tmp), 2 parcial, 4 formato antigo (cópia
     * guardada), 8 mídia ausente; bits 16+ = quantos assets faltaram.
     */
    fun loadNotice(): Int = nativeLoadNotice(nativeHandle)
    fun discardRecovery(): Int = nativeDiscardRecovery(nativeHandle)
    fun recoverSession(): Int = nativeRecoverSession(nativeHandle)

    /**
     * Exporta a composição atual para MP4. `shortSide` = lado menor (720…2160),
     * `fps` 0 = o da composição, `codec` 0 = H.264 / 1 = HEVC, `bitrateMbps` 0 =
     * automático. `safeMode` = o modo de segurança que o motor sugeriu depois de
     * o encoder travar ([ExportProgress.retrySafeMode]; 0 = o pedido). Devolve o
     * código de erro do motor (0 = começou).
     */
    fun startExport(outputPath: String, shortSide: Int, fps: Double, codec: Int, bitrateMbps: Int, aiUpscale: Int = 0, trimToContent: Boolean = false,
                    quality: Int = 1, rateMode: Int = 1, safeMode: Int = 0): Int =
        nativeWork.run(5) { nativeStartExport(nativeHandle, outputPath, shortSide, fps, codec, bitrateMbps, aiUpscale, trimToContent, quality, rateMode, safeMode) }

    fun exportDuration(trimToContent: Boolean = true): Long = nativeExportDuration(nativeHandle, trimToContent)
    fun exportUsesMotionBlur(): Boolean = nativeExportUsesMotionBlur(nativeHandle)

    /** Native startup may freeze a document; call from the lifecycle worker. */
    fun startPreRender(direction: Int): Int =
        nativeWork.run(5) { nativeStartPreRender(nativeHandle, if (direction < 0) -1 else 1) }

    fun cancelPreRender() = nativeWork.run(Unit) { nativeCancelPreRender(nativeHandle) }

    /** Atomic counters only: no file access or GPU wait. */
    fun preRenderProgress(): PreRenderProgress =
        PreRenderProgress.from(nativeWork.run(longArrayOf()) { nativePreRenderProgress(nativeHandle) })

    /**
     * Export como imagem (motor: export/ImageEncode.hpp). `format` 0 = quadro do
     * playhead em PNG, 1 = sequência PNG num .zip, 2 = GIF. `shortSide` 0 = a
     * resolução da composição; `maxWidth` = largura máxima do GIF; `fps` 0 =
     * padrão do formato. Progresso e cancelamento são os do vídeo.
     */
    fun startImageExport(outputPath: String, format: Int, shortSide: Int, maxWidth: Int, fps: Double, trimToContent: Boolean): Int =
        nativeWork.run(5) { nativeStartImageExport(nativeHandle, outputPath, format, shortSide, maxWidth, fps, trimToContent) }

    /** [largura, altura, quadros, alfa, bytes estimados, fps × 1000] pela regra do motor; nulo sem composição. */
    fun imageExportPlan(format: Int, shortSide: Int, maxWidth: Int, fps: Double, trimToContent: Boolean): LongArray? =
        nativeImageExportPlan(nativeHandle, format, shortSide, maxWidth, fps, trimToContent)
    fun cancelExport(): Int = nativeWork.run(5) { nativeCancelExport(nativeHandle) }
    /** Explicit recovery restores the frozen project and restarts at frame zero. */
    fun restartExport(recoveryPath: String, outputPath: String): Int =
        nativeWork.run(5) { nativeRestartExport(nativeHandle, recoveryPath, outputPath) }


    /**
     * Importa um glTF/GLB (arquivo no sandbox do app). Bloqueia: chamar fora da
     * UI. Devolve o id da layer, ou −código de erro; `detail[0]` recebe o
     * motivo (falha) ou os avisos do import (sucesso).
     */
    fun importModel(
        path: String,
        name: String,
        detail: Array<String?>,
        quality: Int = MODEL_QUALITY_ORIGINAL,
        memory: LongArray? = null,
    ): Long = nativeWork.run(-5L) { nativeImportModel(nativeHandle, path, name, detail, quality, memory) }

    /**
     * "Otimizar modelo": custo do arquivo (só cabeçalhos/contagens) e o que cabe
     * neste aparelho por qualidade, ANTES do import. `memory` = [totalMem,
     * availMem, isLowRamDevice] medidos agora (ver [ModelPlan.memoryNow]).
     */
    fun inspectModel(path: String, memory: LongArray?): ModelPlan = ModelPlan(nativeWork.run(longArrayOf()) { nativeInspectModel(nativeHandle, path, memory) })

    /** O último import de modelo: triângulos do arquivo → os que ficaram. */
    fun lastModelImport(): LongArray = nativeLastModelImport(nativeHandle)
    /** Etapa × 1000 + fração × 1000 (ImportPhase do motor). */
    fun importModelProgress(): Int = nativeWork.run(0) { nativeImportModelProgress(nativeHandle) }
    /** Texturas (e o .mtl do OBJ) que o modelo 3D da layer referencia e não achou: só o nome do arquivo. */
    fun modelMissingTextures(layer: Long): List<String> =
        nativeModelMissingTextures(nativeHandle, layer).lines().filter { it.isNotBlank() }
    /** Pasta absoluta do arquivo do modelo (com a barra no fim); vazio = não é modelo importado. */
    fun modelFolder(layer: Long): String = nativeWork.run("") { nativeModelFolder(nativeHandle, layer) }
    /** Relê o modelo com as texturas copiadas para a pasta dele. ≥ 0 = quantas ainda faltam; < 0 = −código. Bloqueia. */
    fun reloadModelTextures(layer: Long, detail: Array<String?>): Int = nativeWork.run(-5) { nativeReloadModelTextures(nativeHandle, layer, detail) }
    fun cancelModelImport() = nativeWork.run(Unit) { nativeCancelModelImport(nativeHandle) }
    fun exportProgress(out: ByteBuffer): Boolean = nativeWork.run(false) { nativeExportProgress(nativeHandle, out) }
    fun exportDiagnostics(): String = nativeWork.run("{}") { nativeExportDiagnostics(nativeHandle) ?: "{}" }
    /** Copies absolute [start, end) pairs; returns the number of ranges, at most 30. */
    fun localAiStatus(): Int = nativeWork.run(0) { nativeLocalAiStatus(nativeHandle) }
    fun previewBufferRanges(out: LongArray): Int = nativeWork.run(0) { nativePreviewBufferRanges(nativeHandle, out) }

    // -------------------------------------------------------------------------
    // Declarações nativas (engine/platform/android/aurea_jni.cpp)
    // -------------------------------------------------------------------------
    private external fun nativeInitialize(
        handle: Long, refreshRate: Float, cacheDir: String, documentsDir: String, debug: Boolean,
        probe: LongArray?, codecs: IntArray?, pluginDir: String,
    ): Boolean
    private external fun nativeInstallPlugin(handle: Long, path: String): String
    private external fun nativeListPlugins(handle: Long): String
    private external fun nativeRemovePlugin(handle: Long, id: String): Boolean
    private external fun nativePluginDirectory(handle: Long): String
    private external fun nativeDeviceReport(handle: Long, out: LongArray): Boolean
    private external fun nativeDeviceSummary(handle: Long): String?
    private external fun nativeSetEffectPreviewSource(handle: Long, rgba: ByteArray, width: Int, height: Int): Boolean
    private external fun nativeShutdown(handle: Long)
    private external fun nativeSuspend(handle: Long)
    private external fun nativeResume(handle: Long)
    private external fun nativeTrimMemory(handle: Long, level: Int): Long
    private external fun nativeMemoryReport(handle: Long, out: LongArray): Int
    private external fun nativeInvalidate(handle: Long)
    private external fun nativeAttachSurface(handle: Long, surface: Surface, width: Int, height: Int): Boolean
    private external fun nativeDetachSurface(handle: Long)
    private external fun nativeResizeSurface(handle: Long, width: Int, height: Int)
    private external fun nativeSubmitCommands(
        handle: Long, commands: ByteBuffer, count: Int, stringBlob: ByteBuffer?, stringBlobSize: Int,
    ): Int
    private external fun nativeReadStatus(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeReadTelemetry(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeReadPerf(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeQueryLayers(
        handle: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer, blobCapacity: Int,
    ): Int
    private external fun nativeQueryTrackCurve(handle: Long, layer: Long, property: Int, effect: Int, param: Int, from: Int, to: Int, out: FloatArray): Int
    private external fun nativeQueryKeyframeEasing(handle: Long, layer: Long, property: Int, effect: Int, param: Int, time: Int, out: FloatArray): Boolean
    private external fun nativeQueryKeyframes(handle: Long, layer: Long, rows: ByteBuffer, capacity: Int): Int
    private external fun nativeQueryAllKeyframes(handle: Long, index: ByteBuffer, layerCapacity: Int, rows: ByteBuffer, capacity: Int): Long
    private external fun nativeQueryCurve(
        handle: Long, layer: Long, property: Int, from: Int, to: Int, out: FloatArray, count: Int,
    ): Int
    private external fun nativeQueryEffectCurve(handle: Long, layer: Long, effect: Int, param: Int, channel: Int, samples: Boolean, out: FloatArray): Int
    private external fun nativeEditEffectCurve(handle: Long, layer: Long, effect: Int, param: Int, channel: Int, action: Int, point: Int, x: Float, y: Float): Int
    private external fun nativeSetEffectSectionOpen(handle: Long, layer: Long, effect: Int, section: Int, open: Boolean): Boolean
    private external fun nativeQueryEffectCatalog(handle: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int
    private external fun nativeQueryEffectSpecs(
        handle: Long, typeId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer,
    ): Int
    private external fun nativeQueryLayerEffects(
        handle: Long, layer: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer,
    ): Int
    private external fun nativeQueryEffectParams(
        handle: Long, layer: Long, effectId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer,
    ): Int
    private external fun nativeQueryLayerDetail(handle: Long, layer: Long, out: ByteBuffer): Boolean
    private external fun nativeQueryComposition(handle: Long, out: DoubleArray): Long
    private external fun nativeFrameTimeNs(handle: Long, frame: Long): Long
    private external fun nativeReadOffscreenMeasure(handle: Long, out: DoubleArray): Boolean
    private external fun nativeOffscreenTimers(handle: Long, on: Boolean): Boolean
    private external fun nativeQueryThumbnail(
        handle: Long, layer: Long, frame: Int, height: Int, out: ByteBuffer, outWidth: IntArray,
    ): Int
    private external fun nativeCaptureFrame(handle: Long, maxDim: Int, out: ByteBuffer, outSize: IntArray): Int
    private external fun nativeCapturePreviewFrame(handle: Long, maxDim: Int, out: ByteBuffer, outSize: IntArray): Int
    private external fun nativeRenderEffectPreview(
        handle: Long, typeId: Int, width: Int, height: Int, out: ByteBuffer, outSize: IntArray,
    ): Boolean
    private external fun nativeRenderEffectPreviewSequence(
        handle: Long, typeId: Int, width: Int, height: Int, frames: Int, scene: Int, out: ByteBuffer, outSize: IntArray,
    ): Int
    private external fun nativeSetSelection(handle: Long, layers: LongArray)
    private external fun nativeClearSelection(handle: Long)
    private external fun nativeImportVideo(handle: Long, source: String, name: String): Long
    private external fun nativeImportAudio(handle: Long, source: String, name: String): Long
    private external fun nativeProbeImageSequence(handle: Long, absolutePaths: Array<String>): String?
    private external fun nativeImportImageSequence(
        handle: Long, absolutePaths: Array<String>, fps: Double, alphaMode: Int, colorMode: Int,
    ): Long
    private external fun nativeExtractAudio(handle: Long, layer: Long): Long
    private external fun nativeAddShape(handle: Long, preset: Int): Long
    private external fun nativeSetSceneEditor(handle: Long, enabled: Boolean, yaw: Float, pitch: Float, distance: Float)
    private external fun nativeSceneGuides(handle: Long, output: FloatArray): Int
    private external fun nativeScenePick(handle: Long, x: Float, y: Float, radius: Float): Long
    private external fun nativeLayoutTransform(handle: Long, layer: Long, property: Int, value: Float): Boolean
    private external fun nativeAddLight(handle: Long, kind: Int): Long
    private external fun nativeLightInfo(handle: Long, layer: Long, output: FloatArray): Boolean
    private external fun nativeSetLightParam(handle: Long, layer: Long, param: Int, value: Float): Boolean
    private external fun nativeQueryCameraLens(handle: Long, layer: Long, output: FloatArray): Boolean
    private external fun nativePickFocusDistance(handle: Long, layer: Long, compX: Float, compY: Float): Float
    private external fun nativeSetCameraParam(handle: Long, layer: Long, param: Int, value: Float): Boolean
    private external fun nativeAddCamera(handle: Long): Long
    private external fun nativeAddNull(handle: Long, threeD: Boolean): Long
    private external fun nativeParentToNewNull(handle: Long, ids: LongArray): Long
    private external fun nativeStaggerLayers(handle: Long, ids: LongArray, stepFrames: Int, keysOnly: Boolean): Int
    private external fun nativeArrangeLayerTimes(handle: Long, ids: LongArray, mode: Int, playhead: Long): Int
    private external fun nativeToggleMarker(handle: Long, frame: Long): Boolean
    private external fun nativeMarkBeatLive(handle: Long): Long
    private external fun nativeSetEditMode(handle: Long, on: Boolean)
    private external fun nativeSetProjectUiMode(handle: Long, mode: Int): Boolean
    private external fun nativeProjectUiMode(handle: Long): Int
    private external fun nativeMainTrackAnchor(handle: Long): Long
    private external fun nativeMoveClipToOverlay(handle: Long, layer: Long, atFrame: Long): Boolean
    private external fun nativeMoveClipToMainTrack(handle: Long, layer: Long): Boolean
    private external fun nativeSetCutTransition(handle: Long, outgoing: Long, incoming: Long, type: Int, frames: Int): Boolean
    private external fun nativeCutTransition(handle: Long, incoming: Long): Int
    private external fun nativeSetMotionBlur(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerAdjustment(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerGuide(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerLabel(handle: Long, layer: Long, label: Int): Boolean
    private external fun nativeSetLayerClipToBelow(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerSolo(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSearchLayers(handle: Long, query: String): LongArray
    private external fun nativeSetThermal(handle: Long, status: Int)
    private external fun nativeListFonts(handle: Long): String?
    private external fun nativeImportFont(handle: Long, path: String): String?
    private external fun nativeImportColorLut(handle: Long, layer: Long, effect: Int, path: String): Int
    private external fun nativeColorLutName(handle: Long, layer: Long, effect: Int): String
    private external fun nativeAddPartEffect(handle: Long, layer: Long, type: Int, part: Int): Int
    private external fun nativeSetEffectImage(handle: Long, layer: Long, effect: Int, path: String): Boolean
    private external fun nativeEffectImageName(handle: Long, layer: Long, effect: Int): String
    private external fun nativeModelPartNames(handle: Long, layer: Long): Array<String>?
    private external fun nativeSetTextFont(handle: Long, layer: Long, family: String, weight: Int, italic: Boolean, path: String): Boolean
    private external fun nativeSetTextStyle(handle: Long, layer: Long, v: FloatArray): Boolean
    private external fun nativeQueryTextStyle(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeSetTextSpan(handle: Long, layer: Long, start: Int, end: Int, hasColor: Boolean, r: Float, g: Float, b: Float, weight: Int, scale: Float): Boolean
    private external fun nativeClearTextSpans(handle: Long, layer: Long, start: Int, end: Int): Boolean
    private external fun nativeQueryTextAnimators(handle: Long, layer: Long): FloatArray?
    private external fun nativeLayerMediaPath(handle: Long, layer: Long): String?
    private external fun nativeReplaceLayerVideo(handle: Long, layer: Long, source: String, name: String): Long
    private external fun nativeReplaceLayerImage(
        handle: Long, layer: Long, rgba: ByteBuffer, width: Int, height: Int, name: String, source: String, orientation: Int,
    ): Long
    private external fun nativeLayerSourcePath(handle: Long, layer: Long): String?
    private external fun nativeProjectFileMedia(handle: Long, path: String): Array<String>?
    private external fun nativeExportProjectPackage(project: String, out: String, title: String, appVersion: String, media: Array<String>): IntArray?
    private external fun nativeImportProjectPackage(pkg: String, projectOut: String, mediaDir: String): Array<String>?
    private external fun nativeExtractModelArchive(archive: String, directory: String): Array<String>?
    private external fun nativeCreateCaptions(handle: Long, layer: Long, texts: Array<String>, times: DoubleArray, ints: IntArray, floats: FloatArray): Int
    private external fun nativeRemoveCaptions(handle: Long, layer: Long): Int
    private external fun nativeCaptionCount(handle: Long, layer: Long): Int
    private external fun nativeParseSrt(srt: String): String?
    private external fun nativeIsFillerWord(word: String): Boolean
    private external fun nativeAddTextAnimator(handle: Long, layer: Long, props: Int): Int
    private external fun nativeRemoveTextAnimator(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeDuplicateTextAnimator(handle: Long, layer: Long, index: Int): Int
    private external fun nativeMoveTextAnimator(handle: Long, layer: Long, from: Int, to: Int): Boolean
    private external fun nativeSetTextAnimator(handle: Long, layer: Long, index: Int, v: FloatArray): Boolean
    private external fun nativeSetTextAnimParam(handle: Long, layer: Long, index: Int, param: Int, value: Float): Boolean
    private external fun nativeToggleTextAnimKey(handle: Long, layer: Long, index: Int, param: Int): Boolean
    private external fun nativeApplyTextPreset(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeQueryLayerAnimators(handle: Long, layer: Long): FloatArray?
    private external fun nativeQueryText3dAnim(handle: Long, layer: Long): FloatArray?
    private external fun nativeApplyText3dAnim(handle: Long, layer: Long, preset: Int, mode: Int, unit: Int, durationSec: Float, staggerMs: Float): Boolean
    private external fun nativeAddLayerAnimator(handle: Long, layer: Long): Int
    private external fun nativeRemoveLayerAnimator(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeSetLayerAnimator(handle: Long, layer: Long, index: Int, v: FloatArray): Boolean
    private external fun nativeSetLayerAnimParam(handle: Long, layer: Long, index: Int, param: Int, value: Float): Boolean
    private external fun nativeToggleLayerAnimKey(handle: Long, layer: Long, index: Int, param: Int): Boolean
    private external fun nativeCopyLayerAnimators(handle: Long, layer: Long): Int
    private external fun nativePasteLayerAnimators(handle: Long, ids: LongArray): Int
    private external fun nativeLayerAnimatorClipboard(handle: Long): Int
    private external fun nativeSetLayerMotionBlurLength(handle: Long, layer: Long, factor: Float): Boolean
    private external fun nativeQueryLayerMotionBlurLength(handle: Long, layer: Long): Float
    private external fun nativeSetAdjustmentScope(handle: Long, layer: Long, scope: Int): Boolean
    private external fun nativeQueryAdjustmentScope(handle: Long, layer: Long): Int
    private external fun nativeSetAdjustmentTarget(handle: Long, layer: Long, target: Long, on: Boolean): Boolean
    private external fun nativeQueryAdjustmentTargets(handle: Long, layer: Long): LongArray?
    private external fun nativeEffectPresets(handle: Long, typeId: Int): Array<String>?
    private external fun nativeApplyEffectPreset(handle: Long, layer: Long, effectId: Int, preset: Int): Boolean
    private external fun nativeEnsureColorGrade(handle: Long, layer: Long, role: Int): Int
    private external fun nativeApplyColorLook(handle: Long, layer: Long, effectId: Int, look: Int): Boolean
    private external fun nativeResetColorGrade(handle: Long, layer: Long, effectId: Int): Boolean
    private external fun nativeRenderColorLooks(handle: Long, maxDim: Int, out: java.nio.ByteBuffer, size: IntArray): Boolean
    private external fun nativeComputeScopes(handle: Long, maxDim: Int, histogram: IntArray, waveform: ShortArray,
                                             parade: ShortArray, vectorscope: ShortArray, size: IntArray): Boolean
    private external fun nativeRotoAddStroke(handle: Long, layer: Long, effectId: Int, background: Boolean, radius: Float, xy: FloatArray): Boolean
    private external fun nativeRotoUndoStroke(handle: Long, layer: Long, effectId: Int): Boolean
    private external fun nativeRotoPropagate(handle: Long, layer: Long, effectId: Int): Boolean
    private external fun nativeRotoCancel(handle: Long)
    private external fun nativeRotoSetView(handle: Long, layer: Long, effectId: Int, mode: Int): Boolean
    private external fun nativeRotoStatus(handle: Long, layer: Long, effectId: Int): LongArray?
    private external fun nativeSetGroupCameraPassThrough(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeQueryGroupCameraPassThrough(handle: Long, layer: Long): Int
    private external fun nativeSetLayerAcceptsLights(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetContentBoundedPlayback(handle: Long, on: Boolean)
    private external fun nativeQueryNavigationEnd(handle: Long): Long
    private external fun nativeSetLayer3D(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeEnableLayer3D(handle: Long, layer: Long): Boolean
    private external fun nativeQueryLayerAcceptsLights(handle: Long, layer: Long): Int
    private external fun nativeAddLayersToGroup(handle: Long, ids: LongArray, group: Long): String?
    private external fun nativeRemoveLayerFromGroup(handle: Long, layer: Long): String?
    private external fun nativeSavePreset(handle: Long, layer: Long, kind: Int, name: ByteArray, parts: Int): ByteArray?
    private external fun nativeSaveEffectPreset(handle: Long, layer: Long, effectId: Int, name: ByteArray): ByteArray?
    private external fun nativeImportAlightMotion(handle: Long, data: ByteArray): ByteArray?
    private external fun nativeApplyPreset(handle: Long, layer: Long, json: ByteArray, duration: Long): ByteArray?
    private external fun nativeMakeCaptionPreset(name: ByteArray, ints: IntArray, floats: FloatArray): ByteArray?
    private external fun nativeParseCaptionPreset(json: ByteArray): FloatArray?
    private external fun nativeMakeCurvePreset(name: ByteArray, interp: Int, x1: Float, y1: Float, x2: Float, y2: Float, power: Int): ByteArray?
    private external fun nativeParseCurvePreset(json: ByteArray): FloatArray?
    private external fun nativeSetExpression(handle: Long, layer: Long, keys: IntArray, source: ByteArray): ByteArray?
    private external fun nativeSetExpressionEnabled(handle: Long, layer: Long, keys: IntArray, enabled: Boolean): Boolean
    private external fun nativeQueryExpression(handle: Long, layer: Long, property: Int, effectIndex: Int, paramIndex: Int): ByteArray?
    private external fun nativeQueryExpressions(handle: Long, layer: Long): IntArray?
    private external fun nativeCheckExpressionSyntax(source: ByteArray): ByteArray?
    private external fun nativeTextFont(handle: Long, layer: Long): String?
    private external fun nativeSetVectorBlur(handle: Long, layer: Long, amount: Float): Boolean
    private external fun nativeSetFrameBlend(handle: Long, layer: Long, mode: Int): Boolean
    private external fun nativeStartCameraTrack(handle: Long, layer: Long, mode: Int): Boolean
    private external fun nativeStartCameraTrackEx(handle: Long, layer: Long, mode: Int, motion: Int, knownFov: Float, shotType: Int, lens: Int): Boolean
    private external fun nativeRefineCameraTrackEx(handle: Long, remove: Boolean, motion: Int, fov: Float, shotType: Int, lens: Int): Boolean
    private external fun nativeCameraTrackFrameInfo(handle: Long, out: FloatArray): Int
    private external fun nativeAddCameraTrackLensEffect(handle: Long, layer: Long, redistort: Boolean): Long
    private external fun nativeCancelCameraTrack(handle: Long)
    private external fun nativeCameraTrackStatus(handle: Long, out: FloatArray): String?
    private external fun nativeCameraTrackFeatures(handle: Long, frame: Long, out: FloatArray): Int
    private external fun nativeApplyCameraTrack(handle: Long, frame: Long, x0: Float, y0: Float, x1: Float, y1: Float): Long
    private external fun nativeRestoreCameraTrack(handle: Long, layer: Long): Boolean
    private external fun nativeCameraTrackDetails(handle: Long, frame: Long, out: FloatArray): Int
    private external fun nativeSelectCameraTrackPoints(handle: Long, ids: IntArray, operation: Int): Int
    private external fun nativeCreateCameraTrackObject(handle: Long, kind: Int): Long
    private external fun nativeQueryTimeRemap(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeQueryTimeRemapCurrentSource(handle: Long, layer: Long): Double
    private external fun nativeEditTimeRemapKey(handle: Long, layer: Long, index: Int, frame: Long, value: Float, interp: Int): Int
    private external fun nativeRemoveTimeRemapKey(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeQuerySpeedCurve(handle: Long, layer: Long, samples: Int, out: FloatArray): Int
    private external fun nativeApplySpeedCurve(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeEditSpeedKey(handle: Long, layer: Long, index: Int, frame: Long, speed: Float, interp: Int): Int
    private external fun nativeRemoveSpeedKey(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeModelCameraCount(handle: Long, layer: Long): Int
    private external fun nativeQueryModelBones(handle: Long, layer: Long, out: FloatArray?): Int
    private external fun nativeDragBone(handle: Long, layer: Long, node: Int, dx: Float, dy: Float, twist: Float, continuing: Boolean): Boolean
    private external fun nativeToggleBoneKey(handle: Long, layer: Long, node: Int): Int
    private external fun nativeResetBonePose(handle: Long, layer: Long, node: Int): Boolean
    private external fun nativeAddModelCameras(handle: Long, layer: Long): Int
    private external fun nativeReverseTimeRemap(handle: Long, layer: Long): Boolean
    private external fun nativeSetKeepPitch(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetTimeRemap(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetTimeRemapValue(handle: Long, layer: Long, frame: Long, value: Float): Boolean
    private external fun nativeAddParticles(handle: Long, preset: Int): Long
    private external fun nativeAddText3d(handle: Long, content: String, fields: FloatArray, fontPath: String): Long
    private external fun nativeSetText3d(handle: Long, layer: Long, content: String, fields: FloatArray, fontPath: String): Boolean
    private external fun nativeSetText3dTexture(handle: Long, layer: Long, path: String): Boolean
    private external fun nativeQueryText3dTexture(handle: Long, layer: Long): String?
    private external fun nativeApplyText3dPreset(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeQueryText3dFont(handle: Long, layer: Long): String?
    private external fun nativeQueryText3d(handle: Long, layer: Long, out: FloatArray): String?
    private external fun nativeSetModelShadows(handle: Long, layer: Long, cast: Boolean, receive: Boolean): Boolean
    private external fun nativeQueryModelShadows(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeSetModelInterior(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeQueryModelInterior(handle: Long, layer: Long): Int
    private external fun nativeMaterialPreview(handle: Long, layer: Long, material: Int, size: Int): IntArray?
    private external fun nativeText3DPresetPreview(handle: Long, preset: Int, size: Int): IntArray?
    private external fun nativeAddShape3d(handle: Long, kind: Int, name: String): Long
    private external fun nativeQueryShape3d(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeSetShape3dPartStyle(handle: Long, layer: Long, part: Int, rgba: FloatArray?, image: String?): Boolean
    private external fun nativeQueryShape3dParts(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeSetShape3dPart(handle: Long, layer: Long, part: Int, values: FloatArray, mask: Int, continuing: Boolean): Boolean
    private external fun nativeToggleShape3dPartKey(handle: Long, layer: Long, part: Int): Int
    private external fun nativeResetShape3dPart(handle: Long, layer: Long, part: Int): Boolean
    private external fun nativeQueryShape3dPartGizmo(handle: Long, layer: Long, part: Int, length: Float, out: FloatArray, localSpace: Boolean): Boolean
    private external fun nativeShape3dPartMove(handle: Long, layer: Long, part: Int, axis: Int, amount: Float, out: FloatArray): Boolean
    private external fun nativeSplitShape3d(handle: Long, layer: Long, axis: Int, count: Int): Long
    private external fun nativeSetTransition(handle: Long, layer: Long, out: Boolean, type: Int, frames: Int): Boolean
    private external fun nativeSetEcho(handle: Long, layer: Long, count: Int, delay: Float, decay: Float): Boolean
    private external fun nativeAddMask(handle: Long, layer: Long, pts: FloatArray?, count: Int, closed: Boolean): Int
    private external fun nativeRemoveMask(handle: Long, layer: Long, mask: Int): Boolean
    private external fun nativeSetMaskPath(handle: Long, layer: Long, mask: Int, pts: FloatArray?, count: Int, closed: Boolean, undo: Boolean): Boolean
    private external fun nativeSetMaskProps(handle: Long, layer: Long, mask: Int, op: Int, inverted: Boolean, feather: Float, expansion: Float, opacity: Float): Boolean
    private external fun nativeToggleMaskPathKey(handle: Long, layer: Long, mask: Int): Int
    private external fun nativeSetMaskParam(handle: Long, layer: Long, mask: Int, param: Int, value: Float): Boolean
    private external fun nativeToggleMaskParamKey(handle: Long, layer: Long, mask: Int, param: Int): Boolean
    private external fun nativeQueryMasks(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeTrackMask(handle: Long, layer: Long, mask: Int, mode: Int): Int
    private external fun nativeQueryRig(handle: Long, layer: Long, bind: Boolean, out: FloatArray): Int
    private external fun nativeRigAddJoint(handle: Long, layer: Long, parent: Int, x: Float, y: Float): Int
    private external fun nativeRigMoveJoint(handle: Long, layer: Long, joint: Int, x: Float, y: Float, continuing: Boolean): Boolean
    private external fun nativeRigRemoveJoint(handle: Long, layer: Long, joint: Int): Boolean
    private external fun nativeRigClear(handle: Long, layer: Long): Boolean
    private external fun nativeRigAutoHumanoid(handle: Long, layer: Long): Int
    private external fun nativeRigPoseJoint(handle: Long, layer: Long, joint: Int, x: Float, y: Float, continuing: Boolean): Boolean
    private external fun nativeSetRigSetupLayer(handle: Long, layer: Long)
    private external fun nativeQueryMeshWarp(handle: Long, layer: Long, effect: Int, out: FloatArray): Int
    private external fun nativeMeshWarpDrag(handle: Long, layer: Long, effect: Int, vertex: Int, grip: Int, u: Float, v: Float,
                                            autoKey: Boolean, continuing: Boolean): Boolean
    private external fun nativeMeshWarpReset(handle: Long, layer: Long, effect: Int): Boolean
    private external fun nativeMeshWarpToggleKey(handle: Long, layer: Long, effect: Int): Boolean
    private external fun nativeQueryPuppet(handle: Long, layer: Long, effect: Int, out: FloatArray): Int
    private external fun nativeQueryPuppetMesh(handle: Long, layer: Long, effect: Int, out: FloatArray): Int
    private external fun nativePuppetAddPin(handle: Long, layer: Long, effect: Int, u: Float, v: Float): Int
    private external fun nativePuppetMovePin(handle: Long, layer: Long, effect: Int, pin: Int, u: Float, v: Float,
                                             autoKey: Boolean, continuing: Boolean): Boolean
    private external fun nativePuppetRemovePin(handle: Long, layer: Long, effect: Int, pin: Int): Boolean
    private external fun nativeSetTrackMatte(handle: Long, layer: Long, matte: Long, mode: Int): Boolean
    private external fun nativeQueryTrackMatte(handle: Long, layer: Long, out: LongArray): Boolean
    private external fun nativeTrackPoint(handle: Long, layer: Long, x: Float, y: Float, stabilize: Boolean, tracked: IntArray): Long
    private external fun nativeSetRgbTime(handle: Long, layer: Long, delay: Float): Boolean
    private external fun nativeQueryEcho(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeApplyParticlePreset(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeSetParticleParam(handle: Long, layer: Long, param: Int, value: Float): Boolean
    private external fun nativeQueryParticles(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeSetParticleSource(handle: Long, layer: Long, source: Long): Boolean
    private external fun nativeSetParticleTexture(handle: Long, layer: Long, image: Long): Boolean
    private external fun nativeSetParticleMesh(handle: Long, layer: Long, model: Long): Boolean
    private external fun nativeSetParticleLifeCurve(handle: Long, layer: Long, kind: Int, values: FloatArray, count: Int): Boolean
    private external fun nativeQueryParticleLinks(handle: Long, layer: Long, out: LongArray): Boolean
    private external fun nativeQueryParticleCurve(handle: Long, layer: Long, kind: Int, out: FloatArray): Int
    private external fun nativeApplySpeedRamp(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativePrecompose(handle: Long, ids: LongArray): Long
    private external fun nativeGroupAndMask(handle: Long, ids: LongArray, exclude: Boolean, name: String?): Long
    private external fun nativeCanGroupAndMask(handle: Long, ids: LongArray): Boolean
    private external fun nativeQueryKeyframeNavigation(handle: Long, layer: Long, property: Int, effect: Int, param: Int, frame: Long, out: LongArray): Boolean
    private external fun nativeQueryTimelineNavigation(handle: Long, layer: Long, frame: Long, direction: Int): Long
    private external fun nativeSeekTimelineEvent(handle: Long, frame: Long): Boolean
    private external fun nativeUngroupPrecomp(handle: Long, layer: Long): String?
    private external fun nativeImportHdri(handle: Long, path: String): Long
    private external fun nativeQueryGizmo(handle: Long, layer: Long, length: Float, out: FloatArray, localSpace: Boolean): Boolean
    private external fun nativeGizmoMoveLocal(handle: Long, layer: Long, axis: Int, amount: Float, out: FloatArray): Boolean
    private external fun nativeClearHdri(handle: Long): Boolean
    private external fun nativeSetEnvironmentBackground(handle: Long, visible: Boolean): Boolean
    private external fun nativeSetEnvironmentBackgroundRange(handle: Long, start: Long, end: Long): Boolean
    private external fun nativeSetEnvironment(handle: Long, intensity: Float, rotation: Float): Boolean
    private external fun nativeQueryEnvironment(handle: Long, out: FloatArray): Boolean
    private external fun nativeSetObjectEnvironment(handle: Long, layer: Long, source: Int, hdri: Long, intensity: Float, rotation: Float, exposure: Float): Boolean
    private external fun nativeQueryObjectEnvironment(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeQueryMaterials(handle: Long, layer: Long): FloatArray
    private external fun nativeSetMaterialParam(handle: Long, layer: Long, material: Int, param: Int, value: Float): Boolean
    private external fun nativeOpenPrecomp(handle: Long, layer: Long): Boolean
    private external fun nativeClosePrecomp(handle: Long): Boolean
    private external fun nativePrecompDepth(handle: Long): Int
    private external fun nativeCompositionName(handle: Long): String?
    private external fun nativeSetCompositionMotionBlur(handle: Long, on: Boolean)
    private external fun nativeSetShutterAngle(handle: Long, degrees: Float)
    private external fun nativeMotionBlurState(handle: Long): Float
    private external fun nativeQueryMotionBlurSettings(handle: Long, out: FloatArray): Boolean
    private external fun nativeSetMotionBlurSettings(handle: Long, enabled: Boolean, angle: Float,
        phase: Float, samples: Int, adaptiveLimit: Int): Boolean
    private external fun nativeCopyLayers(handle: Long, ids: LongArray): Int
    private external fun nativePasteLayers(handle: Long, frame: Long): Int
    private external fun nativeCopyStyle(handle: Long, layer: Long): Boolean
    private external fun nativePasteStyle(handle: Long, ids: LongArray): Int
    private external fun nativeCopyTransform(handle: Long, layer: Long): Boolean
    private external fun nativePasteTransform(handle: Long, ids: LongArray): Int
    private external fun nativeCopyEffects(handle: Long, layer: Long, effect: Int): Int
    private external fun nativePasteEffects(handle: Long, ids: LongArray): Int
    private external fun nativeCopyKeyframes(handle: Long, layer: Long, frame: Long): Int
    private external fun nativeKeyframeSelection(handle: Long, layer: Long, references: LongArray, action: Int, delta: Int): Int
    private external fun nativePasteKeyframes(handle: Long, ids: LongArray, frame: Long): Int
    private external fun nativeCopyAnimation(handle: Long, layer: Long): Int
    private external fun nativeOptimizeKeyframes(handle: Long, layer: Long, property: Int, tolerance: Float): Int
    private external fun nativeClipboardState(handle: Long): Int
    private external fun nativeEditMode(handle: Long): Boolean
    private external fun nativeRippleDelete(handle: Long, ids: LongArray): Boolean
    private external fun nativeRemoveGaps(handle: Long): Long
    private external fun nativeEditClipTime(handle: Long, layer: Long, operation: Int, amount: Long, previous: Long, next: Long): Boolean
    private external fun nativeQueryClipTimeActions(handle: Long, layer: Long, frame: Long): Int
    private external fun nativeTrimComposition(handle: Long, frame: Long): Boolean
    private external fun nativeSetLayerMagneticTrack(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeLayerMagneticTrack(handle: Long, layer: Long): Boolean
    private external fun nativeReorderClip(handle: Long, layer: Long, targetFrame: Long): Boolean
    private external fun nativeMoveLayerToRow(handle: Long, layer: Long, anchor: Long, mode: Int): Boolean
    private external fun nativeMoveMarker(handle: Long, from: Long, to: Long): Boolean
    private external fun nativeEditMarker(handle: Long, from: Long, to: Long, color: Int, label: ByteArray): Boolean
    private external fun nativeDeleteMarker(handle: Long, frame: Long): Boolean
    private external fun nativeMarkerLabel(handle: Long, frame: Long): ByteArray
    private external fun nativeQueryMarkers(handle: Long, out: LongArray): Int
    private external fun nativeDetectBeats(handle: Long, layer: Long, bpm: DoubleArray): Long
    private external fun nativeAddText(handle: Long, content: String): Long
    private external fun nativeQueryText(handle: Long, layer: Long, out: FloatArray): String?
    private external fun nativeFreezeFrame(handle: Long, layer: Long, frame: Int, holdFrames: Int): Long
    private external fun nativeQueryWaveform(
        handle: Long, layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: ByteBuffer,
    ): Int
    private external fun nativeImportImage(
        handle: Long, rgba: ByteBuffer, width: Int, height: Int, name: String, source: String, orientation: Int,
    ): Long
    private external fun nativeNewProject(handle: Long, width: Int, height: Int, fps: Double, title: String, background: FloatArray?): Boolean
    private external fun nativeLoadProject(handle: Long, path: String): Int
    private external fun nativeSaveProject(handle: Long, path: String?): Int
    private external fun nativeSaveProjectIfDirty(handle: Long): Int
    private external fun nativeLoadNotice(handle: Long): Int
    private external fun nativeDiscardRecovery(handle: Long): Int
    private external fun nativeRecoverSession(handle: Long): Int
    private external fun nativeStartExport(handle: Long, outputPath: String, shortSide: Int, fps: Double, codec: Int, bitrateMbps: Int, aiUpscale: Int, trimToContent: Boolean, quality: Int, rateMode: Int, safeMode: Int): Int
    private external fun nativeExportDuration(handle: Long, trimToContent: Boolean): Long
    private external fun nativeExportUsesMotionBlur(handle: Long): Boolean
    private external fun nativeStartPreRender(handle: Long, direction: Int): Int
    private external fun nativeCancelPreRender(handle: Long)
    private external fun nativePreRenderProgress(handle: Long): LongArray
    private external fun nativeCancelExport(handle: Long): Int
    private external fun nativeRestartExport(handle: Long, recoveryPath: String, outputPath: String): Int
    private external fun nativeStartImageExport(handle: Long, outputPath: String, format: Int, shortSide: Int, maxWidth: Int, fps: Double, trimToContent: Boolean): Int
    private external fun nativeImageExportPlan(handle: Long, format: Int, shortSide: Int, maxWidth: Int, fps: Double, trimToContent: Boolean): LongArray?
    private external fun nativeImportModel(handle: Long, path: String, name: String, detail: Array<String?>, quality: Int, memory: LongArray?): Long
    private external fun nativeInspectModel(handle: Long, path: String, memory: LongArray?): LongArray
    private external fun nativeLastModelImport(handle: Long): LongArray
    private external fun nativeImportModelProgress(handle: Long): Int
    private external fun nativeModelMissingTextures(handle: Long, layer: Long): String
    private external fun nativeModelFolder(handle: Long, layer: Long): String
    private external fun nativeReloadModelTextures(handle: Long, layer: Long, detail: Array<String?>): Int
    private external fun nativeCancelModelImport(handle: Long)
    private external fun nativeExportProgress(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeExportDiagnostics(handle: Long): String?
    private external fun nativeLocalAiStatus(handle: Long): Int
    private external fun nativePreviewBufferRanges(handle: Long, out: LongArray): Int

    // --- Camada vetorial (Fase 7D) ---------------------------------------------
    /** Nova camada vetorial: 0 vazia (modo de pontos), 1 retângulo, 2 elipse, 3 polígono, 4 estrela. Id ≥ 0 ou −Errc. */
    fun addVectorLayer(preset: Int): Long = nativeAddVectorLayer(nativeHandle, preset)
    /** Documento (codec de VectorDocument.cpp; ver VectorDoc.kt). */
    fun vectorDocument(layer: Long): FloatArray? = nativeVectorDocument(nativeHandle, layer)
    fun vectorGroupNames(layer: Long): String? = nativeVectorGroupNames(nativeHandle, layer)
    fun setVectorDocument(layer: Long, doc: FloatArray, names: String, continuing: Boolean): Boolean =
        nativeSetVectorDocument(nativeHandle, layer, doc, names, continuing)
    /** [afim grupo→composição (6), flags, bezier…] do caminho no cabeçote. */
    fun vectorPathAt(layer: Long, group: Int, path: Int): FloatArray? = nativeVectorPathAt(nativeHandle, layer, group, path)
    fun setVectorPath(layer: Long, group: Int, path: Int, bez: FloatArray, continuing: Boolean): Boolean =
        nativeSetVectorPath(nativeHandle, layer, group, path, bez, continuing)
    fun toggleVectorPathKey(layer: Long, group: Int, path: Int): Boolean = nativeToggleVectorPathKey(nativeHandle, layer, group, path)
    fun addVectorGroup(layer: Long, kind: Int): Int = nativeAddVectorGroup(nativeHandle, layer, kind)
    fun removeVectorGroup(layer: Long, group: Int): Boolean = nativeRemoveVectorGroup(nativeHandle, layer, group)
    fun addVectorPath(layer: Long, group: Int, kind: Int, bez: FloatArray?): Int = nativeAddVectorPath(nativeHandle, layer, group, kind, bez)
    fun removeVectorPath(layer: Long, group: Int, path: Int): Boolean = nativeRemoveVectorPath(nativeHandle, layer, group, path)
    fun makeVectorPathEditable(layer: Long, group: Int, path: Int): Boolean = nativeMakeVectorPathEditable(nativeHandle, layer, group, path)
    /** Valores animáveis do grupo no cabeçote + bits animados + bits com keyframe. */
    fun queryVectorParams(layer: Long, group: Int): FloatArray? = nativeQueryVectorParams(nativeHandle, layer, group)
    fun queryShapeParams(layer: Long): FloatArray? = nativeQueryShapeParams(nativeHandle, layer)
    fun setShapeParamAnim(layer: Long, param: Int, value: Float, continuing: Boolean): Boolean =
        nativeSetShapeParamAnim(nativeHandle, layer, param, value, continuing)
    fun toggleShapeParamKey(layer: Long, param: Int): Boolean = nativeToggleShapeParamKey(nativeHandle, layer, param)
    fun setVectorParam(layer: Long, group: Int, param: Int, value: Float, continuing: Boolean): Boolean =
        nativeSetVectorParam(nativeHandle, layer, group, param, value, continuing)
    fun toggleVectorParamKey(layer: Long, group: Int, param: Int): Boolean = nativeToggleVectorParamKey(nativeHandle, layer, group, param)
    /** Traço do dedo (x,y em px da composição) → caminho suave. `layer` 0 = camada nova. Id ≥ 0 ou −Errc. */
    fun addFreehandPath(layer: Long, xy: FloatArray, error: Float): Long = nativeAddFreehandPath(nativeHandle, layer, xy, error)
    fun importSvg(bytes: ByteArray, name: String): Long = nativeWork.run(-5L) { nativeImportSvg(nativeHandle, bytes, name) }
    fun importPsd(path: String, name: String): Long = nativeWork.run(-5L) { nativeImportPsd(nativeHandle, path, name) }
    /** GIF animado: pré-composição com os quadros, em loop (0.2.7). */
    fun importGif(path: String, name: String): Long = nativeWork.run(-5L) { nativeImportGif(nativeHandle, path, name) }
    fun foregroundModelDirectory(): String = nativeWork.run("") { nativeForegroundModelDirectory(nativeHandle) }
    fun createGrid(ids: LongArray): Long = nativeCreateGrid(nativeHandle, ids)
    private external fun nativeForegroundModelDirectory(handle: Long): String
    private external fun nativeCreateGrid(handle: Long, ids: LongArray): Long
    private external fun nativeImportPsd(handle: Long, path: String, name: String): Long
    private external fun nativeImportGif(handle: Long, path: String, name: String): Long
    fun setTextPath(layer: Long, pathLayer: Long, offset: Float, perpendicular: Boolean, reverse: Boolean): Boolean =
        nativeSetTextPath(nativeHandle, layer, pathLayer, offset, perpendicular, reverse)
    /**
     * [guia, bits da margem inicial, perpendicular, invertido, máscara (-1 = nenhuma),
     * bits da margem final, forçar alinhamento] ou nulo. As margens são as do cabeçote.
     */
    fun queryTextPath(layer: Long): LongArray? = nativeQueryTextPath(nativeHandle, layer)
    /** Texto no caminho completo (0.2.7): máscara da própria camada (-1 = usa a guia), as duas margens e forçar alinhamento. */
    fun setTextPathOptions(layer: Long, pathLayer: Long, mask: Int, firstMargin: Float, lastMargin: Float,
                           perpendicular: Boolean, reverse: Boolean, forceAlign: Boolean): Boolean =
        nativeSetTextPathOptions(nativeHandle, layer, pathLayer, mask, firstMargin, lastMargin, perpendicular, reverse, forceAlign)

    private external fun nativeAddVectorLayer(handle: Long, preset: Int): Long
    private external fun nativeVectorDocument(handle: Long, layer: Long): FloatArray?
    private external fun nativeVectorGroupNames(handle: Long, layer: Long): String?
    private external fun nativeSetVectorDocument(handle: Long, layer: Long, doc: FloatArray, names: String, continuing: Boolean): Boolean
    private external fun nativeVectorPathAt(handle: Long, layer: Long, group: Int, path: Int): FloatArray?
    private external fun nativeSetVectorPath(handle: Long, layer: Long, group: Int, path: Int, bez: FloatArray, continuing: Boolean): Boolean
    private external fun nativeToggleVectorPathKey(handle: Long, layer: Long, group: Int, path: Int): Boolean
    private external fun nativeAddVectorGroup(handle: Long, layer: Long, kind: Int): Int
    private external fun nativeRemoveVectorGroup(handle: Long, layer: Long, group: Int): Boolean
    private external fun nativeAddVectorPath(handle: Long, layer: Long, group: Int, kind: Int, bez: FloatArray?): Int
    private external fun nativeRemoveVectorPath(handle: Long, layer: Long, group: Int, path: Int): Boolean
    private external fun nativeMakeVectorPathEditable(handle: Long, layer: Long, group: Int, path: Int): Boolean
    private external fun nativeQueryShapeParams(handle: Long, layer: Long): FloatArray?
    private external fun nativeSetShapeParamAnim(handle: Long, layer: Long, param: Int, value: Float, continuing: Boolean): Boolean
    private external fun nativeToggleShapeParamKey(handle: Long, layer: Long, param: Int): Boolean
    private external fun nativeQueryVectorParams(handle: Long, layer: Long, group: Int): FloatArray?
    private external fun nativeSetVectorParam(handle: Long, layer: Long, group: Int, param: Int, value: Float, continuing: Boolean): Boolean
    private external fun nativeToggleVectorParamKey(handle: Long, layer: Long, group: Int, param: Int): Boolean
    private external fun nativeAddFreehandPath(handle: Long, layer: Long, xy: FloatArray, error: Float): Long
    private external fun nativeImportSvg(handle: Long, bytes: ByteArray, name: String): Long
    private external fun nativeSetTextPath(handle: Long, layer: Long, pathLayer: Long, offset: Float, perpendicular: Boolean, reverse: Boolean): Boolean
    private external fun nativeQueryTextPath(handle: Long, layer: Long): LongArray?
    private external fun nativeSetTextPathOptions(handle: Long, layer: Long, pathLayer: Long, mask: Int, firstMargin: Float, lastMargin: Float,
                                                  perpendicular: Boolean, reverse: Boolean, forceAlign: Boolean): Boolean
}

/** The ten-slot JNI contract uses integers, including cache byte counters. */
data class PreRenderProgress(
    val running: Boolean = false,
    val complete: Boolean = false,
    val framesDone: Long = 0,
    val framesTotal: Long = 0,
    val width: Int = 0,
    val height: Int = 0,
    val direction: Int = 1,
    val error: Int = 0,
    val diskBytes: Long = 0,
    val residentBytes: Long = 0,
) {
    val fraction: Float get() = if (framesTotal > 0) (framesDone.toDouble() / framesTotal).coerceIn(0.0, 1.0).toFloat() else 0f
    companion object {
        fun from(values: LongArray): PreRenderProgress {
            if (values.size < 10) return PreRenderProgress()
            return PreRenderProgress(values[0] != 0L, values[1] != 0L,
                values[2].coerceAtLeast(0), values[3].coerceAtLeast(0),
                values[4].coerceIn(0, Int.MAX_VALUE.toLong()).toInt(), values[5].coerceIn(0, Int.MAX_VALUE.toLong()).toInt(),
                if (values[6] < 0) -1 else 1, values[7].toInt(), values[8].coerceAtLeast(0), values[9].coerceAtLeast(0))
        }
    }
}
