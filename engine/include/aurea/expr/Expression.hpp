// =============================================================================
//  Aurea / expr / Expression.hpp
//
//  Expressões no estilo do After Effects, escritas do zero — sem motor de
//  JavaScript, sem download, sem nada que execute código de fora.
//
//  A linguagem é um subconjunto pequeno e fechado de JS: números, vetores
//  ([x, y] / [x, y, z] / 4 componentes) com aritmética por componente,
//  + - * / % ^ (^ é potência, não XOR), comparações, && || !, ternário,
//  var/let/const, atribuição (= += -= *= /=), ++/--, if/else, for, while,
//  return, blocos e listas de instruções — o valor da última instrução é o
//  resultado. Funções: wiggle, loopOut/loopIn (+Duration), linear/ease/
//  easeIn/easeOut, clamp, random/gaussRandom/seedRandom (determinísticos),
//  noise, Math.* e as mesmas como globais, length/normalize/dot/cross/add/
//  sub/mul/div, degreesToRadians/radiansToDegrees, valueAtTime/velocityAtTime,
//  key(i).time/.value, numKeys, framesToTime/timeToFrames, shake (tremor com
//  decaimento), temporalWiggle, speedAtTime, smooth, nearestKey, posterizeTime,
//  timeSinceMarker e `marker` (as marcas da régua: .key(i | "nome"),
//  .nearestKey(t), .numKeys → .time/.index/.name). Objetos: thisLayer,
//  thisComp, thisProperty, layer("Nome" | índice), .transform.position/...,
//  effect("Slider Control")("Slider") (os controles são efeitos de verdade,
//  ver effects/builtin/ExpressionControls.cpp).
//
//  Três decisões que valem registrar:
//
//  1. O PONTO ÚNICO. A expressão mora na Track (fonte + programa compilado +
//     ligada/desligada) e toda amostragem passa por `Track::sample` /
//     `Track::value_or`, que chamam `evaluate_track`. Não existe caminho de
//     leitura de propriedade que "esqueça" a expressão: transform, opacidade,
//     parâmetros de efeito, animadores de texto, volume, time remap — tudo
//     entra pelo mesmo gancho.
//
//  2. SANDBOX. Limite duro de instruções por avaliação (kMaxInstructions),
//     de profundidade da árvore/recursão, de nós, de tamanho de fonte e de
//     texto. Nenhuma E/S existe na linguagem. Erro de sintaxe ou de execução
//     NUNCA derruba nada: a propriedade volta ao valor dos keyframes e o erro
//     (mensagem, linha, coluna) fica guardado para a UI mostrar.
//
//  3. DEPENDÊNCIAS. Uma expressão que lê outra propriedade avalia a
//     expressão DELA (no mesmo instante). A pilha de avaliação por thread
//     detecta o ciclo (A → B → A) e reporta "dependência circular" em vez de
//     recursar para sempre; dentro de um `Scope` (um quadro do renderer) cada
//     (propriedade, quadro) é avaliado uma vez só (memo).
//
//  Unidades: a expressão vê as unidades da INTERFACE — opacidade e escala em
//  % (0..100), rotação em graus, posição em px, time remap em segundos,
//  volume em %. O motor converte na volta.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <vector>

namespace aurea {
struct Track;
struct Layer;
struct TextData;
struct Mask;
struct MaskPoint;
struct EffectInstance;
class Timeline;
class EffectRegistry;
}

namespace aurea::expr {

// --- Limites do sandbox --------------------------------------------------------
inline constexpr u32 kMaxInstructions = 100000;   ///< passos por avaliação
inline constexpr u32 kMaxDepth        = 64;       ///< aninhamento da árvore / recursão do avaliador
inline constexpr u32 kMaxNodes        = 16384;    ///< nós por programa
inline constexpr u32 kMaxSourceBytes  = 32 * 1024;
inline constexpr u32 kMaxStringBytes  = 1024;     ///< literal de texto
inline constexpr u32 kMaxLocals       = 128;
inline constexpr u32 kMaxRefDepth     = 16;       ///< propriedade → propriedade → … (encadeamento)
// AE expressions (lote E): funções da pessoa, vetores longos, objetos.
inline constexpr u32 kMaxEvalDepth    = 224;      ///< recursão nativa do avaliador (árvore + chamadas)
inline constexpr u32 kMaxCallDepth    = 32;       ///< chamadas de função da pessoa aninhadas
inline constexpr u32 kMaxParams       = 16;       ///< parâmetros/argumentos por função
inline constexpr u32 kMaxFunctions    = 256;      ///< funções por programa
inline constexpr u32 kMaxArrayLength  = 65536;    ///< elementos de um vetor
inline constexpr u32 kMaxHeapBytes    = 8u << 20; ///< texto + vetores + objetos criados numa avaliação

/// Erro com posição (linha e coluna começam em 1; 0 = sem posição).
struct Diagnostic {
    bool        ok = true;
    std::string message;
    u32         line = 0;
    u32         column = 0;
    u32         offset = 0;
};

class Program;   ///< árvore compilada (opaca, imutável depois de compilar)

/// Expressão ligada a uma track. IMUTÁVEL depois de criada (fonte + programa +
/// erro de sintaxe): cópias da track (histórico, duplicar camada) dividem o
/// mesmo objeto sem risco. Só o diagnóstico de EXECUÇÃO muda, e ele é
/// protegido por mutex (a thread de render avalia; a UI lê).
struct TrackExpression {
    std::string                    source;
    std::shared_ptr<const Program> program;       ///< nulo = não compilou
    Diagnostic                     parseError;

