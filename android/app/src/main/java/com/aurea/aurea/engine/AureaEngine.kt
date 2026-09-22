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
        /** Tamanho de um `Command` no C++ (`static_assert` do lado nativo). */
        const val COMMAND_SIZE_BYTES = 128

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
        } catch (_: Throwable) {
            -1
        }

        /**
         * Chamado pelo motor ao reabrir um projeto com imagens: decodifica a
         * origem (URI `content://` ou caminho) em RGBA8 com alfa reto. Resposta:
         * 8 bytes (largura, altura em u32 little-endian) + pixels; null se falhar.
         */
        @JvmStatic
        fun decodeImage(source: String): ByteArray? {
            val ctx = appContext ?: return null
            val bmp = decodeBitmapRgba(ctx, Uri.parse(source)) ?: return null
            val w = bmp.width
            val h = bmp.height
            val out = ByteArray(8 + w * h * 4)
            val header = java.nio.ByteBuffer.wrap(out, 0, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            header.putInt(w).putInt(h)
            bmp.copyPixelsToBuffer(java.nio.ByteBuffer.wrap(out, 8, w * h * 4))
            bmp.recycle()
            return out
        }

        /**
         * Decodifica uma imagem em ARGB_8888 NÃO pré-multiplicado (o motor quer
         * alfa reto), reduzida por potência de dois até o lado maior caber em 4096.
         */
        fun decodeBitmapRgba(ctx: Context, uri: Uri): android.graphics.Bitmap? = try {
            val cr = ctx.contentResolver
            val bounds = android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds = true }
            cr.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it, null, bounds) }
            var sample = 1
            while (maxOf(bounds.outWidth, bounds.outHeight) / sample > 4096) sample *= 2
            val opts = android.graphics.BitmapFactory.Options().apply {
                inSampleSize = sample
                inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888
                inPremultiplied = false
            }
            cr.openInputStream(uri)?.use { android.graphics.BitmapFactory.decodeStream(it, null, opts) }
        } catch (_: Throwable) {
            null
        }

        @JvmStatic external fun nativeCreate(): Long
        @JvmStatic external fun nativeDestroy(handle: Long)
    }

    /** Ponteiro para o contexto nativo. 0 = destruído. */
    private var nativeHandle: Long = nativeCreate()

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
    ): Boolean = nativeInitialize(nativeHandle, refreshRate, cacheDir, documentsDir, debug, probe, codecs)

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

    /** Foto de base das prévias de efeito (RGBA8, alfa reto). */
    fun setEffectPreviewSource(rgba: ByteArray, width: Int, height: Int): Boolean =
        nativeSetEffectPreviewSource(nativeHandle, rgba, width, height)

    fun shutdown() = nativeShutdown(nativeHandle)

    fun destroy() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    /** App em segundo plano: pausa, devolve os decoders, grava o cache de pipeline. */
    fun suspend() = nativeSuspend(nativeHandle)

    fun resume() = nativeResume(nativeHandle)
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
        val accepted = nativeSubmitCommands(
            nativeHandle, commandBuffer, commandCount,
            if (stringWriteOffset > 0) stringBuffer else null, stringWriteOffset,
        )
        commandCount = 0
        stringWriteOffset = 0
        return accepted
    }

    // =========================================================================
    // Estado
    // =========================================================================

    fun readStatus(out: ByteBuffer): Boolean = nativeReadStatus(nativeHandle, out)
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
    fun queryAllKeyframes(index: ByteBuffer, layerCapacity: Int, rows: ByteBuffer, capacity: Int): Long =
        nativeQueryAllKeyframes(nativeHandle, index, layerCapacity, rows, capacity)

    fun queryCurve(layer: Long, property: Int, from: Int, to: Int, out: FloatArray): Int =
        nativeQueryCurve(nativeHandle, layer, property, from, to, out, out.size)

    fun queryEffectCatalog(rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectCatalog(nativeHandle, rows, capacity, blob)

    fun queryLayerEffects(layer: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryLayerEffects(nativeHandle, layer, rows, capacity, blob)

    fun queryEffectParams(layer: Long, effectId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectParams(nativeHandle, layer, effectId, rows, capacity, blob)

    /** Declaração dos parâmetros de um TIPO de efeito (a ficha do catálogo). */
    fun queryEffectSpecs(typeId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectSpecs(nativeHandle, typeId, rows, capacity, blob)

    fun queryLayerDetail(layer: Long, out: ByteBuffer): Boolean = nativeQueryLayerDetail(nativeHandle, layer, out)

    /** Composição atual: devolve o id (0 = nenhuma); `out` = [w, h, fps, duração, r, g, b, a]. */
    fun queryComposition(out: DoubleArray): Long = nativeQueryComposition(nativeHandle, out)

    /** RGBA8 da miniatura em `out`; 0 = ainda na fila (ver `thumbnailGeneration`). */
    fun queryThumbnail(layer: Long, frame: Int, height: Int, out: ByteBuffer, outWidth: IntArray): Int =
        nativeQueryThumbnail(nativeHandle, layer, frame, height, out, outWidth)

    /** Frame do playhead em RGBA8 sRGB (lado maior = `maxDim`); `outSize` recebe largura/altura. */
    fun captureFrame(maxDim: Int, out: ByteBuffer, outSize: IntArray): Int =
        nativeCaptureFrame(nativeHandle, maxDim, out, outSize)

    /**
     * A prévia de um efeito: o efeito de verdade rodando sobre a cartela de
     * demonstração do motor, em RGBA8 (pré-multiplicado não: alfa reto).
     * `outSize` recebe largura/altura de verdade. Falso = não deu para
     * pré-visualizar (efeito temporal, sem GPU, tipo desconhecido).
     */
    fun renderEffectPreview(typeId: Int, width: Int, height: Int, out: ByteBuffer, outSize: IntArray): Boolean =
        nativeRenderEffectPreview(nativeHandle, typeId, width, height, out, outSize)

    fun setSelection(layers: LongArray) = nativeSetSelection(nativeHandle, layers)
    fun clearSelection() = nativeClearSelection(nativeHandle)

    // =========================================================================
    // Importação e projeto
    // =========================================================================

    /** Id da layer criada (≥ 0), ou `-código` do erro. */
    fun importVideo(source: String, displayName: String): Long =
        nativeImportVideo(nativeHandle, source, displayName)

    /** Arquivo de áudio: camada de áudio no topo. Id ≥ 0 ou −Errc. */
    fun importAudio(source: String, displayName: String): Long =
        nativeImportAudio(nativeHandle, source, displayName)

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
    fun copyEffects(layer: Long): Int = nativeCopyEffects(nativeHandle, layer)
    fun pasteEffects(ids: LongArray): Int = nativePasteEffects(nativeHandle, ids)
    fun copyKeyframes(layer: Long, frame: Long): Int = nativeCopyKeyframes(nativeHandle, layer, frame)
    fun pasteKeyframes(ids: LongArray, frame: Long): Int = nativePasteKeyframes(nativeHandle, ids, frame)
    /** Bits: 1 camadas, 2 estilo, 4 efeitos, 8 keyframes. */
    fun clipboardState(): Int = nativeClipboardState(nativeHandle)

    // Gizmo 3D.
    fun queryGizmo(layer: Long, length: Float, out: FloatArray): Boolean = nativeQueryGizmo(nativeHandle, layer, length, out)
    fun gizmoMoveLocal(layer: Long, axis: Int, amount: Float, out: FloatArray): Boolean = nativeGizmoMoveLocal(nativeHandle, layer, axis, amount, out)

    // Ambiente 3D (HDRI).
    fun importHdri(path: String): Long = nativeImportHdri(nativeHandle, path)
    fun clearHdri(): Boolean = nativeClearHdri(nativeHandle)
    fun setEnvironment(intensity: Float, rotation: Float): Boolean = nativeSetEnvironment(nativeHandle, intensity, rotation)
    fun queryEnvironment(out: FloatArray): Boolean = nativeQueryEnvironment(nativeHandle, out)

    // Pré-composição.
    fun precompose(ids: LongArray): Long = nativePrecompose(nativeHandle, ids)
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
    /** Floats necessários (Engine::query_masks); só escreve se couber em [out]. */
    fun queryMasks(layer: Long, out: FloatArray): Int = nativeQueryMasks(nativeHandle, layer, out)
    /** Síncrono (decodifica): fora da UI. Quadros rastreados, ou −Errc. */
    fun trackMask(layer: Long, mask: Int, mode: Int): Int = nativeTrackMask(nativeHandle, layer, mask, mode)
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
    fun addText3d(content: String, depth: Float, align: Int, r: Float, g: Float, b: Float): Long =
        nativeAddText3d(nativeHandle, content, depth, align, r, g, b)
    fun setText3d(layer: Long, content: String, depth: Float, align: Int, r: Float, g: Float, b: Float): Boolean =
        nativeSetText3d(nativeHandle, layer, content, depth, align, r, g, b)
    fun queryText3d(layer: Long, out: FloatArray): String? = nativeQueryText3d(nativeHandle, layer, out)
    fun applyParticlePreset(layer: Long, preset: Int): Boolean = nativeApplyParticlePreset(nativeHandle, layer, preset)
    fun setParticleParam(layer: Long, param: Int, value: Float): Boolean = nativeSetParticleParam(nativeHandle, layer, param, value)
    fun queryParticles(layer: Long, out: FloatArray): Boolean = nativeQueryParticles(nativeHandle, layer, out)

    // Remapeamento de tempo / rampas.
    fun setTimeRemap(layer: Long, on: Boolean): Boolean = nativeSetTimeRemap(nativeHandle, layer, on)
    fun applySpeedRamp(layer: Long, preset: Int): Boolean = nativeApplySpeedRamp(nativeHandle, layer, preset)

    // Desfoque de movimento.
    fun setMotionBlur(layer: Long, on: Boolean): Boolean = nativeSetMotionBlur(nativeHandle, layer, on)

    // Organização e papel da camada (7H).
    fun setLayerAdjustment(layer: Long, on: Boolean): Boolean = nativeSetLayerAdjustment(nativeHandle, layer, on)
    fun setLayerGuide(layer: Long, on: Boolean): Boolean = nativeSetLayerGuide(nativeHandle, layer, on)
    fun setLayerLabel(layer: Long, label: Int): Boolean = nativeSetLayerLabel(nativeHandle, layer, label)
    fun setLayerSolo(layer: Long, on: Boolean): Boolean = nativeSetLayerSolo(nativeHandle, layer, on)
    /** Camadas cujo nome/texto contém [query] (sem maiúscula nem acento), da frente para o fundo. */
    fun searchLayers(query: String): LongArray = nativeSearchLayers(nativeHandle, query)
    fun setFrameBlend(layer: Long, mode: Int): Boolean = nativeSetFrameBlend(nativeHandle, layer, mode)
    fun setVectorBlur(layer: Long, amount: Float): Boolean = nativeSetVectorBlur(nativeHandle, layer, amount)

    // Fontes.
    fun listFonts(): String? = nativeListFonts(nativeHandle)
    fun importFont(path: String): String? = nativeImportFont(nativeHandle, path)
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
    fun createCaptions(layer: Long, texts: Array<String>, times: DoubleArray, ints: IntArray, floats: FloatArray): Int =
        nativeCreateCaptions(nativeHandle, layer, texts, times, ints, floats)
    fun removeCaptions(layer: Long): Int = nativeRemoveCaptions(nativeHandle, layer)
    fun captionCount(layer: Long): Int = nativeCaptionCount(nativeHandle, layer)
    fun parseSrt(srt: String): String? = nativeParseSrt(srt)
    fun isFillerWord(word: String): Boolean = nativeIsFillerWord(word)
    fun addTextAnimator(layer: Long, props: Int): Int = nativeAddTextAnimator(nativeHandle, layer, props)
    fun removeTextAnimator(layer: Long, index: Int): Boolean = nativeRemoveTextAnimator(nativeHandle, layer, index)
    fun setTextAnimator(layer: Long, index: Int, v: FloatArray): Boolean = nativeSetTextAnimator(nativeHandle, layer, index, v)
    fun setTextAnimParam(layer: Long, index: Int, param: Int, value: Float): Boolean = nativeSetTextAnimParam(nativeHandle, layer, index, param, value)
    fun toggleTextAnimKey(layer: Long, index: Int, param: Int): Boolean = nativeToggleTextAnimKey(nativeHandle, layer, index, param)
    fun applyTextPreset(layer: Long, preset: Int): Boolean = nativeApplyTextPreset(nativeHandle, layer, preset)

    // Presets (JSON do motor, formato em engine/include/aurea/project/Presets.hpp).
    /** kind 0 efeitos, 1 texto, 2 animação; parts (texto) 1 estilo, 2 animadores. Nulo = nada a salvar. */
    fun savePreset(layer: Long, kind: Int, name: String, parts: Int = 3): String? =
        nativeSavePreset(nativeHandle, layer, kind, name.toByteArray(Charsets.UTF_8), parts)?.toString(Charsets.UTF_8)
    /** Um passo de desfazer. Nulo = aplicado; senão, o motivo. `duration` > 0 estica a animação. */
    fun applyPreset(layer: Long, json: String, duration: Long = 0): String? =
        nativeApplyPreset(nativeHandle, layer, json.toByteArray(Charsets.UTF_8), duration)?.toString(Charsets.UTF_8)
    fun makeCaptionPreset(name: String, ints: IntArray, floats: FloatArray): String? =
        nativeMakeCaptionPreset(name.toByteArray(Charsets.UTF_8), ints, floats)?.toString(Charsets.UTF_8)
    fun parseCaptionPreset(json: String): FloatArray? = nativeParseCaptionPreset(json.toByteArray(Charsets.UTF_8))
    fun makeCurvePreset(name: String, interp: Int, x1: Float, y1: Float, x2: Float, y2: Float): String? =
        nativeMakeCurvePreset(name.toByteArray(Charsets.UTF_8), interp, x1, y1, x2, y2)?.toString(Charsets.UTF_8)
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

    // Rastreio de câmera 3D.
    fun startCameraTrack(layer: Long, mode: Int): Boolean = nativeStartCameraTrack(nativeHandle, layer, mode)
    fun cancelCameraTrack() = nativeCancelCameraTrack(nativeHandle)
    fun cameraTrackStatus(out: FloatArray): String? = nativeCameraTrackStatus(nativeHandle, out)
    fun applyCameraTrack(): Long = nativeApplyCameraTrack(nativeHandle)
    fun cameraTrackFeatures(frame: Long, out: FloatArray): Int = nativeCameraTrackFeatures(nativeHandle, frame, out)
    fun editTimeRemapKey(layer: Long, index: Int, frame: Long, value: Float, interp: Int): Int =
        nativeEditTimeRemapKey(nativeHandle, layer, index, frame, value, interp)
    fun removeTimeRemapKey(layer: Long, index: Int): Boolean = nativeRemoveTimeRemapKey(nativeHandle, layer, index)
    fun setCompositionMotionBlur(on: Boolean) = nativeSetCompositionMotionBlur(nativeHandle, on)
    fun setShutterAngle(degrees: Float) = nativeSetShutterAngle(nativeHandle, degrees)
    /** > 0 = ligado; |valor| − 1 = obturador em graus; 0 = sem projeto. */
    fun motionBlurState(): Float = nativeMotionBlurState(nativeHandle)

    /** Modo Edição (timeline magnética) da composição atual. */
    fun setEditMode(on: Boolean) = nativeSetEditMode(nativeHandle, on)
    fun editMode(): Boolean = nativeEditMode(nativeHandle)
    /** Exclui e fecha só os buracos criados (um passo de desfazer). */
    fun rippleDelete(ids: LongArray): Boolean = nativeRippleDelete(nativeHandle, ids)
    /** Fecha todos os espaços vazios. Devolve os frames removidos. */
    fun removeGaps(): Long = nativeRemoveGaps(nativeHandle)
    fun trimComposition(frame: Long): Boolean = nativeTrimComposition(nativeHandle, frame)

    /** Liga/desliga a marca no frame. true = ficou marcada. */
    fun toggleMarker(frame: Long): Boolean = nativeToggleMarker(nativeHandle, frame)
    fun moveMarker(from: Long, to: Long): Boolean = nativeMoveMarker(nativeHandle, from, to)
    /** Marcas: frame, cor, tipo (3 longs cada). Devolve o total. */
    fun queryMarkers(out: LongArray): Int = nativeQueryMarkers(nativeHandle, out)
    /** Síncrono (decodifica o som): fora da thread de UI. Nº de batidas ou −Errc. */
    fun detectBeats(layer: Long, bpm: DoubleArray): Long = nativeDetectBeats(nativeHandle, layer, bpm)

    /** Nulo 2D ou 3D no centro. Id ≥ 0 ou −Errc. */
    fun addNull(threeD: Boolean): Long = nativeAddNull(nativeHandle, threeD)

    /** Congela o quadro do clipe no `frame` por `holdFrames`; o resto anda. Id ≥ 0 ou −Errc. */
    fun freezeFrame(layer: Long, frame: Int, holdFrames: Int): Long = nativeFreezeFrame(nativeHandle, layer, frame, holdFrames)

    /** Waveform: `count` baldes (u8, compansão raiz) a partir de `startFrame`. 0 = sem som. */
    fun queryWaveform(layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: ByteBuffer): Int =
        nativeQueryWaveform(nativeHandle, layer, startFrame, framesPerBucket, count, out)

    /** RGBA8 sRGB (alfa reto) num buffer direto de `width * height * 4` bytes. */
    fun importImage(rgba: ByteBuffer, width: Int, height: Int, name: String, source: String): Long =
        nativeImportImage(nativeHandle, rgba, width, height, name, source)

    fun newProject(width: Int, height: Int, fps: Float, title: String): Boolean =
        nativeNewProject(nativeHandle, width, height, fps, title)

    fun loadProject(path: String): Int = nativeLoadProject(nativeHandle, path)
    fun saveProject(path: String): Int = nativeSaveProject(nativeHandle, path)
    fun discardRecovery(): Int = nativeDiscardRecovery(nativeHandle)
    fun recoverSession(): Int = nativeRecoverSession(nativeHandle)

    /**
     * Exporta a composição atual para MP4. `shortSide` = lado menor (720…2160),
     * `fps` 0 = o da composição, `codec` 0 = H.264 / 1 = HEVC, `bitrateMbps` 0 =
     * automático. Devolve o código de erro do motor (0 = começou).
     */
    fun startExport(outputPath: String, shortSide: Int, fps: Double, codec: Int, bitrateMbps: Int): Int =
        nativeStartExport(nativeHandle, outputPath, shortSide, fps, codec, bitrateMbps)
    fun cancelExport(): Int = nativeCancelExport(nativeHandle)

    /**
     * Importa um glTF/GLB (arquivo no sandbox do app). Bloqueia: chamar fora da
     * UI. Devolve o id da layer, ou −código de erro; `detail[0]` recebe o
     * motivo (falha) ou os avisos do import (sucesso).
     */
    fun importModel(path: String, name: String, detail: Array<String?>): Long =
        nativeImportModel(nativeHandle, path, name, detail)
    /** Etapa × 1000 + fração × 1000 (ImportPhase do motor). */
    fun importModelProgress(): Int = nativeImportModelProgress(nativeHandle)
    fun cancelModelImport() = nativeCancelModelImport(nativeHandle)
    fun exportProgress(out: ByteBuffer): Boolean = nativeExportProgress(nativeHandle, out)

    // -------------------------------------------------------------------------
    // Declarações nativas (engine/platform/android/aurea_jni.cpp)
    // -------------------------------------------------------------------------
    private external fun nativeInitialize(
        handle: Long, refreshRate: Float, cacheDir: String, documentsDir: String, debug: Boolean,
        probe: LongArray?, codecs: IntArray?,
    ): Boolean
    private external fun nativeDeviceReport(handle: Long, out: LongArray): Boolean
    private external fun nativeDeviceSummary(handle: Long): String?
    private external fun nativeSetEffectPreviewSource(handle: Long, rgba: ByteArray, width: Int, height: Int): Boolean
    private external fun nativeShutdown(handle: Long)
    private external fun nativeSuspend(handle: Long)
    private external fun nativeResume(handle: Long)
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
    private external fun nativeQueryKeyframes(handle: Long, layer: Long, rows: ByteBuffer, capacity: Int): Int
    private external fun nativeQueryAllKeyframes(handle: Long, index: ByteBuffer, layerCapacity: Int, rows: ByteBuffer, capacity: Int): Long
    private external fun nativeQueryCurve(
        handle: Long, layer: Long, property: Int, from: Int, to: Int, out: FloatArray, count: Int,
    ): Int
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
    private external fun nativeQueryThumbnail(
        handle: Long, layer: Long, frame: Int, height: Int, out: ByteBuffer, outWidth: IntArray,
    ): Int
    private external fun nativeCaptureFrame(handle: Long, maxDim: Int, out: ByteBuffer, outSize: IntArray): Int
    private external fun nativeRenderEffectPreview(
        handle: Long, typeId: Int, width: Int, height: Int, out: ByteBuffer, outSize: IntArray,
    ): Boolean
    private external fun nativeSetSelection(handle: Long, layers: LongArray)
    private external fun nativeClearSelection(handle: Long)
    private external fun nativeImportVideo(handle: Long, source: String, name: String): Long
    private external fun nativeImportAudio(handle: Long, source: String, name: String): Long
    private external fun nativeExtractAudio(handle: Long, layer: Long): Long
    private external fun nativeAddShape(handle: Long, preset: Int): Long
    private external fun nativeAddNull(handle: Long, threeD: Boolean): Long
    private external fun nativeToggleMarker(handle: Long, frame: Long): Boolean
    private external fun nativeSetEditMode(handle: Long, on: Boolean)
    private external fun nativeSetMotionBlur(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerAdjustment(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerGuide(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSetLayerLabel(handle: Long, layer: Long, label: Int): Boolean
    private external fun nativeSetLayerSolo(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeSearchLayers(handle: Long, query: String): LongArray
    private external fun nativeSetThermal(handle: Long, status: Int)
    private external fun nativeListFonts(handle: Long): String?
    private external fun nativeImportFont(handle: Long, path: String): String?
    private external fun nativeSetTextFont(handle: Long, layer: Long, family: String, weight: Int, italic: Boolean, path: String): Boolean
    private external fun nativeSetTextStyle(handle: Long, layer: Long, v: FloatArray): Boolean
    private external fun nativeQueryTextStyle(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeSetTextSpan(handle: Long, layer: Long, start: Int, end: Int, hasColor: Boolean, r: Float, g: Float, b: Float, weight: Int, scale: Float): Boolean
    private external fun nativeClearTextSpans(handle: Long, layer: Long, start: Int, end: Int): Boolean
    private external fun nativeQueryTextAnimators(handle: Long, layer: Long): FloatArray?
    private external fun nativeLayerMediaPath(handle: Long, layer: Long): String?
    private external fun nativeCreateCaptions(handle: Long, layer: Long, texts: Array<String>, times: DoubleArray, ints: IntArray, floats: FloatArray): Int
    private external fun nativeRemoveCaptions(handle: Long, layer: Long): Int
    private external fun nativeCaptionCount(handle: Long, layer: Long): Int
    private external fun nativeParseSrt(srt: String): String?
    private external fun nativeIsFillerWord(word: String): Boolean
    private external fun nativeAddTextAnimator(handle: Long, layer: Long, props: Int): Int
    private external fun nativeRemoveTextAnimator(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeSetTextAnimator(handle: Long, layer: Long, index: Int, v: FloatArray): Boolean
    private external fun nativeSetTextAnimParam(handle: Long, layer: Long, index: Int, param: Int, value: Float): Boolean
    private external fun nativeToggleTextAnimKey(handle: Long, layer: Long, index: Int, param: Int): Boolean
    private external fun nativeApplyTextPreset(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeSavePreset(handle: Long, layer: Long, kind: Int, name: ByteArray, parts: Int): ByteArray?
    private external fun nativeApplyPreset(handle: Long, layer: Long, json: ByteArray, duration: Long): ByteArray?
    private external fun nativeMakeCaptionPreset(name: ByteArray, ints: IntArray, floats: FloatArray): ByteArray?
    private external fun nativeParseCaptionPreset(json: ByteArray): FloatArray?
    private external fun nativeMakeCurvePreset(name: ByteArray, interp: Int, x1: Float, y1: Float, x2: Float, y2: Float): ByteArray?
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
    private external fun nativeCancelCameraTrack(handle: Long)
    private external fun nativeCameraTrackStatus(handle: Long, out: FloatArray): String?
    private external fun nativeCameraTrackFeatures(handle: Long, frame: Long, out: FloatArray): Int
    private external fun nativeApplyCameraTrack(handle: Long): Long
    private external fun nativeQueryTimeRemap(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeEditTimeRemapKey(handle: Long, layer: Long, index: Int, frame: Long, value: Float, interp: Int): Int
    private external fun nativeRemoveTimeRemapKey(handle: Long, layer: Long, index: Int): Boolean
    private external fun nativeSetTimeRemap(handle: Long, layer: Long, on: Boolean): Boolean
    private external fun nativeAddParticles(handle: Long, preset: Int): Long
    private external fun nativeAddText3d(handle: Long, content: String, depth: Float, align: Int, r: Float, g: Float, b: Float): Long
    private external fun nativeSetText3d(handle: Long, layer: Long, content: String, depth: Float, align: Int, r: Float, g: Float, b: Float): Boolean
    private external fun nativeQueryText3d(handle: Long, layer: Long, out: FloatArray): String?
    private external fun nativeSetTransition(handle: Long, layer: Long, out: Boolean, type: Int, frames: Int): Boolean
    private external fun nativeSetEcho(handle: Long, layer: Long, count: Int, delay: Float, decay: Float): Boolean
    private external fun nativeAddMask(handle: Long, layer: Long, pts: FloatArray?, count: Int, closed: Boolean): Int
    private external fun nativeRemoveMask(handle: Long, layer: Long, mask: Int): Boolean
    private external fun nativeSetMaskPath(handle: Long, layer: Long, mask: Int, pts: FloatArray?, count: Int, closed: Boolean, undo: Boolean): Boolean
    private external fun nativeSetMaskProps(handle: Long, layer: Long, mask: Int, op: Int, inverted: Boolean, feather: Float, expansion: Float, opacity: Float): Boolean
    private external fun nativeToggleMaskPathKey(handle: Long, layer: Long, mask: Int): Int
    private external fun nativeQueryMasks(handle: Long, layer: Long, out: FloatArray): Int
    private external fun nativeTrackMask(handle: Long, layer: Long, mask: Int, mode: Int): Int
    private external fun nativeSetTrackMatte(handle: Long, layer: Long, matte: Long, mode: Int): Boolean
    private external fun nativeQueryTrackMatte(handle: Long, layer: Long, out: LongArray): Boolean
    private external fun nativeTrackPoint(handle: Long, layer: Long, x: Float, y: Float, stabilize: Boolean, tracked: IntArray): Long
    private external fun nativeSetRgbTime(handle: Long, layer: Long, delay: Float): Boolean
    private external fun nativeQueryEcho(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeApplyParticlePreset(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativeSetParticleParam(handle: Long, layer: Long, param: Int, value: Float): Boolean
    private external fun nativeQueryParticles(handle: Long, layer: Long, out: FloatArray): Boolean
    private external fun nativeApplySpeedRamp(handle: Long, layer: Long, preset: Int): Boolean
    private external fun nativePrecompose(handle: Long, ids: LongArray): Long
    private external fun nativeUngroupPrecomp(handle: Long, layer: Long): String?
    private external fun nativeImportHdri(handle: Long, path: String): Long
    private external fun nativeQueryGizmo(handle: Long, layer: Long, length: Float, out: FloatArray): Boolean
    private external fun nativeGizmoMoveLocal(handle: Long, layer: Long, axis: Int, amount: Float, out: FloatArray): Boolean
    private external fun nativeClearHdri(handle: Long): Boolean
    private external fun nativeSetEnvironment(handle: Long, intensity: Float, rotation: Float): Boolean
    private external fun nativeQueryEnvironment(handle: Long, out: FloatArray): Boolean
    private external fun nativeOpenPrecomp(handle: Long, layer: Long): Boolean
    private external fun nativeClosePrecomp(handle: Long): Boolean
    private external fun nativePrecompDepth(handle: Long): Int
    private external fun nativeCompositionName(handle: Long): String?
    private external fun nativeSetCompositionMotionBlur(handle: Long, on: Boolean)
    private external fun nativeSetShutterAngle(handle: Long, degrees: Float)
    private external fun nativeMotionBlurState(handle: Long): Float
    private external fun nativeCopyLayers(handle: Long, ids: LongArray): Int
    private external fun nativePasteLayers(handle: Long, frame: Long): Int
    private external fun nativeCopyStyle(handle: Long, layer: Long): Boolean
    private external fun nativePasteStyle(handle: Long, ids: LongArray): Int
    private external fun nativeCopyEffects(handle: Long, layer: Long): Int
    private external fun nativePasteEffects(handle: Long, ids: LongArray): Int
    private external fun nativeCopyKeyframes(handle: Long, layer: Long, frame: Long): Int
    private external fun nativePasteKeyframes(handle: Long, ids: LongArray, frame: Long): Int
    private external fun nativeClipboardState(handle: Long): Int
    private external fun nativeEditMode(handle: Long): Boolean
    private external fun nativeRippleDelete(handle: Long, ids: LongArray): Boolean
    private external fun nativeRemoveGaps(handle: Long): Long
    private external fun nativeTrimComposition(handle: Long, frame: Long): Boolean
    private external fun nativeMoveMarker(handle: Long, from: Long, to: Long): Boolean
    private external fun nativeQueryMarkers(handle: Long, out: LongArray): Int
    private external fun nativeDetectBeats(handle: Long, layer: Long, bpm: DoubleArray): Long
    private external fun nativeAddText(handle: Long, content: String): Long
    private external fun nativeQueryText(handle: Long, layer: Long, out: FloatArray): String?
    private external fun nativeFreezeFrame(handle: Long, layer: Long, frame: Int, holdFrames: Int): Long
    private external fun nativeQueryWaveform(
        handle: Long, layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: ByteBuffer,
    ): Int
    private external fun nativeImportImage(
        handle: Long, rgba: ByteBuffer, width: Int, height: Int, name: String, source: String,
    ): Long
    private external fun nativeNewProject(handle: Long, width: Int, height: Int, fps: Float, title: String): Boolean
    private external fun nativeLoadProject(handle: Long, path: String): Int
    private external fun nativeSaveProject(handle: Long, path: String): Int
    private external fun nativeDiscardRecovery(handle: Long): Int
    private external fun nativeRecoverSession(handle: Long): Int
    private external fun nativeStartExport(handle: Long, outputPath: String, shortSide: Int, fps: Double, codec: Int, bitrateMbps: Int): Int
    private external fun nativeCancelExport(handle: Long): Int
    private external fun nativeImportModel(handle: Long, path: String, name: String, detail: Array<String?>): Long
    private external fun nativeImportModelProgress(handle: Long): Int
    private external fun nativeCancelModelImport(handle: Long)
    private external fun nativeExportProgress(handle: Long, out: ByteBuffer): Boolean

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
    fun importSvg(bytes: ByteArray, name: String): Long = nativeImportSvg(nativeHandle, bytes, name)
    fun setTextPath(layer: Long, pathLayer: Long, offset: Float, perpendicular: Boolean, reverse: Boolean): Boolean =
        nativeSetTextPath(nativeHandle, layer, pathLayer, offset, perpendicular, reverse)
    /** [guia, bits da margem, perpendicular, invertido] ou nulo. */
    fun queryTextPath(layer: Long): LongArray? = nativeQueryTextPath(nativeHandle, layer)

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
}
