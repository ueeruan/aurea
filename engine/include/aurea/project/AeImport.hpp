// =============================================================================
//  Aurea / project / AeImport.hpp
//
//  Importação de arquivos do After Effects: presets de animação (.ffx) e
//  scripts (.jsx) — os dois viram edições numa camada do Aurea, num passo de
//  desfazer, com um relatório do que entrou e do que ficou de fora.
//
//  .ffx (RIFX big-endian, forma "FaFX"; formato descrito em
//  build/reference/ae2026/NOTAS.md, lido com código próprio): lista de
//  entradas (caminho de matchNames até o grupo + a árvore de valores). Efeitos
//  passam pela TABELA (AeImportTable.cpp): matchName do AE → chave do Aurea,
//  parâmetro a parâmetro, com conversão de unidade. Text Animator, seletores,
//  Text on Path, máscaras (como caminho), transform e expressões entram direto.
//
//  .jsx: um interpretador próprio de um SUBCONJUNTO de ExtendScript (ES3) com o
//  DOM do AE mais usado em scripts de animação (AeScript.cpp/AeScriptDom.cpp).
//  SANDBOX: nada de arquivo, rede ou sistema (File, Folder, system.callSystem,
//  $.evalFile, app.executeCommand… são relatados e NUNCA executados), teto de
//  instruções, de memória e de recursão; determinístico (Math.random com
//  semente fixa).
//
//  REGRAS (nada derruba a importação por causa de um detalhe):
//   · efeito/propriedade sem equivalente → `skipped` (com o motivo e, no .jsx,
//     a linha); aproximação (o Aurea faz parecido, não igual) → `notes`;
//   · tudo é aplicado num passo de desfazer só (o Engine abre o grupo);
//   · valores fora da faixa do parâmetro do Aurea são presos nela.
// =============================================================================
#pragma once

#include "aurea/core/Handle.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea {

class Composition;
class EffectRegistry;
struct Layer;

namespace ae {

// -----------------------------------------------------------------------------
// Relatório
// -----------------------------------------------------------------------------

/// Um item que não entrou (ou entrou aproximado). `kind`: "efeito",
/// "parametro", "propriedade", "camada", "script", "texto", "mascara",
/// "expressao", "seletor", "chamada", "arquivo".
struct SkipItem {
    std::string kind;
    std::string name;     ///< nome/matchName do AE (ou a chamada do script)
    std::string reason;   ///< por quê, em pt-BR
    u32         line = 0; ///< linha do .jsx (0 = não se aplica)
};

struct ImportReport {
    bool        ok = false;
    std::string message;          ///< resumo legível (pt-BR)
    std::string kind;             ///< "ffx" | "jsx" | "" (não reconhecido)
    u32 effects = 0;              ///< efeitos criados
    u32 keyframes = 0;            ///< keyframes escritos (por componente do Aurea)
    u32 layers = 0;               ///< camadas criadas
    u32 expressions = 0;          ///< expressões ligadas
    u32 animators = 0;            ///< animadores de texto criados
    u32 changes = 0;              ///< escritas no projeto (0 = nada mudou: sem passo de desfazer)
    std::vector<SkipItem> skipped;
    std::vector<SkipItem> notes;

    /// Acrescenta (sem repetir o mesmo item; teto de 400 por lista).
    void skip(std::string kind, std::string name, std::string reason, u32 line = 0);
    void note(std::string kind, std::string name, std::string reason, u32 line = 0);
};

/// JSON para as pontes (contrato com Android e iOS):
/// {"ok":bool,"kind":"ffx|jsx","effects":n,"keyframes":n,"layers":n,
///  "expressions":n,"animators":n,
///  "skipped":[{"kind","name","reason","line"}],
///  "notes":[{"kind","name","reason","line"}],"message":"…"}
[[nodiscard]] std::string report_json(const ImportReport& r);

/// Monta `r.message` a partir das contagens (chamado no fim da importação).
void finish_report(ImportReport& r);

// -----------------------------------------------------------------------------
// .ffx — árvore lida
// -----------------------------------------------------------------------------

/// Ease temporal do AE: velocidade (unidade da propriedade por segundo) e
/// influência em % (0,1..100), como no `KeyframeEase` do scripting.
struct Ease {
    f64 speed = 0.0;
    f64 influence = 100.0 / 6.0;
};

/// Interpolação do AE (o byte gravado: 1 linear, 2 bézier, 3 segurar).
enum class KeyInterp : u8 { Linear = 1, Bezier = 2, Hold = 3 };

struct Key {
    f64 time = 0.0;                    ///< segundos
    std::vector<f64> value;            ///< componentes, na unidade do arquivo
    KeyInterp in = KeyInterp::Linear;
    KeyInterp out = KeyInterp::Linear;
    u16 flags = 0;                     ///< bit 3 = contínuo (observado)
    std::vector<Ease> inEase, outEase; ///< 1 (espacial) ou um por componente
    std::vector<f64> inTangent, outTangent;   ///< espacial (vazio nos outros)
};

/// Caminho de máscara (pontos relativos ao CENTRO da camada no preset;
/// observado). Tangentes absolutas, como no arquivo.
struct ShapeVertex { f32 x = 0, y = 0, inX = 0, inY = 0, outX = 0, outY = 0; };
struct MaskShape {
    bool closed = true;
    std::vector<ShapeVertex> vertices;
};

struct EffectParamDef {
    std::string match;   ///< "ADBE Gaussian Blur 2-0001"
    std::string name;    ///< nome em inglês (pard)
    u8          type = 0;
    std::vector<std::string> popup;
};

struct Node {
    enum class Kind : u8 { Group, Property, Effect, TextDocument, Unknown };
    Kind        kind = Kind::Unknown;
    std::string match;
    std::string name;        ///< nome exibido (tdsn), sem o \t de localização
    u32         flags = 0;   ///< tdsb: bit 0 ligado, bit 1 escondido (observado)
    // Propriedade
    u32  dims = 0;
    bool spatial = false;
    bool hasValue = false;
    std::vector<f64> value;
    std::vector<Key> keys;
    std::string expression;
    bool hasShape = false;
    MaskShape shape;
    bool shapeKeyed = false;
    // Efeito
    std::string effectName;  ///< fnam
    std::vector<EffectParamDef> params;
    std::vector<Node> children;

