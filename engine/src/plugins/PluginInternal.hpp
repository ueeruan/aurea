// =============================================================================
//  Aurea / plugins / PluginInternal.hpp
//
//  As peças internas do sistema de plugins: leitura do plugin.json, conversão
//  do ISF, geração do cabeçalho GLSL e os dois tipos de efeito.
// =============================================================================
#pragma once

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/plugins/Plugin.hpp"
#include "aurea/render/RuntimeShaders.hpp"
#include "PluginExpr.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aurea::json {
struct Value;
class Writer;
} // namespace aurea::json

namespace aurea::plugins {

// --- plugin.json ---------------------------------------------------------------
/// Texto por idioma (texto simples ou objeto idioma → texto). Falso = `error`.
[[nodiscard]] bool read_localized(const json::Value* v, LocalizedText& out, const char* field, usize maxChars, bool required,
                                  std::string& error);
/// 0.3.4 plugins: "script", "permissions", "strings" e "ui" (PluginUiManifest.cpp).
/// Formato 1 com esses campos = recusa (pedem "aureaPlugin": 2).
[[nodiscard]] bool parse_manifest_v2(const json::Value& root, Manifest& m, u32 format, std::string& error);
/// A parte da interface/permissões de um plugin na lista em JSON.
void write_v2_json(json::Writer& w, const PluginInfo& p);
/// Lê e confere o plugin.json. Falso = recusado; `error` diz o campo e o motivo.
[[nodiscard]] bool parse_manifest(std::string_view json, Manifest& out, std::string& error);
/// Serializa de volta (o ISF convertido grava o plugin.json que gerou).
[[nodiscard]] std::string manifest_json(const Manifest& m);
/// Nome que o GLSL aceita e que não colide com o cabeçalho do motor.
[[nodiscard]] bool valid_shader_identifier(std::string_view name) noexcept;

// --- ISF -------------------------------------------------------------------------
/// Um `.fs` do ISF 2.0 vira um plugin "shader" de passe único com cores sRGB.
/// `shaderFile`/`shaderText` é o arquivo GLSL gerado (o corpo do ISF com as
/// macros de compatibilidade). Falso = recusado (`error`).
[[nodiscard]] bool isf_to_plugin(std::string_view fileName, std::string_view source, Manifest& manifest,
                                 std::string& shaderFile, std::string& shaderText, std::string& error);

// --- GLSL ------------------------------------------------------------------------
/// O cabeçalho fixo que o motor põe antes do arquivo do autor, para o passe
/// `pass` (documentado símbolo a símbolo em docs/plugins/README.md).
[[nodiscard]] std::string shader_header(const Manifest& m, u32 pass);
/// O `main` que o motor põe depois do arquivo do autor.
[[nodiscard]] std::string shader_footer(const Manifest& m, u32 pass);
/// Compila o passe (cabeçalho + arquivo + main) para SPIR-V. O erro sai como
/// "arquivo:linha: mensagem" do glslang.
[[nodiscard]] bool compile_pass(const Manifest& m, u32 pass, std::string_view userSource, std::vector<u32>& spirv,
                                std::string& error);

// --- Efeitos ---------------------------------------------------------------------
/// Um passo de pilha já compilado: o embutido e, por parâmetro dele, a
/// expressão de cada componente (ou a cópia de um vetor do plugin).
struct CompiledStackValue {
    u32 param = 0;                 ///< índice do parâmetro no embutido
    ParamType type = ParamType::Float;
    u32 components = 1;
    Expr expr[4];
    i32 copySlot = -1;             ///< >= 0: copia o valor inteiro do parâmetro do plugin
};
struct CompiledStackStep {
    EffectTypeId type = 0;
    const Effect* effect = nullptr;            ///< embutido (vivo enquanto o registro viver)
    const ParameterRegistry* params = nullptr;
    std::vector<CompiledStackValue> values;
};

/// Confere e compila a pilha contra os embutidos de `registry`.
[[nodiscard]] bool compile_stack(const Manifest& m, const EffectRegistry& registry,
                                 std::vector<CompiledStackStep>& out, std::string& error);

/// O efeito de um plugin "shader": um `ShaderId` dinâmico por passe.
[[nodiscard]] std::unique_ptr<Effect> make_shader_effect(const Manifest& m, std::vector<ShaderId> passShaders);
/// O efeito de um plugin "stack".
[[nodiscard]] std::unique_ptr<Effect> make_stack_effect(const Manifest& m, std::vector<CompiledStackStep> steps);

/// A chave do efeito ("plugin.<id>").
[[nodiscard]] inline std::string effect_key(std::string_view id) { return std::string(kEffectKeyPrefix) + std::string(id); }

/// sRGB (0..1) → linear, e o inverso (cores do plugin.json e do ISF).
[[nodiscard]] f32 srgb_to_linear(f32 c) noexcept;
[[nodiscard]] f32 linear_to_srgb(f32 c) noexcept;

} // namespace aurea::plugins
