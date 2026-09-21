#include "aurea/render/RenderScheduler.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Math.hpp"

#include <algorithm>

namespace aurea {

// -----------------------------------------------------------------------------
// FrameCache
// -----------------------------------------------------------------------------
const FrameCache::Entry* FrameCache::find(const Key& key) noexcept {
    for (const Entry& e : entries_) {
        if (e.key == key) {
            ++hits_;
            return &e;
        }
    }
    ++misses_;
    return nullptr;
}

void FrameCache::insert(const Key& key, TextureHandle texture, u32 bytes,
                        u64 frameNumber) noexcept {
    for (Entry& e : entries_) {
        if (e.key == key) {
            e.texture = texture;
            e.lastUsedFrame = frameNumber;
            e.byteSize = bytes;
            return;
        }
    }

    // Capacidade cheia: descarta o menos recentemente usado. É a política
    // certa aqui porque scrubbing e playback têm localidade temporal forte —
    // o frame que vai ser pedido de novo é o que acabou de ser usado.
    if (entries_.size() >= capacity_) {
        usize victim = 0;
        for (usize i = 1; i < entries_.size(); ++i) {
            if (entries_[i].lastUsedFrame < entries_[victim].lastUsedFrame) victim = i;
        }
        bytes_ -= entries_[victim].byteSize;
        entries_[victim] = Entry{key, texture, frameNumber, bytes};
        bytes_ += bytes;
        return;
    }

    entries_.push_back(Entry{key, texture, frameNumber, bytes});
    bytes_ += bytes;
}

void FrameCache::touch(const Key& key, u64 frameNumber) noexcept {
    for (Entry& e : entries_) {
        if (e.key == key) {
            e.lastUsedFrame = frameNumber;
            return;
        }
    }
}

void FrameCache::clear() noexcept {
    entries_.clear();
    bytes_ = 0;
    hits_ = 0;
    misses_ = 0;
}

u32 FrameCache::evict_to_fit(usize targetBytes) noexcept {
    u32 evicted = 0;
    while (bytes_ > targetBytes && !entries_.empty()) {
        usize victim = 0;
        for (usize i = 1; i < entries_.size(); ++i) {
            if (entries_[i].lastUsedFrame < entries_[victim].lastUsedFrame) victim = i;
        }
        bytes_ -= entries_[victim].byteSize;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(victim));
        ++evicted;
    }
    return evicted;
}

// -----------------------------------------------------------------------------
// FramePrefetcher
// -----------------------------------------------------------------------------
void FramePrefetcher::plan(u32 assetSlot, FrameIndex current,
                           FrameIndex first, FrameIndex last,
                           std::vector<PrefetchRequest>& out) const {
    out.clear();

    // Direção define a forma da janela.
    //
    // Parado ou pausado: janela simétrica curta. O usuário pode mexer o
    // playhead para qualquer lado, então não vale apostar.
    //
    // Playback/scrub para frente: a janela olha muito mais para a frente do
    // que para trás. Pré-decodificar para trás durante um arrasto para a
    // frente é trabalho jogado fora — o usuário nunca vai ver aqueles frames.
    u32 behind = behind_;
    u32 ahead = ahead_;

    if (direction_ > 0) {
        behind = 1;
        ahead = ahead_ + 2;
    } else if (direction_ < 0) {
        behind = behind_ + 2;
        ahead = 1;
    }

    auto push_if_valid = [&](i64 f, PrefetchRequest::Priority prio) {
        if (f < first.value || f > last.value) return;
        PrefetchRequest r;
        r.assetSlot = assetSlot;
        r.frame = FrameIndex{f};
        r.priority = prio;
        out.push_back(r);
    };

    // O frame atual é sempre urgente: ele é o que o usuário está vendo.
    push_if_valid(current.value, PrefetchRequest::Priority::Urgent);

    for (u32 d = 1; d <= ahead; ++d) {
        // Os próximos 2 frames são "high" porque a decodificação precisa
        // começar AGORA para chegar a tempo; os seguintes são prefetch de
        // verdade.
        const auto prio = d <= 2 ? PrefetchRequest::Priority::High
                                 : PrefetchRequest::Priority::Normal;
        push_if_valid(current.value + static_cast<i64>(d), prio);
    }
    for (u32 d = 1; d <= behind; ++d) {
        push_if_valid(current.value - static_cast<i64>(d), PrefetchRequest::Priority::Low);
    }
}

void FramePrefetcher::invalidate_behind(std::vector<PrefetchRequest>& requests,
                                        FrameIndex current) const {
    for (PrefetchRequest& r : requests) {
        // Pedido que o usuário já passou: cancelar libera o decoder para o que
        // interessa. Marcar em vez de apagar mantém os índices estáveis para
        // quem está iterando.
        if (direction_ > 0 && r.frame.value < current.value) r.stale = true;
        if (direction_ < 0 && r.frame.value > current.value) r.stale = true;
    }
}

