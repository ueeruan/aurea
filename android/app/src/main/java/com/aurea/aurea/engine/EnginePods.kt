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
    const val LAYER_OFF_OFFSET = 60       // i32 deslocamento do conteúdo

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
    const val KF_OFF_PARAM_INDEX = 20     // u32 (param*4 + componente, efeito)

    // =========================================================================
    // EngineStatusPOD — 256 bytes, quatro linhas de cache.
    // =========================================================================
    const val STATUS_BYTES = 256
    const val ST_OFF_STATE = 0            // i32
    const val ST_OFF_LAST_ERROR = 4       // i32
    const val ST_OFF_ERROR_DETAIL = 8     // char[96]
    const val ST_OFF_MODEL_REVISION = 104 // u32
    const val ST_OFF_COMP_FPS = 112       // f32
    const val ST_OFF_COMP_WIDTH = 116     // u32
    const val ST_OFF_COMP_HEIGHT = 120    // u32
    const val ST_OFF_THUMB_GENERATION = 124 // u32
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
    /** Deslocamento do conteúdo: keyframe local `t` fica na timeline em `t + startFrame - offsetFrames`. */
    val offsetFrames: Int,
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
                offsetFrames = buffer.getInt(b + PodLayout.LAYER_OFF_OFFSET),
                nameOffset = buffer.getInt(b + PodLayout.LAYER_OFF_NAME_OFFSET),
                nameLength = buffer.getInt(b + PodLayout.LAYER_OFF_NAME_LENGTH),
                nameBlob = nameBlob,
            )
        }
    }
}

/** Um keyframe, para a barra de keyframes da timeline. */
/**
 * Um keyframe. `time` é o tempo LOCAL da camada: na timeline fica em
 * `time + start - offset` (ver [LayerDetail]).
 */
data class KeyframeRow(
    val property: Int,
    val effectIndex: Int,
    val time: Int,
    val value: Float,
    val interpolation: Int,
    val paramIndex: Int,
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
                paramIndex = buffer.getInt(b + PodLayout.KF_OFF_PARAM_INDEX),
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
    var compFps: Float = 0f
    var compWidth: Int = 0
    var compHeight: Int = 0
    var modelRevision: Int = 0
    var thumbnailGeneration: Int = 0
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
        compFps = buffer.getFloat(PodLayout.ST_OFF_COMP_FPS)
        compWidth = buffer.getInt(PodLayout.ST_OFF_COMP_WIDTH)
        compHeight = buffer.getInt(PodLayout.ST_OFF_COMP_HEIGHT)
        modelRevision = buffer.getInt(PodLayout.ST_OFF_MODEL_REVISION)
        thumbnailGeneration = buffer.getInt(PodLayout.ST_OFF_THUMB_GENERATION)
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
        message = String(bytes, Charsets.UTF_8).takeWhile { it.code != 0 }
    }
}

/** Cria um buffer direto, com a ordem de bytes nativa do aparelho. */
internal fun directBuffer(bytes: Int): ByteBuffer =
    ByteBuffer.allocateDirect(bytes).also { it.order(ByteOrder.nativeOrder()) }

// =============================================================================
// Efeitos
// =============================================================================

/** Lê `length` bytes UTF-8 do blob de texto das consultas. */
internal fun ByteBuffer.utf8(offset: Int, length: Int): String {
    if (length <= 0 || offset < 0 || offset + length > capacity()) return ""
    val bytes = ByteArray(length)
    val dup = duplicate()
    dup.position(offset)
    dup.get(bytes, 0, length)
    return String(bytes, Charsets.UTF_8)
}

/** Espelho de `bridge::EffectCatalogRow` (32 bytes). */
data class EffectCatalogEntry(
    val typeId: Int,
    val effectClass: Int,
    val paramCount: Int,
    val name: String,
    val category: String,
) {
    companion object {
        const val ROW_BYTES = 32

        internal fun read(rows: ByteBuffer, index: Int, blob: ByteBuffer): EffectCatalogEntry {
            val b = index * ROW_BYTES
            return EffectCatalogEntry(
                typeId = rows.getInt(b),
                effectClass = rows.getInt(b + 4),
                paramCount = rows.getInt(b + 8),
                name = blob.utf8(rows.getInt(b + 12), rows.getInt(b + 16)),
                category = blob.utf8(rows.getInt(b + 20), rows.getInt(b + 24)),
            )
        }
    }
}

