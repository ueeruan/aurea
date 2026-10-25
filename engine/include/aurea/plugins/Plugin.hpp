// =============================================================================
//  Aurea / plugins / Plugin.hpp
//
//  Plugins de efeito de terceiros: o formato `.aureaplugin`, os limites e o
//  que a interface recebe de cada plugin instalado.
//
//  Um plugin é um ZIP com `plugin.json` e arquivos. Ele acrescenta UM efeito
//  ao navegador (categoria "Plugins"), com a chave estável `plugin.<id>` — a
//  mesma em qualquer aparelho, por isso o projeto salva o efeito como salva um
//  embutido. Dois tipos:
//
//    shader  GLSL 450 escrito pelo autor (`vec4 aurea_main(vec2 uv)`), 1 a 8
//            passes, compilado NO APARELHO (glslang → SPIR-V → API da GPU).
//    stack   uma pilha de efeitos embutidos com valores calculados a partir
//            dos parâmetros do plugin (expressões aritméticas). Sem shader.
//
//  Também se instala um `.fs` do ISF 2.0 (passe único): ele vira um plugin
//  "shader" com cores sRGB (`colorSpace: srgb`) e id `isf.<nome>`.
//
//  A referência completa (todo campo, todo símbolo do cabeçalho, as mensagens
//  de erro) está em docs/plugins/README.md.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/effects/Parameter.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurea::plugins {

// --- Limites (e o porquê de cada um está na documentação) ---------------------
inline constexpr u32 kFormatVersion = 1;            ///< "aureaPlugin" que este app lê
inline constexpr u32 kMaxParams = 16;
inline constexpr u32 kMaxPasses = 8;
inline constexpr u32 kMaxPassInputs = 3;            ///< entradas nomeadas além da fonte
inline constexpr u32 kMaxStackSteps = 16;
inline constexpr u32 kMaxEnumOptions = 32;
inline constexpr usize kMaxShaderBytes = 64 * 1024; ///< por arquivo de shader
inline constexpr usize kMaxManifestBytes = 256 * 1024;
inline constexpr u64 kMaxPackageBytes = 16ull << 20;
inline constexpr f32 kMaxMargin = 1024.0f;           ///< px da camada
inline constexpr u32 kTestRenderSize = 256;          ///< prévia de teste na instalação
inline constexpr f32 kTestRenderBudgetMs = 250.0f;   ///< teto do quadro de teste
/// 0.3.1: parâmetros "layer" (câmera/nulo/luz ligados ao plugin 3D).
inline constexpr u32 kMaxLayerParams = 4;

/// Prefixo da chave do efeito: "plugin." + id.
inline constexpr std::string_view kEffectKeyPrefix = "plugin.";
/// Categoria do efeito no catálogo (a categoria do plugin aparece dentro dela).
inline constexpr const char* kCatalogCategory = "Plugins";

/// Texto por idioma, na ordem declarada. Um texto simples no plugin.json vira
/// uma entrada só com idioma "*" (vale para todos).
using LocalizedText = std::vector<std::pair<std::string, std::string>>;

/// O texto no idioma pedido: exato, senão "en", senão "pt-BR", senão "*",
/// senão o primeiro declarado.
[[nodiscard]] std::string localized(const LocalizedText& text, std::string_view locale);

enum class Kind : u8 { Shader = 0, Stack };
enum class ColorSpace : u8 { Linear = 0, Srgb };
enum class ParamKind : u8 { Float = 0, Int, Bool, Enum, Color, Point, Angle, Layer };

/// Que camadas um parâmetro "layer" aceita (bits; 0 = qualquer uma). Os mesmos
/// bits viram as flags kParamRef* da declaração do efeito (Parameter.hpp).
inline constexpr u16 kAcceptCamera = 1u << 0;
inline constexpr u16 kAcceptNull = 1u << 1;
inline constexpr u16 kAcceptLight = 1u << 2;

[[nodiscard]] const char* kind_name(Kind k) noexcept;
[[nodiscard]] const char* param_kind_name(ParamKind k) noexcept;

