// =============================================================================
//  Glitchify reference (aurea.glitch.glitchify_reference).
//
//  CSpice Glitchify 1.0.0 is the behaviour specification (REA evidence in
//  build/reference/effects-20261115/batch-c/rea/Glitchify, earlier notes in
//  build/reference/glitchify/SPEC.md). The contract is the native one: 65
//  functional controls in native order with native defaults and ranges, and
//  the five transfer-mode selectors (39 modes, separators removed). The code
//  is Aurea's: shared C++ (contract, permutation, per-frame wiggles and module
//  constants) plus GLSL, identical for Android and iOS.
//
//  Reconstructed exactly (REA, verified where an AE frame exists):
//    noise engine (permutation, simplex, octaves, 0..255 map) — show-noise
//      frame ≤ 1 byte; Transition ramp; Transform matrix, crop and wiggles;
//      Channel Split offsets; Channel Scale mapping and strength; the Color
//      Glitch byte-stream recurrence (compute pass, exact); Glitch Block copy
//      rule; the module order and transfer-mode/opacity composition.
//  Aurea approximations (native code not reproduced): Channel Scale ordering
//    and Fill Gaps (gather instead of scatter), Pixel Streak segments, the
//    WebP slice re-encode with a flipped bit, JPEG and Floyd–Steinberg.
//  The 8-bit domain holds the linear working-space values, as AE does for an
//  8 bpc effect in a linearized 32 bpc project (the oracle frames).
//
//  The legacy aurea.glitch.glitchify keeps its 60 slots and old output.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/GlitchifyReference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>

