// =============================================================================
//  Aurea / hyper / neural / NeuralUpscale.cpp
//
//  Hyper Upscale (Lite/Max) e Hyper Clean pela rede.
//
//    General Lite  SPAN ×2/×4 (ch48). Vulkan: fp16; CPU: fp16 na conta onde a CPU
//                  tem (ARMv8.2), senão fp32 — o int8 foi medido e recusado (o
//                  SPAN amplifica o erro da quantização: −2,6 dB a 40 dB do fp32,
//                  sem ganho de tempo; ver tools/hyper_models.py). Aparelho fraco
//                  (32 bits ou tile lento) no ×4: rede ×2 + FSR 1 ×2 na CPU (HyperTurbo.hpp).
//    General Max   Real-ESRGAN realesr-general-x4v3 com os pesos MISTURADOS ao
//                  wdn-x4v3 pela força do denoise (w·x4v3 + (1−w)·wdn, a
//                  interpolação de pesos do -dn do Real-ESRGAN); ×2 = ×4 da
//                  rede + média 2×2.
//    Anime         realesr-animevideov3 (o modelo da 0.1.4, engine/assets/ai);
//                  Lite na CPU em int8 MISTO (11 de 18 conv; 35,5 dB do fp32,
//                  ~25% mais rápido no host). ×2 = ×4 + média 2×2.
//    Clean         realesr-general-x4v3/wdn misturados (força = denoise, ou 0,5
//                  só com deblock) + média 4×4: a rede foi treinada com ruído e
//                  compressão JPEG (degradação de 2ª ordem do Real-ESRGAN), e a
//                  média devolve a resolução original já limpa.
//
//  Lite: tile/margem do perfil, pula tile parado e mistura temporal (o
//  `stateful`). Max: tile maior, margem 16, sem histórico (retoma no meio).
// =============================================================================
#include "aurea/hyper/HyperTurbo.hpp"
#include "hyper/neural/NeuralCommon.hpp"
#include "hyper/neural/NeuralStages.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace aurea::ai::embedded {
const unsigned char* model_weights();
const char* model_param(unsigned scale);
} // namespace aurea::ai::embedded

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

namespace {

enum class Net : u8 { SpanX2, SpanX4, EsrganGeneral, Anime };

/// Quanto cada rede custa por pixel de entrada, relativo ao SPAN ×2 do medidor
/// (conta de MACs: SPAN 21 conv 3×3 de 48 canais; x4v3 34 de 64; anime 18 de 64).
f64 relative_cost(Net net) noexcept {
    switch (net) {
        case Net::SpanX2: return 1.0;
        case Net::SpanX4: return 1.15;
        case Net::EsrganGeneral: return 2.9;
        case Net::Anime: return 1.5;
    }
    return 1.0;
}

bool weak_device(const DeviceProfile& p) noexcept {
    return !p.abi64 || kAddressSpace32 || (p.measured && p.liteMsPerTile > 250.0f);
}

struct Choice {
    Net net = Net::SpanX2;
    u32 netScale = 2, outDiv = 1;
    bool finalCatmull = false;   ///< ×2 final na CPU (4× em aparelho fraco)
};

Choice choose(StageKind kind, Path path, UpscaleMode mode, u32 scale, const DeviceProfile& p) noexcept {
    Choice c;
    if (kind == StageKind::Clean) {
        c.net = Net::EsrganGeneral; c.netScale = 4; c.outDiv = 4;
        return c;
    }
    const bool weak4 = scale == 4 && path == Path::Lite && weak_device(p);
    if (mode == UpscaleMode::Anime) {
        c.net = Net::Anime; c.netScale = 4;
        c.outDiv = weak4 ? 2 : 4 / scale;
        c.finalCatmull = weak4;
        return c;
    }
    if (path == Path::Max) {
        c.net = Net::EsrganGeneral; c.netScale = 4; c.outDiv = 4 / scale;
        return c;
    }
    if (scale == 4 && !weak4) { c.net = Net::SpanX4; c.netScale = 4; }
    else { c.net = Net::SpanX2; c.netScale = 2; c.finalCatmull = weak4; }
    return c;
}

/// A memória de ativação de um tile (bytes), do maior blob vivo × folga.
u64 tile_activation_bytes(Net net, u32 ext, u32 netScale, bool int8) noexcept {
    const u64 px = static_cast<u64>(ext) * ext;
    const u64 ch = net == Net::SpanX2 || net == Net::SpanX4 ? 48 : 64;
    // ~4 blobs de features vivos (splits do SPAN) + a saída ×s + Winograd.
    u64 bytes = px * ch * 4 * (net == Net::EsrganGeneral || net == Net::Anime ? 3 : 5);
    bytes += px * netScale * netScale * 3 * 4 * 2;
    bytes += px * ch * 4 * 3;   // Winograd/sgemm workspace
    if (int8 && net == Net::Anime) bytes = bytes * 3 / 4;
    return bytes;
}

} // namespace

