// =============================================================================
//  Aurea / hyper / HyperGraph.hpp (privado do motor)
//
//  O grafo Hyper do export: as etapas Lite/Max (HyperStage.hpp) entre o
//  readback e o encoder. Uma thread de processamento própria; o worker do
//  encoder empurra o NV12 lido (cópia: o slot da GPU volta já) e puxa, em
//  ordem, o que sai. Memória LIMITADA: a entrada de cada etapa guarda no
//  máximo a janela temporal dela + 1 (2·raio+2 quadros) e a saída do grafo
//  2 lugares. Nada cresce com a duração do vídeo.
//
//  Ordem garantida: a saída k é o quadro de entrada k processado. O número
//  de quadros não muda (o Motion interpola no render).
// =============================================================================
#pragma once

#include "aurea/hyper/HyperStage.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace aurea::hyper {

class HyperGraph {
public:
    HyperGraph() = default;
    ~HyperGraph() { shutdown(); }
    HyperGraph(const HyperGraph&) = delete;
    HyperGraph& operator=(const HyperGraph&) = delete;

    /// Cria e prepara as etapas (na thread que chama: pesos carregam aqui) e
    /// sobe a thread. `fitW`×`fitH` > 0 e diferente da saída das etapas: um
    /// ajuste final (bilinear em NV12, quadro inteiro, sem cortar).
    [[nodiscard]] Status start(const DeviceProfile& profile, const Recipe& recipe,
                               std::span<const StageDescriptor* const> stages, const StageFormat& in,
                               u32 fitW, u32 fitH, const std::atomic<bool>* cancel,
                               void (*beat)(void*) noexcept, void* beatContext) noexcept;
    /// Para os testes e implementações embutidas: etapas já criadas.
    [[nodiscard]] Status start_with(const DeviceProfile& profile, const Recipe& recipe,
                                    std::vector<std::unique_ptr<HyperStage>> stages, const StageFormat& in,
                                    u32 fitW, u32 fitH, const std::atomic<bool>* cancel,
                                    void (*beat)(void*) noexcept, void* beatContext) noexcept;
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    [[nodiscard]] const StageFormat& output_format() const noexcept { return out_; }

    /// Empurra um quadro (copiado). Ok(false) = a entrada está cheia (puxe uma
    /// saída e tente de novo). Erro = o grafo falhou ou foi cancelado.
    [[nodiscard]] Result<bool> try_push(const FrameView& frame) noexcept;
    /// Não haverá mais entrada: o que falta sai com a borda repetida.
    void end_input() noexcept;
    /// Próxima saída, em ordem, esperando até `timeoutNs`. Ok(true) = `out`
    /// aponta o quadro (vale até o próximo pop); Ok(false) = nada ainda;
    /// Errc::NotFound = acabou; outro erro = falha/cancelamento.
    [[nodiscard]] Result<bool> pop(const FrameBuffer*& out, u64 timeoutNs) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] u64 peak_bytes() const noexcept;
    /// Etapa com estado ou vizinhos: a passagem não retoma no meio.
    [[nodiscard]] bool resumable() const noexcept { return resumable_; }
    [[nodiscard]] f32 frame_progress() const noexcept { return frameProgress_.load(std::memory_order_relaxed); }
    [[nodiscard]] u32 stage_count() const noexcept { return static_cast<u32>(lanes_.size()); }
    [[nodiscard]] const char* stage_name(u32 i) const noexcept {
        return i < lanes_.size() && lanes_[i].stage ? lanes_[i].stage->name() : "";
    }
    /// Quadros vivos agora / o máximo da sessão (o teste da memória limitada).
    [[nodiscard]] u32 live_frames() const noexcept;
    [[nodiscard]] u32 max_live_frames() const noexcept { return maxLive_.load(std::memory_order_relaxed); }

private:
    struct Item {
        FrameBuffer fb;
        u32 pool = 0;     ///< de qual fila de buffers ele veio (volta para lá)
    };
    struct Window {
        std::deque<Item> items;
        u32 first = 0;      ///< índice do items.front()
        u32 received = 0;   ///< quadros que já entraram
        bool ended = false;
        u32 capacity = 2;
    };
    struct Lane {
        std::unique_ptr<HyperStage> stage;
        StageFormat in{}, out{};
        u32 radius = 0;
        u32 next = 0;       ///< próximo centro a processar
        bool motion = false;            ///< StageKind::Motion: par de origem + fase
        // Quadro repetido pelo render (mesmo floor(sourcePosition)): a saída
        // anterior é copiada em vez de processar de novo (etapas antes do Motion).
        i64 lastFloor = -1;
        FrameBuffer lastOut;
        bool haveLast = false;
    };
    /// Quantos quadros à frente o Motion espera pelo quadro B de origem.
    static constexpr u32 kMotionLookahead = 10;
    void run() noexcept;
    [[nodiscard]] Item take_buffer_locked(u32 pool) noexcept;
    void give_back_locked(Item&& item) noexcept;
    void note_live_locked() noexcept;
    [[nodiscard]] u32 live_locked() const noexcept;

    std::vector<Lane> lanes_;
    std::vector<Window> windows_;        ///< windows_[k] = entrada da etapa k; a última = saída
    std::vector<std::vector<FrameBuffer>> pools_;
    Item popped_{};
    bool havePopped_ = false;
    StageFormat in_{}, out_{};
    Status failure_ = OkStatus;
    bool stop_ = false;
    bool resumable_ = true;
    const std::atomic<bool>* cancel_ = nullptr;
    StageContext stageCtx_{};
    std::atomic<f32> frameProgress_{0.0f};
    std::atomic<u32> maxLive_{0};
    u64 stageBytes_ = 0;
    u32 allocated_ = 0;                  ///< buffers criados (vivos = alocados − nas filas de volta)
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
};

/// O ajuste final de tamanho (bilinear NV12, quadro inteiro).
[[nodiscard]] std::unique_ptr<HyperStage> make_fit_stage(u32 width, u32 height);

} // namespace aurea::hyper
