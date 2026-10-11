// =============================================================================
//  Aurea / platform / android / GlVideoBridge.hpp
//
//  O caminho "GL do driver" do vídeo (ver aurea/platform/AndroidVideoPath.hpp):
//  o AHardwareBuffer PRIVATE que o MediaCodec entregou vira EGLImage amostrado
//  como YUV cru via EXT_YUV_target — o driver reconstrói croma/AFBC, e a mesma
//  função color.glsl do Vulkan/Metal aplica a matriz/faixa do próprio vídeo. Um
//  um quadrilátero com o crop nas coordenadas desenha a região visível num
//  AHardwareBuffer RGBA8 (EGLImage + FBO). É o que o app antigo fazia com
//  samplerExternalOES comum não é usado: sua conversão depende de metadados
//  privados do buffer, que alguns codecs perdem ao recriar a superfície.
//
//  Um contexto EGL próprio por decoder (ES 3, pbuffer 1×1). Ele só fica
//  corrente DURANTE uma conversão, na thread que chamou: a thread de decode
//  pode acabar e o destrutor pode rodar em outra (a fila de encerramento do
//  MediaManager) sem deixar contexto preso a uma thread morta.
//
//  Sincronização: fence EGL (EGL_KHR_fence_sync) esperado na CPU, na thread de
//  decode, antes de o quadro seguir para o renderer; sem a extensão, glFinish.
//  O alvo RGBA só volta ao conjunto quando o último FrameRef dele sai — e o
//  renderer só solta depois do fence do Vulkan. Nenhum dos dois lados escreve
//  por cima do que o outro ainda lê.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/media/VideoTypes.hpp"
#include "aurea/platform/AndroidVideoPath.hpp"

#include <android/hardware_buffer.h>

#include <memory>

namespace aurea::android {

class GlVideoBridge {
public:
    /// Um alvo RGBA com o quadro convertido. Enquanto o `shared_ptr` existir o
    /// buffer é deste quadro; depois volta ao conjunto do decoder.
    struct Target {
        AHardwareBuffer* buffer = nullptr;
        u32 width = 0, height = 0;
    };

    /// nullptr = este aparelho não tem o que o caminho precisa (EGL, extensões
    /// de imagem nativa / textura externa, shader) — o decoder usa os planos.
    [[nodiscard]] static std::unique_ptr<GlVideoBridge> create(bool diagnosticProbe = false) noexcept;

    /// Optional debug observation of the completed RGBA FBO, before Vulkan.
    /// Four samples at normalized positions (.92,.25), (.42,.55), (.60,.55),
    /// (.25,.55); coordinates follow the decoded image's top-to-bottom memory.
    struct DiagnosticPixels { u8 rgba[16]{}; bool valid = false; };

    /// Teto de alvos RGBA vivos (driver_gl_live_frames do tamanho do vídeo).
    void set_max_live_targets(u32 n) noexcept;
    ~GlVideoBridge();
    GlVideoBridge(const GlVideoBridge&) = delete;
    GlVideoBridge& operator=(const GlVideoBridge&) = delete;

    /// Converte `source` (buffer do decoder) na região `quad` para um alvo RGBA.
    /// BudgetExceeded = todos os alvos em uso (contrapressão, tente de novo);
    /// qualquer outro erro = o caminho GL não serve para este vídeo.
    [[nodiscard]] Status convert(AHardwareBuffer* source, const ExternalQuad& quad, const VideoColorInfo& color,
                                 std::shared_ptr<const Target>& out, DiagnosticPixels* probe = nullptr) noexcept;

    /// Solta as imagens EGL dos buffers do decoder (o ImageReader foi recriado).
    void forget_sources() noexcept;

    [[nodiscard]] u32 max_live_targets() const noexcept;

    /// Native diagnostics: retained EGL imports and the current pool generation.
    /// Old displayed frames may outlive a resize, but their imports must not
    /// remain cached in this decoder or count as reusable current-size targets.
    struct CacheStats { u32 sourceImports = 0, targetImports = 0, liveTargets = 0, freeTargets = 0; u64 generation = 0; };
    [[nodiscard]] CacheStats cache_stats() const noexcept;

    struct Impl;

private:
    explicit GlVideoBridge(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace aurea::android
