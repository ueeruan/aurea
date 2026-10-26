// =============================================================================
//  Aurea / hyper / HyperRegistry.cpp — o registro das etapas Lite/Max.
//
//  Embutido: Hyper Upscale Anime (Lite e Max) com o Real-ESRGAN animevideov3
//  da 0.1.4 (ai/Upscaler, pesos em engine/assets/ai). É o caminho do antigo
//  "Upscale com IA" do export (ExportSettings::aiUpscale). As redes novas
//  (src/hyper/neural/**) entram por `register_neural_stages`.
// =============================================================================
#include "aurea/hyper/HyperStage.hpp"

#include "aurea/ai/TemporalStabilizer.hpp"
#include "aurea/ai/Upscaler.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/export/UpscaleColor.hpp"

#include <algorithm>
#include <mutex>

namespace aurea::hyper {

void StageRegistry::add(const StageDescriptor& descriptor) {
    if (!descriptor.create) return;
    entries_.push_back(descriptor);
}

const StageDescriptor* StageRegistry::find(StageKind kind, Path path, i32 mode,
                                           const DeviceProfile& profile) const noexcept {
    const StageDescriptor* best = nullptr;
    for (const StageDescriptor& d : entries_) {
        if (d.kind != kind || d.path != path) continue;
        if (d.mode >= 0 && mode >= 0 && d.mode != mode) continue;
        if (d.available && !d.available(profile)) continue;
        if (!best || d.priority > best->priority) best = &d;
    }
    return best;
}

namespace {

/// Real-ESRGAN animevideov3 (×2 usa o ×4 da rede + redução bicúbica de lá).
/// Lite: tile do aparelho (64–96). Max: tile 128 (halo de 20 px custa 1,7× a
/// área útil em 128 contra 2,6× em 64). Os dois mantêm a estabilização
/// temporal conservadora do detalhe parado (ai/TemporalStabilizer) — por isso
/// a passagem não retoma no meio (`stateful`).
class EsrganAnimeStage final : public HyperStage {
public:
    explicit EsrganAnimeStage(Path p) : path_(p) {}
    const char* name() const noexcept override { return path_ == Path::Max ? "upscale.anime.esrgan.max" : "upscale.anime.esrgan.lite"; }
    StageKind kind() const noexcept override { return StageKind::Upscale; }
    Path path() const noexcept override { return path_; }
    bool stateful() const noexcept override { return true; }

    Status prepare(const DeviceProfile& profile, const Recipe& recipe, const StageFormat& in,
                   StageFormat& out) noexcept override {
        scale_ = recipe.upscale == 4 ? 4u : 2u;
        if (!in.width || !in.height || (in.width | in.height) & 1u) return Errc::InvalidArgument;
        const u32 tile = path_ == Path::Max ? 128u : std::clamp<u32>(profile.tile ? profile.tile : 96u, 64u, 96u);
        Status loaded = Errc::OutOfMemory;
        try {
            upscaler_ = std::make_unique<ai::Upscaler>();
            rgb_.resize(static_cast<usize>(in.width) * in.height * 3);
        } catch (...) {
            return Status{Errc::OutOfMemory, "memoria insuficiente para o upscale neural"};
        }
        loaded = upscaler_->load(scale_, 2, tile);
        if (!loaded.ok()) return loaded;
        in_ = in;
        out = in;
        out.width = in.width * scale_;
        out.height = in.height * scale_;
        out_ = out;
        temporal_.reset();
        return OkStatus;
    }

    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override {
        if (!upscaler_) return Errc::InvalidState;
        nv12_to_rgb24(in.current, rgb_.data(), in_.width * 3);
        struct Destination { FrameBuffer* out; const StageContext* ctx; } d{&out, &ctx};
        static const std::atomic<bool> kNever{false};
        const Status s = upscaler_->run(rgb_.data(), in_.width, in_.height, in_.width * 3,
            ctx.cancel ? *ctx.cancel : kNever,
            [](void* opaque, const ai::Upscaler::Tile& tile) {
                auto& dst = *static_cast<Destination*>(opaque);
                if (!ai::rgb_tile_to_nv12_709(tile.rgb, tile.stride, tile.x, tile.y, tile.width, tile.height,
                                              dst.out->width, dst.out->height, dst.out->nv12.data())) return false;
                dst.ctx->progress(tile.progress);
                return !dst.ctx->cancelled();
            }, &d);
        if (ctx.cancelled()) return Errc::Cancelled;
        if (!s.ok()) return s;
        const Status t = temporal_.process(rgb_.data(), in_.width, in_.height, out.y(), out.width, out.height,
                                           in.current.index);
        if (!t.ok()) return Status{t.code(), "memoria insuficiente para preservar estabilizacao temporal da IA"};
        return OkStatus;
    }

    void release() noexcept override {
        upscaler_.reset();
        std::vector<u8>().swap(rgb_);
        temporal_.reset();
    }
    u64 peak_bytes() const noexcept override {
        return (upscaler_ ? upscaler_->peak_working_bytes() : 0) + rgb_.size()
             + static_cast<u64>(out_.width) * out_.height * 2 + (48ull << 20);   // pesos + workspace do ncnn
    }

private:
    Path path_;
    u32 scale_ = 2;
    StageFormat in_{}, out_{};
    std::unique_ptr<ai::Upscaler> upscaler_;
    ai::TemporalStabilizer temporal_;
    std::vector<u8> rgb_;
};

f64 esrgan_ms_per_mp(const DeviceProfile& p) noexcept {
    // Um tile de 128² com halo ≈ 0,028 MP úteis; sem medição, 0,5 s por tile.
    const f64 tileMs = p.liteMsPerTile > 0.0f ? p.liteMsPerTile : 500.0;
    const f64 tileMp = 0.0128;   // tile 128 com halo de 20 px: ~0,013 MP úteis por chamada
    return tileMs / tileMp;
}

void register_builtin_stages(StageRegistry& r) {
    StageDescriptor lite;
    lite.id = "upscale.anime.esrgan.lite";
    lite.kind = StageKind::Upscale;
    lite.path = Path::Lite;
    lite.mode = static_cast<i32>(UpscaleMode::Anime);
    lite.priority = 0;
    lite.create = []() -> std::unique_ptr<HyperStage> {
        return std::unique_ptr<HyperStage>(new (std::nothrow) EsrganAnimeStage(Path::Lite));
    };
    lite.msPerMegapixel = &esrgan_ms_per_mp;
    r.add(lite);
    StageDescriptor max = lite;
    max.id = "upscale.anime.esrgan.max";
    max.path = Path::Max;
    max.create = []() -> std::unique_ptr<HyperStage> {
        return std::unique_ptr<HyperStage>(new (std::nothrow) EsrganAnimeStage(Path::Max));
    };
    r.add(max);
}

} // namespace

#if !defined(AUREA_HYPER_HAS_NEURAL)
// Padrão até src/hyper/neural/NeuralStages.cpp existir (o CMake define a macro).
void register_neural_stages(StageRegistry&) {}
#endif

const StageRegistry& stage_registry() {
    static StageRegistry registry;
    static std::once_flag once;
    std::call_once(once, [] {
        register_builtin_stages(registry);
        try {
            register_neural_stages(registry);
        } catch (...) {
            AUREA_LOG_ERROR("hyper: register_neural_stages lancou excecao; seguindo com os embutidos");
        }
        AUREA_LOG_INFO("hyper: %u etapas Lite/Max registradas", static_cast<u32>(registry.all().size()));
    });
    return registry;
}

} // namespace aurea::hyper
