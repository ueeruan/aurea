// Hyper Clean neural processing at the original resolution.
#include "hyper/neural/NeuralCommon.hpp"
#include "hyper/neural/NeuralStages.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace aurea::hyper::neural {

namespace {

f32 half_to_float(u16 h) noexcept {
    const u32 sign = static_cast<u32>(h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1Fu;
    u32 mant = h & 0x3FFu;
    u32 bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400u)) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

} // namespace

Status esrgan_general_weights(f32 denoise, std::vector<unsigned char>& out) {
    const Blob a = embedded("realesr-general-x4v3/x4v3.bin16");
    const Blob b = embedded("realesr-general-x4v3/wdn-x4v3.bin16");
    if (!a || !b || a.size != b.size || a.size % 2) return Status{Errc::NotFound, "pesos do realesr-general-x4v3 ausentes"};
    const f32 w = std::clamp(denoise, 0.0f, 1.0f);
    const usize words = a.size / 2;
    out.resize(words * 4);
    auto* dst = reinterpret_cast<f32*>(out.data());
    for (usize i = 0; i < words; ++i) {
        u16 ha, hb;
        std::memcpy(&ha, a.data + i * 2, 2);
        std::memcpy(&hb, b.data + i * 2, 2);
        // As marcas de tipo do .bin fp32 do ncnn valem 0 nos dois: continuam 0.
        dst[i] = w * half_to_float(ha) + (1.0f - w) * half_to_float(hb);
    }
    return OkStatus;
}

Status load_clean_net(f32 denoise, Backend backend, bool fp16, NetRunner& runner,
                      std::vector<unsigned char>& owned) {
    const Status weights = esrgan_general_weights(denoise, owned);
    if (!weights.ok()) return weights;
    NetSource src;
    src.param = embedded("realesr-general-x4v3/srvgg-x4.param");
    src.weights = {owned.data(), owned.size()};
    return runner.load(src, backend, fp16, true);
}

namespace {

class NeuralCleanStage final : public HyperStage {
public:
    explicit NeuralCleanStage(Path path) : path_(path) {}
    const char* name() const noexcept override {
        return path_ == Path::Max ? "clean.esrgan-x4v3.max" : "clean.esrgan-x4v3.lite";
    }
    StageKind kind() const noexcept override { return StageKind::Clean; }
    Path path() const noexcept override { return path_; }
    bool stateful() const noexcept override { return path_ == Path::Lite; }

