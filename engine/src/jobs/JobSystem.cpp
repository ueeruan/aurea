#include "aurea/jobs/JobSystem.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"

#include <chrono>
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
namespace {

/// Envelhecimento: quantas tarefas de cima podem passar na frente de uma fila
/// que tem trabalho esperando antes de ela ganhar a vez. REALTIME nunca espera
/// (é a de cima); as de baixo esperam mais, mas nunca para sempre.
constexpr u32 kAgeLimit[JobSystem::kQueueCount] = {0, 16, 8, 16, 32};

/// Giro curto antes de dormir: uma tarefa que chega logo depois de outra
/// terminar (o caso comum de uma rajada) não paga a ida e volta do sono. É
/// limitado — girar indefinidamente era o defeito que queimava um núcleo por
/// worker com o app parado.
constexpr u32 kSpinBeforeSleep = 64;

} // namespace

JobSystem::~JobSystem() { stop(); }

Status JobSystem::start(u32 workerCount) noexcept {
    if (running_.load(std::memory_order_acquire)) return OkStatus;

    for (u8 i = 0; i < kQueueCount; ++i) {
        queues_[i].init();
        skipped_[i].store(0, std::memory_order_relaxed);
    }
    pendingFg_.store(0, std::memory_order_relaxed);
    pendingBg_.store(0, std::memory_order_relaxed);
    bgRunning_.store(0, std::memory_order_relaxed);

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
    if (workerCount > kMaxWorkers) workerCount = kMaxWorkers;

    workerCount_ = workerCount;
    activeWorkers_.store(workerCount_, std::memory_order_release);
    set_background_limit(bgLimitUser_.load(std::memory_order_relaxed));
    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    // Contados ANTES de nascer: um stop() logo em seguida não pode achar zero.
    liveWorkers_.store(workerCount_, std::memory_order_release);

    threads_.reserve(workerCount_);
    for (u32 i = 0; i < workerCount_; ++i) {
        threads_.emplace_back([this, i] { worker_main(this, i); });
    }

    AUREA_LOG_INFO("JobSystem: %u workers (fundo ate %u)", workerCount_, bgLimit_.load());
    return OkStatus;
}

void JobSystem::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(sleepMutex_);
    }
    sleepCv_.notify_all();

    // Espera os WORKERS saírem do laço (cada um termina a tarefa em curso e
    // não pega outra). Com prazo: uma tarefa travada não pode travar o
    // encerramento do app inteiro.
    const u64 deadline = monotonic_ns() + 2'000'000'000ull;   // 2 s
    while (liveWorkers_.load(std::memory_order_acquire) != 0) {
        if (monotonic_ns() > deadline) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const bool allOut = liveWorkers_.load(std::memory_order_acquire) == 0;
    for (std::thread& t : threads_) {
        if (!t.joinable()) continue;
        // Todos fora do laço: o join é imediato e devolve a thread ao sistema.
        // Algum preso numa tarefa: destacado (e registrado) em vez de travar.
        if (allOut) t.join();
        else t.detach();
    }
    if (!allOut) {
        AUREA_LOG_WARN("JobSystem: shutdown com %u workers presos apos 2 s (tarefa travada)",
                       liveWorkers_.load(std::memory_order_relaxed));
    }
    threads_.clear();

    for (u8 i = 0; i < kQueueCount; ++i) {
        Task t;
        while (queues_[i].try_pop(t)) { /* descarta o que sobrou */ }
    }
    pendingFg_.store(0, std::memory_order_relaxed);
    pendingBg_.store(0, std::memory_order_relaxed);
}

void JobSystem::set_active_workers(u32 n) noexcept {
    const u32 total = workerCount_ ? workerCount_ : 1;
    if (n == 0) n = 1;
    if (n > total) n = total;
    activeWorkers_.store(n, std::memory_order_release);
    set_background_limit(bgLimitUser_.load(std::memory_order_relaxed));
}

void JobSystem::set_background_limit(u32 n) noexcept {
    bgLimitUser_.store(n, std::memory_order_relaxed);
    const u32 active = activeWorkers_.load(std::memory_order_relaxed);
    // Automático: todos menos um. O que sobra é do quadro atual — é a garantia
    // de que miniatura/proxy nunca atrasam o preview.
    const u32 autoLimit = active > 1 ? active - 1 : 1;
    u32 lim = n == 0 ? autoLimit : (n < autoLimit ? n : autoLimit);
    if (lim == 0) lim = 1;
    bgLimit_.store(lim, std::memory_order_release);
    notify_workers(kMaxWorkers);
}

