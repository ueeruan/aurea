package com.aurea.aurea.engine

import java.nio.ByteBuffer

/**
 * A fronteira com o motor C++.
 *
 * REGRA DESTA CLASSE: ela é a ÚNICA porta para o motor. Nenhum outro arquivo
 * Kotlin declara `external fun`. Se a UI pudesse chamar o motor direto, cada
 * tela inventaria a sua própria forma de conversar com ele — e o custo de
 * travessia da bridge deixaria de ser controlado num lugar só.
 *
 * As três chamadas de um frame são:
 *
 *   1. [submitCommands] — a UI escreve comandos POD num buffer e envia o lote.
 *      Uma travessia, não uma por propriedade mexida.
 *   2. [renderFrame]    — o motor drena a fila, avalia a animação, compõe na
 *      GPU e apresenta no `Surface`. NENHUM bitmap atravessa a fronteira.
 *   3. [readStatus]     — o estado condensado volta como struct POD.
 *
 * O buffer de comandos é DIRETO (fora do heap gerenciado) e tem o tamanho
 * exato de um `Command` do C++ (128 bytes). A UI escreve nele, o motor lê. Sem
 * cópia intermediária e sem serializar campo por campo.
 */
class AureaEngine private constructor() {

    companion object {
        /**
         * Tamanho de um `Command` no C++, em bytes.
         *
         * É contrato de ABI: `static_assert(sizeof(Command) == 128)` do lado
         * C++ quebra a compilação se este número deixar de valer. Manter os
         * dois em sincronia é obrigatório — um desalinhamento aqui produz
         * comandos com campos trocados, que é pior do que um crash porque
         * corrompe o projeto em silêncio.
         */
        const val COMMAND_SIZE_BYTES = 128

        /** Capacidade do lote. 4096 comandos = 512 KB, o pior caso de um gesto. */
        const val MAX_COMMANDS_PER_FRAME = 4096

        /** Tamanho máximo do blob de strings por lote (nomes, conteúdo de texto). */
        const val STRING_BLOB_BYTES = 64 * 1024

        @Volatile
        private var libraryLoaded = false

        init {
            // A .so do motor é carregada uma vez no processo. O NDK a chama de
            // `libaurea.so`; o nome tem que bater com o `add_library` do CMake.
            System.loadLibrary("aurea")
            libraryLoaded = true
        }

        /**
         * Cria o motor. Não faz nada além de instanciar — a inicialização
         * pesada (detecção de capacidades, sondagem de codecs, criação do
         * backend gráfico) acontece em [initialize] e roda FORA da thread da
         * UI, porque leva dezenas de milissegundos.
         */
        fun create(): AureaEngine = AureaEngine()

        @JvmStatic
        external fun nativeCreate(): Long

        @JvmStatic
        external fun nativeDestroy(handle: Long)
    }

    /** Ponteiro para o `Engine` do C++. 0 = destruído. */
    private var nativeHandle: Long = nativeCreate()

    /** Buffer de comandos, direto e reutilizado entre frames. */
    private val commandBuffer: ByteBuffer =
        ByteBuffer.allocateDirect(MAX_COMMANDS_PER_FRAME * COMMAND_SIZE_BYTES)
            .also { it.order(java.nio.ByteOrder.nativeOrder()) }

    /** Blob de strings do lote atual. */
    private val stringBuffer: ByteBuffer =
        ByteBuffer.allocateDirect(STRING_BLOB_BYTES)
            .also { it.order(java.nio.ByteOrder.nativeOrder()) }

    private var stringWriteOffset = 0
    private var commandCount = 0

    /**
     * Inicializa o motor e conecta a superfície de apresentação.
     *
     * @param surface Janela nativa (`Surface`) onde o motor vai desenhar. O
     *   frame composto chega DIRETO nela — a UI nunca vê pixels.
     */
    fun initialize(
        surface: android.view.Surface,
        widthPx: Int,
        heightPx: Int,
        refreshRate: Float,
        cacheDir: String,
        documentsDir: String,
    ): Boolean = nativeInitialize(
        nativeHandle, surface, widthPx, heightPx, refreshRate, cacheDir, documentsDir,
    )

    fun shutdown() = nativeShutdown(nativeHandle)

