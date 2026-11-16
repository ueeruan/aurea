// AE reference batch A: one registration point, appended after every other
// builtin, so the registry order of the older effects does not change.
#include "BuiltinEffects.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace aurea::builtin {
namespace {

/// Desenha a imagem pedida a outro instante (EffectBuildContext::temporal_checkout)
/// no espaço da camada, como o Twitch faz (TwitchReferenceEffect.cpp).
Status rasterize_checkout(EffectBuildContext& ctx, LayerImage& source, const Mat4& matrix, f32 opacity,
                          f32 density, const char* label) {
    const Rect r = source.region;
    f32 minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (Vec2 p : {Vec2{r.x, r.y}, Vec2{r.x + r.w, r.y}, Vec2{r.x, r.y + r.h}, Vec2{r.x + r.w, r.y + r.h}}) {
        const Vec4 q = matrix * Vec4{p.x, p.y, 0, 1};
        if (!std::isfinite(q.w) || std::abs(q.w) < 1e-6f) return Status{Errc::OutOfRange, "temporal source is unbounded"};
        minX = std::min(minX, q.x / q.w); minY = std::min(minY, q.y / q.w);
        maxX = std::max(maxX, q.x / q.w); maxY = std::max(maxY, q.y / q.w);
    }
    const Rect region{std::floor(minX), std::floor(minY), std::max(1.f, std::ceil(maxX) - std::floor(minX)),
                      std::max(1.f, std::ceil(maxY) - std::floor(minY))};
    if (std::ceil(region.w * density) > ctx.max_texture_size() || std::ceil(region.h * density) > ctx.max_texture_size())
        return Status{Errc::OutOfDeviceMemory, "temporal source exceeds the GPU texture extent"};
    auto pipeline = ctx.shaders().pipeline(PipelineKey::graphics(ShaderId::effects_cartoon_sample_vert,
        ShaderId::effects_cartoon_sample_frag, ctx.work_format(), true, BlendMode::Normal));
    if (!pipeline.ok()) return pipeline.status();
    u32 width, height;
    ctx.region_size(region, density, width, height);
    const FGTexture target = ctx.texture(label, width, height);
    const Mat4 projection = Mat4::scale(Vec3{2.f / region.w, 2.f / region.h, 1})
        * Mat4::translation(Vec3{-region.x - region.w * .5f, -region.y - region.h * .5f, 0});
    struct Push { Mat4 matrix; Vec4 region, tint, params; };
    const Push push{projection * matrix, Vec4{r.x, r.y, r.w, r.h}, Vec4{0, 0, 0, -1}, Vec4{opacity, 0, 0, 0}};
    const FGTexture texture = source.texture;
    const PipelineHandle handle = *pipeline;
    const u64 sampler = ctx.shaders().sampler(source.width == 1 && source.height == 1
        ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id;
    const u32 pass = ctx.graph().add_raster_pass(label, PassStage::Effects, target, LoadOp::Clear, Vec4{},
        [handle, texture, sampler, push](PassContext& pc) {
            pc.cmds.bind_pipeline(handle);
            pc.cmds.bind_texture(0, pc.texture(texture), SamplerHandle{sampler});
            pc.cmds.push_constants(&push, sizeof(push));
            pc.cmds.draw(6);
        });
    ctx.graph().read(pass, texture);
    source = {target, region, width, height};
    return OkStatus;
}

f64 finite_value(f32 v, f64 fallback) noexcept { return std::isfinite(v) ? static_cast<f64>(v) : fallback; }

// -----------------------------------------------------------------------------
// Posterizar tempo (referência do AE, ADBE Posterize Time). O efeito nativo
// pede a ENTRADA da camada (a fonte com as máscaras e os efeitos ACIMA dele)
// no instante trunc(trunc(t / P) · P), com t em tiques do tempo da CAMADA e
// P = escala de tempo / taxa; o transform e os efeitos abaixo continuam no
// instante de agora. Aqui: o pedido a outro instante do motor (o mesmo do
// Twitch). A escala de tempo é a do AE: a taxa da composição quando inteira;
// senão em centésimos/milésimos (29,97 = 2997/100), então o instante segurado
// pode cair entre dois quadros e vai fracionário ao pedido.
// Evidência: build/reference/effects-20261115/batch-a/posterize-time.md e
// batch-a2/posterize-time.md.
// -----------------------------------------------------------------------------
f64 posterize_held_frames(f64 nowFrames, f64 fps, f32 rate) noexcept {
    if (!(fps > 0.0) || !std::isfinite(nowFrames)) return nowFrames;
    const f64 r = std::clamp(finite_value(rate, 24.0), 0.01, 999.0);
    f64 ticks = 1000.0;
    for (const f64 q : {1.0, 100.0, 1000.0}) {
        if (std::fabs(fps * q - std::round(fps * q)) < 1e-6 * q) { ticks = q; break; }
    }
    const f64 scale = std::round(fps * ticks);
    const f64 t = std::floor(nowFrames * ticks + 1e-6);
    const f64 period = scale / r;
    return std::trunc(std::trunc(t / period) * period) / ticks;
}

class PosterizeTimeReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.time.posterize_reference", "Posterizar tempo", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frame_rate", "Quadros por segundo", 24.0f, 1.0f, 60.0f, kParamAnimatable, "fps");
        p.typed_range(0.01f, 999.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[0] = ParamValue::scalar(8);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const f64 now = e.time_frames();
        return std::fabs(posterize_held_frames(now, e.framesPerSecond, e.f(0)) - now) < 1e-9;
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_cartoon_sample_vert, ShaderId::effects_cartoon_sample_frag,
                                            work, true, BlendMode::Normal));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.temporalSamples = std::make_shared<std::vector<EffectTemporalSample>>();
        if (!e.layer) return;
        const f64 now = e.time_frames();
        const f64 held = posterize_held_frames(now, e.framesPerSecond, e.f(0));
        if (std::fabs(held - now) < 1e-9) return;
        e.temporalSamples->push_back({held + static_cast<f64>(e.layer->start.value - e.layer->offset.value)});
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        out = input;
        if (!e.temporalSamples || e.temporalSamples->empty() || e.temporalSamples->front().snapshot == kInvalidIndex)
            return OkStatus;
        LayerImage source;
        Mat4 matrix;
        f32 opacity = 1;
        if (!ctx.temporal_checkout(e.temporalSamples->front().snapshot, source, matrix, opacity))
            return Status{Errc::Busy, "Posterize Time: temporal source pending"};
        matrix = temporal_sample_matrix(e.temporalSamples->front(), matrix, e.placement ? e.placement->sourceShift : Vec2{});
        const Status status = rasterize_checkout(ctx, source, matrix, opacity, std::max(input.texel_scale_x(), e.texelScale),
                                                 "posterizar-tempo-ae");
        if (!status.ok()) return status;
        out = source;
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Deslocamento no tempo (referência do AE, ADBE Time Displacement). O efeito
// nativo tem dois caminhos; o motor segue o de 8 bits (o projeto padrão do AE,
// 8 bpc sem espaço linear), o de 16 bits (projetos de 16/32 bpc) fica pronto:
//   L = luma Rec.601 inteira do mapa (8 bits 0..255, 16 bits 0..32768);
//   t(L) = agora + (L − 127)/128 · máx (8 bits) ou (L − 16384)/16384 · máx
//   (16 bits), em segundos do tempo da CAMADA,
//   limitado a [0, último quadro da camada] e levado para baixo na grade
//   1/resolução (t − fmod(t, 1/res)), guardado em 1/65536 s;
//   cada instante distinto é um NÍVEL: a entrada da camada é pedida naquele
//   instante e os pixels do nível são COPIADOS dela (sem mistura).
// Aurea junta os níveis que caem no mesmo quadro da composição (o quadro que
// a fonte mostraria naquele instante) e no máximo 64 pedidos por quadro;
// acima disso os níveis vizinhos dividem um pedido. O mapa sem camada é a
// própria camada (o padrão do AE é "a própria camada"); a camada do mapa é a
// imagem dela na composição, como no Mapa de deslocamento do motor.
// Evidência: build/reference/effects-20261115/batch-a2/time-displacement.md.
// -----------------------------------------------------------------------------
struct TimeLevel {
    f64 frame = 0;   ///< quadro no tempo da camada
    u32 lo = 0, hi = 0;
};