void JobSystem::apply_thermal(u32 thermalLevel) noexcept {
    // ThermalState::Level: 0 Nominal, 1 Fair, 2 Serious, 3 Critical, 4 Emergency.
    const u32 total = workerCount_ ? workerCount_ : 1;
    u32 active = total;
    u32 bg = 0;   // automático
    switch (thermalLevel) {
        case 0: break;
        case 1: bg = total > 2 ? total / 2 : 1; break;                     // morno: menos fundo
        case 2: bg = 1; break;                                              // quente: um só de fundo
        case 3:
        case 4: active = total > 1 ? (total + 1) / 2 : 1; bg = 1; break;   // crítico: metade do pool
        default: break;                                                     // desconhecido: sem mudança
    }
    activeWorkers_.store(active, std::memory_order_release);
    set_background_limit(bg);
}

JobSystem::Stats JobSystem::stats() const noexcept {
    Stats s;
    for (u8 i = 0; i < kQueueCount; ++i) {
        s.completed[i] = doneByPrio_[i].load(std::memory_order_relaxed);
        s.agedPromotions[i] = aged_[i].load(std::memory_order_relaxed);
    }
    s.sleeps = sleeps_.load(std::memory_order_relaxed);
    s.liveThreads = liveWorkers_.load(std::memory_order_relaxed);
    return s;
}

void JobSystem::notify_workers(u32 n) noexcept {
    if (sleepers_.load(std::memory_order_seq_cst) == 0) return;
    {
        // O lock vazio fecha a janela entre o predicado do worker e o wait.
        std::lock_guard<std::mutex> lock(sleepMutex_);
    }
    // Com parte do pool desligada (calor), um notify_one pode cair num worker
    // inativo, que volta a dormir e "come" o aviso: aí acorda todos.
    if (n <= 1 && activeWorkers_.load(std::memory_order_relaxed) >= workerCount_) sleepCv_.notify_one();
    else sleepCv_.notify_all();
}