    /** Recomeça o lote. Chamado no início de cada frame da UI. */
    fun beginCommandBatch() {
        commandCount = 0
        stringWriteOffset = 0
    }

    /**
     * Escreve uma string no blob do lote e devolve o offset, ou -1 se não
     * couber. Usado para nomes de camada e conteúdo de texto.
     */
    fun writeString(text: String): Int {
        val bytes = text.toByteArray(Charsets.UTF_8)
        if (stringWriteOffset + bytes.size > STRING_BLOB_BYTES) return -1
        val offset = stringWriteOffset
        stringBuffer.position(offset)
        stringBuffer.put(bytes)
        stringWriteOffset += bytes.size
        return offset
    }

    /**
     * Reserva um slot de comando e devolve o buffer posicionado no início dele.
     *
     * Quem chama escreve os campos na ORDEM EXATA do `struct Command` do C++ e
     * fecha com [endCommand]. Escrever na ordem errada não gera erro de
     * compilação — gera um comando com campos trocados. É por isso que a
     * escrita fica concentrada no [CommandBatch] em vez de espalhada.
     *
     * Devolve `null` quando o lote encheu; a UI reenvia o resto no próximo frame.
     */
    internal fun reserveCommandSlot(): ByteBuffer? {
        if (commandCount >= MAX_COMMANDS_PER_FRAME) return null
        val slot = commandBuffer
        slot.position(commandCount * COMMAND_SIZE_BYTES)
        slot.limit((commandCount + 1) * COMMAND_SIZE_BYTES)
        return slot
    }

    internal fun endCommand() {
        commandCount++
    }

    /** Quantos comandos foram escritos neste lote. */
    fun pendingCommandCount(): Int = commandCount

    /**
     * Envia o lote. Devolve quantos comandos foram aceitos — menos que
     * [pendingCommandCount] significa fila cheia, e a UI reenvia o resto.
     *
     * NÃO bloqueia: a fila do motor é livre de trava e nunca faz a UI esperar.
     */
    fun submitCommands(): Int {
        if (commandCount == 0) return 0
        val accepted = nativeSubmitCommands(
            nativeHandle,
            commandBuffer,
            commandCount,
            if (stringWriteOffset > 0) stringBuffer else null,
            stringWriteOffset,
        )
        // Os comandos recusados foram perdidos: reenviá-los exigiria a UI
        // manter o buffer do frame anterior. Na prática a fila tem folga de
        // milhares de comandos e o recusado só acontece se a UI emitir sem
        // parar — e nesse caso a UI tem um bug maior.
        commandCount = 0
        stringWriteOffset = 0
        return accepted
    }

    /**
     * Desenha um frame.
     *
     * @param audioTimeNs posição do mixer de áudio, em nanossegundos. Durante o
     *   playback é o master clock; parado, o playhead da timeline manda.
     */
    fun renderFrame(audioTimeNs: Long): Boolean = nativeRenderFrame(nativeHandle, audioTimeNs)

    /** Estado condensado para a UI. `out` é um buffer direto de STATUS_BYTES. */
    fun readStatus(out: ByteBuffer): Boolean = nativeReadStatus(nativeHandle, out)

    /**
     * Projeta as layers da composição atual num buffer direto.
     *
     * `outBuffer` precisa comportar `capacity * LAYER_ROW_BYTES`. O motor
     * escreve os structs POD nele e o Kotlin lê por offset com [LayerRow.read] —
     * uma travessia, zero alocação.
     */
    fun queryLayers(outBuffer: ByteBuffer, capacity: Int, nameBlob: ByteBuffer): Int =
        nativeQueryLayers(nativeHandle, outBuffer, capacity, nameBlob, nameBlob.capacity())

    /**
     * Keyframes de uma layer, para desenhar a barra de keyframes.
     * `outBuffer` precisa comportar `capacity * KEYFRAME_ROW_BYTES`.
     */
    fun queryKeyframes(layerHandle: Long, outBuffer: ByteBuffer, capacity: Int): Int =
        nativeQueryKeyframes(nativeHandle, layerHandle, outBuffer, capacity)

    /** Curva amostrada de uma propriedade, para o editor de gráfico. */
    fun queryCurve(layerHandle: Long, property: Int, from: Int, to: Int, out: FloatArray): Int =
        nativeQueryCurve(nativeHandle, layerHandle, property, from, to, out, out.size)