    /// Erro de sintaxe, ou o último erro de execução (vazio = ok).
    [[nodiscard]] Diagnostic error() const;

    // Diagnóstico de execução (escrito pelo avaliador, só quando muda).
    void report_runtime(const Diagnostic& d) const;
    void clear_runtime() const;

    mutable std::mutex        mutex_;
    mutable Diagnostic        runtime_;
    mutable std::atomic<bool> hasRuntime_{false};
};

/// Compila (com cache global de programa por texto: mil camadas com
/// "wiggle(2,30)" dividem uma árvore só). Nunca devolve nulo; erro de sintaxe
/// fica em `parseError` e o programa nulo.
[[nodiscard]] std::shared_ptr<const TrackExpression> compile(std::string_view source);

/// Só compila e devolve o diagnóstico (a UI valida enquanto a pessoa digita).
[[nodiscard]] Diagnostic check_syntax(std::string_view source);

/// O gancho: valor da track com a expressão aplicada no instante local `t`.
/// `fallback` = valor parado que quem chama usaria sem keyframes (ex.: a
/// posição do Transform da camada); nulo = a própria track decide.
[[nodiscard]] f32 evaluate_track(const Track& track, FrameIndex t, const f32* fallback) noexcept;
[[nodiscard]] f32 evaluate_track_f(const Track& track, f64 t, const f32* fallback) noexcept;

/// Contexto de avaliação de um trecho (um quadro do renderer, a montagem do
/// mix de áudio). Dá à expressão a timeline (para achar a camada dona, as
/// outras camadas e a taxa) e liga o memo por (propriedade, quadro). É
/// thread-local e aninhável; o mesmo Timeline aninhado não custa nada.
/// PRECONDIÇÃO: o modelo não muda enquanto o escopo existe (quem cria segura o
/// lock do modelo — o renderer prepara o quadro sob ele).
class Scope {
public:
    explicit Scope(const Timeline& timeline) noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    bool pushed_ = false;
};

/// Per-glyph selector context. Index is one-based, as in AE text expressions.
/// The context also participates in memo keys, so letters never share a result.
class TextScope {
public:
    /// `selector`: o valor dos seletores de cima, em % por eixo (`selectorValue`).
    TextScope(u32 index, u32 total, const Vec3* selector = nullptr) noexcept;
    ~TextScope();
    TextScope(const TextScope&) = delete;
    TextScope& operator=(const TextScope&) = delete;
private:
    u64 previous_;
    Vec3 previousSelector_;
};

/// Fora de um Scope (consultas da UI), a avaliação pergunta a quem se
/// registrou qual timeline está viva (o Engine se registra na inicialização).
using TimelineProvider = const Timeline* (*)(void* ctx);
void register_provider(TimelineProvider fn, void* ctx);
void unregister_provider(void* ctx);
/// Lote M (timeline/PathRig): as timelines que esta thread enxerga agora — a
/// do Scope de cima primeiro, depois as dos provedores (sem repetir). Vale a
/// mesma precondição do Scope: o modelo não muda enquanto quem chama lê.
u32 live_timelines(const Timeline** out, u32 capacity) noexcept;

/// Avaliação isolada (testes, prévia da UI): sem camada; `value` com `n`
/// componentes, `time` em segundos. Devolve até 4 componentes.
struct StandaloneResult {
    bool       ok = false;
    u32        count = 0;
    f64        v[4]{};
    Diagnostic diag;
};
[[nodiscard]] StandaloneResult evaluate_standalone(std::string_view source, f64 time,
                                                   const f64* value, u32 n, f64 fps = 30.0);

/// v70, expressão no texto (o Texto-fonte do AE com expressão). `value` = o
/// texto antes dela (com o key do texto-fonte no instante), que a expressão
/// lê como `value` e `text.sourceText`; `localFrame` = quadro local da camada.
/// O resultado vira texto como no JavaScript (número 2 → "2"). Com erro, `ok`
/// falso, o diagnóstico vai para `te` (a interface mostra) e vale o texto.
struct TextResult {
    bool        ok = false;
    std::string text;
    Diagnostic  diag;
};
[[nodiscard]] TextResult evaluate_text(const TrackExpression& te, const Layer& layer, std::string_view value,
                                       f64 localFrame) noexcept;
/// A expressão do texto-fonte da camada (ligada e válida) no conteúdo de `t`
/// (que já traz o key do instante). Devolve se o conteúdo mudou.
bool apply_source_expression(const Layer& layer, f64 localFrame, TextData& t) noexcept;

// --- AE expressions (lote E) ---------------------------------------------------

/// Expressão no CAMINHO de uma máscara (`Mask::pathExpression`): `points` entra
/// com o caminho de antes (parado ou dos keys no instante; é o `thisProperty`
/// e o `value` da expressão) e sai com o resultado de `createPath(...)` (ou de
/// outro caminho lido: `mask("x").maskPath`, `content("G").path`). Devolve se
/// mudou. Com erro nada muda e o diagnóstico fica na expressão. `closedOut`
/// (opcional) recebe o "fechado" do resultado; a máscara continua com o dela.
bool apply_mask_path_expression(const Mask& mask, f64 localFrame, std::vector<MaskPoint>& points,
                                bool* closedOut = nullptr) noexcept;

/// Menu suspenso (`aurea.control.dropdown`, o Dropdown Menu Control do AE): os
/// itens da instância (`EffectInstance::menuItems`) ou, sem nenhum, o padrão
/// do After Effects ("Item 1", "Item 2", "Item 3").
inline constexpr u32 kMaxMenuItems = 64;
[[nodiscard]] bool is_menu_control(u32 effectType) noexcept;
[[nodiscard]] std::vector<std::string> menu_items(const EffectInstance& effect);

/// `footage("nome")` e `layer.source`: quem conhece os arquivos do projeto (o
/// Engine) responde. `name` vazio = procurar por `asset` (AssetId empacotado).
struct FootageInfo {
    std::string name;
    u32 width = 0, height = 0;
    f64 duration = 0.0;         ///< segundos (imagem: 0)
    f64 frameDuration = 0.0;    ///< segundos por quadro (0 = desconhecido)
    u64 asset = 0;              ///< AssetId empacotado
};
using FootageProvider = bool (*)(void* ctx, const Timeline* timeline, std::string_view name, u64 asset, FootageInfo& out);
void register_footage_provider(FootageProvider fn, void* ctx);
void unregister_footage_provider(void* ctx);

/// `sampleImage(ponto, raio, depoisDosEfeitos, t)`. A avaliação NÃO lê a GPU:
/// ela consulta uma imagem pequena da camada na CPU, publicada por quem tem os
/// pixels. Duas origens, nesta ordem:
///   1. `publish_layer_image`: o último quadro que alguém publicou para a
///      camada (o resultado fica UM QUADRO ATRASADO em relação ao render);
///   2. `ImageSource` registrado (síncrono, só CPU, chamado a cada leitura:
///      quem registra guarda a imagem pequena pronta): os pixels da FONTE da
///      camada (o motor responde pelas camadas de imagem), sem efeitos nem
///      máscaras.
/// Sem nenhuma das duas, vale a cor chapada conhecida do modelo (forma/sólido)
/// ou [0, 0, 0, 0]. Cada pedido sem imagem publicada entra numa fila que o
/// dono dos pixels esvazia com `take_image_requests`.
struct LayerPixels {
    u32 width = 0, height = 0;       ///< da imagem pequena
    f32 layerWidth = 0.0f, layerHeight = 0.0f;   ///< px da camada que ela cobre
    f64 frame = 0.0;                 ///< quadro da composição em que foi capturada
    std::vector<u8> rgba;            ///< RGBA8, cor no espaço da interface (sRGB), alfa reto
};
/// `layerKey` = LayerId empacotado; `compKey` = índice (1..) da composição na
/// ordem da timeline (o mesmo de `ImageRequest::comp`).
void publish_layer_image(const Timeline* timeline, u32 compKey, u64 layerKey, bool postEffect, LayerPixels pixels);
void clear_layer_images(const Timeline* timeline) noexcept;
struct ImageRequest {
    const Timeline* timeline = nullptr;
    u32  comp = 0;
    u64  layer = 0;
    bool postEffect = true;
    f64  frame = 0.0;
};
/// Tira da fila os pedidos pendentes (sem repetir camada). Devolve quantos.
u32 take_image_requests(std::vector<ImageRequest>& out);
using ImageSource = std::shared_ptr<const LayerPixels> (*)(void* ctx, const Timeline* timeline, const Layer& layer, f64 localFrame);
void register_image_source(ImageSource fn, void* ctx);
void unregister_image_source(void* ctx);

/// Valor parado da trilha `t` da camada `l` (o campo da camada que vale sem
/// keyframe: a posição do Transform, a constante do parâmetro de efeito...),
/// na unidade guardada.
[[nodiscard]] f32 static_value(const Layer& l, const Track& t) noexcept;

/// Registro de efeitos embutidos usado para achar efeitos por nome e o número
/// de componentes de cada parâmetro (criado uma vez, só leitura).
[[nodiscard]] const EffectRegistry& builtin_effects();

} // namespace aurea::expr
