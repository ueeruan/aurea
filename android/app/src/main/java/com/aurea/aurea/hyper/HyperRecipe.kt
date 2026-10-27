package com.aurea.aurea.hyper

import kotlin.math.roundToInt

// =============================================================================
//  AureaHyper (0.3.2) — o espelho de aurea::hyper::Recipe (engine/include/
//  aurea/hyper/Hyper.hpp) e as receitas prontas da tela 2 da Home.
//
//  A regra fica no motor (sanitize/is_identity): aqui só a forma dos campos e
//  o empacotamento para a ponte. O mesmo arquivo no iOS: HyperModel.swift.
// =============================================================================

/** hyper::Path. O índice é o de Estimate::seconds. */
enum class HyperPath(val code: Int) {
    Auto(0), Turbo(1), Lite(2), Max(3);

    companion object {
        fun of(code: Int): HyperPath = entries.firstOrNull { it.code == code } ?: Turbo
        /** Os três que a tela oferece (Auto = o padrão do aparelho). */
        val Choices = listOf(Turbo, Lite, Max)
    }
}

/** hyper::UpscaleMode. */
enum class HyperUpscaleMode(val code: Int) { General(0), Anime(1) }

/** hyper::Recipe. Zeros/false = etapa desligada. */
data class HyperRecipe(
    val upscale: Int = 1,
    val upscaleMode: HyperUpscaleMode = HyperUpscaleMode.General,
    val denoise: Float = 0f,
    val deblock: Boolean = false,
    /** 0 desligado, 1 automático (só se entrelaçado), 2 sempre. */
    val deinterlace: Int = 0,
    /** Desligado por padrão: pode "trocar" o rosto. */
    val face: Boolean = false,
    val faceStrength: Float = 0.5f,
    /** 0 = manter. */
    val targetFps: Double = 0.0,
    val slowMotion: Int = 1,
    val stabilize: Boolean = false,
    val stabilizeStrength: Float = 0.5f,
    val stabilizeToKeyframes: Boolean = false,
    val path: HyperPath = HyperPath.Auto,
) {
    /** Nenhuma etapa ligada (o motor confere de novo com `is_identity`). */
    val identity: Boolean get() = upscale <= 1 && denoise <= 0f && !deblock && deinterlace == 0 && !face &&
        targetFps <= 0.0 && slowMotion <= 1 && !stabilize

    /**
     * A ordem que a ponte (aurea_jni.cpp / AureaEngine.mm `hyper_recipe_from`)
     * lê. Mudar aqui exige mudar lá.
     */
    fun pack(): DoubleArray = doubleArrayOf(
        upscale.toDouble(), upscaleMode.code.toDouble(), denoise.toDouble(), if (deblock) 1.0 else 0.0,
        deinterlace.toDouble(), if (face) 1.0 else 0.0, faceStrength.toDouble(), targetFps, slowMotion.toDouble(),
        if (stabilize) 1.0 else 0.0, stabilizeStrength.toDouble(), if (stabilizeToKeyframes) 1.0 else 0.0, path.code.toDouble(),
    )

    /** Nome curto para o título do projeto ("Upscale 2× · 60 fps"). */
    fun summary(): String = buildList {
        if (denoise > 0f || deblock || deinterlace > 0) add("Clean")
        if (stabilize) add("Stable")
        if (upscale > 1) add("Upscale ${upscale}×")
        if (face) add("Face")
        if (targetFps > 0.0) add("${targetFps.roundToInt()} fps")
        if (slowMotion > 1) add("Slow ${slowMotion}×")
    }.joinToString(" · ").ifBlank { "Hyper" }

    companion object {
        const val PACKED_SIZE = 13
        val UpscaleFactors = listOf(1, 2, 4)
        val SlowMotionFactors = listOf(1, 2, 4, 8)
        val TargetFpsChoices = listOf(0.0, 30.0, 60.0)
    }
}

/**
 * As receitas da tela 2. `variants` = as escolhas dentro do cartão (2×/4×,
 * 2×/4×/8×); a primeira é a padrão.
 */
enum class HyperPreset(val id: String, val variants: List<Int> = emptyList()) {
    Restore("restore"),
    Sharper("sharper", listOf(2, 4)),
    Fps60("fps60"),
    SlowMotion("slowmo", listOf(2, 4, 8)),
    Anime("anime"),
    Stabilize("stabilize");

