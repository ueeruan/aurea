// =============================================================================
//  Aurea / effects / EffectRegistry.hpp
//
//  Os tipos de efeito disponíveis. Preenchido uma vez na inicialização do
//  motor; depois é só leitura (seguro entre threads).
// =============================================================================
#pragma once

#include "aurea/effects/Effect.hpp"

#include <memory>
#include <vector>

namespace aurea {

class EffectRegistry {
public:
    /// Registra um tipo. Recusa chave repetida — duas implementações com a
    /// mesma chave fariam o projeto abrir com o efeito errado.
    [[nodiscard]] Status add(std::unique_ptr<Effect> effect);

    [[nodiscard]] const Effect* find(EffectTypeId id) const noexcept;
    [[nodiscard]] const ParameterRegistry* params(EffectTypeId id) const noexcept;
    [[nodiscard]] EffectTypeId find_key(std::string_view key) const noexcept;

    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(entries_.size()); }
    [[nodiscard]] const Effect& at(u32 i) const noexcept { return *entries_[i].effect; }
    [[nodiscard]] const ParameterRegistry& params_at(u32 i) const noexcept { return entries_[i].params; }

    /// Todos os pipelines de todos os efeitos — o pré-aquecimento.
    void collect_pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const;

private:
    struct Entry {
        std::unique_ptr<Effect> effect;
        ParameterRegistry params;
        EffectTypeId id = 0;
    };
    std::vector<Entry> entries_;
};

/// Registra os efeitos embutidos do Aurea.
void register_builtin_effects(EffectRegistry& registry);

// Chaves estáveis dos efeitos embutidos (o id no projeto é o hash delas).
namespace effect_keys {
    inline constexpr const char* kBoxBlur = "aurea.blur.box";
    inline constexpr const char* kDirectionalBlur = "aurea.blur.directional";
    inline constexpr const char* kLensFlare = "aurea.light.lens_flare";
    inline constexpr const char* kRipple = "aurea.distort.ripple";
    inline constexpr const char* kOpticsCompensation = "aurea.distort.optics_compensation";
    inline constexpr const char* kLinearWipe = "aurea.transition.linear_wipe";
    inline constexpr const char* kRadialWipe = "aurea.transition.radial_wipe";
    inline constexpr const char* kBlockDissolve = "aurea.transition.block_dissolve";
    inline constexpr const char* kTransform          = "aurea.transform";
    inline constexpr const char* kExposure           = "aurea.color.exposure";
    inline constexpr const char* kBrightnessContrast = "aurea.color.brightness_contrast";
    inline constexpr const char* kSaturation         = "aurea.color.saturation";
    inline constexpr const char* kTint               = "aurea.color.tint";
    inline constexpr const char* kColorMatrix        = "aurea.color.matrix";
    inline constexpr const char* kLevels             = "aurea.color.levels";
    inline constexpr const char* kCurves             = "aurea.color.curves";
    inline constexpr const char* kGaussianBlur       = "aurea.blur.gaussian";
    inline constexpr const char* kSharpen            = "aurea.blur.sharpen";
    inline constexpr const char* kGlow               = "aurea.light.glow";
    inline constexpr const char* kMotionTile         = "aurea.stylize.motion_tile";
    inline constexpr const char* kLumaKey            = "aurea.key.luma";
    inline constexpr const char* kChromaKey          = "aurea.key.chroma";
    inline constexpr const char* kEchoTrail          = "aurea.time.echo";

    // --- Fase 7.3 (§22): o pacote de efeitos novos ---
    // Cor
    inline constexpr const char* kInvert             = "aurea.color.invert";
    inline constexpr const char* kColorama           = "aurea.color.colorama";
    // Desfoque e nitidez
    inline constexpr const char* kUnsharp            = "aurea.blur.unsharp";
    inline constexpr const char* kLensBlur           = "aurea.blur.lens";
    // Luz
    inline constexpr const char* kHalation           = "aurea.light.halation";
    inline constexpr const char* kDeepGlow           = "aurea.light.deep_glow";
    inline constexpr const char* kRays               = "aurea.light.rays";
    inline constexpr const char* kLightSweep         = "aurea.light.sweep";
    // Distorção
    inline constexpr const char* kShake              = "aurea.distort.shake";
    inline constexpr const char* kTurbulence         = "aurea.distort.turbulence";
    inline constexpr const char* kWaveWarp           = "aurea.distort.wave_warp";
    inline constexpr const char* kWarp               = "aurea.distort.warp";
    inline constexpr const char* kRippleDissolve     = "aurea.distort.ripple_dissolve";
    // Estilizar
    inline constexpr const char* kScanlines          = "aurea.stylize.scanlines";
    inline constexpr const char* kGrain              = "aurea.stylize.grain";
    inline constexpr const char* kHalftone           = "aurea.stylize.halftone";
    inline constexpr const char* kMinimax            = "aurea.stylize.minimax";
    inline constexpr const char* kPixelSort          = "aurea.stylize.pixel_sort";
    inline constexpr const char* kFilmDamage         = "aurea.stylize.film_damage";
    inline constexpr const char* kJpegDamage         = "aurea.stylize.jpeg_damage";
    inline constexpr const char* kHoloMatrix         = "aurea.stylize.holomatrix";
    // Glitch
    inline constexpr const char* kGlitchify          = "aurea.glitch.glitchify";
    inline constexpr const char* kVhs                = "aurea.glitch.vhs";
    inline constexpr const char* kUniVhs             = "aurea.glitch.uni_vhs";
    inline constexpr const char* kSignal             = "aurea.glitch.signal";
    inline constexpr const char* kCrossGlitch        = "aurea.glitch.cross";
    // Tempo (integrados ao Temporal Engine, não desenham na cadeia de pixels)
    inline constexpr const char* kPosterizeTime      = "aurea.time.posterize";
    inline constexpr const char* kTimeWarpRgb        = "aurea.time.warp_rgb";
    inline constexpr const char* kTimeRemap          = "aurea.time.remap";
    // Controles de expressão (não desenham; ver ExpressionControls.cpp).
    inline constexpr const char* kSliderControl      = "aurea.control.slider";
    inline constexpr const char* kAngleControl       = "aurea.control.angle";
    inline constexpr const char* kCheckboxControl    = "aurea.control.checkbox";
    inline constexpr const char* kColorControl       = "aurea.control.color";
    inline constexpr const char* kPointControl       = "aurea.control.point";
}

} // namespace aurea
