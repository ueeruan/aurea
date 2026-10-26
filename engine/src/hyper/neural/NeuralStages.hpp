// =============================================================================
//  Aurea / hyper / neural / NeuralStages.hpp (privado das redes)
//
//  As fábricas das etapas de cada arquivo, para NeuralStages.cpp registrar e
//  para os testes do host (tests/test_hyper_neural.cpp) inspecionarem.
// =============================================================================
#pragma once

#include "aurea/hyper/HyperNeural.hpp"
#include "aurea/hyper/HyperStage.hpp"

#include <memory>
#include <vector>

namespace aurea::hyper::neural {

// NeuralUpscale.cpp — Upscale (General/Anime) e Clean.
[[nodiscard]] std::unique_ptr<HyperStage> make_upscale_stage(StageKind kind, Path path, UpscaleMode mode);
/// O que a última chamada de `process` fez (testes, log).
struct UpscaleStats {
    u32 tiles = 0, skipped = 0, tile = 0, margin = 0, workers = 0;
    u64 peakBytes = 0, historyBytes = 0, budgetPerWorker = 0;
    bool vulkan = false, fastCpu = false, finalCatmull = false;   ///< fastCpu: int8 misto (anime) ou conta fp16 (SPAN)
};
[[nodiscard]] UpscaleStats upscale_stage_stats(const HyperStage& stage) noexcept;
[[nodiscard]] f64 cost_span_lite(const DeviceProfile& p) noexcept;
[[nodiscard]] f64 cost_esrgan(const DeviceProfile& p) noexcept;
[[nodiscard]] f64 cost_anime(const DeviceProfile& p) noexcept;
/// Pesos fp32 do realesr-general-x4v3 misturados ao wdn (w = força do denoise).
[[nodiscard]] Status esrgan_general_weights(f32 denoise, std::vector<unsigned char>& out);
/// 0 SPAN ×2, 1 SPAN ×4, 2 x4v3 (misturado), 3 animevideov3.
class NetRunner;
enum class Backend : u8;
[[nodiscard]] Status load_upscale_net(int net, f32 denoise, Backend backend, bool fp16, bool int8, NetRunner& runner,
                                      std::vector<unsigned char>& owned);
/// Orçamento de trabalho que as etapas usam (memória do sistema + perfil).
[[nodiscard]] u64 inference_budget(const DeviceProfile& profile, u64 fixedBytes) noexcept;

// NeuralMotion.cpp — Motion (RIFE v4.6).
[[nodiscard]] std::unique_ptr<HyperStage> make_motion_stage(Path path);
[[nodiscard]] f64 cost_rife(const DeviceProfile& p) noexcept;

// NeuralFace.cpp — Face (GFPGAN v1.4, sob demanda).
[[nodiscard]] std::unique_ptr<HyperStage> make_face_stage(Path path);
[[nodiscard]] bool face_available(const DeviceProfile& p) noexcept;
[[nodiscard]] f64 cost_gfpgan(const DeviceProfile& p) noexcept;
/// O detector registrado (nulo = nenhum). Thread-safe.
[[nodiscard]] u32 detect_faces(const u8* rgb, u32 w, u32 h, u32 stride, FaceBox* boxes, u32 maxBoxes) noexcept;
/// A rede do GFPGAN baixada, no backend do perfil, contra a saída fp32 do
/// PyTorch guardada no manifesto (padrão de referência 512²).
[[nodiscard]] Status gfpgan_reference_check(const DeviceProfile& profile, f64& psnr, f64& ms) noexcept;
/// Quantos rostos a última chamada de `process` restaurou (testes).
[[nodiscard]] u32 face_stage_restored(const HyperStage& stage) noexcept;

} // namespace aurea::hyper::neural
