package com.aurea.aurea.engine

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * O CONTRATO DE MEMÓRIA entre o motor e esta camada.
 *
 * Espelho exato de `engine/include/aurea/bridge/BridgePods.hpp`. Cada constante
 * aqui tem um `static_assert` do lado C++ amarrando o offset — mudar um campo lá
 * sem mudar aqui QUEBRA A COMPILAÇÃO DO MOTOR, em vez de corromper dados em
 * produção.
 *
 * POR QUE BUFFERS DIRETOS E NÃO OBJETOS:
 *
 * A alternativa "natural" seria uma data class por camada, criada pelo JNI. Isso
 * custa, por elemento, uma busca de campo por nome (`GetFieldID`), uma chamada
 * para ler cada campo e uma alocação no heap gerenciado. Com 200 camadas a
 * 60 Hz são 200 alocações e alguns milhares de buscas por frame — e o coletor
 * de lixo cobra a conta no meio do playback, que é exatamente quando não pode
 * acontecer.
 *
 * Aqui o motor escreve structs POD num `ByteBuffer` DIRETO e o Kotlin lê por
 * offset. Zero alocação, zero busca por nome, uma travessia por frame.
 */
internal object PodLayout {

    // =========================================================================
    // Command — escritos pela UI, lidos pelo motor.
    // =========================================================================
    const val COMMAND_BYTES = 128
    const val CMD_OFF_TYPE = 0            // u16
    const val CMD_OFF_STRING_OFFSET = 4   // u32
    const val CMD_OFF_STRING_LENGTH = 8   // u32
    const val CMD_OFF_PAYLOAD = 16        // union, 64 bytes
    const val CMD_PAYLOAD_BYTES = 64
    const val CMD_OFF_CORRELATION = 80    // u64

    // =========================================================================
    // LayerRow — lido pela UI para desenhar a timeline.
    // =========================================================================
    const val LAYER_ROW_BYTES = 64
    const val LAYER_OFF_ID = 0            // u64
    const val LAYER_OFF_KIND = 8          // u32
    const val LAYER_OFF_Z_INDEX = 12      // u32
    const val LAYER_OFF_START = 16        // i32
    const val LAYER_OFF_END = 20          // i32
    const val LAYER_OFF_OPACITY = 24      // f32
    const val LAYER_OFF_FLAGS = 28        // u32
    const val LAYER_OFF_EFFECT_COUNT = 32 // u32
    const val LAYER_OFF_MASK_COUNT = 36   // u32
    const val LAYER_OFF_KEYFRAME_COUNT = 40 // u32
    const val LAYER_OFF_NAME_OFFSET = 44  // u32
    const val LAYER_OFF_NAME_LENGTH = 48  // u32
    const val LAYER_OFF_BLEND_MODE = 52   // u32
    const val LAYER_OFF_PARENT_INDEX = 56 // u32

    /** Bits de `LayerRow::flags`, na ordem definida em BridgePods.hpp. */
    const val FLAG_VISIBLE = 1 shl 0
    const val FLAG_LOCKED = 1 shl 1
    const val FLAG_SOLO = 1 shl 2
    const val FLAG_ANIMATED = 1 shl 3
    const val FLAG_SELECTED = 1 shl 4
    const val FLAG_THREE_D = 1 shl 5

    // =========================================================================
    // KeyframeRow
    // =========================================================================
    const val KEYFRAME_ROW_BYTES = 24
    const val KF_OFF_PROPERTY = 0         // u32
    const val KF_OFF_EFFECT_INDEX = 4     // u32
    const val KF_OFF_TIME = 8             // i32
    const val KF_OFF_VALUE = 12           // f32
    const val KF_OFF_INTERPOLATION = 16   // u32