namespace aurea::glitchify_reference {

namespace {
/// The documented MSVC CRT generator: state = state · 214013 + 2531011,
/// rand() = (state >> 16) & 0x7fff.
struct CrtRand {
    u32 state;
    explicit CrtRand(u32 seed) noexcept : state(seed) {}
    i32 next() noexcept { state = state * 214013u + 2531011u; return static_cast<i32>((state >> 16) & 0x7fffu); }
};

f32 grad_dot(i32 g, f32 x, f32 y, f32 z) noexcept {
    switch (g) {
        case 0: return x + y;   case 1: return -x + y;  case 2: return x - y;   case 3: return -x - y;
        case 4: return x + z;   case 5: return -x + z;  case 6: return x - z;   case 7: return -x - z;
        case 8: return y + z;   case 9: return -y + z;
        default: return -y - z;  // entries 10 and 11 are the same in the native table
    }
}

f32 simplex(const std::array<u8, 256>& p, f32 x, f32 y, f32 z) noexcept {
    auto P = [&](i32 i) { return static_cast<i32>(p[static_cast<u32>(i) & 255u]); };
    constexpr f32 F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
    const f32 s = (x + y + z) * F3;
    const i32 i = static_cast<i32>(std::floor(x + s)), j = static_cast<i32>(std::floor(y + s)),
              k = static_cast<i32>(std::floor(z + s));
    const f32 t = static_cast<f32>(i + j + k) * G3;
    const f32 x0 = x - (static_cast<f32>(i) - t), y0 = y - (static_cast<f32>(j) - t), z0 = z - (static_cast<f32>(k) - t);
    i32 o1[3], o2[3];
    auto set = [](i32* o, i32 a, i32 b, i32 c) { o[0] = a; o[1] = b; o[2] = c; };
    if (x0 >= y0) {
        if (y0 >= z0) { set(o1, 1, 0, 0); set(o2, 1, 1, 0); }
        else if (x0 >= z0) { set(o1, 1, 0, 0); set(o2, 1, 0, 1); }
        else { set(o1, 0, 0, 1); set(o2, 1, 0, 1); }
    } else {
        if (y0 < z0) { set(o1, 0, 0, 1); set(o2, 0, 1, 1); }
        else if (x0 < z0) { set(o1, 0, 1, 0); set(o2, 0, 1, 1); }
        else { set(o1, 0, 1, 0); set(o2, 1, 1, 0); }
    }
    const i32 zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
    const i32* offsets[4] = {zero, o1, o2, one};
    f32 n = 0;
    for (u32 c = 0; c < 4; ++c) {
        const f32 cx = x0 - static_cast<f32>(offsets[c][0]) + static_cast<f32>(c) * G3;
        const f32 cy = y0 - static_cast<f32>(offsets[c][1]) + static_cast<f32>(c) * G3;
        const f32 cz = z0 - static_cast<f32>(offsets[c][2]) + static_cast<f32>(c) * G3;
        f32 r = 0.6f - cx * cx - cy * cy - cz * cz;
        if (r < 0) continue;
        const i32 g = P(i + offsets[c][0] + P(j + offsets[c][1] + P(k + offsets[c][2]))) % 12;
        r *= r;
        n += r * r * grad_dot(g, cx, cy, cz);
    }
    return n;
}
} // namespace

std::array<u8, 256> permutation(u32 seed) noexcept {
    std::array<u8, 256> p{};
    for (u32 i = 0; i < 256; ++i) p[i] = static_cast<u8>(i);
    CrtRand rng(seed);
    // random_shuffle: position i ≥ 1 swaps with rand() % (i + 1).
    for (u32 i = 1; i < 256; ++i) std::swap(p[i], p[static_cast<u32>(rng.next()) % (i + 1)]);
    return p;
}

f32 sample(const std::array<u8, 256>& perm, f32 x, f32 y, f32 z, f32 height, u32 octaves, f32 lacunarity,
           f32 gain) noexcept {
    f32 f = 1.0f / std::max(height, 1.0f), amplitude = 1, total = 0;
    for (u32 o = 0; o < octaves; ++o) {
        total += amplitude * simplex(perm, y * f, x * f, z * f);
        f *= lacunarity;
        amplitude *= gain;
    }
    return total * 16.0f + 0.5f;
}

f32 wiggle(const std::array<u8, 256>& perm, i32 salt, f32 z, f32 steps, f32 height, u32 octaves, f32 lacunarity,
           f32 gain) noexcept {
    f32 sum = 0;
    for (i32 i = 0; i < 12; ++i)
        sum += sample(perm, static_cast<f32>(salt), static_cast<f32>(salt + 30 * i), z, height, octaves, lacunarity, gain);
    const f32 q = std::max(steps, 1e-3f);
    return std::trunc((sum - 6.0f) / 6.0f * q) / q;
}

namespace {

struct Uniforms {
    Vec4 region, layer, noise, stage, inverse0, inverse1, crop, transform, transition, split, scale, modes,
         enables, streak, slice, block, block2, compress;
};
static_assert(sizeof(Uniforms) == 18 * 16);

const char* const kTransferModes[kTransferModeCount] = {
    "Normal", "Copiar", "Atrás", "Na frente", "Escurecer", "Multiplicar", "Superexposição de cor",
    "Superexposição de cor clássica", "Superexposição linear", "Cor mais escura", "Adicionar", "Clarear",
    "Tela", "Subexposição de cor", "Subexposição de cor clássica", "Subexposição linear", "Cor mais clara",
    "Sobrepor", "Luz suave", "Luz direta", "Luz linear", "Luz vívida", "Luz do pino", "Mistura sólida",
    "Diferença", "Diferença clássica", "Exclusão", "Subtrair", "Dividir", "Matiz", "Saturação", "Cor",
    "Luminosidade", "Estêncil alfa", "Estêncil luma", "Silhueta alfa", "Silhueta luma",
    "Pré-multiplicação aditiva", "Adicionar alfa"};

class GlitchifyReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kKey, "Glitchify", "Glitch", EffectClass::Domain};
        return i;
    }
    bool needs_full_input() const noexcept override { return true; }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr u16 pct = kParamAnimatable | kParamPercent;
        p.add_float("amount", "Quantidade do Glitchify", 50, 0, 100, pct, "%");
        p.add_float("speed", "Velocidade do Glitchify", 50, 0, 100, kParamAnimatable);
        p.add_int("seed", "Semente aleatória", 0, 0, 32768);
        // Transition
        p.add_float("completion", "Conclusão", 0, 0, 100, pct, "%");
        p.add_float("softness", "Suavidade", 10, 0, 100, pct, "%");
        static const char* const kTransition[] = {"Vermelho", "Verde", "Azul", "Alfa", "Luminância", "Claridade",
                                                  "Matiz", "Saturação", "Ruído"};
        p.add_enum("transition_channel", "Canal", kTransition, 9, 5);
        // Transform Glitch
        p.add_float("transform_speed", "Velocidade", 1, 0, 100, kParamAnimatable);
        p.add_point2("position", "Posição", {.5f, .5f}, -2, 3, kParamAnimatable | kParamRelative);
        p.add_float("position_wiggle", "Oscilação da posição", 0, 0, 100, pct, "%");
        p.typed_range(0, 30000);
        p.add_float("scale", "Escala", 100, -200, 200, pct, "%");
        p.typed_range(-30000, 30000);
        p.add_float("scale_wiggle", "Oscilação da escala", 0, 0, 100, pct, "%");
        p.typed_range(0, 30000);
        p.add_point2("crop_top_left", "Recorte", {0, 0}, -2, 3, kParamAnimatable | kParamRelative);
        p.add_point2("crop_bottom_right", "Recorte (canto inferior direito)", {1, 1}, -2, 3, kParamAnimatable | kParamRelative);
        p.add_float("crop_wiggle", "Oscilação do recorte", 0, 0, 100, pct, "%");
        p.typed_range(0, 30000);
        p.add_float("transform_opacity", "Opacidade", 100, 0, 100, pct, "%");
        p.add_enum("transform_mode", "Modo de transferência", kTransferModes, kTransferModeCount, 0);
        p.add_bool("composite_over", "Compor sobre o original", false);
        // Channel Glitch
        p.add_bool("channel_enabled", "Ativar", true);
        static const char* const kSplit[] = {"Vermelho-ciano", "Verde-magenta", "Azul-amarelo", "Y′", "Cb", "Cr",
                                             "Matiz", "Saturação", "Valor"};
        p.add_enum("split_channel", "Canal", kSplit, 9, 7);
        p.add_float("split_horizontal", "Quantidade horizontal", 50, -100, 100, kParamAnimatable);
        p.add_float("split_vertical", "Quantidade vertical", 0, -100, 100, kParamAnimatable);
        p.add_float("split_speed", "Multiplicador de velocidade", 0, 0, 10, kParamAnimatable);
        p.add_float("channel_scale", "Escala", 30, -100, 100, kParamAnimatable);
        p.typed_range(-800, 800);
        static const char* const kScale[] = {"Luma", "Claridade", "Matiz", "Saturação", "Vermelho", "Verde", "Azul", "Alfa"};
        p.add_enum("scale_channel", "Canal", kScale, 8, 0);
        p.add_bool("scale_vertical", "Vertical", false);
        p.add_float("scale_offset", "Deslocamento", 50, 0, 100, pct, "%");
        p.add_float("scale_speed", "Multiplicador de velocidade", 3, 0, 10, kParamAnimatable);
        p.add_enum("channel_mode", "Modo de transferência", kTransferModes, kTransferModeCount, 0);
        // Color Glitch
        p.add_bool("color_enabled", "Ativar", true);
        p.add_float("color_offset", "Deslocamento", 20, 0, 100, kParamAnimatable);
        p.add_float("color_amount", "Quantidade", 80, 0, 100, pct, "%");
        p.add_float("color_opacity", "Opacidade", 25, 0, 100, pct, "%");
        p.add_enum("color_mode", "Modo de transferência", kTransferModes, kTransferModeCount, 0);
        // Image Glitch
        p.add_bool("image_enabled", "Ativar", true);
        p.add_float("pixel_streak", "Rastro de pixels", 25, 0, 100, pct, "%");
        p.add_bool("vertical_sort", "Ordenação vertical", false);
        p.add_float("slice_amount", "Quantidade", 50, 0, 100, pct, "%");
        p.add_float("slice_position", "Posição", 50, 0, 100, pct, "%");
        p.add_float("slice_opacity", "Opacidade", 50, 0, 100, pct, "%");
        p.add_enum("slice_mode", "Modo de transferência", kTransferModes, kTransferModeCount, kTransferDivide);
        p.add_float("block_amount", "Quantidade", 50, 0, 100, pct, "%");
        p.add_float("block_speed", "Multiplicador de velocidade", 50, 0, 100, kParamAnimatable);
        p.add_float("block_width", "Tamanho horizontal", 150, 0, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, 100000);
        p.add_float("block_height", "Tamanho vertical", 50, 0, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, 100000);
        static const char* const kDirections[] = {"Horizontal", "Vertical", "Horizontal e vertical"};
        p.add_enum("block_direction", "Direção", kDirections, 3, 0);
        p.add_enum("image_mode", "Modo de transferência", kTransferModes, kTransferModeCount, kTransferOverlay);
        // Compression Glitch
        p.add_bool("compression_enabled", "Ativar", false);
        p.add_float("compression_multiplier", "Multiplicador", 50, 0, 100, pct, "%");
        p.add_float("dither_amount", "Quantidade de pontilhado", 50, 0, 100, pct, "%");
        p.add_float("jpeg_amount", "Quantidade de JPEG", 50, 0, 100, pct, "%");
        // Advanced
        p.add_bool("show_noise", "Mostrar o motor de ruído", false);
        p.add_float("noise_gain", "Ganho", 1.5f, 0, 5, kParamAnimatable);
        p.add_float("noise_lacunarity", "Lacunaridade", 10, 0, 30, kParamAnimatable);
        p.add_int("noise_octaves", "Oitavas", 2, 0, 16);
        p.add_bool("repeat_edges", "Repetir pixels da borda", true);
        p.add_bool("sigmoid_steps", "Degraus sigmoides", false);
        p.add_float("scale_steps", "Passos de quantização", 2, 1, 64, kParamAnimatable);
        p.add_bool("fill_gaps", "Preencher lacunas", false);
        p.add_bool("use_noise", "Usar ruído", true);
        p.add_int("noise_cutoff", "Corte do ruído", 50, 0, 100);
        p.add_float("slice_quality", "Qualidade", 10, 0, 100, pct, "%");
        p.add_float("slice_shift", "Deslocamento aleatório máximo", 40, 0, 100, pct, "%");
        p.add_float("slice_steps", "Passos de quantização", 8, 1, 64, kParamAnimatable);
        p.add_float("grouping", "Fator de agrupamento", 5, 0, 1000, kParamAnimatable);
        p.add_float("wiggle_steps", "Passos de quantização da oscilação", 2, 1, 64, kParamAnimatable);
        p.add_section_range("transition", "Transição", kCompletion, kTransitionChannel);
        p.add_section_range("transform", "Glitch de transformação", kTransformSpeed, kCompositeOver);
        p.add_section_range("channel", "Glitch de canal", kChannelEnabled, kChannelMode, kChannelEnabled);
        p.add_section_range("color", "Glitch de cor", kColorEnabled, kColorMode, kColorEnabled);
        p.add_section_range("image", "Glitch de imagem", kImageEnabled, kImageMode, kImageEnabled);
        p.add_section_range("compression", "Glitch de compressão", kCompressionEnabled, kJpegAmount, kCompressionEnabled, false);
        p.add_section_range("advanced", "Avançado", kShowNoise, kWiggleSteps, kInvalidIndex, false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(100);
        v[kPositionWiggle] = ParamValue::scalar(20);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glitchify_reference_base_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glitchify_reference_frag, work));
        out.push_back(PipelineKey::compute_shader(ShaderId::effects_glitchify_reference_corrupt_comp));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = {};
        const u32 seed = static_cast<u32>(std::clamp(e.f(kSeed), 0.0f, 32768.0f));
        const auto perm = permutation(seed);
        Vec4 texels[256];
        for (u32 i = 0; i < 256; ++i) texels[i] = Vec4{static_cast<f32>(perm[i]), 0, 0, 0};
        if (e.resources) e.aux = e.resources->data_texture(0x474c4954434852ull ^ (static_cast<u64>(seed) << 8), texels, 256, 1);
    }

    static FGTexture byte_image(EffectBuildContext& ctx, u32 width, u32 height) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = SurfaceFormat::R32F;
        desc.sampled = true;
        desc.storage = true;
        desc.debugName = "glitchify-ref-bytes";
        return ctx.graph().create_texture("glitchify-ref-bytes", desc);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        if (!input.width || !input.height || !e.aux.valid()) { out = input; return OkStatus; }
        const f32 W = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 H = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f32 scaleX = input.texel_scale_x();
        const f32 amount = std::clamp(e.f(kAmount), 0.0f, 100.0f) * 0.01f;
        const auto perm = permutation(static_cast<u32>(std::clamp(e.f(kSeed), 0.0f, 32768.0f)));
        const u32 octaves = static_cast<u32>(std::clamp(e.f(kNoiseOctaves), 0.0f, 16.0f));
        const f32 lac = std::max(0.0f, e.f(kNoiseLacunarity)), gain = std::max(0.0f, e.f(kNoiseGain));
        const f64 seconds = e.time_frames() / std::max(e.framesPerSecond, 1e-6);
        const f32 z0 = static_cast<f32>(2.0 * seconds * e.f(kSpeed));   // the engine's time (2·t·speed)
        auto wig = [&](i32 salt, f32 mult, f32 steps) { return wiggle(perm, salt, z0 * mult, steps, H, octaves, lac, gain); };
        auto smp = [&](f32 x, f32 y, f32 mult) { return sample(perm, x, y, z0 * mult, H, octaves, lac, gain); };

        Uniforms u{};
        u.region = {input.region.x, input.region.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
        u.layer = {W, H, 1, z0};   // bytes = linear working-space values (AE linearized project, as the oracle)
        u.noise = {static_cast<f32>(octaves), lac, gain, amount};
        u.stage.x = e.b(kShowNoise) ? 1.f : 0.f;

        // Transform Glitch (render 0x18005E570): dst = s·(src − W/2) + Position − wiggle.
        const f32 mult = e.f(kTransformSpeed) * e.f(kSpeed) * 0.1f;
        const f32 steps = std::clamp(e.f(kWiggleSteps), 1.0f, 64.0f);
        const f32 posW = e.f(kPositionWiggle) * 0.01f * amount;
        const Vec2 wiggle0{wig(0xd0a, mult, steps) * posW * W, wig(0x208d, mult, steps) * posW * H};
        const f32 s = e.f(kScale) * 0.01f + e.f(kScaleWiggle) * 0.01f * amount * wig(-0x227, mult, steps);
        const Vec2 pos{e.p2(kPosition).x * W, e.p2(kPosition).y * H};
        const f32 inv = std::fabs(s) > 1e-6f ? 1.0f / s : 0.0f;
        // src = (dst − Position + wiggle)/s + W/2
        u.inverse0 = {inv, 0, (-pos.x + wiggle0.x) * inv + W * 0.5f, 0};
        u.inverse1 = {0, inv, (-pos.y + wiggle0.y) * inv + H * 0.5f, 0};
        const f32 cropW = e.f(kCropWiggle) * 0.01f * W * amount;
        f32 x0 = std::round(e.p2(kCropTopLeft).x * W) + std::trunc(wig(0x7c3, mult, steps) * cropW);
        f32 y0 = std::round(e.p2(kCropTopLeft).y * H) + std::trunc(wig(0xf93, mult, steps) * cropW);
        f32 x1 = std::round(e.p2(kCropBottomRight).x * W) + std::trunc(wig(0x1763, mult, steps) * cropW);
        f32 y1 = std::round(e.p2(kCropBottomRight).y * H) + std::trunc(wig(0x1f33, mult, steps) * cropW);
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        u.crop = {x0, y0, x1, y1};
        u.transform = {static_cast<f32>(std::min(e.e(kTransformMode), kTransferModeCount - 1)),
                       std::clamp(e.f(kTransformOpacity), 0.0f, 100.0f) * 0.01f,
                       std::clamp(e.f(kCompletion), 0.0f, 100.0f) * 0.01f,
                       std::clamp(e.f(kSoftness), 0.0f, 100.0f) * 2.55f};
        u.transition = {static_cast<f32>(std::min(e.e(kTransitionChannel), 8u)), e.b(kCompositeOver) ? 1.f : 0.f, 0, 0};

        // Channel Glitch (0x1800590A0, 0x180057E60).
        const f32 sm = e.f(kSplitSpeed);
        const f32 decay = 1.0f - std::exp(-0.4f * sm);
        const f32 n1 = smp(1234, 5678, sm * 0.4f), n2 = smp(5678, 1234, sm * 0.4f);
        const f32 dx = std::trunc((1.0f - decay * (n1 - 0.5f) * 2.0f) * e.f(kSplitHorizontal) * amount);
        const f32 dy = std::trunc((1.0f - decay * (n2 - 0.5f) * 2.0f) * e.f(kSplitVertical) * amount);
        u.split = {static_cast<f32>(std::min(e.e(kSplitChannel), 8u)), dx * scaleX, dy * scaleX, e.b(kRepeatEdges) ? 1.f : 0.f};
        f32 q = smp(13, 13, e.f(kScaleSpeed));
        if (e.b(kSigmoidSteps)) q = std::tanh((q - 0.5f) * 4.0f);
        const f32 qs = std::clamp(e.f(kScaleSteps), 1.0f, 64.0f);
        q = std::trunc(q * qs) / qs;
        const f32 strength = std::trunc(e.f(kChannelScale) * amount *
            (1.0f - (1.0f - std::exp(-3.0f * e.f(kScaleSpeed))) * (q - 0.5f) * 2.0f));
        const bool vertical = e.b(kScaleVertical);
        const f32 line = std::trunc((vertical ? H : W) * e.f(kScaleOffset) * 0.01f);
        const f32 lineTexel = (line - (vertical ? input.region.y : input.region.x)) * scaleX;
        u.scale = {static_cast<f32>(std::min(e.e(kScaleChannel), 7u)), vertical ? 1.f : 0.f, std::trunc(lineTexel), strength};
        u.modes = {static_cast<f32>(std::min(e.e(kChannelMode), kTransferModeCount - 1)),
                   static_cast<f32>(std::min(e.e(kColorMode), kTransferModeCount - 1)),
                   static_cast<f32>(std::min(e.e(kSliceMode), kTransferModeCount - 1)),
                   static_cast<f32>(std::min(e.e(kImageMode), kTransferModeCount - 1))};
        // Color Glitch (0x18005BBD0): byte offset and gain.
        const f32 offsetBytes = std::trunc(std::max(0.0f, e.f(kColorOffset)) * 80.0f);
        const f32 k = (smp(50, 50, 1.0f) * 0.2f + 0.8f) * std::clamp(e.f(kColorAmount), 0.0f, 100.0f) * 0.01f * amount;
        const bool color = e.b(kColorEnabled) && offsetBytes > 0;
        u.stage.y = offsetBytes;
        u.stage.z = k;
        u.stage.w = color ? 1.f : 0.f;
        u.enables = {e.b(kChannelEnabled) ? 1.f : 0.f, color ? 1.f : 0.f, e.b(kImageEnabled) ? 1.f : 0.f,
                     e.b(kCompressionEnabled) ? 1.f : 0.f};
        // Image Glitch.
        u.streak = {std::round(e.f(kPixelStreak) * 0.4f * amount * scaleX), e.b(kVerticalSort) ? 1.f : 0.f,
                    e.b(kUseNoise) ? 1.f : 0.f, std::clamp(e.f(kNoiseCutoff), 0.0f, 100.0f)};
        const f32 a = std::clamp(e.f(kSliceAmount), 0.0f, 100.0f) * 0.01f * amount;
        const f32 sliceSteps = std::clamp(e.f(kSliceSteps), 1.0f, 64.0f);
        const f32 sliceCenter = e.f(kSlicePosition) * 0.01f * (H - 1) +
            wig(0x2b67, 4.0f, sliceSteps) * (1 - a * a) * e.f(kSliceShift) * 0.01f * H;
        const f32 bandH = a * H;
        u.slice = {sliceCenter - bandH * 0.5f, bandH, std::clamp(e.f(kSliceOpacity), 0.0f, 100.0f) * 0.01f,
                   1.0f - std::clamp(e.f(kSliceQuality), 0.0f, 100.0f) * 0.01f};
        const f32 b = std::clamp(e.f(kBlockAmount), 0.0f, 100.0f) * 0.01f * amount;
        u.block = {(1 - b * b) * 100.0f, std::min(std::max(0.0f, e.f(kBlockWidth)), W), std::min(std::max(0.0f, e.f(kBlockHeight)), H),
                   static_cast<f32>(std::min(e.e(kBlockDirection), 2u))};
        u.block2 = {std::max(0.0f, e.f(kGrouping)), z0 * e.f(kBlockSpeed) / 25.0f,
                    std::clamp(e.f(kColorOpacity), 0.0f, 100.0f) * 0.01f, z0 * 4.0f};
        const f32 multiplier = std::clamp(e.f(kCompressionMultiplier), 0.0f, 100.0f) * 0.01f;
        u.compress = {std::clamp(e.f(kJpegAmount), 0.0f, 100.0f) * 0.01f * multiplier * amount,
                      std::clamp(e.f(kDitherAmount), 0.0f, 100.0f) * 0.01f * multiplier, 0, 0};

        const PassTexture permTexture{{}, e.aux, CommonSampler::NearestClamp};
        const FGTexture base = ctx.texture("glitchify-ref-base", input.width, input.height);
        if (ctx.fullscreen_pass("glitchify-ref-base", PassStage::Effects, base, ShaderId::effects_glitchify_reference_base_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}, PassTexture{input.texture, {}, CommonSampler::NearestClamp}, permTexture},
                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        out = input;
        if (e.b(kShowNoise)) { out.texture = base; return OkStatus; }
        // The native modules share ONE running world: each reads the current
        // result and is composited back into it before the next module runs.
        FGTexture running = base;
        static const char* const kStageNames[6] = {"", "glitchify-ref-channel", "glitchify-ref-color", "glitchify-ref-streak",
                                                   "glitchify-ref-slice", "glitchify-ref-final"};
        auto stage = [&](u32 index, FGTexture bytesTexture) -> Status {
            Uniforms s = u;
            s.compress.z = static_cast<f32>(index);
            const FGTexture target = ctx.texture(kStageNames[index], input.width, input.height);
            if (ctx.fullscreen_pass(kStageNames[index], PassStage::Effects, target, ShaderId::effects_glitchify_reference_frag,
                    {PassTexture{running, {}, CommonSampler::NearestClamp}, PassTexture{bytesTexture, {}, CommonSampler::NearestClamp},
                     permTexture, PassTexture{input.texture, {}, CommonSampler::NearestClamp}},
                    &s, sizeof(s)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            running = target;
            return OkStatus;
        };
        if (e.b(kChannelEnabled)) if (const Status st = stage(1, base); !st.ok()) return st;
        if (color) {
            const FGTexture source = running;
            const FGTexture bytes = byte_image(ctx, input.width * 4, input.height);
            const auto pipeline = ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_glitchify_reference_corrupt_comp));
            if (!pipeline.ok()) return pipeline.status();
            const auto sampler = ctx.shaders().sampler(CommonSampler::NearestClamp);
            const auto handle = *pipeline;
            const u32 chains = static_cast<u32>(offsetBytes);
            const u32 pass = ctx.graph().add_compute_pass("glitchify-ref-corrupt", PassStage::Effects,
                [u, source, bytes, sampler, handle, chains](PassContext& pc) {
                    pc.cmds.bind_pipeline(handle);
                    pc.cmds.bind_texture(0, pc.texture(source), sampler);
                    pc.cmds.bind_storage_image(0, pc.texture(bytes));
                    pc.cmds.set_uniforms(&u, sizeof(u));
                    pc.cmds.dispatch((chains + 63u) / 64u, 1, 1);
                });
            ctx.graph().read(pass, source);
            ctx.graph().write_storage(pass, bytes);
            if (const Status st = stage(2, bytes); !st.ok()) return st;
        }
        if (e.b(kImageEnabled)) {
            if (u.streak.x >= 1.0f) if (const Status st = stage(3, base); !st.ok()) return st;
            if (u.slice.y > 0.0f) if (const Status st = stage(4, base); !st.ok()) return st;
        }
        if (const Status st = stage(5, base); !st.ok()) return st;
        out.texture = running;
        return OkStatus;
    }
};

} // namespace

void register_effect(EffectRegistry& registry) { (void)registry.add(std::make_unique<GlitchifyReference>()); }

} // namespace aurea::glitchify_reference
