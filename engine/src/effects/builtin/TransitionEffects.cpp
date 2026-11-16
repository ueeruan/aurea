#include "BuiltinEffects.hpp"
#include "aurea/effects/FastBlurReference.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <mutex>

namespace aurea::builtin {
namespace {
f32 finite_or(f32 value, f32 fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

// Native alpha transitions. Coordinates stay in layer space, including cropped
// preview tiles, so changing preview resolution cannot move the transition.
class Wipe final : public Effect {
public:
    explicit Wipe(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kLinearWipe, "Varredura linear", "Transição", EffectClass::Domain},
            {effect_keys::kRadialWipe, "Varredura radial", "Transição", EffectClass::Domain},
            {effect_keys::kBlockDissolve, "Dissolver em blocos", "Transição", EffectClass::Domain}
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Ângulo inicial", 0.f);
        p.add_float("feather", "Suavidade", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("reverse", "Inverter direção", false);
        if (mode_ == 1) {
            p.add_float("center_x", "Centro X", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("center_y", "Centro Y", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        } else if (mode_ == 2) {
            p.add_float("block_width", "Largura do bloco", 32.f, 1.f, 1024.f, kParamAnimatable | kParamPixels, "px");
            p.add_float("block_height", "Altura do bloco", 32.f, 1.f, 1024.f, kParamAnimatable | kParamPixels, "px");
            p.add_int("seed", "Semente", 1, 0, 65535);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(0) <= 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        auto u = base_uniforms(input);
        u.p0 = {std::clamp(e.f(0) / 100.f, 0.f, 1.f), e.f(1), e.f(2) / 100.f, e.b(3) ? 1.f : 0.f};
        u.p1 = {static_cast<f32>(mode_), mode_ == 0 ? 0.f : e.f(4), mode_ == 0 ? 0.f : e.f(5), mode_ == 2 ? static_cast<f32>(e.e(6)) : 0.f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w,
                e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h, 0.f, 0.f};
        return single_pass(ctx, ShaderId::effects_wipe_frag, input, u, "wipe", out);
    }
private:
    u32 mode_;
};

class BlockDissolveReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.transition.block_dissolve_reference", "Block Dissolve", "Transição", EffectClass::Domain};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão da transição", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("block_width", "Largura do bloco", 1, 1, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1, 32000);
        p.add_float("block_height", "Altura do bloco", 1, 1, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1, 32000);
        p.add_float("feather", "Difusão", 0, 0, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, 32000);
        p.add_bool("soft_edges", "Bordas suaves", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return progress(e.f(0)) <= 0; }
    // AE starts at completion 0 (an identity); previews show a half-dissolved grid.
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[0] = ParamValue::scalar(50.0f);
        v[1] = ParamValue::scalar(8.0f);
        v[2] = ParamValue::scalar(8.0f);
        return true;
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_block_dissolve_reference_frag, format));
        fast_blur_reference::pipelines(out, format);
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = {};
        e.auxInfo = {};
        if (!e.resources || !e.placement || progress(e.f(0)) <= 0 || progress(e.f(0)) >= 1) return;
        const f64 columnCount = std::floor(static_cast<f64>(e.placement->layerWidth) / block_size(e.f(1))) + 2;
        const f64 rowCount = std::floor(static_cast<f64>(e.placement->layerHeight) / block_size(e.f(2))) + 2;
        if (columnCount > static_cast<f64>(UINT32_MAX) || rowCount > static_cast<f64>(UINT32_MAX)) {
            e.auxInfo.w = -1;
            return;
        }
        const u32 columns = static_cast<u32>(columnCount), rows = static_cast<u32>(rowCount);
        const RankGrid grid = ranks(columns, rows);
        if (!grid.bytes) {
            e.auxInfo.w = -1;
            return;
        }
        const u64 key = 0x424c4f434b320001ull ^ (static_cast<u64>(columns) << 32) ^ rows;
        e.aux = e.resources->image_texture(key, grid.bytes->data(), grid.width, grid.height, false);
        e.auxInfo = {static_cast<f32>(columns), static_cast<f32>(rows), static_cast<f32>(grid.width), static_cast<f32>(grid.height)};
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (progress(e.f(0)) >= 1) {
            out = input;
            out.texture = ctx.texture("block-dissolve-empty", input.width, input.height);
            (void)ctx.graph().add_raster_pass("block-dissolve-empty", PassStage::Effects, out.texture,
                LoadOp::Clear, Vec4{}, [](PassContext&) {});
            return OkStatus;
        }
        if (e.auxInfo.w < 0) return Status{Errc::BudgetExceeded, "Block Dissolve rank grid exceeds supported dimensions"};
        if (!e.aux.valid()) return Status{Errc::Busy, "Block Dissolve rank grid pending"};
        struct Uniforms {
            Vec4 uvMap, texel, block, grid, region, unused;
            std::array<u32, 4> limits;
        } u{};
        u.block = {block_size(e.f(1)), block_size(e.f(2)), 0, 0};
        u.grid = e.auxInfo;
        u.region = {input.region.x, input.region.y, input.region.w, input.region.h};
        const f64 cells = static_cast<f64>(e.auxInfo.x) * e.auxInfo.y;
        u.limits = {static_cast<u32>(std::floor(cells * progress(e.f(0)))),
                    2, e.b(4) ? 1u : 0u, 0};
        auto pass = [&](const char* label, FGTexture target, PassTexture second) {
            return ctx.fullscreen_pass(label, PassStage::Effects, target, ShaderId::effects_block_dissolve_reference_frag,
                {{input.texture, {}, CommonSampler::LinearClamp}, second}, &u, sizeof(u)) != kInvalidIndex;
        };
        out = input;
        out.texture = ctx.texture("block-dissolve-reference", input.width, input.height);
        const f32 feather = std::clamp(finite_or(e.f(3), 0.0f), 0.0f, 32000.0f);
        if (feather <= 0) {
            if (!pass("block-dissolve-reference", out.texture, {{}, e.aux, CommonSampler::NearestClamp}))
                return Errc::PipelineCompileFailed;
            return OkStatus;
        }
        LayerImage mask = input;
        mask.texture = ctx.texture("block-dissolve-mask", input.width, input.height);
        u.limits[1] = 0;
        if (!pass("block-dissolve-mask", mask.texture, {{}, e.aux, CommonSampler::NearestClamp}))
            return Errc::PipelineCompileFailed;
        // The AE 2027 oracle (feather 10, 16 px blocks) matches Fast Blur with
        // Blurriness = feather applied to the 8-bit soft mask, clamped to the
        // layer before every box pass (interior error 9e-6, border 0.013).
        fast_blur_reference::Request request;
        request.radiusX = request.radiusY = feather;
        request.repeatEdges = true;
        request.clampEachPass = true;
        request.density = std::max(finite_or(e.texelScale, 1.0f), input.texel_scale_x());
        request.outRegion = input.region;
        LayerImage blurred;
        const Status status = fast_blur_reference::build(ctx, mask, request, blurred);
        if (!status.ok()) return status;
        u.limits[1] = 1;
        if (!pass("block-dissolve-compose", out.texture, {blurred.texture, {}, CommonSampler::LinearClamp}))
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    static f64 progress(f32 value) noexcept {
        return std::round(std::clamp(static_cast<f64>(finite_or(value, 0)), 0.0, 100.0) * 65536) / 6553600;
    }
    struct RankGrid {
        u32 columns = 0, rows = 0, width = 0, height = 0;
        std::shared_ptr<const std::vector<u8>> bytes;
        u64 stamp = 0;
    };
    static f32 block_size(f32 value) noexcept {
        return static_cast<f32>(std::round(std::clamp(static_cast<f64>(finite_or(value, 1)), 1.0, 32000.0) * 65536) / 65536);
    }
    static RankGrid ranks(u32 columns, u32 rows) noexcept {
        if (columns < 2 || rows < 2) return {};
        const u64 count = static_cast<u64>(columns) * rows;
        const u32 columnBits = std::bit_width(columns);
        const u32 degree = columnBits + std::bit_width(rows);
        if (count > u64{8192} * 8192 || degree > 32) return {};
        static std::mutex mutex;
        static std::vector<RankGrid> cache;
        static u64 stamp = 0;
        constexpr usize capacity = 128u * 1024u * 1024u;
        std::lock_guard lock(mutex);
        for (auto& entry : cache) {
            if (entry.columns == columns && entry.rows == rows) {
                entry.stamp = ++stamp;
                return entry;
            }
        }
        RankGrid result;
        result.columns = columns;
        result.rows = rows;
        result.width = std::clamp(std::bit_ceil(std::max(columns - 2, 1u)), 2048u, 8192u);
        result.height = static_cast<u32>((count + result.width - 1) / result.width);
        if (!count || result.height > 8192) return {};
        auto pixels = std::make_shared<std::vector<u8>>(static_cast<usize>(result.width) * result.height * 4, 255);
        auto write = [&](u32 x, u32 y, u32 rank) {
            const usize index = (static_cast<usize>(y) * columns + x) * 4;
            for (u32 channel = 0; channel < 4; ++channel) (*pixels)[index + channel] = static_cast<u8>(rank >> (channel * 8));
        };
        // Primitive binary-polynomial feedback coefficients specify the
        // deterministic permutation. Pixel data is generated locally.
        constexpr std::array<u32, 33> feedback{0, 0, 3, 6, 12, 20, 48, 96, 184, 272, 576, 1280,
            3232, 6912, 13568, 24576, 46080, 73728, 132096, 466944, 589824, 1310720, 3145728,
            5242880, 14155776, 18874368, 59244544, 119537664, 150994944, 335544320,
            847249408, 1207959552, 2734686208u};
        if (degree >= feedback.size()) return {};
        const u32 columnMask = (1u << columnBits) - 1;
        u32 state = 1, rank = 0;
        do {
            const u32 x = state & columnMask;
            const u32 y = state >> columnBits;
            if (x < columns && y < rows) write(x, y, ++rank);
            state = (state >> 1) ^ ((state & 1) ? feedback[degree] : 0);
        } while (state != 1);
        write(0, 0, 0);
        result.bytes = std::move(pixels);
        result.stamp = ++stamp;
        usize bytes = 0;
        for (const auto& entry : cache) bytes += entry.bytes->size();
        while (!cache.empty() && (cache.size() >= 8 || bytes + result.bytes->size() > capacity)) {
            const auto oldest = std::min_element(cache.begin(), cache.end(), [](const RankGrid& a, const RankGrid& b) { return a.stamp < b.stamp; });
            bytes -= oldest->bytes->size();
            cache.erase(oldest);
        }
        if (result.bytes->size() <= capacity) cache.push_back(result);
        return result;
    }
};

// -----------------------------------------------------------------------------
// Transições por forma: Íris, Caixa e Persianas.
//
// Mesmo contrato das varreduras acima (conclusão 0% = camada inteira, 100% =
// vazia, EXATOS; suavidade não muda as pontas; "Inverter direção" é o
// complemento). A diferença é a forma do limiar — ver shape_wipe.frag.
// -----------------------------------------------------------------------------
class ShapeWipe final : public Effect {
public:
    enum Mode : u32 { kIris = 0, kBox, kBlinds };
    enum : u32 { kCompletion = 0, kFeather, kReverse };

    explicit ShapeWipe(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kIrisWipe, "Íris", "Transição", EffectClass::Domain},
            {effect_keys::kBoxWipe, "Caixa", "Transição", EffectClass::Domain},
            {effect_keys::kVenetianBlinds, "Persianas", "Transição", EffectClass::Domain},
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("feather", "Suavidade", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("reverse", "Inverter direção", false);
        if (mode_ == kBlinds) {
            p.add_angle("direction", "Direção", 0.f);
            // O custo é fixo (uma conta por pixel); o teto é de legibilidade:
            // mais faixas que pixels vira ruído.
            p.add_float("count", "Faixas", 10.f, 1.f, 200.f);
            p.typed_range(1.f, 2000.f);
            // 0.4.5 (índice 5, no fim): largura da faixa em px, como nas
            // Persianas dos editores de movimento. 0 = pelo número de Faixas.
            p.add_float("width", "Largura da faixa", 0.f, 0.f, 500.f, kParamAnimatable | kParamPixels, "px");
            p.typed_range(0.f, 10000.f);
            p.set_tip(5, "0 = a largura sai do número de faixas.");
            return;
        }
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.f, 2.f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.f, 10.f);
        p.add_angle("rotation", "Rotação", 0.f);
        if (mode_ == kIris) {
            p.add_int("sides", "Lados da íris", 0, 0, 16);   // 0..2 = círculo
            // AE 2026 (no fim): "Usar raio interno" faz das pontas uma ESTRELA —
            // o raio interno é a fração do externo nos vales (com 3+ lados).
            p.add_bool("use_inner_radius", "Usar raio interno", false);
            p.add_float("inner_radius", "Raio interno", 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            p.set_enabled_by(7, 6, 1u);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kCompletion) <= 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_shape_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        auto u = base_uniforms(input);
        u.p0 = {std::clamp(e.f(kCompletion) / 100.f, 0.f, 1.f), std::clamp(e.f(kFeather) / 100.f, 0.f, 1.f),
                e.b(kReverse) ? 1.f : 0.f, static_cast<f32>(mode_)};
        if (mode_ == kBlinds) {
            f32 count = std::clamp(e.f(4), 1.f, 2000.f);
            // Largura em px (> 0) manda: vira o número de faixas que cabe na
            // extensão da camada ao longo da direção (a mesma conta do shader).
            const f32 band = e.count > 5 ? e.f(5) : 0.f;
            if (std::isfinite(band) && band > 0.f) {
                const f32 a = e.f(3) * kDeg2Rad;
                const f32 span = std::fabs(std::cos(a)) * w + std::fabs(std::sin(a)) * h;
                count = std::clamp(span / std::max(band, 0.5f), 1e-3f, 20000.f);
            }
            u.p1 = {0.f, 0.f, count, e.f(3) * kDeg2Rad};
        } else {
            const Vec2 c = e.p2(3);
            u.p1 = {c.x * w, c.y * h, mode_ == kIris ? static_cast<f32>(e.value(5).as_int()) : 0.f,
                    e.f(4) * kDeg2Rad};
        }
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {w, h, 0.f, 0.f};
        if (mode_ == kIris && e.count > 7 && e.b(6)) {
            // p3.z > 0: estrela, raio interno/externo (o polígono de antes = 0).
            const f32 inner = std::isfinite(e.f(7)) ? std::clamp(e.f(7) / 100.f, 0.01f, 1.f) : 0.5f;
            u.p3.z = inner;
        }
        return single_pass(ctx, ShaderId::effects_shape_wipe_frag, input, u, "shape-wipe", out);
    }
private:
    u32 mode_;
};
}
void register_transition_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Wipe>(mode));
    (void)r.add(std::make_unique<BlockDissolveReference>());
    register_radial_wipe_reference(r);
}
void register_shape_transition_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<ShapeWipe>(mode));
}
}
