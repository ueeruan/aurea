#pragma once
#include "aurea/timeline/Composition.hpp"
namespace aurea::grid {
inline constexpr const char* kController="aurea.layout.grid_builder";
inline constexpr const char* kItem="aurea.layout.grid_item";
void declare_parameters(ParameterRegistry&);
struct Placement { Mat4 matrix; f32 opacity=1; };
[[nodiscard]] Placement evaluate(const Composition&,const Layer&,f64 frame);

// Omega grid (0.3.5): nulo de controle da grade. A camada controladora (a que
// leva o efeito da grade) vira filha de um nulo; a cadeia de pais inteira do
// nulo vale (nulo → nulo → …, sem teto de profundidade).
/// Resultados de `Engine::set_grid_control_null` (iguais nas duas UIs).
inline constexpr i32 kLinkOk=0;
inline constexpr i32 kLinkNotGrid=1;    ///< a camada não é uma grade
inline constexpr i32 kLinkNotNull=2;    ///< o alvo não é um nulo desta composição
inline constexpr i32 kLinkCycle=3;      ///< o nulo já segue a grade: fecharia um ciclo
inline constexpr i32 kLinkLocked=4;     ///< grade bloqueada / sem projeto
inline constexpr i32 kLinkFailed=5;     ///< o vínculo não pôde ser medido (escala zero)
/// A camada é a controladora de uma grade (tem o efeito da grade).
[[nodiscard]] bool is_controller(const Layer&) noexcept;
/// `parent` como pai de `child` fecharia um ciclo (ou é a própria camada)?
/// Anda a cadeia inteira de `parent`, sem teto fixo: um caminho mais longo que
/// o número de camadas só existe se já houver ciclo (também conta como ciclo).
[[nodiscard]] bool would_cycle(const Composition&, LayerId child, LayerId parent) noexcept;
}