    [[nodiscard]] bool hidden() const noexcept { return (flags & 2u) != 0; }
    [[nodiscard]] bool enabled() const noexcept { return (flags & 1u) != 0; }
    [[nodiscard]] const Node* child(std::string_view m) const noexcept;
};

struct PathStep {
    std::string match;
    i32 index = -1;
};

struct Entry {
    std::vector<PathStep> path;  ///< sem o sentinela
    std::string name;
    Node value;
};

struct FfxDocument {
    u32 timescale = 0;           ///< tiques por segundo (beso, offset 12)
    std::vector<Entry> entries;
};

/// Lê o .ffx. Falso (com `error`) só para arquivo que não é RIFX/FaFX ou
/// está truncado na estrutura; pedaço desconhecido é ignorado.
[[nodiscard]] bool parse_ffx(std::string_view bytes, FfxDocument& out, std::string& error);

// -----------------------------------------------------------------------------
// O que o importador pede ao motor
// -----------------------------------------------------------------------------

enum class NewLayer : u8 { Text, Solid, Shape, Null, Camera, Light };

struct NewLayerSpec {
    std::string name;
    std::string text;            ///< Text
    f32 color[4]{1, 1, 1, 1};    ///< Solid (sRGB)
    u32 width = 0, height = 0;   ///< Solid (0 = a composição)
    f64 durationSec = -1.0;      ///< < 0 = até o fim da composição
    u32 lightKind = 1;           ///< Light: 0 direcional, 1 pontual, 2 spot
    bool threeD = false;         ///< Null
};

/// Implementado pelo Engine (sob a trava do modelo, dentro do grupo de
/// desfazer) e pelos testes.
class Host {
public:
    virtual ~Host() = default;
    [[nodiscard]] virtual Composition* composition() noexcept = 0;
    [[nodiscard]] virtual const EffectRegistry& registry() const noexcept = 0;
    /// Quadro do cabeçote (timeline).
    [[nodiscard]] virtual i64 playhead() const noexcept = 0;
    /// Camada nova no topo, com tempo e posição padrão. Inválido = recusado.
    [[nodiscard]] virtual LayerId create_layer(NewLayer kind, const NewLayerSpec& spec) noexcept = 0;
    /// Efeito novo no fim da pilha (as mesmas regras do EffectAdd). Devolve o
    /// id estável do efeito na camada (kInvalidIndex = recusado).
    [[nodiscard]] virtual u32 add_effect(LayerId layer, u32 effectType) noexcept = 0;
    /// Conteúdo/estilo do texto mudou: o motor refaz a caixa.
    virtual void text_changed(LayerId layer) noexcept = 0;
    /// Camada removida pelo script (layer.remove()).
    virtual bool remove_layer(LayerId layer) noexcept = 0;
    /// Pai da camada (inválido = solta). `keepWorld` = compensa para nada sair
    /// do lugar (layer.parent = x); falso = sem compensar (setParentWithJump).
    virtual bool set_parent(LayerId child, LayerId parent, bool keepWorld) noexcept = 0;
};

// -----------------------------------------------------------------------------
// Aplicação
// -----------------------------------------------------------------------------

/// Aplica o preset na camada `target`. Os keyframes começam no cabeçote (ou
/// no início da camada, se o cabeçote estiver fora dela), como o AE faz com o
/// indicador de tempo atual. Falso = nada pôde ser aplicado (`report` diz).
bool apply_ffx(const FfxDocument& doc, Host& host, LayerId target, ImportReport& report);

/// Limites do sandbox do .jsx.
struct ScriptLimits {
    u64 maxSteps = 5'000'000;        ///< passos do avaliador
    u32 maxDepth = 200;              ///< recursão de chamadas
    u64 maxHeapBytes = 64ull << 20;  ///< objetos, arrays e textos vivos
    u32 maxSourceBytes = 2u << 20;
};

/// Roda o script. `target` = camada selecionada (vira `comp.selectedLayers`
/// e a camada de `layer(1)` quando não há outra referência); pode ser
/// inválido. Falso = erro de sintaxe ou o script parou num limite.
bool run_jsx(std::string_view source, Host& host, LayerId target, const ScriptLimits& limits,
             ImportReport& report);

/// Detecta o tipo pelo conteúdo (RIFX/FaFX) e pela extensão. "" = nenhum.
[[nodiscard]] const char* detect_kind(std::string_view data, std::string_view fileName) noexcept;

// -----------------------------------------------------------------------------
// Tabela (para testes e relatórios)
// -----------------------------------------------------------------------------

/// Chave do Aurea para o matchName do efeito do AE; nulo = sem equivalente.
[[nodiscard]] const char* aurea_effect_for(std::string_view aeMatchName) noexcept;
/// Quantos efeitos do AE a tabela conhece (com e sem equivalente).
[[nodiscard]] u32 table_size() noexcept;
/// Conferência da tabela contra o registro: chaves e ids que não existem
/// (vazio = tabela em dia).
[[nodiscard]] std::vector<std::string> mapping_problems(const EffectRegistry& registry);

} // namespace ae
} // namespace aurea