// -----------------------------------------------------------------------------
// AdaptiveResolutionController
// -----------------------------------------------------------------------------
AdaptiveResolutionController::AdaptiveResolutionController(
    const DeviceCapabilities& caps) noexcept
    : caps_(caps) {
    rebuild_options();
    recompute_budget();
}

void AdaptiveResolutionController::configure(u32 compWidth, u32 compHeight,
                                             f32 targetFps) noexcept {
    compWidth_ = compWidth ? compWidth : 1920;
    compHeight_ = compHeight ? compHeight : 1080;

    // Teto do aparelho. Uma composição maior que o teto do preview nunca é
    // renderizada em resolução cheia no preview — não adianta, a tela não
    // mostra, e o custo é real.
    maxWidth_ = caps_.max_preview_width();
    maxHeight_ = caps_.max_preview_height();

    // Taxa alvo limitada pelo que o display entrega. Pedir 60 num display de
    // 120 Hz desperdiça metade da margem; pedir 120 num de 60 é impossível.
    if (targetFps <= 0.0f) targetFps = 60.0f;
    budget_.targetFps = targetFps;
    budget_.totalMs = 1000.0f / targetFps;

    // Divisão do orçamento, com base em medição de aparelho: em 1080p o
    // decode de hardware fica em ~2 ms, a composição em ~60% do resto, e a
    // apresentação com vsync come um pedaço fixo. A reserva existe porque um
    // frame que usa 100% do orçamento já está perdido — a variação normal o
    // empurra para fora.
    budget_.decodeMs  = budget_.totalMs * 0.15f;
    budget_.presentMs = budget_.totalMs * 0.10f;
    budget_.reserveMs = budget_.totalMs * 0.20f;
    budget_.renderMs  = budget_.totalMs - budget_.decodeMs
                      - budget_.presentMs - budget_.reserveMs;

    rebuild_options();

    if (userScale_ == PreviewScale::Auto) {
        // Ponto de partida sugerido pelo aparelho, mas o controlador ajusta no
        // primeiro frame medido. Começar em FULL e descer mostra um engasgo
        // desnecessário ao abrir um projeto 4K.
        const PreviewScale suggested = caps_.recommended_initial_scale(compWidth, compHeight);
        switch (suggested) {
            case PreviewScale::Half:    state_.numerator = 1; state_.denominator = 2; break;
            case PreviewScale::Quarter: state_.numerator = 1; state_.denominator = 4; break;
            case PreviewScale::Eighth:  state_.numerator = 1; state_.denominator = 8; break;
            default:                    state_.numerator = 1; state_.denominator = 1; break;
        }
        state_.current = suggested;
    }
}

void AdaptiveResolutionController::rebuild_options() noexcept {
    options_.clear();

    auto make = [&](PreviewScale scale, u32 num, u32 den) {
        PreviewScaleOption o;
        o.scale = scale;
        o.numerator = num;
        o.denominator = den;

        u32 w = compWidth_ * num / den;
        u32 h = compHeight_ * num / den;
        // O teto do aparelho se aplica a TODAS as opções, inclusive FULL: a
        // UI não pode oferecer uma opção que o hardware não sustenta.
        if (w > maxWidth_) { h = h * maxWidth_ / (w ? w : 1); w = maxWidth_; }
        if (h > maxHeight_) { w = w * maxHeight_ / (h ? h : 1); h = maxHeight_; }
        // Resolução ímpar quebra o alinhamento de bloco de 2x2 dos codecs de
        // hardware na hora de codificar. Arredonda para baixo em múltiplo de 2.
        w &= ~1u;
        h &= ~1u;
        if (w == 0) w = 2;
        if (h == 0) h = 2;

        o.width = w;
        o.height = h;
        options_.push_back(o);
    };

    make(PreviewScale::Auto, 1, 1);
    make(PreviewScale::Full, 1, 1);
    make(PreviewScale::Half, 1, 2);
    make(PreviewScale::Quarter, 1, 4);
    make(PreviewScale::Eighth, 1, 8);
}

void AdaptiveResolutionController::recompute_budget() noexcept {
    // Nada a recalcular além do que `configure` faz. Existe como ponto único
    // caso o orçamento passe a depender do estado térmico.
}

void AdaptiveResolutionController::set_user_scale(PreviewScale scale) noexcept {
    userScale_ = scale;
    if (scale == PreviewScale::Auto) return;   // volta ao automático

    // Escolha manual nunca é sobreposta pelo automático. O usuário mandou; ele
    // vê o resultado, mesmo que o frame caia. A UI avisa se ficar lento.
    state_.current = scale;
    const u32 num = (scale == PreviewScale::Full)    ? 1 : 1;
    const u32 den = (scale == PreviewScale::Full)    ? 1 :
                    (scale == PreviewScale::Half)    ? 2 :
                    (scale == PreviewScale::Quarter) ? 4 : 8;
    state_.numerator = num;
    state_.denominator = den;
    state_.overBudgetStreak = 0;
    state_.underBudgetStreak = 0;
    ++changeCount_;
}