/** Espelho de `bridge::LayerEffectRow` (32 bytes). */
data class LayerEffect(
    val effectId: Int,
    val typeId: Int,
    val enabled: Boolean,
    val paramCount: Int,
    val name: String,
    val known: Boolean,
) {
    companion object {
        const val ROW_BYTES = 32

        internal fun read(rows: ByteBuffer, index: Int, blob: ByteBuffer): LayerEffect {
            val b = index * ROW_BYTES
            return LayerEffect(
                effectId = rows.getInt(b),
                typeId = rows.getInt(b + 4),
                enabled = rows.getInt(b + 8) != 0,
                paramCount = rows.getInt(b + 12),
                name = blob.utf8(rows.getInt(b + 16), rows.getInt(b + 20)),
                known = rows.getInt(b + 24) != 0,
            )
        }
    }
}

/** `aurea::ParamType` e `aurea::ParamFlags`. */
object ParamType {
    const val FLOAT = 0
    const val INT = 1
    const val BOOL = 2
    const val COLOR = 3
    const val POINT2D = 4
    const val POINT3D = 5
    const val ANGLE = 6
    const val ENUM = 7
    const val CURVE = 8
    const val GRADIENT = 9
    const val LAYER_REFERENCE = 10
    const val TEXTURE_REFERENCE = 11

    /** Componentes numéricos animáveis (espelho de `aurea::component_count`). */
    fun componentCount(type: Int): Int = when (type) {
        FLOAT, INT, BOOL, ANGLE, ENUM -> 1
        POINT2D -> 2
        POINT3D -> 3
        COLOR -> 4
        else -> 0
    }

    const val FLAG_ANIMATABLE = 1 shl 0
    const val FLAG_PIXELS = 1 shl 1
    const val FLAG_PERCENT = 1 shl 2
    const val FLAG_RELATIVE = 1 shl 3
    const val FLAG_HIDDEN = 1 shl 4
}

/** Espelho de `bridge::EffectParamRow` (96 bytes). `value` é o valor no playhead. */
class EffectParam(
    val index: Int,
    val type: Int,
    val flags: Int,
    val min: Float,
    val max: Float,
    val value: FloatArray,
    val defaultValue: FloatArray,
    val label: String,
    val unit: String,
    val enumLabels: List<String>,
    val animated: Boolean,
) {
    val hidden: Boolean get() = (flags and ParamType.FLAG_HIDDEN) != 0

    override fun equals(other: Any?): Boolean =
        other is EffectParam && index == other.index && type == other.type &&
            value.contentEquals(other.value) && label == other.label && animated == other.animated

    override fun hashCode(): Int = 31 * (31 * index + type) + value.contentHashCode()

    companion object {
        const val ROW_BYTES = 96

        internal fun read(rows: ByteBuffer, i: Int, blob: ByteBuffer): EffectParam {
            val b = i * ROW_BYTES
            val enumText = blob.utf8(rows.getInt(b + 72), rows.getInt(b + 76))
            return EffectParam(
                index = rows.getInt(b),
                type = rows.getInt(b + 4),
                flags = rows.getInt(b + 8),
                min = rows.getFloat(b + 16),
                max = rows.getFloat(b + 20),
                value = FloatArray(4) { rows.getFloat(b + 24 + it * 4) },
                defaultValue = FloatArray(4) { rows.getFloat(b + 40 + it * 4) },
                label = blob.utf8(rows.getInt(b + 56), rows.getInt(b + 60)),
                unit = blob.utf8(rows.getInt(b + 64), rows.getInt(b + 68)),
                enumLabels = if (enumText.isEmpty()) emptyList() else enumText.split('|'),
                animated = rows.getInt(b + 80) != 0,
            )
        }
    }
}