bool JobSystem::take(Task& out, bool allowBackground, bool& bgSlot) noexcept {
    bgSlot = false;

    auto claim_bg = [&]() -> bool {
        u32 cur = bgRunning_.load(std::memory_order_acquire);
        const u32 lim = bgLimit_.load(std::memory_order_acquire);
        while (cur < lim) {
            if (bgRunning_.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel)) return true;
        }
        return false;
    };
    auto pop_from = [&](u8 p) -> bool {
        const bool bg = is_background(p);
        if (bg) {
            if (!allowBackground || queues_[p].empty() || !claim_bg()) return false;
        }
        if (!queues_[p].try_pop(out)) {
            if (bg) bgRunning_.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
        (bg ? pendingBg_ : pendingFg_).fetch_sub(1, std::memory_order_acq_rel);
        bgSlot = bg;
        return true;
    };

    // 1) Envelhecimento: uma fila de baixo que já viu passar tarefas demais na
    //    frente ganha a vez agora. Varre de baixo para cima (a mais faminta
    //    primeiro). REALTIME não envelhece — é sempre a primeira.
    for (u8 p = kQueueCount - 1; p >= 1; --p) {
        if (skipped_[p].load(std::memory_order_relaxed) < kAgeLimit[p]) continue;
        if (queues_[p].empty()) { skipped_[p].store(0, std::memory_order_relaxed); continue; }
        if (pop_from(p)) {
            skipped_[p].store(0, std::memory_order_relaxed);
            aged_[p].fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    // 2) Ordem de prioridade. Cada tarefa que passa na frente de uma fila com
    //    trabalho conta como "pulo" daquela fila.
    for (u8 p = 0; p < kQueueCount; ++p) {
        if (!pop_from(p)) continue;
        for (u8 q = static_cast<u8>(p + 1); q < kQueueCount; ++q) {
            if (!queues_[q].empty()) skipped_[q].fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

void JobSystem::run_task(const Task& t, u32 workerIndex, bool bgSlot) noexcept {
    activeTasks_.fetch_add(1, std::memory_order_acq_rel);
    JobContext ctx(t.prio, workerIndex, &stop_);
    t.fn(t.userData, ctx);
    activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
    completed_.fetch_add(1, std::memory_order_relaxed);
    doneByPrio_[static_cast<u8>(t.prio)].fetch_add(1, std::memory_order_relaxed);
    if (bgSlot) {
        bgRunning_.fetch_sub(1, std::memory_order_acq_rel);
        // Vaga de fundo liberada: se há fundo esperando, alguém dormindo pode
        // pegar agora.
        if (pendingBg_.load(std::memory_order_acquire) != 0) notify_workers(1);
    }
}

bool JobSystem::has_work_for(u32 workerIndex) const noexcept {
    if (workerIndex >= activeWorkers_.load(std::memory_order_acquire)) return false;
    if (pendingFg_.load(std::memory_order_acquire) != 0) return true;
    return pendingBg_.load(std::memory_order_acquire) != 0
        && bgRunning_.load(std::memory_order_acquire) < bgLimit_.load(std::memory_order_acquire);
}

void JobSystem::worker_main(JobSystem* self, u32 index) {
    set_current_thread_name("aurea-job");
    ThreadPriority current = ThreadPriority::Normal;
    u32 idleSpins = 0;

    while (self->running_.load(std::memory_order_acquire)) {
        Task t;
        bool bgSlot = false;
        const bool active = index < self->activeWorkers_.load(std::memory_order_acquire);
        if (active && self->take(t, true, bgSlot)) {
            idleSpins = 0;
            // Tarefa de fundo roda com a thread em prioridade de fundo: o
            // agendador do sistema passa o render e o decode na frente dela
            // mesmo quando os núcleos estão todos ocupados.
            const ThreadPriority want = bgSlot ? ThreadPriority::Background : ThreadPriority::Normal;
            if (want != current) { set_current_thread_priority(want); current = want; }
            self->run_task(t, index, bgSlot);
            continue;
        }

        if (active && ++idleSpins < kSpinBeforeSleep) {
            std::this_thread::yield();
            continue;
        }
        idleSpins = 0;

        // Sem trabalho que este worker possa pegar: dorme de verdade.
        std::unique_lock<std::mutex> lock(self->sleepMutex_);
        self->sleepers_.fetch_add(1, std::memory_order_seq_cst);
        self->sleeps_.fetch_add(1, std::memory_order_relaxed);
        self->sleepCv_.wait(lock, [&] {
            return !self->running_.load(std::memory_order_acquire) || self->has_work_for(index);
        });
        self->sleepers_.fetch_sub(1, std::memory_order_seq_cst);
    }
    // Última leitura de `self`: depois disto o stop() pode destruir o objeto.
    self->liveWorkers_.fetch_sub(1, std::memory_order_acq_rel);
}

JobHandle JobSystem::submit(JobPriority prio, JobFn fn, void* userData) noexcept {
    if (!running_.load(std::memory_order_acquire) || !fn) return JobHandle{};
    const u8 p = static_cast<u8>(prio) < kQueueCount ? static_cast<u8>(prio) : static_cast<u8>(JobPriority::Normal);

    Task t;
    t.fn = fn;
    t.userData = userData;
    t.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    t.prio = static_cast<JobPriority>(p);

    if (!queues_[p].try_push(t)) {
        AUREA_LOG_WARN("JobSystem: fila %s cheia, submissao recusada", to_string(t.prio));
        return JobHandle{};
    }
    // Contado ANTES de ler `sleepers_` (seq_cst dos dois lados): ou o worker
    // vê a tarefa no predicado, ou nós vemos o worker dormindo e o acordamos.
    (is_background(p) ? pendingBg_ : pendingFg_).fetch_add(1, std::memory_order_seq_cst);
    notify_workers(1);
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
    JobContext ctx(JobPriority::Realtime, 0, &stop_);
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
        Task t;
        bool bgSlot = false;
        if (take(t, true, bgSlot)) run_task(t, 0, bgSlot);
        else std::this_thread::yield();
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
        Task t;
        bool bgSlot = false;
        // A thread que chama (UI/render) não pega fundo: não pode ficar presa
        // numa análise longa.
        if (!take(t, false, bgSlot)) break;
        run_task(t, 0, bgSlot);
        ++done;
    }
}

u32 JobSystem::queue_depth(JobPriority p) const noexcept {
    return queues_[static_cast<u8>(p)].size();
}

} // namespace aurea
