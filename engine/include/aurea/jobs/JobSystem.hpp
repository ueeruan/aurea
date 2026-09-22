// =============================================================================
//  Aurea / jobs / JobSystem.hpp
//
//  Sistema de tarefas do motor.
//
//  O que ele NÃO é: uma thread por coisa. Criar thread aleatória é como o
//  editor antigo morria — 30 threads disputando, scheduler do sistema
//  repriorizando no meio do playback, frame perdido.
//
//  O que ele é: um pool fixo, dimensionado pelos núcleos reais do aparelho
//  (respeitando os núcleos pequenos: um A55 rodando trabalho de render atrasa
//  mais do que ajuda), com filas por prioridade e uma fila sem trava para o
//  caminho de submissão.
//
//  Fase 8 (§31–34, §38):
//   - cinco prioridades: REALTIME (áudio), HIGH (preview, decode atual,
//     scrub), NORMAL (miniaturas/waveform visíveis), LOW (proxy, análise) e
//     BACKGROUND;
//   - sem starvation: cada fila de baixo ganha a vez depois de N tarefas de
//     cima passarem na frente dela (envelhecimento por contagem);
//   - LOW/BACKGROUND nunca ocupam todos os workers: sobra sempre um para o
//     que segura o quadro (medido em Jobs.BackgroundDoesNotDelayHighPriority);
//   - worker sem trabalho DORME (variável de condição). Antes ele girava em
//     `yield()` — cada worker queimava um núcleo inteiro com o app parado;
//   - workers ativos seguem a temperatura (`apply_thermal`): quente, menos
//     trabalho de fundo; crítico, metade do pool.
//
//  Toda tarefa é `void(void*, JobContext&)`. Sem std::function no caminho
//  quente: o custo de uma alocação por submissão seria pago 60 vezes por
//  segundo, por layer, por efeito.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace aurea {

/// Prioridades. A ordem importa: `Count` é o número de filas.
enum class JobPriority : u8 {
    /// Áudio e o que segura o quadro. Tem que terminar antes do próximo vsync.
    Realtime = 0,
    /// Nome antigo de Realtime (mesma fila).
    Critical = Realtime,
    /// Preview, decode do quadro atual, scrub.
    High = 1,
    /// Miniaturas e waveform VISÍVEIS, prefetch.
    Normal = 2,
    /// Proxy, análise, indexação. Cede para tudo.
    Low = 3,
    /// Limpeza, cache em disco, o que pode esperar minutos.
    Background = 4,
    Count = 5,
};

[[nodiscard]] constexpr const char* to_string(JobPriority p) noexcept {
    switch (p) {
        case JobPriority::Realtime:   return "realtime";
        case JobPriority::High:       return "high";
        case JobPriority::Normal:     return "normal";
        case JobPriority::Low:        return "low";
        case JobPriority::Background: return "background";
        case JobPriority::Count:      break;
    }
    return "?";
}

using JobFn = void (*)(void* userData, class JobContext& ctx);

/// Contexto entregue a cada tarefa. Dá acesso ao resto do sistema sem que a
/// tarefa precise de ponteiros globais.
class JobContext {
public:
    JobContext(JobPriority prio, u32 workerIndex,
               const std::atomic<bool>* stopFlag) noexcept
        : priority_(prio), worker_(workerIndex), stop_(stopFlag) {}

    [[nodiscard]] JobPriority priority() const noexcept { return priority_; }
    [[nodiscard]] u32 worker_index() const noexcept { return worker_; }

    /// Verdadeiro quando o sistema está encerrando. Tarefa longa (um decode de
    /// 4K) precisa checar isto periodicamente e sair, em vez de segurar o
    /// shutdown até o usuário matar o app.
    [[nodiscard]] bool should_stop() const noexcept {
        return stop_ && stop_->load(std::memory_order_relaxed);
    }

    /// Contador de iterações para tarefas longas. `tick()` devolve true a cada
    /// 256 chamadas — barato o bastante para rodar dentro de um laço apertado.
    [[nodiscard]] bool tick() noexcept {
        if (++ticks_ >= 256) { ticks_ = 0; return true; }
        return false;
    }

private:
    JobPriority               priority_;
    u32                       worker_;
    u32                       ticks_ = 0;
    const std::atomic<bool>*  stop_  = nullptr;
};

