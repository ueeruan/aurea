// =============================================================================
//  Aurea / render / RuntimeShaders.hpp
//
//  Shaders que NÃO vieram do build: os dos plugins de efeito, compilados no
//  aparelho na instalação (ou na abertura do motor) e registrados aqui.
//
//  Duas peças:
//
//   1. COMPILADOR. GLSL 450 (o mesmo dialeto Vulkan dos shaders do motor) →
//      SPIR-V 1.0 com o glslang. Roda só na CPU, fora do caminho do quadro —
//      a regra "nada de GLSL em runtime no playback" continua valendo: o plugin
//      compila uma vez, ao instalar/abrir, e o pipeline sai como qualquer outro
//      (ShaderLibrary, pré-aquecimento quando o projeto o usa).
//
//   2. REGISTRO GLOBAL. Cada shader registrado ganha um `ShaderId` DINÂMICO
//      (acima de `kRuntimeShaderBase`, nunca reaproveitado no processo). Toda
//      ShaderLibrary (preview, export, prévia do catálogo) cria o módulo do
//      backend sob demanda a partir daqui — o export, que tem renderer próprio,
//      enxerga o plugin sem nada a mais. Tirar o registro (desinstalar,
//      reinstalar) faz as bibliotecas soltarem os pipelines que o usavam.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/shaders/ShaderIds.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aurea {

/// Primeiro id de shader de runtime. Os embutidos vão de 0 a `kShaderCount`.
inline constexpr u16 kRuntimeShaderBase = 0x8000;

[[nodiscard]] constexpr bool is_runtime_shader(ShaderId id) noexcept {
    return static_cast<u16>(id) >= kRuntimeShaderBase && static_cast<u16>(id) != 0xFFFF;
}

namespace runtime_shaders {

/// Um trecho de fonte com nome: o nome aparece nas mensagens de erro
/// ("rgb_wave.frag:12: ..."). O cabeçalho que o motor gera tem nome próprio.
struct SourcePart {
    std::string_view name;
    std::string_view text;
};

/// O glslang está neste binário? (AUREA_RUNTIME_SHADERS no build.)
[[nodiscard]] bool compiler_available() noexcept;

/// GLSL 450 (semântica Vulkan) → SPIR-V 1.0. `parts` são concatenados na
/// ordem (cada um com a própria numeração de linha). `log` recebe os erros já
/// no formato "arquivo:linha: mensagem", um por linha. Thread-safe.
[[nodiscard]] bool compile_glsl(ShaderStage stage, std::span<const SourcePart> parts, std::vector<u32>& spirv,
                                std::string& log) noexcept;

/// O que o registro guarda de um shader de runtime (imutável).
struct Source {
    ShaderStage stage = ShaderStage::Fragment;
    std::vector<u32> spirv;
    std::string name;   ///< "plugin <id> passe N" — nome de depuração/erro
};

/// Registra e devolve o id dinâmico; `ShaderId::Count` se o espaço de ids acabou.
[[nodiscard]] ShaderId register_shader(ShaderStage stage, std::vector<u32> spirv, std::string name) noexcept;
/// Tira do registro. As bibliotecas soltam os pipelines na próxima consulta.
void unregister_shader(ShaderId id) noexcept;
/// O shader registrado com este id (nulo = não existe mais).
[[nodiscard]] std::shared_ptr<const Source> find(ShaderId id) noexcept;
/// Sobe a cada registro/remoção: a ShaderLibrary só varre o que perdeu quando muda.
[[nodiscard]] u64 generation() noexcept;
/// Quantos estão registrados agora (testes).
[[nodiscard]] u32 registered_count() noexcept;

} // namespace runtime_shaders
} // namespace aurea
