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
    // Controles de expressão (não desenham; ver ExpressionControls.cpp).
    inline constexpr const char* kSliderControl      = "aurea.control.slider";
    inline constexpr const char* kAngleControl       = "aurea.control.angle";
    inline constexpr const char* kCheckboxControl    = "aurea.control.checkbox";
    inline constexpr const char* kColorControl       = "aurea.control.color";
    inline constexpr const char* kPointControl       = "aurea.control.point";
}

} // namespace aurea
