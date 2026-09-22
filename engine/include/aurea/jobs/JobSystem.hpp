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
//  Toda tarefa é `void(void*, JobContext&)`. Sem std::function no caminho
//  quente: o custo de uma alocação por submissão seria pago 60 vezes por
//  segundo, por layer, por efeito.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace aurea {

/// Prioridades. A ordem importa: `Count` é o número de filas.
enum class JobPriority : u8 {
    /// Trabalho que segura um frame. Tem que terminar antes do próximo vsync.
    Critical = 0,
    /// Trabalho do frame atual, mas que pode cair para o próximo.
    High,
    /// Prefetch, decode antecipado, geração de proxy.
    Normal,
    /// Miniaturas, análise de waveform, indexação. Cede para tudo.
    Low,
    Count,
};

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
    JobSystem() = default;
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Sobe o pool. `workerCount == 0` escolhe automaticamente a partir de
    /// DeviceCapabilities (núcleos de performance, não o total).
    [[nodiscard]] Status start(u32 workerCount = 0) noexcept;

    /// Encerra o pool e espera todas as tarefas. Idempotente.
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

    [[nodiscard]] u32 worker_count() const noexcept { return workerCount_; }
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] u64 completed_count() const noexcept { return completed_.load(std::memory_order_relaxed); }
    [[nodiscard]] u32 queue_depth(JobPriority p) const noexcept;

    /// Ociosidade medida (§38): quantas vezes um worker foi dormir sem
    /// trabalho e quantas vezes acordou. Parado, os dois ficam PARADOS — um
    /// contador que sobe sem tarefa nenhuma é um worker girando à toa.
    [[nodiscard]] u64 idle_parks() const noexcept { return parks_.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 idle_wakeups() const noexcept { return wakeups_.load(std::memory_order_relaxed); }

private:
    friend class JobContext;
    static void worker_main(JobSystem* self, u32 index);

    struct Task;
    /// Tira a próxima tarefa (a de maior prioridade) e desconta `pending_`.
    /// Todo consumidor passa aqui — worker, `wait`, `pump` —, senão o contador
    /// que acorda os workers descola das filas.
    [[nodiscard]] bool pop_any(Task& out) noexcept;
    /// Dorme até haver tarefa pendente ou o pool parar.
    void park() noexcept;

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

    WorkQueue                queues_[static_cast<u8>(JobPriority::Count)];
    std::atomic<bool>        running_{false};
    std::atomic<bool>        stop_{false};
    std::atomic<u64>         nextId_{1};
    std::atomic<u64>         completed_{0};
    std::atomic<u32>         activeTasks_{0};
    /// Workers ainda dentro do laço. O stop() espera zerar: um worker
    /// destacado que ainda lê `this` depois da destruição derrubava o
    /// processo no teste seguinte.
    std::atomic<u32>         liveWorkers_{0};

    /// Tarefas enfileiradas e ainda não retiradas. É o que o worker ocioso
    /// espera virar > 0. Sobe ANTES do push (nunca fica negativo) e desce no
    /// pop; `sleepers_` diz ao `submit` se há alguém para acordar — sem worker
    /// dormindo, a submissão não toca no mutex.
    std::atomic<u32>         pending_{0};
    std::atomic<u32>         sleepers_{0};
    std::mutex               idleMutex_;
    std::condition_variable  idleCv_;
    std::atomic<u64>         parks_{0};
    std::atomic<u64>         wakeups_{0};

    u32                      workerCount_ = 0;
    void*                    threads_[32]{};
};

} // namespace aurea