    // =========================================================================
    // EngineStatusPOD — 256 bytes, quatro linhas de cache.
    // =========================================================================
    const val STATUS_BYTES = 256
    const val ST_OFF_STATE = 0            // i32
    const val ST_OFF_LAST_ERROR = 4       // i32
    const val ST_OFF_ERROR_DETAIL = 8     // char[120]
    const val ST_OFF_CURRENT_FPS = 128    // f32
    const val ST_OFF_AVERAGE_FRAME_MS = 132
    const val ST_OFF_GPU_MS = 136
    const val ST_OFF_CPU_MS = 140
    const val ST_OFF_DECODE_MS = 144
    const val ST_OFF_CACHE_HIT_RATE = 148
    const val ST_OFF_MEMORY_PRESSURE = 152
    const val ST_OFF_PREVIEW_WIDTH = 156  // u32
    const val ST_OFF_PREVIEW_HEIGHT = 160
    const val ST_OFF_PREVIEW_NUMERATOR = 164
    const val ST_OFF_PREVIEW_DENOMINATOR = 168
    const val ST_OFF_PREVIEW_AUTO = 172
    const val ST_OFF_PLAYHEAD = 176       // i64
    const val ST_OFF_DURATION = 184       // i64
    const val ST_OFF_PLAYING = 192        // u32
    const val ST_OFF_LAYER_COUNT = 196
    const val ST_OFF_SELECTED_COUNT = 200
    const val ST_OFF_CAN_UNDO = 204
    const val ST_OFF_CAN_REDO = 208
    const val ST_OFF_UNDO_DEPTH = 212
    const val ST_OFF_ASSET_COUNT = 216
    const val ST_OFF_DIRTY = 220
    const val ST_OFF_RECOVERY = 224
    const val ST_OFF_DROPPED_FRAMES = 228
    const val ST_OFF_PASSES_EXECUTED = 232
    const val ST_OFF_PASSES_CULLED = 236
    const val ST_OFF_GPU_MEMORY = 240     // u64
    const val ST_OFF_CPU_MEMORY = 248     // u64

    // =========================================================================
    // ExportProgressPOD
    // =========================================================================
    const val EXPORT_PROGRESS_BYTES = 128
    const val EPD_OFF_RUNNING = 0         // u32
    const val EPD_OFF_FINISHED = 4        // u32
    const val EPD_OFF_RESULT = 8          // i32
    const val EPD_OFF_TOTAL = 12
    const val EPD_OFF_DONE = 16
    const val EPD_OFF_FPS = 20            // f32
    const val EPD_OFF_ETA = 24
    const val EPD_OFF_MESSAGE = 32        // char[96]

    // =========================================================================
    // TelemetryPOD
    // =========================================================================
    const val TELEMETRY_BYTES = 128
    const val TEL_OFF_FRAME_MS = 0
    const val TEL_OFF_GPU_MS = 4
    const val TEL_OFF_CPU_MS = 8
    const val TEL_OFF_DECODE_MS = 12
    const val TEL_OFF_FRAME_CACHE_HIT = 16
    const val TEL_OFF_PIPELINE_HIT = 20
    const val TEL_OFF_WORKER_COUNT = 24
    const val TEL_OFF_PASSES_EXECUTED = 28
    const val TEL_OFF_PASSES_CULLED = 32
    const val TEL_OFF_DRAW_CALLS = 36
    const val TEL_OFF_TRIANGLES = 40
    const val TEL_OFF_PARTICLES = 44
    const val TEL_OFF_SHADER_COUNT = 48
    const val TEL_OFF_PIPELINE_COUNT = 52
    const val TEL_OFF_SHADER_FAILURES = 56
    const val TEL_OFF_ACTIVE_EFFECTS = 60
    const val TEL_OFF_ACTIVE_LAYERS = 64
    const val TEL_OFF_ADAPTIVE_CHANGES = 68
    const val TEL_OFF_PHYSICAL_RESOURCES = 72
    const val TEL_OFF_LOGICAL_RESOURCES = 76
    const val TEL_OFF_THERMAL = 80        // f32
    const val TEL_OFF_THROTTLING = 84
    const val TEL_OFF_GPU_MEMORY = 88     // u64
    const val TEL_OFF_CPU_MEMORY = 96     // u64
    const val TEL_OFF_UNDO_BLOB = 104     // u64
    const val TEL_OFF_COMMANDS_DROPPED = 112 // u64
    const val TEL_OFF_FRAMES_IN_FLIGHT = 120
}

/**
 * Uma linha da lista de camadas.
 *
 * O nome NÃO é copiado na leitura: ele é resolvido do blob quando alguém pede.
 * Copiar a string de 200 camadas a 60 Hz seriam 200 alocações por frame — que é
 * justamente o que este desenho existe para evitar.
 */