constexpr u32 kMaxTimeLevels = 64;
/// O caminho de 8 bits do efeito nativo (ver o comentário acima).
constexpr bool kTimeDisplacementEightBit = true;

void time_displacement_levels(f64 nowFrames, f64 fps, f32 maxSeconds, f32 resolution, f64 lastFrame,
                              std::vector<TimeLevel>& out, bool eightBit = kTimeDisplacementEightBit) {
    out.clear();
    if (!(fps > 0.0)) fps = 30.0;
    const f64 fixedMax = std::clamp(std::round(finite_value(maxSeconds, 1.0) * 65536.0), -3600.0 * 65536.0, 3600.0 * 65536.0);
    const f64 fixedRes = std::clamp(std::round(finite_value(resolution, 60.0) * 65536.0), 655.0, 3600.0 * 65536.0);
    const f64 now = (std::isfinite(nowFrames) ? nowFrames : 0.0) / fps;
    const f64 upper = std::max(0.0, std::isfinite(lastFrame) ? lastFrame / fps : 0.0);
    const f64 step = 65536.0 / fixedRes;
    const f64 amount = fixedMax * 1.52587890625e-05;
    i64 previous = std::numeric_limits<i64>::min();
    const u32 top = eightBit ? 255u : 32768u;
    for (u32 level = 0; level <= top; ++level) {
        const f32 unit = eightBit ? (static_cast<f32>(level) - 127.0f) * 0.0078125f
                                  : (static_cast<f32>(level) - 16384.0f) * 6.1035156e-05f;
        const f64 t = static_cast<f64>(unit) * amount + now;
        const f64 clamped = t <= upper ? std::max(t, 0.0) : upper;
        const f64 snapped = clamped - std::fmod(clamped, step);
        const f64 fixed = std::trunc(snapped * 65536.0 + (snapped < 0.0 ? -0.5 : 0.5));
        const i64 frame = static_cast<i64>(std::floor(fixed / 65536.0 * fps + 1e-3));
        if (out.empty() || frame != previous) out.push_back({static_cast<f64>(frame), level, level});
        else out.back().hi = level;
        previous = frame;
    }
    if (out.size() > kMaxTimeLevels) {
        std::vector<TimeLevel> merged;
        merged.reserve(kMaxTimeLevels);
        const usize n = out.size();
        for (usize b = 0; b < kMaxTimeLevels; ++b) {
            const usize first = n * b / kMaxTimeLevels, last = n * (b + 1) / kMaxTimeLevels - 1;
            merged.push_back({out[(first + last) / 2].frame, out[first].lo, out[last].hi});
        }
        out.swap(merged);
    }
}