// =============================================================================
// Métricas do painel DEV — espelho de `bridge::PerfPOD` (256 bytes)
// =============================================================================
data class PerfStats(
    val previewFps: Float = 0f,
    val cpuFrameMs: Float = 0f,
    val gpuFrameMs: Float = 0f,
    val decodeMs: Float = 0f,
    val colorConvMs: Float = 0f,
    val effectsMs: Float = 0f,
    val blurMs: Float = 0f,
    val glowMs: Float = 0f,
    val compositeMs: Float = 0f,
    val outputMs: Float = 0f,
    val presentMs: Float = 0f,
    val acquireMs: Float = 0f,
    val lastSeekMs: Float = 0f,
    val frameBudgetMs: Float = 0f,
    val droppedFrames: Int = 0,
    val droppedRecent: Int = 0,
    val renderScaleNum: Int = 1,
    val renderScaleDen: Int = 1,
    val renderAuto: Boolean = true,
    val previewWidth: Int = 0,
    val previewHeight: Int = 0,
    val decodedCacheFrames: Int = 0,
    val decodedCacheBytes: Long = 0,
    val ramBytes: Long = 0,
    val gpuMemoryBytes: Long = 0,
    val transientBytes: Long = 0,
    val passesExecuted: Int = 0,
    val passesCulled: Int = 0,
    val texturesCreated: Int = 0,
    val transientTextures: Int = 0,
    val physicalTextures: Int = 0,
    val aliasedTextures: Int = 0,
    val pipelineCompilesLive: Int = 0,
    val pipelinesTotal: Int = 0,
    val zeroCopy: Boolean = false,
    val hardwareDecoder: Boolean = false,
    val gpuTimers: Boolean = false,
    val seeks: Int = 0,
    val coalesced: Int = 0,
    val staleFrames: Int = 0,
    val layersRendered: Int = 0,
    val thermal: Int = 0,
    val decoder: String = "",
    val gpuName: String = "",
) {
    companion object {
        const val BYTES = 256

        private fun ByteBuffer.cString(offset: Int, max: Int): String {
            var n = 0
            while (n < max && get(offset + n) != 0.toByte()) n++
            return utf8(offset, n)
        }

        internal fun read(b: ByteBuffer) = PerfStats(
            previewFps = b.getFloat(0),
            cpuFrameMs = b.getFloat(4),
            gpuFrameMs = b.getFloat(8),
            decodeMs = b.getFloat(12),
            colorConvMs = b.getFloat(16),
            effectsMs = b.getFloat(20),
            blurMs = b.getFloat(24),
            glowMs = b.getFloat(28),
            compositeMs = b.getFloat(32),
            outputMs = b.getFloat(36),
            presentMs = b.getFloat(40),
            acquireMs = b.getFloat(44),
            lastSeekMs = b.getFloat(48),
            frameBudgetMs = b.getFloat(52),
            droppedFrames = b.getInt(56),
            droppedRecent = b.getInt(60),
            renderScaleNum = b.getInt(64),
            renderScaleDen = b.getInt(68),
            renderAuto = b.getInt(72) != 0,
            previewWidth = b.getInt(76),
            previewHeight = b.getInt(80),
            decodedCacheFrames = b.getInt(84),
            decodedCacheBytes = b.getLong(88),
            ramBytes = b.getLong(96),
            gpuMemoryBytes = b.getLong(104),
            transientBytes = b.getLong(112),
            passesExecuted = b.getInt(120),
            passesCulled = b.getInt(124),
            texturesCreated = b.getInt(128),
            transientTextures = b.getInt(132),
            physicalTextures = b.getInt(136),
            aliasedTextures = b.getInt(140),
            pipelineCompilesLive = b.getInt(144),
            pipelinesTotal = b.getInt(148),
            zeroCopy = b.getInt(152) != 0,
            hardwareDecoder = b.getInt(156) != 0,
            gpuTimers = b.getInt(160) != 0,
            seeks = b.getInt(164),
            coalesced = b.getInt(168),
            staleFrames = b.getInt(172),
            layersRendered = b.getInt(176),
            thermal = b.getInt(180),
            decoder = b.cString(184, 48),
            gpuName = b.cString(232, 24),
        )
    }
}

// =============================================================================
// Detalhe da camada — espelho de `bridge::LayerDetailPOD` (256 bytes)
// =============================================================================