bool AdaptiveResolutionController::auto_mode() const noexcept {
    return userScale_ == PreviewScale::Auto;
}

u32 AdaptiveResolutionController::render_width() const noexcept {
    u32 w = compWidth_ * state_.numerator / state_.denominator;
    if (w > maxWidth_) w = maxWidth_;
    w &= ~1u;
    return w ? w : 2;
}

u32 AdaptiveResolutionController::render_height() const noexcept {
    u32 h = compHeight_ * state_.numerator / state_.denominator;
    if (h > maxHeight_) h = maxHeight_;
    h &= ~1u;
    return h ? h : 2;
}

void AdaptiveResolutionController::step_down() noexcept {
    // Degradação em ordem: 1 → 1/2 → 1/4 → 1/8. O primeiro degrau é o mais
    // perceptível em qualidade e o mais eficaz em custo (metade dos pixels).
    if (state_.denominator == 1)      { state_.denominator = 2; state_.current = PreviewScale::Half; }
    else if (state_.denominator == 2) { state_.denominator = 4; state_.current = PreviewScale::Quarter; }
    else if (state_.denominator < 8)  { state_.denominator = 8; state_.current = PreviewScale::Eighth; }
    else return;   // já está no mínimo; não há mais o que reduzir

    ++changeCount_;
    // Trava por 30 frames depois de mudar. Sem isso o editor oscila: desce, o
    // frame sobra, sobe, o frame estoura, desce — e o usuário vê a resolução
    // piscando, o que é pior do que ficar baixo.
    state_.cooldownFrames = 30;
    state_.overBudgetStreak = 0;
    state_.underBudgetStreak = 0;
}

void AdaptiveResolutionController::step_up() noexcept {
    if (state_.denominator == 8)      { state_.denominator = 4; state_.current = PreviewScale::Quarter; }
    else if (state_.denominator == 4) { state_.denominator = 2; state_.current = PreviewScale::Half; }
    else if (state_.denominator == 2) { state_.denominator = 1; state_.current = PreviewScale::Full; }
    else return;

    ++changeCount_;
    state_.cooldownFrames = 60;   // subir exige mais calma que descer
    state_.overBudgetStreak = 0;
    state_.underBudgetStreak = 0;
}

const AdaptiveState& AdaptiveResolutionController::update(const FrameStats& stats,
                                                          const ThermalState& thermal) noexcept {
    // Média móvel exponencial. O último frame sozinho não decide: o primeiro
    // frame depois de um seek é sempre caro (decodifica, aloca, compila), e
    // reagir a ele derrubaria a resolução sem motivo.
    constexpr f32 kAlpha = 0.15f;
    const f32 frameMs = stats.gpuMs > 0.0f ? (stats.cpuMs > stats.gpuMs ? stats.cpuMs : stats.gpuMs)
                                           : stats.cpuMs;

    state_.averageFrameMs = state_.averageFrameMs <= 0.0f
                          ? frameMs
                          : lerpf(state_.averageFrameMs, frameMs, kAlpha);
    state_.averageGpuMs = lerpf(state_.averageGpuMs, stats.gpuMs, kAlpha);
    state_.averageCpuMs = lerpf(state_.averageCpuMs, stats.cpuMs, kAlpha);
    state_.decodeMs     = lerpf(state_.decodeMs, stats.decodeMs, kAlpha);
    state_.framesInFlight = stats.frameIndex;

    if (state_.cooldownFrames > 0) --state_.cooldownFrames;

    const bool over = state_.averageFrameMs > budget_.totalMs * FrameBudget::kComfortThreshold;
    const bool wayUnder = state_.averageFrameMs < budget_.totalMs * 0.55f;

    if (over) { ++state_.overBudgetStreak; state_.underBudgetStreak = 0; }
    else if (wayUnder) { ++state_.underBudgetStreak; state_.overBudgetStreak = 0; }
    else { state_.overBudgetStreak = 0; state_.underBudgetStreak = 0; }

    // Resolução travada pelo usuário: o automático para de mexer.
    if (userScale_ != PreviewScale::Auto) {
        return state_;
    }

    if (state_.cooldownFrames > 0) {
        return state_;
    }

    // Térmico: no nível crítico, desce mesmo que o frame esteja dentro do
    // orçamento — porque o aparelho VAI estrangular, e descer depois do
    // estrangulamento é tarde demais (já houve engasgo).
    if (thermal.severe() && state_.denominator < 4) {
        step_down();
        return state_;
    }

    // Três frames seguidos acima do orçamento é padrão, não ruído.
    if (state_.overBudgetStreak >= 3) {
        step_down();
        return state_;
    }

    // Subir exige bem mais evidência que descer: 90 frames (1,5 s a 60 Hz)
    // confortavelmente abaixo. Mexer para cima cedo demais produz a oscilação
    // que o cooldown existe para evitar.
    if (state_.underBudgetStreak >= 90 && state_.denominator > 1) {
        if (!thermal.should_degrade()) {
            step_up();
        }
    }

    return state_;
}

} // namespace aurea
