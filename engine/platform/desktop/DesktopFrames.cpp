// =============================================================================
//  Aurea / platform / desktop / DesktopFrames.cpp
//
//  Aurea PC: a bomba de quadros da interface Compose Desktop
//  (com.aurea.aurea.desktop.DesktopFrames). Fica fora do AureaEngine.kt de
//  propósito: a classe do Android não muda.
//
//  O motor desenha o preview num alvo próprio (apresentação no host,
//  gpu/vulkan/VulkanHostPresent.cpp); aqui a UI pega o quadro terminado mais
//  novo, a cada vsync dela, e o desenha por baixo dos gizmos.
// =============================================================================
#include <jni.h>

#include "VulkanBackend.hpp"
#include "aurea/render/GPUBackend.hpp"

#include <algorithm>

aurea::GPUBackend* aurea_desktop_backend(jlong handle) noexcept;   // aurea_jni.cpp

namespace {
[[nodiscard]] aurea::vk::Backend* host_backend(jlong handle) noexcept {
    // No PC o backend é sempre o Vulkan (aurea_jni.cpp, nativeInitialize).
    return static_cast<aurea::vk::Backend*>(aurea_desktop_backend(handle));
}
} // namespace

#define AUREA_DESKTOP_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_aurea_aurea_desktop_DesktopFrames_##name

/// Copia o quadro novo para o ByteBuffer direto e devolve o número de série
/// dele (crescente). 0 = nada novo desde a última chamada, nada anexado, ou
/// buffer pequeno — neste caso `outInfo` já traz o tamanho para realocar.
/// `outInfo` = [largura, altura, bytes por linha]. Pixels: BGRA8, sRGB como
/// na tela, alfa pré-multiplicado (o passe de saída escreve opaco).
AUREA_DESKTOP_FN(jlong, nativeAcquireFrame)(JNIEnv* env, jclass, jlong engineHandle, jobject directByteBuffer,
                                            jint capacityBytes, jintArray outInfo) {
    aurea::vk::Backend* gpu = host_backend(engineHandle);
    if (!gpu) return 0;
    void* dst = directByteBuffer ? env->GetDirectBufferAddress(directByteBuffer) : nullptr;
    jlong capacity = 0;
    if (dst) {
        capacity = std::min<jlong>(std::max<jint>(0, capacityBytes), env->GetDirectBufferCapacity(directByteBuffer));
        if (capacity < 0) capacity = 0;
    }
    aurea::vk::Backend::HostFrameInfo info;
    const aurea::u64 serial = gpu->copy_host_frame(dst, static_cast<aurea::usize>(capacity), info);
    if (outInfo && env->GetArrayLength(outInfo) >= 3 && info.width) {
        const jint values[3] = {static_cast<jint>(info.width), static_cast<jint>(info.height),
                                static_cast<jint>(info.rowBytes)};
        env->SetIntArrayRegion(outInfo, 0, 3, values);
    }
    return static_cast<jlong>(serial);
}

/// Nome da GPU Vulkan que o motor escolheu (a dedicada, quando há).
AUREA_DESKTOP_FN(jstring, nativeGpuName)(JNIEnv* env, jclass, jlong engineHandle) {
    aurea::GPUBackend* gpu = aurea_desktop_backend(engineHandle);
    return env->NewStringUTF(gpu ? gpu->capabilities().deviceName.c_str() : "");
}
