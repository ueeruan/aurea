#include "aurea/jobs/JobSystem.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"

#include <thread>
#include <vector>

namespace aurea {

// -----------------------------------------------------------------------------
// parallel_for: bloco compartilhado pelas fatias.
//
// Precisa ser um tipo de namespace, não local: um lambda sem captura não pode
// referenciar um tipo declarado dentro da função em todos os compiladores, e
// o MSVC recusa. Como o corpo da tarefa é um ponteiro de função puro, o tipo
// tem que ser visível no ponto onde o lambda é definido.
// -----------------------------------------------------------------------------
namespace {

/// Uma fatia: o intervalo [start, end) de índices que UMA tarefa percorre.
///
/// `shared` existe para o descarte: o bloco só pode ser liberado depois que a
/// última fatia terminar, e cada fatia precisa saber onde está o contador.
struct ParallelForShared;

struct ParallelSlice {
    ParallelForShared* shared = nullptr;
    void (*fn)(void* base, u32 index, JobContext& ctx) = nullptr;
    void*  base   = nullptr;
    usize  stride = 0;
    u32    start  = 0;
    u32    end    = 0;
};

struct ParallelForShared {
    static constexpr u32 kMaxSlices = 32;
    ParallelSlice    slices[kMaxSlices];
    std::atomic<u32> remaining{0};
    u32              sliceCount = 0;
};

void parallel_slice_fn(void* ud, JobContext& ctx) {
    // Cada tarefa recebe UM slice, não o conjunto. Percorrer todos aqui faria
    // cada worker processar o trabalho inteiro e os índices serem visitados N
    // vezes — o resultado sai certo por acidente em testes pequenos e errado
    // (trabalho multiplicado) em produção.
    auto* sl = static_cast<ParallelSlice*>(ud);

    if (sl->start < sl->end) {
        for (u32 k = sl->start; k < sl->end; ++k) {
            // Tarefa longa: cada worker precisa ceder no shutdown, senão o app
            // demora a fechar e o sistema o mata.
            if (ctx.should_stop()) break;
            sl->fn(sl->base, k, ctx);
        }
    }

    ParallelForShared* shared = sl->shared;
    if (shared->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete shared;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// WorkQueue — MPMC com sequência por slot (algoritmo de Vyukov).
//
// Por que não uma fila com mutex: a submissão acontece na thread da UI e no
// renderer, enquanto N workers consomem. Um mutex único transforma isso numa
// disputa constante. Aqui cada lado escreve num contador atômico próprio, e o
// slot só é reivindicado por quem vê a sequência esperada.
//
// Capacidade fixa, potência de dois: o índice vira máscara, sem módulo.
// -----------------------------------------------------------------------------
bool JobSystem::WorkQueue::try_push(const Task& t) noexcept {
    u32 pos = tail_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[pos & kMask];
        const u64 seq = slot.sequence.load(std::memory_order_acquire);
        const i64 diff = static_cast<i64>(seq) - static_cast<i64>(pos);

        if (diff == 0) {
            if (tail_.compare_exchange_weak(pos, pos + 1,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
                slot.task = t;
                storage_.fetch_add(1, std::memory_order_relaxed);
                slot.sequence.store(static_cast<u64>(pos) + 1, std::memory_order_release);
                return true;
            }
            // Perdeu a corrida; `pos` foi atualizado pelo CAS.
        } else if (diff < 0) {
            // Fila cheia. Devolve falha em vez de bloquear: uma fila cheia numa
            // prioridade não pode travar a submissão de outra.
            return false;
        } else {
            pos = tail_.load(std::memory_order_relaxed);
        }
    }
}

bool JobSystem::WorkQueue::try_pop(Task& out) noexcept {
    u32 pos = head_.load(std::memory_order_relaxed);
    for (;;) {
        Slot& slot = slots_[pos & kMask];
        const u64 seq = slot.sequence.load(std::memory_order_acquire);
        const i64 diff = static_cast<i64>(seq) - static_cast<i64>(pos + 1);

        if (diff == 0) {
            if (head_.compare_exchange_weak(pos, pos + 1,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
                out = slot.task;
                slot.task = Task{};
                storage_.fetch_sub(1, std::memory_order_relaxed);
                slot.sequence.store(static_cast<u64>(pos) + kCapacity, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;   // vazia
        } else {
            pos = head_.load(std::memory_order_relaxed);
        }
    }
}

u32 JobSystem::WorkQueue::size() const noexcept {
    return storage_.load(std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// JobSystem
// -----------------------------------------------------------------------------
JobSystem::~JobSystem() { stop(); }

Status JobSystem::start(u32 workerCount) noexcept {
    if (running_.load(std::memory_order_acquire)) return OkStatus;

    for (u8 i = 0; i < static_cast<u8>(JobPriority::Count); ++i) {
        queues_[i].init();
    }

    if (workerCount == 0) {
        // Dimensiona por núcleos de PERFORMANCE, não pelo total. Um pool que
        // ocupa os 8 núcleos de um big.LITTLE deixa a thread de render e o
        // sistema sem CPU — o scheduler tira a thread de render no meio do
        // frame, e o ganho dos núcleos extras some.
        DeviceCapabilities caps;
        caps.detect();
        workerCount = caps.recommended_worker_count();
    }
    if (workerCount == 0) workerCount = 1;
    if (workerCount > 32) workerCount = 32;

    workerCount_ = workerCount;
    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    for (u32 i = 0; i < workerCount_; ++i) {
        std::thread t([this, i] { worker_main(this, i); });
        // Cada worker cuida do próprio ciclo de vida; destacado para não
        // precisar de join coordenado no shutdown.
        t.detach();
    }

    AUREA_LOG_INFO("JobSystem: %u workers", workerCount_);
    return OkStatus;
}

void JobSystem::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    stop_.store(true, std::memory_order_release);

    // Espera as tarefas em curso terminarem antes de deixar o pool morrer.
    // Sem isso, um decode em andamento escreveria num buffer já liberado.
    const u64 deadline = monotonic_ns() + 500'000'000ull;   // 500 ms
    while (activeTasks_.load(std::memory_order_acquire) != 0) {
        if (monotonic_ns() > deadline) {
            AUREA_LOG_WARN("JobSystem: shutdown com %u tarefas ativas apos 500 ms",
                           activeTasks_.load(std::memory_order_relaxed));
            break;
        }
        std::this_thread::yield();
    }

    for (u8 i = 0; i < static_cast<u8>(JobPriority::Count); ++i) {
        Task t;
        while (queues_[i].try_pop(t)) { /* descarta o que sobrou */ }
    }
}

void JobSystem::worker_main(JobSystem* self, u32 index) {
    while (self->running_.load(std::memory_order_acquire)) {
        bool ran = false;

        // Varre as filas em ordem de prioridade. Critical é checada primeiro
        // em toda iteração — é o que segura o frame.
        for (u8 p = 0; p < static_cast<u8>(JobPriority::Count); ++p) {
            Task t;
            if (!self->queues_[p].try_pop(t)) continue;

            self->activeTasks_.fetch_add(1, std::memory_order_acq_rel);
            JobContext ctx(static_cast<JobPriority>(p), index, &self->stop_);
            t.fn(t.userData, ctx);
            self->activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
            self->completed_.fetch_add(1, std::memory_order_relaxed);
            ran = true;
            break;
        }

        if (!ran) {
            // Sem trabalho. Cede a CPU em vez de girar: girar em 8 núcleos
            // aquece o aparelho e derruba a bateria sem fazer nada.
            std::this_thread::yield();
        }
    }
}

JobHandle JobSystem::submit(JobPriority prio, JobFn fn, void* userData) noexcept {
    if (!running_.load(std::memory_order_acquire) || !fn) return JobHandle{};

    Task t;
    t.fn = fn;
    t.userData = userData;
    t.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    t.prio = prio;

    if (!queues_[static_cast<u8>(prio)].try_push(t)) {
        AUREA_LOG_WARN("JobSystem: fila %u cheia, submissao recusada",
                       static_cast<unsigned>(prio));
        return JobHandle{};
    }
    return JobHandle{t.id};
}

bool JobSystem::parallel_for(JobPriority prio, u32 count,
                             void (*fn)(void* base, u32 index, JobContext& ctx),
                             void* base, usize stride) noexcept {
    if (count == 0 || !fn) return true;
    if (!running_.load(std::memory_order_acquire)) return false;

    // Cada fatia é uma tarefa. O número de fatias é o número de workers (uma
    // por worker), não uma por item: 2000 tarefas minúsculas custam mais em
    // submissão do que o trabalho que realizam.
    const u32 slices = count < workerCount_ ? count : workerCount_;
    const u32 perSlice = (count + slices - 1) / slices;

    // A base do payload precisa sobreviver até as tarefas rodarem. Aloca um
    // bloco por chamada e o libera na última tarefa via contador atômico.
    auto* shared = new (std::nothrow) ParallelForShared();
    if (!shared) return false;

    u32 submitted = 0;
    for (u32 s = 0; s < slices && submitted < ParallelForShared::kMaxSlices; ++s) {
        const u32 start = s * perSlice;
        if (start >= count) break;
        const u32 end = (start + perSlice) < count ? (start + perSlice) : count;
        ParallelSlice& slice = shared->slices[submitted];
        slice.shared = shared;
        slice.fn     = fn;
        slice.base   = base;
        slice.stride = stride;
        slice.start  = start;
        slice.end    = end;
        ++submitted;
    }
    if (submitted == 0) { delete shared; return true; }
    shared->sliceCount = submitted;
    shared->remaining.store(submitted, std::memory_order_release);

    // O contador é incrementado ANTES do primeiro envio e descontado por cada
    // submissão que falhar: uma tarefa já enfileirada pode terminar e liberar o
    // bloco antes de o laço acabar, o que faria o `shared` ser usado depois de
    // liberado.
    for (u32 s = 0; s < submitted; ++s) {
        if (!submit(prio, parallel_slice_fn, &shared->slices[s])) {
            if (shared->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete shared;
                return false;
            }
        }
    }
    return true;
}

void JobSystem::run_inline(JobFn fn, void* userData) noexcept {
    if (!fn) return;
    JobContext ctx(JobPriority::Critical, 0, &stop_);
    fn(userData, ctx);
    completed_.fetch_add(1, std::memory_order_relaxed);
}

void JobSystem::wait(JobHandle handle) noexcept {
    if (!handle.valid()) return;

    // Se quem espera é um worker, BLOQUEAR seria transformar o pool de N
    // threads em pool de N-1 (ou pior, deadlock se todas esperarem). Então
    // quem espera trabalha: processa a fila até a tarefa alvo ter rodado.
    const u64 deadline = monotonic_ns() + 10'000'000'000ull;   // 10 s
    while (completed_.load(std::memory_order_relaxed) < handle.id) {
        if (monotonic_ns() > deadline) {
            AUREA_LOG_WARN("JobSystem: espera excedeu 10 s pela tarefa %llu",
                           static_cast<unsigned long long>(handle.id));
            return;
        }
        bool ran = false;
        for (u8 p = 0; p < static_cast<u8>(JobPriority::Count); ++p) {
            Task t;
            if (!queues_[p].try_pop(t)) continue;
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
            JobContext ctx(static_cast<JobPriority>(p), 0, &stop_);
            t.fn(t.userData, ctx);
            activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
            completed_.fetch_add(1, std::memory_order_relaxed);
            ran = true;
            break;
        }
        if (!ran) std::this_thread::yield();
    }
}

void JobSystem::wait_idle(JobPriority prio) noexcept {
    const u8 p = static_cast<u8>(prio);
    const u64 deadline = monotonic_ns() + 5'000'000'000ull;
    while (queues_[p].size() != 0 || activeTasks_.load(std::memory_order_acquire) != 0) {
        if (monotonic_ns() > deadline) return;
        std::this_thread::yield();
    }
}

void JobSystem::pump(u32 maxTasks) noexcept {
    u32 done = 0;
    while (done < maxTasks) {
        bool ran = false;
        for (u8 p = 0; p < static_cast<u8>(JobPriority::Count); ++p) {
            Task t;
            if (!queues_[p].try_pop(t)) continue;
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
            JobContext ctx(static_cast<JobPriority>(p), 0, &stop_);
            t.fn(t.userData, ctx);
            activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
            completed_.fetch_add(1, std::memory_order_relaxed);
            ran = true;
            ++done;
            break;
        }
        if (!ran) break;
    }
}

u32 JobSystem::queue_depth(JobPriority p) const noexcept {
    return queues_[static_cast<u8>(p)].size();
}

} // namespace aurea
