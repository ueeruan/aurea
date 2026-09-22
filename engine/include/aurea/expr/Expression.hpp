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
//  key(i).time/.value, numKeys, framesToTime/timeToFrames. Objetos: thisLayer,
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

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace aurea {
struct Track;
struct Layer;
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

/// Fora de um Scope (consultas da UI), a avaliação pergunta a quem se
/// registrou qual timeline está viva (o Engine se registra na inicialização).
using TimelineProvider = const Timeline* (*)(void* ctx);
void register_provider(TimelineProvider fn, void* ctx);
void unregister_provider(void* ctx);

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

/// Valor parado da trilha `t` da camada `l` (o campo da camada que vale sem
/// keyframe: a posição do Transform, a constante do parâmetro de efeito...),
/// na unidade guardada.
[[nodiscard]] f32 static_value(const Layer& l, const Track& t) noexcept;

/// Registro de efeitos embutidos usado para achar efeitos por nome e o número
/// de componentes de cada parâmetro (criado uma vez, só leitura).
[[nodiscard]] const EffectRegistry& builtin_effects();

} // namespace aurea::expr