class LayerRow internal constructor(
    val id: Long,
    val kind: Int,
    val zIndex: Int,
    val startFrame: Int,
    val endFrame: Int,
    val opacity: Float,
    val flags: Int,
    val effectCount: Int,
    val maskCount: Int,
    val keyframeCount: Int,
    val blendMode: Int,
    val parentIndex: Int,
    private val nameOffset: Int,
    private val nameLength: Int,
    private val nameBlob: ByteBuffer,
) {
    val visible: Boolean get() = (flags and PodLayout.FLAG_VISIBLE) != 0
    val locked: Boolean get() = (flags and PodLayout.FLAG_LOCKED) != 0
    val solo: Boolean get() = (flags and PodLayout.FLAG_SOLO) != 0
    val animated: Boolean get() = (flags and PodLayout.FLAG_ANIMATED) != 0
    val selected: Boolean get() = (flags and PodLayout.FLAG_SELECTED) != 0
    val isThreeD: Boolean get() = (flags and PodLayout.FLAG_THREE_D) != 0

    val durationFrames: Int get() = endFrame - startFrame
    val hasParent: Boolean get() = parentIndex != INVALID_INDEX

    /** Nome da camada. Uma alocação de String, só quando o chamador pede. */
    val name: String
        get() {
            if (nameLength <= 0) return ""
            val bytes = ByteArray(nameLength)
            val dup = nameBlob.duplicate()
            dup.position(nameOffset)
            dup.get(bytes, 0, nameLength)
            return String(bytes, Charsets.UTF_8)
        }

    companion object {
        const val INVALID_INDEX = -1

        internal fun read(buffer: ByteBuffer, index: Int, nameBlob: ByteBuffer): LayerRow {
            val b = index * PodLayout.LAYER_ROW_BYTES
            return LayerRow(
                id = buffer.getLong(b + PodLayout.LAYER_OFF_ID),
                kind = buffer.getInt(b + PodLayout.LAYER_OFF_KIND),
                zIndex = buffer.getInt(b + PodLayout.LAYER_OFF_Z_INDEX),
                startFrame = buffer.getInt(b + PodLayout.LAYER_OFF_START),
                endFrame = buffer.getInt(b + PodLayout.LAYER_OFF_END),
                opacity = buffer.getFloat(b + PodLayout.LAYER_OFF_OPACITY),
                flags = buffer.getInt(b + PodLayout.LAYER_OFF_FLAGS),
                effectCount = buffer.getInt(b + PodLayout.LAYER_OFF_EFFECT_COUNT),
                maskCount = buffer.getInt(b + PodLayout.LAYER_OFF_MASK_COUNT),
                keyframeCount = buffer.getInt(b + PodLayout.LAYER_OFF_KEYFRAME_COUNT),
                blendMode = buffer.getInt(b + PodLayout.LAYER_OFF_BLEND_MODE),
                parentIndex = buffer.getInt(b + PodLayout.LAYER_OFF_PARENT_INDEX),
                nameOffset = buffer.getInt(b + PodLayout.LAYER_OFF_NAME_OFFSET),
                nameLength = buffer.getInt(b + PodLayout.LAYER_OFF_NAME_LENGTH),
                nameBlob = nameBlob,
            )
        }
    }
}

/** Um keyframe, para a barra de keyframes da timeline. */
class KeyframeRow internal constructor(
    val property: Int,
    val effectIndex: Int,
    val time: Int,
    val value: Float,
    val interpolation: Int,
) {
    companion object {
        internal fun read(buffer: ByteBuffer, index: Int): KeyframeRow {
            val b = index * PodLayout.KEYFRAME_ROW_BYTES
            return KeyframeRow(
                property = buffer.getInt(b + PodLayout.KF_OFF_PROPERTY),
                effectIndex = buffer.getInt(b + PodLayout.KF_OFF_EFFECT_INDEX),
                time = buffer.getInt(b + PodLayout.KF_OFF_TIME),
                value = buffer.getFloat(b + PodLayout.KF_OFF_VALUE),
                interpolation = buffer.getInt(b + PodLayout.KF_OFF_INTERPOLATION),
            )
        }
    }
}

/**
 * Estado do motor. REUSADO entre frames — `readStatus` sobrescreve os campos.
 *
 * Não é imutável de propósito: alocar um objeto por frame a 60 Hz dá 3600
 * objetos por minuto no heap gerenciado, e o coletor cobraria essa conta durante
 * o playback. A UI lê os campos e os copia para o estado do Compose, que é onde
 * a imutabilidade importa.
 */
class EngineStatus {
    var state: Int = 0
    var lastError: Int = 0
    var errorDetail: String = ""
    var currentFps: Float = 0f
    var averageFrameMs: Float = 0f
    var gpuMs: Float = 0f
    var cpuMs: Float = 0f
    var decodeMs: Float = 0f
    var cacheHitRate: Float = 0f
    var memoryPressure: Float = 0f
    var previewWidth: Int = 0
    var previewHeight: Int = 0
    var previewNumerator: Int = 1
    var previewDenominator: Int = 1
    var previewAuto: Boolean = true
    var playhead: Long = 0
    var duration: Long = 0
    var playing: Boolean = false
    var layerCount: Int = 0
    var selectedCount: Int = 0
    var canUndo: Boolean = false
    var canRedo: Boolean = false
    var undoDepth: Int = 0
    var assetCount: Int = 0
    var dirty: Boolean = false
    var recoveryAvailable: Boolean = false
    var droppedFrames: Int = 0
    var passesExecuted: Int = 0
    var passesCulled: Int = 0
    var gpuMemoryBytes: Long = 0
    var cpuMemoryBytes: Long = 0

