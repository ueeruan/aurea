// =============================================================================
//  Aurea / animation / Curve.hpp
//
//  Motor de keyframes.
//
//  Decisões que valem registrar:
//
//  1. NÃO existe um objeto por keyframe. Keyframes de uma propriedade moram num
//     vetor plano, ordenado por tempo. Uma layer com 500 keyframes em 12
//     propriedades custa 12 vetores, não 6000 objetos. Avaliar um frame é uma
//     busca binária num vetor contíguo — cabe no cache.
//
//  2. Sem alocação na avaliação. `sample()` recebe o tempo e devolve um float,
//     sempre com o mesmo custo previsível.
//
//  3. O que é animável é decidido por dado, não por hierarquia de classes: uma
//     Track tem um `TrackProperty` e o avaliador é um único switch. Adicionar
//     uma propriedade nova não cria tipo novo nem quebra o serializador.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace aurea {

namespace expr { struct TrackExpression; }

/// Um keyframe. 48 bytes, POD, memcpy-ável para o arquivo .aurea.
struct Keyframe {
    FrameIndex    time{0};
    f32           value = 0.0f;

    /// Curva de saída deste keyframe até o próximo. Guardado no keyframe da
    /// ESQUERDA porque é a borda que pertence ao intervalo.
    Interpolation interp = Interpolation::Linear;

    /// Control points do bezier de saída. Ignorados quando interp não é
    /// Bezier/CustomCurve.
    f32 bx1 = 0.33f, by1 = 0.0f, bx2 = 0.67f, by2 = 1.0f;

    /// Handle de velocidade no editor de curvas (em unidades de valor por
    /// segundo). Zero = a curva é definida só pelos control points normalizados.
    /// Existe para o graph editor poder mostrar tangentes livres.
    f32 tangentIn  = 0.0f;
    f32 tangentOut = 0.0f;

    /// Rótulo de easing escolhido na UI (ex.: "Ease Out Quart"), só para a UI
    /// mostrar o que o usuário escolheu. Não participa da avaliação.
    u16 easingPreset = 0;

    [[nodiscard]] bool operator<(FrameIndex other) const noexcept { return time < other; }
    [[nodiscard]] bool operator<(const Keyframe& o) const noexcept { return time < o.time; }
};

static_assert(std::is_trivially_copyable_v<Keyframe>, "Keyframe vai cru para o .aurea");

/// Uma propriedade animável de uma layer — ou uma propriedade estática, se
/// tiver zero ou um keyframe.
///
/// A mesma estrutura serve para os dois casos de propósito: "opacidade = 0.8"
/// é uma track com um keyframe em t=0, e "opacidade animada" é a mesma track
/// com N keyframes. A UI não precisa de caminho de código diferente para
/// animar algo que ainda não era animado.
struct Track {
    TrackProperty      property = TrackProperty::Opacity;
    u32                effectIndex      = kInvalidIndex;
    u32                effectParamIndex = 0;

    /// Valor usado quando a track está vazia. É o valor "de fábrica".
    f32                staticValue = 0.0f;

    std::vector<Keyframe> keys;

    /// Cache do último índice avaliado. Reprodução anda para frente, então a
    /// busca binária quase sempre cai no mesmo intervalo — guardar o índice
    /// transforma a busca em um teste.
    mutable u32 lastIndex = 0;

    /// Expressão (expr/Expression.hpp): nula = sem expressão. O objeto é
    /// imutável e compartilhado entre cópias da track (histórico, duplicar);
    /// trocar a expressão troca o ponteiro. `expressionEnabled` mora aqui, e
    /// não no objeto, para desligar/ligar ser desfazível como qualquer ajuste.
    std::shared_ptr<const expr::TrackExpression> expression;
    bool expressionEnabled = true;

    [[nodiscard]] bool has_expression() const noexcept { return expression && expressionEnabled; }

    /// A propriedade muda no tempo: keyframes ou uma expressão ligada (uma
    /// expressão pode depender do tempo, de outra camada ou de um controle).
    [[nodiscard]] bool animated() const noexcept { return keys.size() > 1 || has_expression(); }
    /// Há algo além do valor parado da camada: keyframe ou expressão. É a
    /// pergunta que os leitores fazem antes de trocar o valor do Transform
    /// pelo da track.
    [[nodiscard]] bool driven() const noexcept { return !keys.empty() || has_expression(); }
    [[nodiscard]] bool empty() const noexcept { return keys.empty(); }

    /// Define o valor usado quando a track não tem keyframe (ou tem só um).
    /// Sem keyframe, a track inteira vira um valor parado — e uma layer com 30
    /// propriedades ajustadas e nenhuma animada não carrega 30 vetores.
    void set_static(f32 value) noexcept { staticValue = value; }

    /// Índice do keyframe exatamente neste tempo, ou kInvalidIndex.
    [[nodiscard]] u32 find_exact(FrameIndex t) const noexcept;