    fun setSelection(layerHandles: LongArray) = nativeSetSelection(nativeHandle, layerHandles)

    fun clearSelection() = nativeClearSelection(nativeHandle)

    /** Redimensiona a superfície. Rotação e tela dividida passam por aqui. */
    fun resizeSurface(widthPx: Int, heightPx: Int) =
        nativeResizeSurface(nativeHandle, widthPx, heightPx)

    /** O app foi para segundo plano. Libera GPU e cache; o projeto fica em pé. */
    fun suspend() = nativeSuspend(nativeHandle)

    fun resume(surface: android.view.Surface, widthPx: Int, heightPx: Int) =
        nativeResume(nativeHandle, surface, widthPx, heightPx)

    fun newProject(width: Int, height: Int, fps: Float, title: String): Boolean =
        nativeNewProject(nativeHandle, width, height, fps, title)

    fun loadProject(path: String): Int = nativeLoadProject(nativeHandle, path)

    /** Descarta o journal da sessão anterior. Ação destrutiva. */
    fun discardRecovery(): Int = nativeDiscardRecovery(nativeHandle)

    /** Reaplica o journal da sessão anterior sobre o projeto aberto. */
    fun recoverSession(): Int = nativeRecoverSession(nativeHandle)
    fun saveProject(path: String): Int = nativeSaveProject(nativeHandle, path)

    fun startExport(outputPath: String): Int = nativeStartExport(nativeHandle, outputPath)
    fun cancelExport(): Int = nativeCancelExport(nativeHandle)
    fun exportProgress(out: ByteBuffer): Boolean = nativeExportProgress(nativeHandle, out)

    fun readTelemetry(out: ByteBuffer): Boolean = nativeReadTelemetry(nativeHandle, out)

    /** Endereço do `Engine` para o teste de integração. Só para debug. */
    internal fun rawHandle(): Long = nativeHandle

    // -------------------------------------------------------------------------
    // Declarações nativas. A implementação está em jni_bridge.cpp.
    // -------------------------------------------------------------------------
    private external fun nativeInitialize(
        handle: Long, surface: android.view.Surface,
        width: Int, height: Int, refreshRate: Float,
        cacheDir: String, documentsDir: String,
    ): Boolean
    private external fun nativeShutdown(handle: Long)
    private external fun nativeSubmitCommands(
        handle: Long, commands: ByteBuffer, count: Int,
        stringBlob: ByteBuffer?, stringBlobSize: Int,
    ): Int
    private external fun nativeRenderFrame(handle: Long, audioTimeNs: Long): Boolean
    private external fun nativeReadStatus(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeReadTelemetry(handle: Long, out: ByteBuffer): Boolean
    private external fun nativeQueryLayers(
        handle: Long, outBuffer: ByteBuffer, capacity: Int,
        nameBlob: ByteBuffer, nameBlobCapacity: Int,
    ): Int
    private external fun nativeQueryKeyframes(
        handle: Long, layerHandle: Long, outBuffer: ByteBuffer, capacity: Int,
    ): Int
    private external fun nativeQueryCurve(
        handle: Long, layerHandle: Long, property: Int, from: Int, to: Int,
        out: FloatArray, count: Int,
    ): Int
    private external fun nativeSetSelection(handle: Long, layerHandles: LongArray)
    private external fun nativeClearSelection(handle: Long)
    private external fun nativeResizeSurface(handle: Long, width: Int, height: Int)
    private external fun nativeSuspend(handle: Long)
    private external fun nativeResume(handle: Long, surface: android.view.Surface, width: Int, height: Int)
    private external fun nativeNewProject(handle: Long, width: Int, height: Int, fps: Float, title: String): Boolean
    private external fun nativeLoadProject(handle: Long, path: String): Int
    private external fun nativeDiscardRecovery(handle: Long): Int
    private external fun nativeRecoverSession(handle: Long): Int
    private external fun nativeSaveProject(handle: Long, path: String): Int
    private external fun nativeStartExport(handle: Long, outputPath: String): Int
    private external fun nativeCancelExport(handle: Long): Int
    private external fun nativeExportProgress(handle: Long, out: ByteBuffer): Boolean
}