    fun recipe(variant: Int = variants.firstOrNull() ?: 0): HyperRecipe = when (this) {
        // Clean + Upscale 2× + Face (o rosto é o risco; a tela avisa).
        Restore -> HyperRecipe(upscale = 2, denoise = 0.5f, deblock = true, deinterlace = 1, face = true, faceStrength = 0.5f)
        Sharper -> HyperRecipe(upscale = if (variant == 4) 4 else 2)
        Fps60 -> HyperRecipe(targetFps = 60.0)
        SlowMotion -> HyperRecipe(slowMotion = variant.takeIf { it in HyperRecipe.SlowMotionFactors && it > 1 } ?: 2)
        Anime -> HyperRecipe(upscale = 2, upscaleMode = HyperUpscaleMode.Anime)
        Stabilize -> HyperRecipe(stabilize = true, stabilizeStrength = 0.5f)
    }
}

/** hyper::DeviceProfile, como a ponte devolve (JSON). */
data class HyperDeviceProfile(
    val measured: Boolean = false,
    val abi64: Boolean = false,
    val freeRamBytes: Long = 0,
    val vulkan: Boolean = false,
    val vulkanFp16: Boolean = false,
    val gles3: Boolean = false,
    val npu: Boolean = false,
    val turboMsPerFrame: Float = 0f,
    val liteMsPerTile: Float = 0f,
    val thermalAfter: Float = 0f,
    val defaultPath: HyperPath = HyperPath.Turbo,
    val tile: Int = 128,
    val tileMargin: Int = 12,
    val backendNote: String = "",
) {
    companion object {
        fun parse(json: String?): HyperDeviceProfile? = runCatching {
            val o = org.json.JSONObject(json ?: return null)
            HyperDeviceProfile(
                measured = o.optBoolean("measured"), abi64 = o.optBoolean("abi64"), freeRamBytes = o.optLong("freeRamBytes"),
                vulkan = o.optBoolean("vulkan"), vulkanFp16 = o.optBoolean("vulkanFp16"), gles3 = o.optBoolean("gles3"),
                npu = o.optBoolean("npu"), turboMsPerFrame = o.optDouble("turboMsPerFrame", 0.0).toFloat(),
                liteMsPerTile = o.optDouble("liteMsPerTile", 0.0).toFloat(), thermalAfter = o.optDouble("thermalAfter", 0.0).toFloat(),
                defaultPath = HyperPath.of(o.optInt("defaultPath", 1)), tile = o.optInt("tile", 128),
                tileMargin = o.optInt("tileMargin", 12), backendNote = o.optString("backendNote"),
            )
        }.getOrNull()
    }
}

/** hyper::Estimate: segundos por caminho (< 0 = indisponível) e a saída. */
data class HyperEstimate(
    val seconds: DoubleArray = doubleArrayOf(-1.0, -1.0, -1.0, -1.0),
    val peakBytes: Long = 0,
    val outputWidth: Int = 0,
    val outputHeight: Int = 0,
    val outputFps: Double = 0.0,
) {
    fun secondsFor(path: HyperPath): Double = seconds.getOrElse(path.code) { -1.0 }
    fun available(path: HyperPath): Boolean = secondsFor(path) >= 0.0

    override fun equals(other: Any?): Boolean = other is HyperEstimate && seconds.contentEquals(other.seconds) &&
        peakBytes == other.peakBytes && outputWidth == other.outputWidth && outputHeight == other.outputHeight && outputFps == other.outputFps
    override fun hashCode(): Int = seconds.contentHashCode() * 31 + outputWidth * 7 + outputHeight

    companion object {
        /** [s0, s1, s2, s3, peakBytes, outW, outH, outFps] (aurea_jni.cpp nativeHyperEstimate). */
        fun decode(v: DoubleArray?): HyperEstimate? {
            if (v == null || v.size < 8) return null
            return HyperEstimate(v.copyOfRange(0, 4), v[4].toLong(), v[5].toInt(), v[6].toInt(), v[7])
        }
    }
}

/** hyper::Step — a lista "honesta" da tela 4. */
enum class HyperStep { Prepare, Clean, Stable, Upscale, Face, Motion, Encode, Finish }

