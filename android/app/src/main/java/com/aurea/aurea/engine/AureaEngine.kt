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
    fun initialize(refreshRate: Float, cacheDir: String, documentsDir: String, debug: Boolean): Boolean =
        nativeInitialize(nativeHandle, refreshRate, cacheDir, documentsDir, debug)

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

    fun queryCurve(layer: Long, property: Int, from: Int, to: Int, out: FloatArray): Int =
        nativeQueryCurve(nativeHandle, layer, property, from, to, out, out.size)

    fun queryEffectCatalog(rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectCatalog(nativeHandle, rows, capacity, blob)

    fun queryLayerEffects(layer: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryLayerEffects(nativeHandle, layer, rows, capacity, blob)

    fun queryEffectParams(layer: Long, effectId: Int, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int =
        nativeQueryEffectParams(nativeHandle, layer, effectId, rows, capacity, blob)

    fun queryLayerDetail(layer: Long, out: ByteBuffer): Boolean = nativeQueryLayerDetail(nativeHandle, layer, out)

    /** Composição atual: devolve o id (0 = nenhuma); `out` = [w, h, fps, duração, r, g, b, a]. */
    fun queryComposition(out: DoubleArray): Long = nativeQueryComposition(nativeHandle, out)

    /** RGBA8 da miniatura em `out`; 0 = ainda na fila (ver `thumbnailGeneration`). */
    fun queryThumbnail(layer: Long, frame: Int, height: Int, out: ByteBuffer, outWidth: IntArray): Int =
        nativeQueryThumbnail(nativeHandle, layer, frame, height, out, outWidth)

    /** Frame do playhead em RGBA8 sRGB (lado maior = `maxDim`); `outSize` recebe largura/altura. */
    fun captureFrame(maxDim: Int, out: ByteBuffer, outSize: IntArray): Int =
        nativeCaptureFrame(nativeHandle, maxDim, out, outSize)

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
    fun setFrameBlend(layer: Long, mode: Int): Boolean = nativeSetFrameBlend(nativeHandle, layer, mode)
    fun setVectorBlur(layer: Long, amount: Float): Boolean = nativeSetVectorBlur(nativeHandle, layer, amount)
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
    ): Boolean
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
    private external fun nativeQueryCurve(
        handle: Long, layer: Long, property: Int, from: Int, to: Int, out: FloatArray, count: Int,
    ): Int
    private external fun nativeQueryEffectCatalog(handle: Long, rows: ByteBuffer, capacity: Int, blob: ByteBuffer): Int
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
    private external fun nativeSetThermal(handle: Long, status: Int)
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
}
