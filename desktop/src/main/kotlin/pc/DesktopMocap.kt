package com.aurea.aurea.desktop

/**
 * A ponte do Motion Capture do PC (engine/platform/desktop/DesktopMocap.cpp):
 * vídeo pelo Media Foundation → BlazePose do motor (ncnn) → quadros no
 * formato do motor. A UI puxa os lotes; nada volta pela JNI.
 */
object DesktopMocap {
    /** Abre a sessão (0 = falhou). [info] = erro (0 ok, 1 modelos, 2 vídeo), duração s, Vulkan 0/1, largura, altura, fps. */
    @JvmStatic external fun nativeOpen(modelDir: String, videoPath: String, reduced: Boolean, info: FloatArray): Long

    /**
     * Até [maxFrames] quadros (2 + 33 × 4 floats cada) em [out]; devolve quantos (−1 = erro).
     * [state] = progresso 0..1, largura/altura, fim 0/1, ms de rede por quadro, vezes do detector, quadros com pessoa.
     */
    @JvmStatic external fun nativeNext(session: Long, out: FloatArray, maxFrames: Int, state: FloatArray): Int

    @JvmStatic external fun nativeClose(session: Long)
}