    /// Índice do keyframe em ou antes de `t`. kInvalidIndex se `t` é antes do
    /// primeiro.
    [[nodiscard]] u32 find_before(FrameIndex t) const noexcept;

    /// Valor da propriedade em `t`, COM a expressão (se houver e estiver
    /// ligada). Sem keyframe, o valor parado é `staticValue`.
    [[nodiscard]] f32 sample(FrameIndex t) const noexcept;

    /// Como `sample`, mas sem keyframe o valor parado é `fallback` (o campo da
    /// camada que quem chama usaria — a posição do Transform, por exemplo). É
    /// a leitura dos renderers: `tr ? tr->value_or(t, base) : base`.
    [[nodiscard]] f32 value_or(FrameIndex t, f32 fallback) const noexcept;

    /// Só os keyframes (valor "pré-expressão"): editar keyframe, o graph
    /// editor e o próprio avaliador (`value`, loopOut) leem daqui.
    [[nodiscard]] f32 sample_keys(FrameIndex t) const noexcept;

    /// Insere ou substitui um keyframe em `t`. Devolve o índice resultante.
    u32 set(FrameIndex t, f32 value, Interpolation interp = Interpolation::Linear) noexcept;

    /// Remove o keyframe em `t`. Devolve true se removeu.
    bool remove(FrameIndex t) noexcept;

    /// Move o keyframe de `from` para `to`, mantendo a ordem. Devolve o novo
    /// índice, ou kInvalidIndex.
    u32 move(FrameIndex from, FrameIndex to) noexcept;

    void set_interpolation(FrameIndex t, Interpolation in, f32 bx1, f32 by1,
                           f32 bx2, f32 by2) noexcept;

    /// Primeiro e último tempo com keyframe. Usados para desenhar a barra de
    /// keyframes na timeline sem varrer tudo a cada quadro.
    [[nodiscard]] FrameIndex first_time() const noexcept {
        return keys.empty() ? FrameIndex{0} : keys.front().time;
    }
    [[nodiscard]] FrameIndex last_time() const noexcept {
        return keys.empty() ? FrameIndex{0} : keys.back().time;
    }

    void clear() noexcept { keys.clear(); lastIndex = 0; }
};

/// Conjunto de tracks de uma layer. Acesso por propriedade é linear no número
/// de tracks ativas — na prática 5 a 15, e o vetor é pequeno o bastante para
/// caber no cache.
class TrackSet {
public:
    static constexpr u32 kInlineCapacity = 16;

    TrackSet() { tracks_.reserve(kInlineCapacity); }

    /// Devolve a track da propriedade, criando se não existir.
    [[nodiscard]] Track& get_or_create(TrackProperty p, u32 effectIndex = kInvalidIndex,
                                       u32 effectParamIndex = 0) noexcept;

    /// Devolve nullptr se a propriedade não é animada nem tem valor próprio —
    /// nesse caso quem chama usa o valor padrão da layer.
    [[nodiscard]] Track* find(TrackProperty p, u32 effectIndex = kInvalidIndex,
                              u32 effectParamIndex = 0) noexcept;
    [[nodiscard]] const Track* find(TrackProperty p, u32 effectIndex = kInvalidIndex,
                                    u32 effectParamIndex = 0) const noexcept;

    /// Valor avaliado, ou `fallback` se a track não existe.
    [[nodiscard]] f32 sample_or(TrackProperty p, FrameIndex t, f32 fallback,
                                u32 effectIndex = kInvalidIndex,
                                u32 effectParamIndex = 0) const noexcept;

    /// Define um valor estático (cria a track se preciso, sem animar).
    void set_static(TrackProperty p, f32 value, u32 effectIndex = kInvalidIndex,
                    u32 effectParamIndex = 0) noexcept;

    /// Verdadeiro se QUALQUER track tem mais de um keyframe (ou expressão). É o que decide se
    /// a layer precisa de avaliação animada no frame ou pode usar o cache.
    [[nodiscard]] bool has_animation() const noexcept;

    [[nodiscard]] bool empty() const noexcept { return tracks_.empty(); }
    [[nodiscard]] u32 size() const noexcept { return static_cast<u32>(tracks_.size()); }
    [[nodiscard]] Track& at(u32 i) noexcept { return tracks_[i]; }
    [[nodiscard]] const Track& at(u32 i) const noexcept { return tracks_[i]; }

    void clear() noexcept { tracks_.clear(); }

    /// Remove as tracks em que `pred(track)` é verdadeiro.
    template <class Pred>
    void remove_if(Pred pred) {
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), pred), tracks_.end());
    }
    /// Acrescenta uma track pronta (quem chama garante que a chave é nova).
    void add(Track t) { tracks_.push_back(std::move(t)); }

private:
    std::vector<Track> tracks_;
};

} // namespace aurea