/// Carrega a rede escolhida no backend. `int8` = modo rápido da CPU no Lite:
/// animevideov3 quantizado (int8 misto); SPAN com conta fp16 onde a CPU tem.
Status load_upscale_net(int netId, f32 denoise, Backend backend, bool fp16, bool int8, NetRunner& runner,
                        std::vector<unsigned char>& owned) {
    const Net net = static_cast<Net>(netId);
    NetSource src;
    switch (net) {
        case Net::SpanX2:
        case Net::SpanX4: {
            const char* tag = net == Net::SpanX2 ? "span-x2/span-x2" : "span-x4/span-x4";
            const std::string p = std::string(tag) + "-fp16.param";
            const std::string b = std::string(tag) + "-fp16.bin";
            src.cpuFp16 = int8;
            src.cpuFp16Arithmetic = int8;
            src.param = embedded(p.c_str());
            src.weights = embedded(b.c_str());
            break;
        }
        case Net::EsrganGeneral: {
            const Status w = esrgan_general_weights(denoise, owned);
            if (!w.ok()) return w;
            src.param = embedded("realesr-general-x4v3/srvgg-x4.param");
            src.weights = {owned.data(), owned.size()};
            break;
        }
        case Net::Anime: {
            src.input = "data";
            src.output = "output";
            if (int8) {
                src.param = embedded("realesr-animevideov3/animevideov3-x4-int8.param");
                src.weights = embedded("realesr-animevideov3/animevideov3-x4-int8.bin");
            } else {
                src.param = {reinterpret_cast<const unsigned char*>(ai::embedded::model_param(4)), 1};
                src.weights = {ai::embedded::model_weights(), 1};
            }
            break;
        }
    }
    return runner.load(src, backend, fp16, true);
}

namespace {

class NeuralUpscaleStage final : public HyperStage {
public:
    NeuralUpscaleStage(StageKind kind, Path path, UpscaleMode mode) : kind_(kind), path_(path), mode_(mode) {}
    const char* name() const noexcept override {
        if (kind_ == StageKind::Clean) return path_ == Path::Max ? "clean.esrgan-x4v3.max" : "clean.esrgan-x4v3.lite";
        if (mode_ == UpscaleMode::Anime) return path_ == Path::Max ? "upscale.anime.animevideov3.max" : "upscale.anime.animevideov3.lite";
        return path_ == Path::Max ? "upscale.general.esrgan-x4v3.max" : "upscale.general.span.lite";
    }
    StageKind kind() const noexcept override { return kind_; }
    Path path() const noexcept override { return path_; }
    bool stateful() const noexcept override { return path_ == Path::Lite; }