    internal fun readFrom(buffer: ByteBuffer) {
        state = buffer.getInt(PodLayout.ST_OFF_STATE)
        lastError = buffer.getInt(PodLayout.ST_OFF_LAST_ERROR)
        currentFps = buffer.getFloat(PodLayout.ST_OFF_CURRENT_FPS)
        averageFrameMs = buffer.getFloat(PodLayout.ST_OFF_AVERAGE_FRAME_MS)
        gpuMs = buffer.getFloat(PodLayout.ST_OFF_GPU_MS)
        cpuMs = buffer.getFloat(PodLayout.ST_OFF_CPU_MS)
        decodeMs = buffer.getFloat(PodLayout.ST_OFF_DECODE_MS)
        cacheHitRate = buffer.getFloat(PodLayout.ST_OFF_CACHE_HIT_RATE)
        memoryPressure = buffer.getFloat(PodLayout.ST_OFF_MEMORY_PRESSURE)
        previewWidth = buffer.getInt(PodLayout.ST_OFF_PREVIEW_WIDTH)
        previewHeight = buffer.getInt(PodLayout.ST_OFF_PREVIEW_HEIGHT)
        previewNumerator = buffer.getInt(PodLayout.ST_OFF_PREVIEW_NUMERATOR)
        previewDenominator = buffer.getInt(PodLayout.ST_OFF_PREVIEW_DENOMINATOR)
        previewAuto = buffer.getInt(PodLayout.ST_OFF_PREVIEW_AUTO) != 0
        playhead = buffer.getLong(PodLayout.ST_OFF_PLAYHEAD)
        duration = buffer.getLong(PodLayout.ST_OFF_DURATION)
        playing = buffer.getInt(PodLayout.ST_OFF_PLAYING) != 0
        layerCount = buffer.getInt(PodLayout.ST_OFF_LAYER_COUNT)
        selectedCount = buffer.getInt(PodLayout.ST_OFF_SELECTED_COUNT)
        canUndo = buffer.getInt(PodLayout.ST_OFF_CAN_UNDO) != 0
        canRedo = buffer.getInt(PodLayout.ST_OFF_CAN_REDO) != 0
        undoDepth = buffer.getInt(PodLayout.ST_OFF_UNDO_DEPTH)
        assetCount = buffer.getInt(PodLayout.ST_OFF_ASSET_COUNT)
        dirty = buffer.getInt(PodLayout.ST_OFF_DIRTY) != 0
        recoveryAvailable = buffer.getInt(PodLayout.ST_OFF_RECOVERY) != 0
        droppedFrames = buffer.getInt(PodLayout.ST_OFF_DROPPED_FRAMES)
        passesExecuted = buffer.getInt(PodLayout.ST_OFF_PASSES_EXECUTED)
        passesCulled = buffer.getInt(PodLayout.ST_OFF_PASSES_CULLED)
        gpuMemoryBytes = buffer.getLong(PodLayout.ST_OFF_GPU_MEMORY)
        cpuMemoryBytes = buffer.getLong(PodLayout.ST_OFF_CPU_MEMORY)
    }
}

/** Telemetria do painel de debug. Só é lida quando o painel está aberto. */
class EngineTelemetry {
    var frameMs: Float = 0f
    var gpuMs: Float = 0f
    var cpuMs: Float = 0f
    var decodeMs: Float = 0f
    var frameCacheHit: Float = 0f
    var pipelineHit: Float = 0f
    var workerCount: Int = 0
    var passesExecuted: Int = 0
    var passesCulled: Int = 0
    var drawCalls: Int = 0
    var triangles: Int = 0
    var particles: Int = 0
    var shaderCount: Int = 0
    var pipelineCount: Int = 0
    var shaderFailures: Int = 0
    var activeEffects: Int = 0
    var activeLayers: Int = 0
    var adaptiveChanges: Int = 0
    var physicalResources: Int = 0
    var logicalResources: Int = 0
    var thermal: Float = 0f
    var throttling: Boolean = false
    var gpuMemoryBytes: Long = 0
    var cpuMemoryBytes: Long = 0
    var undoBlobBytes: Long = 0
    var commandsDropped: Long = 0
    var framesInFlight: Int = 0