    Status prepare(const DeviceProfile& profile, const Recipe& recipe, const StageFormat& in,
                   StageFormat& out) noexcept override try {
        release();
        if (!in.width || !in.height || (in.width | in.height) & 1u) return Errc::InvalidArgument;
        const bool vulkan = profile.vulkan && vulkan_device_available();
        const Backend backend = vulkan ? Backend::Vulkan : Backend::Cpu;
        const f32 denoise = std::max(recipe.denoise, recipe.deblock ? 0.5f : 0.0f);
        const Status loaded = load_clean_net(denoise, backend, profile.vulkanFp16, runner_, owned_);
        if (!loaded.ok()) return loaded;
        // Tile e margem: Lite = o do perfil (64–96 em 32 bits, até 128 em 64);
        // Max = tile maior e margem 16 (a rede inteira, sem pressa).
        const u32 tileCap = profile.abi64 && !kAddressSpace32 ? 128u : 96u;
        u32 tile = path_ == Path::Lite ? std::clamp<u32>(profile.tile ? profile.tile : 96u, 64u, tileCap)
                                       : (profile.abi64 && !kAddressSpace32 ? 192u : 96u);
        const u32 margin = path_ == Path::Lite ? std::clamp<u32>(profile.tileMargin ? profile.tileMargin : 12u, 8u, 12u) : 16u;
        u32 workers = runner_.backend() == Backend::Vulkan ? 1u : cpu_workers(profile);
        const u64 fixed = static_cast<u64>(in.width) * in.height * 6 + owned_.size();
        u64 budget = inference_budget(profile, fixed);
        // Cabe? Menos workers primeiro, depois tile menor (nunca abaixo de 64).
        for (;;) {
            const u64 ext = tile + 2 * margin;
            // Three live feature maps, Winograd workspace, and two RGB ×4 buffers.
            const u64 need = ext * ext * (6 * 64 * 4 + 2 * 16 * 3 * 4);
            if (need * workers <= budget) break;
            if (workers > 1) { --workers; continue; }
            if (tile > 64) { tile = std::max(64u, tile - 32u); continue; }
            return Status{Errc::BudgetExceeded, "memoria insuficiente para a rede neste aparelho"};
        }
        TileConfig cfg;
        cfg.tile = tile;
        cfg.margin = margin;
        cfg.netScale = 4;
        cfg.outDiv = 4;
        cfg.skipStatic = path_ == Path::Lite;
        cfg.temporalBlend = path_ == Path::Lite;
        budgetPerWorker_ = budget / workers;
        Status s = tiles_.prepare(cfg, in.width, in.height, workers, budgetPerWorker_);
        if (!s.ok()) return s;
        s = pool_.start(workers);
        if (!s.ok()) return s;
        rgbIn_.resize(static_cast<usize>(in.width) * in.height * 3);
        rgbOut_.resize(rgbIn_.size());
        in_ = in;
        out = in;
        AUREA_LOG_INFO("hyper: %s %ux%u, %s, tile %u+%u, %u worker(s)", name(), in.width, in.height,
                       runner_.backend() == Backend::Vulkan ? "Vulkan" : "CPU", tile, margin, workers);
        return OkStatus;
    } catch (const std::bad_alloc&) {
        release();
        return Status{Errc::OutOfMemory, "memoria insuficiente para a rede"};
    }

    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override {
        if (!runner_.loaded() || !in.current.valid()) return Errc::InvalidState;
        if (in.current.width != in_.width || in.current.height != in_.height) return Errc::InvalidArgument;
        nv12_to_rgb24(in.current, rgbIn_.data(), in_.width * 3);
        if (out.width != in_.width || out.height != in_.height) return Errc::InvalidArgument;
        const Status s = tiles_.run(runner_, pool_, rgbIn_.data(), rgbOut_.data(), ctx);
        if (!s.ok()) return s;
        if (!rgb24_to_nv12(rgbOut_.data(), out.width * 3, out.width, out.height, out)) return Errc::InvalidArgument;
        return OkStatus;
    }

    void release() noexcept override {
        pool_.stop();
        runner_.release();
        std::vector<unsigned char>().swap(owned_);
        std::vector<u8>().swap(rgbIn_);
        std::vector<u8>().swap(rgbOut_);
    }

    u64 peak_bytes() const noexcept override {
        return tiles_.peak_bytes() + rgbIn_.size() + rgbOut_.size() + owned_.size() + (5ull << 20);
    }

    [[nodiscard]] CleanStats stats() const noexcept {
        CleanStats st;
        st.tiles = tiles_.tiles_total();
        st.skipped = tiles_.tiles_skipped();
        st.tile = tiles_.config().tile;
        st.margin = tiles_.config().margin;
        st.workers = pool_.size();
        st.peakBytes = tiles_.peak_bytes();
        st.historyBytes = tiles_.history_bytes();
        st.budgetPerWorker = budgetPerWorker_;
        st.vulkan = runner_.backend() == Backend::Vulkan;
        return st;
    }

private:
    Path path_;
    NetRunner runner_;
    WorkerPool pool_;
    TiledRunner tiles_;
    std::vector<unsigned char> owned_;
    std::vector<u8> rgbIn_, rgbOut_;
    StageFormat in_{};
    u64 budgetPerWorker_ = 0;
};

} // namespace

std::unique_ptr<HyperStage> make_clean_stage(Path path) {
    return std::unique_ptr<HyperStage>(new (std::nothrow) NeuralCleanStage(path));
}

CleanStats clean_stage_stats(const HyperStage& stage) noexcept {
    const std::string_view name = stage.name();
    if (name == "clean.esrgan-x4v3.max" || name == "clean.esrgan-x4v3.lite")
        return static_cast<const NeuralCleanStage&>(stage).stats();
    return {};
}

f64 cost_esrgan(const DeviceProfile& p) noexcept {
    const f64 tile = p.tile ? p.tile : 96.0;
    const f64 ms = p.liteMsPerTile > 0.0f ? p.liteMsPerTile : 870.0;
    return ms * 1e6 / (tile * tile);
}

} // namespace aurea::hyper::neural