    Status prepare(const DeviceProfile& profile, const Recipe& recipe, const StageFormat& in,
                   StageFormat& out) noexcept override try {
        release();
        if (!in.width || !in.height || (in.width | in.height) & 1u) return Errc::InvalidArgument;
        const u32 scale = kind_ == StageKind::Clean ? 1u : (recipe.upscale == 4 ? 4u : 2u);
        choice_ = choose(kind_, path_, mode_, scale, profile);
        // Backend: Vulkan quando o aparelho tem e (se medido) o tile de
        // referência saiu certo; conta fp16 só se o medidor conferiu.
        const bool vulkan = profile.vulkan && vulkan_device_available();
        const Backend backend = vulkan ? Backend::Vulkan : Backend::Cpu;
        const bool int8 = !vulkan && path_ == Path::Lite && choice_.net != Net::EsrganGeneral;   // modo rápido da CPU
        const f32 dn = kind_ == StageKind::Clean ? std::max(recipe.denoise, recipe.deblock ? 0.5f : 0.0f)
                                                 : (recipe.denoise > 0.0f ? recipe.denoise : 0.5f);
        Status loaded = load_upscale_net(static_cast<int>(choice_.net), dn, backend, profile.vulkanFp16, int8, runner_, owned_);
        if (!loaded.ok()) return loaded;
        int8_ = int8 && runner_.backend() == Backend::Cpu;
        // Tile e margem: Lite = o do perfil (64–96 em 32 bits, até 128 em 64);
        // Max = tile maior e margem 16 (a rede inteira, sem pressa).
        const u32 tileCap = profile.abi64 && !kAddressSpace32 ? 128u : 96u;
        u32 tile = path_ == Path::Lite ? std::clamp<u32>(profile.tile ? profile.tile : 96u, 64u, tileCap)
                                       : (profile.abi64 && !kAddressSpace32 ? 192u : 96u);
        const u32 margin = path_ == Path::Lite ? std::clamp<u32>(profile.tileMargin ? profile.tileMargin : 12u, 8u, 12u) : 16u;
        u32 workers = runner_.backend() == Backend::Vulkan ? 1u : cpu_workers(profile);
        // Memória fixa: quadros RGB de entrada/saída (+ o intermediário do ×2).
        midW_ = in.width * choice_.netScale / choice_.outDiv;
        midH_ = in.height * choice_.netScale / choice_.outDiv;
        outW_ = choice_.finalCatmull ? midW_ * 2 : midW_;
        outH_ = choice_.finalCatmull ? midH_ * 2 : midH_;
        const u64 fixed = static_cast<u64>(in.width) * in.height * 3 + static_cast<u64>(outW_) * outH_ * 3
                        + (choice_.finalCatmull ? static_cast<u64>(midW_) * midH_ * 3 : 0) + owned_.size();
        u64 budget = inference_budget(profile, fixed);
        // Cabe? Menos workers primeiro, depois tile menor (nunca abaixo de 64).
        for (;;) {
            const u64 need = tile_activation_bytes(choice_.net, tile + 2 * margin, choice_.netScale, int8_);
            if (need * workers <= budget) break;
            if (workers > 1) { --workers; continue; }
            if (tile > 64) { tile = std::max(64u, tile - 32u); continue; }
            return Status{Errc::BudgetExceeded, "memoria insuficiente para a rede neste aparelho"};
        }
        TileConfig cfg;
        cfg.tile = tile;
        cfg.margin = margin;
        cfg.netScale = choice_.netScale;
        cfg.outDiv = choice_.outDiv;
        cfg.skipStatic = path_ == Path::Lite;
        cfg.temporalBlend = path_ == Path::Lite;
        budgetPerWorker_ = budget / workers;
        Status s = tiles_.prepare(cfg, in.width, in.height, workers, budgetPerWorker_);
        if (!s.ok()) return s;
        s = pool_.start(workers);
        if (!s.ok()) return s;
        rgbIn_.resize(static_cast<usize>(in.width) * in.height * 3);
        rgbOut_.assign(static_cast<usize>(outW_) * outH_ * 3, 0);
        if (choice_.finalCatmull) rgbMid_.assign(static_cast<usize>(midW_) * midH_ * 3, 0);
        in_ = in;
        out = in;
        out.width = outW_;
        out.height = outH_;
        AUREA_LOG_INFO("hyper: %s %ux%u -> %ux%u, %s%s, tile %u+%u, %u worker(s)", name(), in.width, in.height,
                       outW_, outH_, runner_.backend() == Backend::Vulkan ? "Vulkan" : "CPU", int8_ ? (choice_.net == Net::Anime ? " int8" : " fp16-cpu") : "",
                       tile, margin, workers);
        return OkStatus;
    } catch (const std::bad_alloc&) {
        release();
        return Status{Errc::OutOfMemory, "memoria insuficiente para a rede"};
    }

    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override {
        if (!runner_.loaded() || !in.current.valid()) return Errc::InvalidState;
        if (in.current.width != in_.width || in.current.height != in_.height) return Errc::InvalidArgument;
        nv12_to_rgb24(in.current, rgbIn_.data(), in_.width * 3);
        u8* target = choice_.finalCatmull ? rgbMid_.data() : rgbOut_.data();
        const Status s = tiles_.run(runner_, pool_, rgbIn_.data(), target, ctx);
        if (!s.ok()) return s;
        // ×2 final do aparelho fraco: o FSR 1 (EASU + RCAS) do Turbo, na CPU
        // (aurea/hyper/HyperTurbo.hpp), no lugar do Catmull-Rom.
        if (choice_.finalCatmull)
            fsr_upscale_rgb(rgbMid_.data(), midW_, midH_, rgbOut_.data(), outW_, outH_, 0.5f, std::max(1u, pool_.size()));
        if (out.width != outW_ || out.height != outH_) return Errc::InvalidArgument;
        if (!rgb24_to_nv12(rgbOut_.data(), outW_ * 3, outW_, outH_, out)) return Errc::InvalidArgument;
        return OkStatus;
    }