    internal fun readFrom(buffer: ByteBuffer) {
        frameMs = buffer.getFloat(PodLayout.TEL_OFF_FRAME_MS)
        gpuMs = buffer.getFloat(PodLayout.TEL_OFF_GPU_MS)
        cpuMs = buffer.getFloat(PodLayout.TEL_OFF_CPU_MS)
        decodeMs = buffer.getFloat(PodLayout.TEL_OFF_DECODE_MS)
        frameCacheHit = buffer.getFloat(PodLayout.TEL_OFF_FRAME_CACHE_HIT)
        pipelineHit = buffer.getFloat(PodLayout.TEL_OFF_PIPELINE_HIT)
        workerCount = buffer.getInt(PodLayout.TEL_OFF_WORKER_COUNT)
        passesExecuted = buffer.getInt(PodLayout.TEL_OFF_PASSES_EXECUTED)
        passesCulled = buffer.getInt(PodLayout.TEL_OFF_PASSES_CULLED)
        drawCalls = buffer.getInt(PodLayout.TEL_OFF_DRAW_CALLS)
        triangles = buffer.getInt(PodLayout.TEL_OFF_TRIANGLES)
        particles = buffer.getInt(PodLayout.TEL_OFF_PARTICLES)
        shaderCount = buffer.getInt(PodLayout.TEL_OFF_SHADER_COUNT)
        pipelineCount = buffer.getInt(PodLayout.TEL_OFF_PIPELINE_COUNT)
        shaderFailures = buffer.getInt(PodLayout.TEL_OFF_SHADER_FAILURES)
        activeEffects = buffer.getInt(PodLayout.TEL_OFF_ACTIVE_EFFECTS)
        activeLayers = buffer.getInt(PodLayout.TEL_OFF_ACTIVE_LAYERS)
        adaptiveChanges = buffer.getInt(PodLayout.TEL_OFF_ADAPTIVE_CHANGES)
        physicalResources = buffer.getInt(PodLayout.TEL_OFF_PHYSICAL_RESOURCES)
        logicalResources = buffer.getInt(PodLayout.TEL_OFF_LOGICAL_RESOURCES)
        thermal = buffer.getFloat(PodLayout.TEL_OFF_THERMAL)
        throttling = buffer.getInt(PodLayout.TEL_OFF_THROTTLING) != 0
        gpuMemoryBytes = buffer.getLong(PodLayout.TEL_OFF_GPU_MEMORY)
        cpuMemoryBytes = buffer.getLong(PodLayout.TEL_OFF_CPU_MEMORY)
        undoBlobBytes = buffer.getLong(PodLayout.TEL_OFF_UNDO_BLOB)
        commandsDropped = buffer.getLong(PodLayout.TEL_OFF_COMMANDS_DROPPED)
        framesInFlight = buffer.getInt(PodLayout.TEL_OFF_FRAMES_IN_FLIGHT)
    }
}

/** Progresso de exportação. */
class ExportProgress {
    var running: Boolean = false
    var finished: Boolean = false
    var result: Int = 0
    var framesTotal: Int = 0
    var framesDone: Int = 0
    var fps: Float = 0f
    var etaSeconds: Int = 0
    var message: String = ""

    internal fun readFrom(buffer: ByteBuffer) {
        running = buffer.getInt(PodLayout.EPD_OFF_RUNNING) != 0
        finished = buffer.getInt(PodLayout.EPD_OFF_FINISHED) != 0
        result = buffer.getInt(PodLayout.EPD_OFF_RESULT)
        framesTotal = buffer.getInt(PodLayout.EPD_OFF_TOTAL)
        framesDone = buffer.getInt(PodLayout.EPD_OFF_DONE)
        fps = buffer.getFloat(PodLayout.EPD_OFF_FPS)
        etaSeconds = buffer.getInt(PodLayout.EPD_OFF_ETA)
        val bytes = ByteArray(95)
        val dup = buffer.duplicate()
        dup.position(PodLayout.EPD_OFF_MESSAGE)
        dup.get(bytes, 0, 95)
        message = String(bytes, Charsets.UTF_8).takeWhile { it != ' ' }
    }
}

/** Cria um buffer direto, com a ordem de bytes nativa do aparelho. */
internal fun directBuffer(bytes: Int): ByteBuffer =
    ByteBuffer.allocateDirect(bytes).also { it.order(ByteOrder.nativeOrder()) }