/// Identificador de uma tarefa submetida. Permite esperar por ela.
struct JobHandle {
    u64 id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }
};

class JobSystem {
public:
    static constexpr u32 kMaxWorkers = 32;
    static constexpr u8  kQueueCount = static_cast<u8>(JobPriority::Count);

    JobSystem() = default;
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Sobe o pool. `workerCount == 0` escolhe automaticamente a partir de
    /// DeviceCapabilities (núcleos de performance, não o total).
    [[nodiscard]] Status start(u32 workerCount = 0) noexcept;

    /// Encerra o pool e espera todas as tarefas. Idempotente. As threads são
    /// JUNTADAS (join): depois do stop a contagem de threads do processo volta
    /// ao que era — é o que o teste de ciclo longo confere.
    void stop() noexcept;

    /// Submete uma tarefa. Devolve handle inválido se o sistema está parado.
    [[nodiscard]] JobHandle submit(JobPriority prio, JobFn fn, void* userData) noexcept;

    /// Submete N tarefas iguais com dados distintos — o caso comum de fatiar um
    /// trabalho por faixa (linhas de uma imagem, frames de um intervalo).
    [[nodiscard]] bool parallel_for(JobPriority prio, u32 count,
                                    void (*fn)(void* base, u32 index, JobContext& ctx),
                                    void* base, usize stride) noexcept;

    /// Executa uma tarefa na thread chamadora. Usado quando o trabalho é
    /// pequeno demais para justificar ir ao pool — a ida e volta custa mais.
    void run_inline(JobFn fn, void* userData) noexcept;

    /// Espera a tarefa terminar. Se chamada de dentro de um worker, EXECUTA a
    /// tarefa em vez de bloquear: bloquear um worker esperando outro é como um
    /// pool de N threads vira pool de 1.
    void wait(JobHandle handle) noexcept;

    /// Espera todas as tarefas de uma prioridade terminarem.
    void wait_idle(JobPriority prio) noexcept;

    /// Processa tarefas pendentes na thread chamadora até não haver mais.
    /// A UI chama isto antes de dormir, para não deixar o pool ocioso.
    void pump(u32 maxTasks = 64) noexcept;

    /// Quantos workers podem pegar trabalho (os demais dormem). 1..worker_count.
    void set_active_workers(u32 n) noexcept;
    /// Teto de workers rodando LOW/BACKGROUND ao mesmo tempo. Sempre deixa ao
    /// menos um worker livre para o trabalho de cima quando há mais de um ativo.
    void set_background_limit(u32 n) noexcept;
    /// Política térmica (§35–36, §33): nível de ThermalState::Level (0 nominal
    /// .. 4 emergência). Nominal: tudo; Fair (morno): metade dos workers para
    /// fundo; Serious (quente): um só para fundo; Critical/Emergency: metade
    /// do pool ativo e um só para fundo.
    void apply_thermal(u32 thermalLevel) noexcept;

    [[nodiscard]] u32 worker_count() const noexcept { return workerCount_; }
    [[nodiscard]] u32 active_workers() const noexcept { return activeWorkers_.load(std::memory_order_relaxed); }
    [[nodiscard]] u32 background_limit() const noexcept { return bgLimit_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] u64 completed_count() const noexcept { return completed_.load(std::memory_order_relaxed); }
    [[nodiscard]] u32 queue_depth(JobPriority p) const noexcept;

    struct Stats {
        u64 completed[kQueueCount]{};
        /// Vezes que uma fila de baixo ganhou a vez por envelhecimento.
        u64 agedPromotions[kQueueCount]{};
        /// Vezes que um worker dormiu sem trabalho (ociosidade real, sem giro).
        u64 sleeps = 0;
        u32 liveThreads = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;

private:
    friend class JobContext;
    static void worker_main(JobSystem* self, u32 index);

    struct Task {
        JobFn    fn       = nullptr;
        void*    userData = nullptr;
        u64      id       = 0;
        JobPriority prio  = JobPriority::Normal;
    };