class TimeDisplacementReference final : public Effect {
public:
    enum : u32 { kMapLayer = 0, kMaxTime, kResolution, kStretch };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.time.time_displacement_reference", "Time Displacement", "Tempo",
                                  EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("map_layer", "Camada do mapa");
        p.add_float("max_time", "Deslocamento máximo", 1.0f, -4.0f, 4.0f, kParamAnimatable, "s");
        p.typed_range(-3600.0f, 3600.0f);
        p.add_float("time_resolution", "Resolução do tempo", 60.0f, 1.0f, 127.0f, kParamAnimatable, "fps");
        p.typed_range(0.01f, 3600.0f);
        p.add_bool("stretch", "Esticar o mapa", true);
        p.set_tip(kMapLayer, "Branco = adiantado, preto = atrasado, cinza 50% = agora.");
    }
    bool needs_full_input() const noexcept override { return true; }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kMapLayer); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_time_displace_frag,
                                            work, true, BlendMode::Normal));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.temporalSamples = std::make_shared<std::vector<EffectTemporalSample>>();
        e.auxInfo = Vec4{};
        if (!e.layer) return;
        const f64 lastFrame = static_cast<f64>(e.layer->offset.value + (e.layer->end.value - e.layer->start.value)) - 1.0;
        e.auxInfo = Vec4{static_cast<f32>(lastFrame), 1.0f, 0.0f, 0.0f};
        std::vector<TimeLevel> levels;
        time_displacement_levels(e.time_frames(), e.framesPerSecond, e.f(kMaxTime), e.f(kResolution), lastFrame, levels);
        const f64 shift = static_cast<f64>(e.layer->start.value - e.layer->offset.value);
        for (const TimeLevel& level : levels) e.temporalSamples->push_back({level.frame + shift});
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        out = input;
        if (!e.temporalSamples || e.temporalSamples->empty() || e.auxInfo.y < 0.5f) return OkStatus;
        std::vector<TimeLevel> levels;
        time_displacement_levels(e.time_frames(), e.framesPerSecond, e.f(kMaxTime), e.f(kResolution),
                                 static_cast<f64>(e.auxInfo.x), levels);
        if (levels.size() != e.temporalSamples->size()) return OkStatus;
        const u64 ref = e.value(kMapLayer).ref;
        const LayerImage* map = ref ? ctx.layer_input(ref) : &input;
        // A camada escolhida não aparece neste instante: o AE enche o mapa de
        // cinza do meio (agora).
        if (!map || !map->valid()) return OkStatus;

        const Rect region = input.region;
        const f32 density = std::max(input.texel_scale_x(), e.texelScale);
        if (std::ceil(region.w * density) > ctx.max_texture_size() || std::ceil(region.h * density) > ctx.max_texture_size())
            return Status{Errc::OutOfDeviceMemory, "Time Displacement: output exceeds the GPU texture extent"};
        u32 width, height;
        ctx.region_size(region, density, width, height);

        // Onde o mapa fica sobre a camada (px da camada): a própria camada no
        // mesmo pixel; a camada do mapa esticada na caixa da camada ou
        // centralizada 1:1 (o resto é cinza).
        const f32 layerW = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 layerH = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        Vec2 origin{map->region.x, map->region.y}, size{map->region.w, map->region.h};
        if (ref) {
            if (e.b(kStretch)) {
                origin = Vec2{0, 0};
                size = Vec2{layerW, layerH};
            } else {
                origin = Vec2{std::floor((layerW - map->region.w) * .5f), std::floor((layerH - map->region.h) * .5f)};
            }
        }
        size = Vec2{std::max(size.x, 1e-3f), std::max(size.y, 1e-3f)};
        const Vec4 mapFromFragment{region.w / static_cast<f32>(width) / size.x, region.h / static_cast<f32>(height) / size.y,
                                   (region.x - origin.x) / size.x, (region.y - origin.y) / size.y};

        struct Push { Mat4 clipFromLayer; Vec4 region, uvRect, params, slice; };
        struct Draw { FGTexture texture; u64 sampler; Push push; };
        std::vector<Draw> draws;
        draws.reserve(levels.size());
        const Mat4 projection = Mat4::scale(Vec3{2.f / region.w, 2.f / region.h, 1})
            * Mat4::translation(Vec3{-region.x - region.w * .5f, -region.y - region.h * .5f, 0});
        for (usize i = 0; i < levels.size(); ++i) {
            const EffectTemporalSample& sample = (*e.temporalSamples)[i];
            LayerImage image = input;
            Mat4 matrix = Mat4::identity();
            f32 opacity = 1;
            if (sample.snapshot != kInvalidIndex) {
                if (!ctx.temporal_checkout(sample.snapshot, image, matrix, opacity))
                    return Status{Errc::Busy, "Time Displacement: temporal source pending"};
                matrix = temporal_sample_matrix(sample, matrix, e.placement ? e.placement->sourceShift : Vec2{});
            }
            if (!image.valid()) continue;
            Draw d;
            d.texture = image.texture;
            d.sampler = ctx.shaders().sampler(image.width == 1 && image.height == 1
                ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id;
            d.push.clipFromLayer = projection * matrix;
            d.push.region = Vec4{image.region.x, image.region.y, image.region.w, image.region.h};
            d.push.uvRect = Vec4{0, 0, 1, 1};
            d.push.params = Vec4{opacity, static_cast<f32>(levels[i].lo), static_cast<f32>(levels[i].hi),
                                 kTimeDisplacementEightBit ? 24.0f : 8.0f};
            d.push.slice = mapFromFragment;
            draws.push_back(d);
        }
        auto pipeline = ctx.shaders().pipeline(PipelineKey::graphics(ShaderId::composite_layer_vert,
            ShaderId::composite_time_displace_frag, ctx.work_format(), true, BlendMode::Normal));
        if (!pipeline.ok()) return pipeline.status();
        const PipelineHandle handle = *pipeline;
        const FGTexture target = ctx.texture("deslocamento-no-tempo-ae", width, height);
        const FGTexture mapTexture = map->texture;
        const u64 mapSampler = ctx.shaders().sampler(CommonSampler::LinearClamp).id;
        const u32 pass = ctx.graph().add_raster_pass("deslocamento-no-tempo-ae", PassStage::Effects, target, LoadOp::Clear,
            Vec4{}, [handle, draws, mapTexture, mapSampler](PassContext& pc) {
                pc.cmds.bind_pipeline(handle);
                pc.cmds.bind_texture(1, pc.texture(mapTexture), SamplerHandle{mapSampler});
                for (const Draw& d : draws) {
                    pc.cmds.bind_texture(0, pc.texture(d.texture), SamplerHandle{d.sampler});
                    pc.cmds.push_constants(&d.push, sizeof(d.push));
                    pc.cmds.draw(6);
                }
            });
        std::vector<FGTexture> read{mapTexture};
        for (const Draw& d : draws)
            if (std::find(read.begin(), read.end(), d.texture) == read.end()) read.push_back(d.texture);
        for (const FGTexture& t : read) ctx.graph().read(pass, t);
        out = LayerImage{target, region, width, height};
        return OkStatus;
    }
};

} // namespace

void register_reference_batch_a(EffectRegistry& registry) {
    register_radial_blur_reference(registry);
    register_radial_shadow_reference(registry);
    (void)registry.add(std::make_unique<PosterizeTimeReference>());
    (void)registry.add(std::make_unique<TimeDisplacementReference>());
}

} // namespace aurea::builtin