/** O estado de um passo: 0 esperando, 1 rodando, 2 feito, 3 pulado. */
enum class HyperStepState { Waiting, Running, Done, Skipped;
    companion object { fun of(code: Int): HyperStepState = entries.getOrElse(code) { Waiting } }
}

/**
 * Por que um caminho não está disponível. 0.3.4 (bugs-034, hyper): sem motivo
 * de APARELHO (32 bits, GPU, medição): todo caminho vale em qualquer celular,
 * por conta e risco da pessoa ([HyperPathRules.risky]). Só sobra o motivo dos
 * AJUSTES: nenhuma etapa pedida tem rede nesse caminho (igual em todo aparelho).
 */
enum class HyperPathBlock { None, Unavailable }

object HyperPathRules {
    /** A decisão é do motor (Estimate < 0 = nenhuma rede para estes ajustes); o perfil não bloqueia. */
    @Suppress("UNUSED_PARAMETER")
    // 0.3.5: nenhum caminho é bloqueado. Sem estimativa do motor (vídeo que não
    // deu para medir, rede ainda não baixada) a tela diz "tempo desconhecido" e a
    // pessoa escolhe assim mesmo — se algo faltar, o trabalho mostra o erro.
    fun block(path: HyperPath, estimate: HyperEstimate?, profile: HyperDeviceProfile?): HyperPathBlock = HyperPathBlock.None

    /**
     * O aviso "por sua conta e risco": um caminho com rede (Lite/Max) mais pesado
     * que o sugerido pelo aparelho, ou num aparelho que ainda não foi medido.
     * Só avisa; não desliga nada.
     */
    fun risky(path: HyperPath, profile: HyperDeviceProfile?): Boolean {
        if (path != HyperPath.Lite && path != HyperPath.Max) return false
        if (profile == null || !profile.measured) return true
        val suggested = profile.defaultPath.takeIf { it != HyperPath.Auto } ?: HyperPath.Turbo
        return path.code > suggested.code
    }

    /** O caminho que a tela pré-seleciona: o sugerido pelo aparelho, se disponível; senão o primeiro disponível. */
    fun initial(estimate: HyperEstimate?, profile: HyperDeviceProfile?): HyperPath {
        val suggested = profile?.defaultPath?.takeIf { it != HyperPath.Auto } ?: HyperPath.Turbo
        if (estimate == null || estimate.available(suggested)) return suggested
        return HyperPath.Choices.firstOrNull { estimate.available(it) } ?: HyperPath.Turbo
    }
}

/**
 * hyper::JobSteps como a ponte devolve (aurea_jni.cpp nativeHyperJobSteps):
 * [estado por passo ×8, ativo, rodando, terminou, resultado, passagem,
 * passagens, som mudo, progresso ×1000, caminho por passo ×8, largura,
 * altura, quadros]. Estado 3 = o passo não existe neste job.
 */
data class HyperJobInfo(
    val states: List<Int>,
    val active: Boolean,
    val running: Boolean,
    val finished: Boolean,
    val result: Int,
    val pass: Int,
    val passCount: Int,
    val audioMuted: Boolean,
    val progress: Float,
    val paths: List<HyperPath>,
) {
    fun present(step: HyperStep): Boolean = states.getOrElse(step.ordinal) { 3 } != 3
    fun state(step: HyperStep): HyperStepState = when (states.getOrElse(step.ordinal) { 0 }) {
        1 -> HyperStepState.Running
        2 -> HyperStepState.Done
        3 -> HyperStepState.Skipped
        else -> HyperStepState.Waiting
    }
    fun path(step: HyperStep): HyperPath = paths.getOrElse(step.ordinal) { HyperPath.Auto }

    companion object {
        const val SIZE = 27
        fun decode(v: IntArray?): HyperJobInfo? {
            if (v == null || v.size < SIZE) return null
            val k = HyperStep.entries.size
            return HyperJobInfo(
                states = (0 until k).map { v[it] }, active = v[k] != 0, running = v[k + 1] != 0, finished = v[k + 2] != 0,
                result = v[k + 3], pass = v[k + 4], passCount = v[k + 5], audioMuted = v[k + 6] != 0,
                progress = v[k + 7] / 1000f, paths = (0 until k).map { HyperPath.of(v[k + 8 + it]) },
            )
        }
    }
}