    /// Fila sem trava de produtor único / consumidor múltiplo, com capacidade
    /// fixa em potência de dois. Cheia devolve falha em vez de bloquear: quem
    /// submete decide o que fazer (rodar inline, adiar), e uma fila cheia numa
    /// prioridade não pode travar a submissão da prioridade crítica.
    class WorkQueue {
    public:
        static constexpr u32 kCapacity = 1024;
        static constexpr u32 kMask = kCapacity - 1;

        void init() noexcept {
            head_.store(0, std::memory_order_relaxed);
            tail_.store(0, std::memory_order_relaxed);
            storage_.store(0, std::memory_order_relaxed);

            // A sequência de cada slot começa valendo o PRÓPRIO índice. Sem
            // isto o produtor vê `sequence - pos == -1` no slot 1 e conclui que
            // a fila está cheia — depois de aceitar exatamente uma tarefa. O
            // sintoma é o pool parar de receber trabalho para sempre, e é o
            // tipo de erro que só aparece sob carga.
            for (u32 i = 0; i < kCapacity; ++i) {
                slots_[i].sequence.store(static_cast<u64>(i), std::memory_order_relaxed);
                slots_[i].task = Task{};
            }
        }
        [[nodiscard]] bool try_push(const Task& t) noexcept;
        [[nodiscard]] bool try_pop(Task& out) noexcept;
        [[nodiscard]] u32  size() const noexcept;
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
        struct Slot {
            std::atomic<u64> sequence{0};
            Task task{};
        };
        Slot slots_[kCapacity]{};
        alignas(64) std::atomic<u32> head_{0};
        alignas(64) std::atomic<u32> tail_{0};

        /// Contagem de itens presentes. Separada de (tail - head) porque os
        /// dois contadores avançam de forma independente — a diferença não é
        /// observável de forma consistente sem uma barreira por operação, e
        /// consultar o tamanho da fila não vale uma barreira.
        alignas(64) std::atomic<u32> storage_{0};
    };

    /// Pega a próxima tarefa respeitando prioridade, envelhecimento e o teto
    /// de fundo. `bgSlot` volta true quando a tarefa ocupou uma vaga de fundo
    /// (quem roda devolve a vaga ao terminar).
    [[nodiscard]] bool take(Task& out, bool allowBackground, bool& bgSlot) noexcept;
    void run_task(const Task& t, u32 workerIndex, bool bgSlot) noexcept;
    [[nodiscard]] bool has_work_for(u32 workerIndex) const noexcept;
    void notify_workers(u32 n) noexcept;

    static constexpr bool is_background(u8 p) noexcept { return p >= static_cast<u8>(JobPriority::Low); }

    WorkQueue                queues_[kQueueCount];
    std::atomic<bool>        running_{false};
    std::atomic<bool>        stop_{false};
    std::atomic<u64>         nextId_{1};
    std::atomic<u64>         completed_{0};
    std::atomic<u32>         activeTasks_{0};
    /// Workers ainda dentro do laço. O stop() espera zerar antes de juntar.
    std::atomic<u32>         liveWorkers_{0};
    u32                      workerCount_ = 0;
    std::vector<std::thread> threads_;

    // Sono dos workers. `pending*` é contado ANTES de acordar alguém e o
    // predicado da espera o relê sob o mutex: sem janela de despertar perdido.
    std::mutex               sleepMutex_;
    std::condition_variable  sleepCv_;
    std::atomic<u32>         sleepers_{0};
    std::atomic<u32>         pendingFg_{0};   ///< REALTIME/HIGH/NORMAL enfileiradas
    std::atomic<u32>         pendingBg_{0};   ///< LOW/BACKGROUND enfileiradas
    std::atomic<u32>         bgRunning_{0};   ///< LOW/BACKGROUND rodando agora
    std::atomic<u32>         bgLimit_{1};
    std::atomic<u32>         bgLimitUser_{0}; ///< 0 = automático (ativos − 1)
    std::atomic<u32>         activeWorkers_{0};

    /// Envelhecimento: tarefas de cima que passaram na frente de cada fila
    /// enquanto ela tinha trabalho esperando.
    std::atomic<u32>         skipped_[kQueueCount]{};
    std::atomic<u64>         doneByPrio_[kQueueCount]{};
    std::atomic<u64>         aged_[kQueueCount]{};
    std::atomic<u64>         sleeps_{0};
};

} // namespace aurea
