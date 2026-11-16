// =============================================================================
//  Aurea / shaderxlate / ShaderTranslate.hpp
//
//  A tradução SPIR-V → ESSL 3.10 (GLES) e SPIR-V → MSL (Metal), num lugar só.
//
//  Quem usa:
//    - as ferramentas do HOST (tools/gles-shaders, tools/metal-shaders), que
//      traduzem os shaders embutidos no build;
//    - os backends GLES e Metal, que traduzem em RUNTIME os shaders dos
//      plugins (compilados no aparelho pelo glslang).
//
//  É o MESMO código, com as MESMAS opções e o MESMO remapeamento de bindings:
//  um shader de plugin chega ao driver exatamente como chegaria se tivesse sido
//  embutido no build. Mudar uma opção aqui muda os dois caminhos juntos.
//
//  Este cabeçalho não inclui o SPIRV-Cross: o motor (compilado sem exceções)
//  só vê funções `noexcept` que devolvem o erro em texto. As exceções do
//  SPIRV-Cross ficam presas em ShaderTranslate.cpp, o único arquivo com elas.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace aurea::xlate {

/// Estágio do shader, lido do próprio SPIR-V.
enum class Stage : uint8_t { Vertex = 0, Fragment, Compute, Unknown };

// AUREAMSL version-1 header flags. Capability bits apply to source and metallib payloads.
inline constexpr uint32_t kMslFlagMetallib = 1u;
inline constexpr uint32_t kMslFlagRequiresReadWriteTier2 = 1u << 1;
inline constexpr uint32_t kMslFlagRequiresReadWriteTier1 = 1u << 2;
inline constexpr uint32_t kMslFlagUnsupportedReadWriteFormat = 1u << 3;
inline constexpr uint32_t kMslKnownFlags = kMslFlagMetallib | kMslFlagRequiresReadWriteTier2
    | kMslFlagRequiresReadWriteTier1 | kMslFlagUnsupportedReadWriteFormat;

/// Reflects read-write storage-image formats. Read-only/write-only images do
/// not require a Metal read-write texture tier. Invalid modules return false.
[[nodiscard]] bool spirv_msl_resource_flags(const uint32_t* words, size_t wordCount,
                                           uint32_t& flags, std::string& error) noexcept;

/// FNV-1a 64 dos bytes do SPIR-V — a chave com que o backend GLES acha o ESSL
/// embutido (tools/gles-shaders/embed.py).
[[nodiscard]] uint64_t spirv_fingerprint(const uint32_t* words, size_t wordCount) noexcept;

/// SPIR-V → ESSL 3.10 com o ABI do backend GLES (bindings remapeados, nomes
/// dos recursos, borda transparente emulada). `out` recebe o texto COMPLETO,
/// já com a primeira linha `// AUREA_SPIRV_FNV64 <hex>` — byte a byte o que a
/// ferramenta do build grava. `keyWords` (opcional) é o SPIR-V cuja impressão
/// digital vai na primeira linha quando o ESSL é traduzido de um módulo
/// equivalente compatível com o driver (shape.frag em -O0).
[[nodiscard]] bool spirv_to_essl(const uint32_t* words, size_t wordCount, std::string& out, std::string& error,
                                 const uint32_t* keyWords = nullptr, size_t keyWordCount = 0) noexcept;

/// Há tradução para MSL neste binário? (o Android não a leva: o Metal só
/// existe na Apple; o host leva para os testes.)
[[nodiscard]] bool msl_available() noexcept;

/// Nome da função de entrada que o backend Metal procura por estágio
/// (msl_glue.md §2): vs_main, fs_main, cs_main.
[[nodiscard]] const char* msl_entry_for(Stage stage) noexcept;

/// SPIR-V → MSL 2.1 (iOS) com a tabela fixa de recursos do backend Metal.
/// `fragOutputMask`: bit N mantém a saída de cor N (msl_glue.md §5).
/// `group` (opcional) recebe o tamanho do grupo de um compute.
[[nodiscard]] bool spirv_to_msl(const uint32_t* words, size_t wordCount, const char* entry, uint32_t fragOutputMask,
                                uint32_t group[3], std::string& out, std::string& error) noexcept;

/// O fragment shader escreve `layout(location = N)` com N >= 1? (segundo alvo
/// do MRT: o Metal precisa da variante `fs_main_c0`.)
[[nodiscard]] bool spirv_writes_color1_or_more(const uint32_t* words, size_t wordCount) noexcept;

/// Estágio declarado no SPIR-V (Unknown = módulo ilegível).
[[nodiscard]] Stage spirv_stage(const uint32_t* words, size_t wordCount) noexcept;

} // namespace aurea::xlate