/// Um parâmetro declarado em plugin.json.
struct ParamDecl {
    std::string id;
    ParamKind kind = ParamKind::Float;
    LocalizedText label;
    /// Padrão já na unidade do motor: cor em RGBA LINEAR (o plugin.json declara
    /// sRGB, como um seletor de cor), ponto relativo ao tamanho da camada,
    /// ângulo em graus, enum = índice, bool = 0/1.
    f32 def[4]{0, 0, 0, 1};
    f32 min = 0.0f;
    f32 max = 1.0f;
    std::vector<LocalizedText> options;   ///< enum
    bool animatable = true;
    /// "layer": tipos aceitos (kAccept*; 0 = qualquer camada).
    u16 accept = 0;
};

/// Um passe de um plugin "shader".
struct PassDecl {
    std::string shader;    ///< arquivo no pacote (relativo à raiz)
    std::string target;    ///< textura intermediária; vazio = saída (último passe)
    f32 scale = 1.0f;      ///< resolução relativa à imagem da camada (0,125..1)
    /// Entradas nomeadas (sampler no shader → "source" ou o target de um passe anterior).
    std::vector<std::pair<std::string, std::string>> inputs;
};

/// Um valor de um parâmetro de efeito embutido num plugin "stack": número
/// fixo ou expressão sobre os parâmetros do plugin, por componente.
struct StackValue {
    std::string param;                    ///< id do parâmetro do embutido
    std::vector<std::string> components;  ///< texto da expressão de cada componente
};

struct StackStepDecl {
    std::string effect;                   ///< chave do embutido ("aurea.blur.gaussian")
    std::vector<StackValue> set;
};

/// O plugin.json lido e conferido.
struct Manifest {
    u32 format = kFormatVersion;
    std::string id;
    std::string version;
    std::string author;
    LocalizedText name;
    LocalizedText description;
    std::string category;
    Kind kind = Kind::Shader;
    /// 0.3.1, "kind": "shader3d": o shader recebe a câmera da cena e o mundo das
    /// camadas ligadas pelos parâmetros "layer" (docs/plugins/README.md §5).
    bool scene3d = false;
    std::vector<ParamDecl> params;
    std::vector<PassDecl> passes;
    f32 margin = 0.0f;
    ColorSpace colorSpace = ColorSpace::Linear;
    std::vector<StackStepDecl> stack;
};

/// O que a interface recebe de um plugin instalado (Engine::list_plugins).
struct PluginInfo {
    std::string id;
    std::string version;
    std::string author;
    Kind kind = Kind::Shader;
    bool scene3d = false;           ///< "shader3d" (câmera e camadas ligadas)
    std::string category;
    LocalizedText names;
    LocalizedText descriptions;
    std::string effectKey;          ///< "plugin.<id>"
    u32 effectTypeId = 0;           ///< FNV-1a 32 da chave (o id do efeito no projeto)
    std::string directory;          ///< pasta instalada
    bool ok = false;                ///< registrado e disponível no catálogo
    std::string errorCode;          ///< estável ("shader_compile", "manifest"...); vazio = ok
    std::string error;              ///< mensagem para a pessoa (pt-BR)
    std::vector<ParamDecl> params;  ///< rótulos por idioma para o painel
    u32 passCount = 0;
    f32 testRenderMs = -1.0f;       ///< medido na instalação (-1 = não medido nesta sessão)
};

/// `id` válido: [a-z0-9._-]{3,64}, sem começar/terminar em ponto, sem "..".
[[nodiscard]] bool valid_plugin_id(std::string_view id) noexcept;
/// "x.y.z" (cada parte 0..99999). Falso = malformado.
[[nodiscard]] bool parse_version(std::string_view text, u32 (&out)[3]) noexcept;
/// <0, 0, >0 como strcmp. Versões malformadas comparam como 0.0.0.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b) noexcept;

/// Os plugins em JSON (a lista que as pontes devolvem à interface).
[[nodiscard]] std::string to_json(const std::vector<PluginInfo>& plugins);

} // namespace aurea::plugins
