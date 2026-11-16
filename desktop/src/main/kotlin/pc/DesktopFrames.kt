package com.aurea.aurea.desktop

import java.nio.ByteBuffer

/**
 * A ponte do palco do PC (engine/platform/android/aurea_jni.cpp, parte desktop):
 * o motor publica o quadro renderizado na GPU; aqui ele é copiado para um
 * buffer direto. `AureaEngine` continua sendo a única porta para o resto.
 */
object DesktopFrames {
    /** Serial do quadro copiado (0 = nada novo). [outInfo] = largura, altura, bytes por linha. BGRA pré-multiplicado. */
    @JvmStatic external fun nativeAcquireFrame(engineHandle: Long, dst: ByteBuffer, capacityBytes: Int, outInfo: IntArray): Long

    /** O nome da GPU que o motor escolheu (Vulkan). */
    @JvmStatic external fun nativeGpuName(engineHandle: Long): String?
}