    void release() noexcept override {
        pool_.stop();
        runner_.release();
        std::vector<unsigned char>().swap(owned_);
        std::vector<u8>().swap(rgbIn_);
        std::vector<u8>().swap(rgbOut_);
        std::vector<u8>().swap(rgbMid_);
    }

    u64 peak_bytes() const noexcept override {
        return tiles_.peak_bytes() + rgbIn_.size() + rgbOut_.size() + rgbMid_.size() + owned_.size()
             + weights_bytes();
    }

    [[nodiscard]] UpscaleStats stats() const noexcept {
        UpscaleStats st;
        st.tiles = tiles_.tiles_total();
        st.skipped = tiles_.tiles_skipped();
        st.tile = tiles_.config().tile;
        st.margin = tiles_.config().margin;
        st.workers = pool_.size();
        st.peakBytes = tiles_.peak_bytes();
        st.historyBytes = tiles_.history_bytes();
        st.budgetPerWorker = budgetPerWorker_;
        st.vulkan = runner_.backend() == Backend::Vulkan;
        st.fastCpu = int8_;
        st.finalCatmull = choice_.finalCatmull;
        return st;
    }

private:
    u64 weights_bytes() const noexcept {
        switch (choice_.net) {
            case Net::SpanX2: case Net::SpanX4: return 2ull << 20;
            case Net::EsrganGeneral: return 5ull << 20;
            case Net::Anime: return 3ull << 20;
        }
        return 0;
    }
    StageKind kind_;
    Path path_;
    UpscaleMode mode_;
    Choice choice_{};
    NetRunner runner_;
    WorkerPool pool_;
    TiledRunner tiles_;
    std::vector<unsigned char> owned_;
    std::vector<u8> rgbIn_, rgbOut_, rgbMid_;
    StageFormat in_{};
    u32 midW_ = 0, midH_ = 0, outW_ = 0, outH_ = 0;
    u64 budgetPerWorker_ = 0;
    bool int8_ = false;
};

f64 upscale_cost(const DeviceProfile& p, Net net) noexcept {
    // ms por MP de entrada: o tile Lite medido (SPAN ×2, tile+2·margem) vale
    // tile² pixels úteis; sem medição, 300 ms por tile de 96.
    const f64 tile = p.tile ? p.tile : 96.0;
    const f64 ms = p.liteMsPerTile > 0.0f ? p.liteMsPerTile : 300.0;
    return ms * 1e6 / (tile * tile) * relative_cost(net);
}

} // namespace

std::unique_ptr<HyperStage> make_upscale_stage(StageKind kind, Path path, UpscaleMode mode) {
    return std::unique_ptr<HyperStage>(new (std::nothrow) NeuralUpscaleStage(kind, path, mode));
}

UpscaleStats upscale_stage_stats(const HyperStage& stage) noexcept {
    // Sem RTTI (Android/iOS compilam com -fno-rtti): as etapas desta classe se
    // reconhecem pelo nome, que só ela devolve.
    static constexpr const char* kNames[] = {
        "clean.esrgan-x4v3.max", "clean.esrgan-x4v3.lite", "upscale.anime.animevideov3.max",
        "upscale.anime.animevideov3.lite", "upscale.general.esrgan-x4v3.max", "upscale.general.span.lite"};
    const char* name = stage.name();
    for (const char* n : kNames)
        if (name && std::strcmp(name, n) == 0) return static_cast<const NeuralUpscaleStage&>(stage).stats();
    return UpscaleStats{};
}

f64 cost_span_lite(const DeviceProfile& p) noexcept { return upscale_cost(p, Net::SpanX2); }
f64 cost_esrgan(const DeviceProfile& p) noexcept { return upscale_cost(p, Net::EsrganGeneral); }
f64 cost_anime(const DeviceProfile& p) noexcept { return upscale_cost(p, Net::Anime); }

} // namespace aurea::hyper::neural