/** Propriedades de transform (`aurea::TrackProperty`), na ordem dos bits de `animatedMask`. */
object TrackProperty {
    const val POSITION_X = 0
    const val POSITION_Y = 1
    const val POSITION_Z = 2
    const val SCALE_X = 3
    const val SCALE_Y = 4
    const val SCALE_Z = 5
    const val ROTATION_X = 6
    const val ROTATION_Y = 7
    const val ROTATION_Z = 8
    const val ANCHOR_X = 9
    const val ANCHOR_Y = 10
    const val ANCHOR_Z = 11
    const val OPACITY = 12
    const val SKEW_X = 13
    const val SKEW_Y = 14
    const val TIME_REMAP = 30
    const val EFFECT_PARAM = 31
    const val AUDIO_VOLUME = 32
}

/**
 * Transform avaliado no playhead (animação aplicada), o que está animado e o
 * que tem keyframe exatamente no playhead. A UI NÃO guarda cópia: relê quando
 * `modelRevision` ou o playhead mudam.
 */
data class LayerDetail(
    val id: Long,
    val kind: Int,
    val flags: Int,
    val startFrame: Int,
    val endFrame: Int,
    val offsetFrames: Int,
    val blendMode: Int,
    val position: List<Float>,
    val scale: List<Float>,
    val rotation: List<Float>,
    val anchor: List<Float>,
    val opacity: Float,
    val skew: List<Float>,
    val animatedMask: Int,
    val keyAtPlayheadMask: Int,
    val sourceWidth: Int,
    val sourceHeight: Int,
    val sourceFps: Float,
    val sourceFrames: Int,
    val effectCount: Int,
    val maskCount: Int,
    val localPlayhead: Int,
    val parentId: Long,
    val audioGain: Float = 1f,
    val audioVolume: Float = 1f,
    val audioPan: Float = 0f,
    val audioFadeIn: Int = 0,
    val audioFadeOut: Int = 0,
    val audioFlags: Int = 0,
    val speed: Float = 1f,
    val timeFlags: Int = 0,
    val shapeTypePoints: Int = 0,
    val shapeFill: Int = 0,
    val shapeStroke: Int = 0,
    val shapeStrokeWidth: Float = 0f,
    val shapeCorner: Float = 0f,
    val shapeInner: Float = 0f,
    /** Cantos TL,TR,BR,BL em px da composição (mundo, com pais e câmera), do motor. */
    val worldCorners: FloatArray = FloatArray(8),
    /** Pai → composição (a b c d tx ty). Identidade sem pai. */
    val parentAffine: FloatArray = floatArrayOf(1f, 0f, 0f, 1f, 0f, 0f),
    val geomFlags: Int = 0,
    val transitions: Int = 0,
) {
    val transitionIn: Int get() = transitions and 0xF
    val transitionOut: Int get() = (transitions shr 4) and 0xF
    val transitionInFrames: Int get() = (transitions ushr 8) and 0xFFF
    val transitionOutFrames: Int get() = (transitions ushr 20) and 0xFFF

    val hasWorldCorners: Boolean get() = (geomFlags and 1) != 0
    val perspective: Boolean get() = (geomFlags and 2) != 0

    /** Ponto no espaço do pai → composição. */
    fun parentToComp(x: Float, y: Float, out: FloatArray) {
        val a = parentAffine
        out[0] = a[0] * x + a[2] * y + a[4]
        out[1] = a[1] * x + a[3] * y + a[5]
    }

    /** Composição → espaço do pai (o que a posição local usa). */
    fun compToParent(x: Float, y: Float, out: FloatArray) {
        val a = parentAffine
        val det = a[0] * a[3] - a[2] * a[1]
        if (kotlin.math.abs(det) < 1e-9f) { out[0] = x; out[1] = y; return }
        val dx = x - a[4]
        val dy = y - a[5]
        out[0] = (a[3] * dx - a[2] * dy) / det
        out[1] = (-a[1] * dx + a[0] * dy) / det
    }

    val reversed: Boolean get() = (timeFlags and 1) != 0
    val motionBlur: Boolean get() = (timeFlags and 2) != 0
    val timeRemap: Boolean get() = (timeFlags and 4) != 0
    /** 0 repete o quadro, 1 mistura, 2 movimento de pixels (optical flow). */
    val vectorBlur: Boolean get() = (timeFlags and 32) != 0
    val frameBlendMode: Int get() = if ((timeFlags and 16) != 0) 2 else if ((timeFlags and 8) != 0) 1 else 0

    val audioMuted: Boolean get() = (audioFlags and 1) != 0
    val audioSolo: Boolean get() = (audioFlags and 2) != 0
    /** A camada tem som de verdade (vídeo com trilha, ou camada de áudio). */
    val hasAudio: Boolean get() = (audioFlags and 4) != 0
    val volumeAnimated: Boolean get() = (audioFlags and 8) != 0

    fun isAnimated(property: Int) = (animatedMask and (1 shl property)) != 0
    fun hasKeyAtPlayhead(property: Int) = (keyAtPlayheadMask and (1 shl property)) != 0

    /** Tempo local (dos keyframes) → frame da timeline. */
    fun timelineFrame(localFrame: Int) = localFrame + startFrame - offsetFrames

    /** Frame da timeline → tempo local. */
    fun localFrame(timelineFrame: Int) = timelineFrame - startFrame + offsetFrames

    val visible: Boolean get() = (flags and PodLayout.FLAG_VISIBLE) != 0
    val locked: Boolean get() = (flags and PodLayout.FLAG_LOCKED) != 0

    companion object {
        const val BYTES = 256

        internal fun read(b: ByteBuffer): LayerDetail {
            fun v3(o: Int) = listOf(b.getFloat(o), b.getFloat(o + 4), b.getFloat(o + 8))
            return LayerDetail(
                id = b.getLong(0),
                kind = b.getInt(8),
                flags = b.getInt(12),
                startFrame = b.getInt(16),
                endFrame = b.getInt(20),
                offsetFrames = b.getInt(24),
                blendMode = b.getInt(28),
                position = v3(32),
                scale = v3(44),
                rotation = v3(56),
                anchor = v3(68),
                opacity = b.getFloat(80),
                skew = listOf(b.getFloat(84), b.getFloat(88)),
                animatedMask = b.getInt(92),
                keyAtPlayheadMask = b.getInt(96),
                sourceWidth = b.getInt(100),
                sourceHeight = b.getInt(104),
                sourceFps = b.getFloat(108),
                sourceFrames = b.getInt(112),
                effectCount = b.getInt(116),
                maskCount = b.getInt(120),
                localPlayhead = b.getInt(124),
                parentId = b.getLong(128),
                audioGain = b.getFloat(136),
                audioVolume = b.getFloat(140),
                audioPan = b.getFloat(144),
                audioFadeIn = b.getInt(148),
                audioFadeOut = b.getInt(152),
                audioFlags = b.getInt(156),
                speed = b.getFloat(160),
                timeFlags = b.getInt(164),
                shapeTypePoints = b.getInt(168),
                shapeFill = b.getInt(172),
                shapeStroke = b.getInt(176),
                shapeStrokeWidth = b.getFloat(180),
                shapeCorner = b.getFloat(184),
                shapeInner = b.getFloat(188),
                worldCorners = FloatArray(8) { b.getFloat(192 + it * 4) },
                parentAffine = FloatArray(6) { b.getFloat(224 + it * 4) },
                geomFlags = b.getInt(248),
                transitions = b.getInt(252),
            )
        }
    }
}


/** Texto da camada de texto (lido por `queryText`, não cabe no LayerDetail). */
data class TextDetail(
    val content: String,
    val size: Float,
    val color: FloatArray,
    val strokeWidth: Float,
    val strokeColor: FloatArray,
    val alignment: Int,
    val lineHeight: Float,
    val tracking: Float,
) {
    companion object {
        fun of(content: String, v: FloatArray) = TextDetail(
            content = content,
            size = v[0],
            color = floatArrayOf(v[1], v[2], v[3], v[4]),
            strokeWidth = v[5],
            strokeColor = floatArrayOf(v[6], v[7], v[8], v[9]),
            alignment = v[10].toInt(),
            lineHeight = v[11],
            tracking = v[12],
        )
    }
}
