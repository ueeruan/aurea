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
    // Contados ANTES de nascer: um stop() logo em seguida não pode achar zero.
    liveWorkers_.store(workerCount_, std::memory_order_release);

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
    // Acorda quem está dormindo. O aviso sai COM o mutex: um worker que acabou
    // de ver `running_` verdadeiro e ainda não entrou no wait não perde o
    // aviso (ele só solta o mutex dentro do wait).
    {
        std::lock_guard<std::mutex> lk(idleMutex_);
        idleCv_.notify_all();
    }

    // Espera os WORKERS saírem do laço (cada um termina a tarefa em curso e
    // não pega outra). Esperar só `activeTasks_` não basta: entre o try_pop e
    // o incremento há uma janela, e um worker ainda no laço lê `this` — com o
    // objeto destruído, o processo caía depois (use-after-free).
    const u64 deadline = monotonic_ns() + 2'000'000'000ull;   // 2 s
    while (liveWorkers_.load(std::memory_order_acquire) != 0) {
        if (monotonic_ns() > deadline) {
            AUREA_LOG_WARN("JobSystem: shutdown com %u workers presos apos 2 s (tarefa travada)",
                           liveWorkers_.load(std::memory_order_relaxed));
            break;
        }
        std::this_thread::yield();
    }

    for (u8 i = 0; i < static_cast<u8>(JobPriority::Count); ++i) {
        Task t;
        while (queues_[i].try_pop(t)) { /* descarta o que sobrou */ }
    }
    pending_.store(0, std::memory_order_seq_cst);
}

bool JobSystem::pop_any(Task& out) noexcept {
    // Varre as filas em ordem de prioridade. Critical é checada primeiro
    // em toda chamada — é o que segura o frame.
    for (u8 p = 0; p < static_cast<u8>(JobPriority::Count); ++p) {
        if (!queues_[p].try_pop(out)) continue;
        pending_.fetch_sub(1, std::memory_order_seq_cst);
        out.prio = static_cast<JobPriority>(p);
        return true;
    }
    return false;
}

void JobSystem::park() noexcept {
    std::unique_lock<std::mutex> lk(idleMutex_);
    // Dekker com o `submit`: aqui sobe `sleepers_` e depois lê `pending_`; lá
    // sobe `pending_` e depois lê `sleepers_` (os dois seq_cst). Pelo menos um
    // lado vê o outro — ou este worker vê a tarefa e não dorme, ou o submit vê
    // o worker e o acorda sob o mutex. Nenhuma tarefa fica esperando um worker
    // que dormiu depois dela.
    sleepers_.fetch_add(1, std::memory_order_seq_cst);
    parks_.fetch_add(1, std::memory_order_relaxed);
    idleCv_.wait(lk, [this] {
        return pending_.load(std::memory_order_seq_cst) > 0 || !running_.load(std::memory_order_acquire);
    });
    sleepers_.fetch_sub(1, std::memory_order_seq_cst);
    wakeups_.fetch_add(1, std::memory_order_relaxed);
}

void JobSystem::worker_main(JobSystem* self, u32 index) {
    // Rajada: o trabalho de um frame chega em várias tarefas a microssegundos
    // umas das outras. Girar um pouco antes de dormir evita pagar o acordar
    // (dezenas de µs no Android) entre duas fatias do mesmo parallel_for.
    constexpr u64 kSpinNs = 200'000;   // 0,2 ms
    u64 idleSince = 0;
    while (self->running_.load(std::memory_order_acquire)) {
        Task t;
        if (self->pop_any(t)) {
            self->activeTasks_.fetch_add(1, std::memory_order_acq_rel);
            JobContext ctx(t.prio, index, &self->stop_);
            t.fn(t.userData, ctx);
            self->activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
            self->completed_.fetch_add(1, std::memory_order_relaxed);
            idleSince = 0;
            continue;
        }

        // Sem trabalho. Antes (até a Fase 8) o worker cedia a CPU com yield e
        // voltava a olhar a fila PARA SEMPRE: `yield` sem outra thread pronta
        // volta na hora, então cada worker ocioso ocupava um núcleo inteiro
        // (medido no host: 4 workers parados = 384 % de um núcleo). No
        // celular, isso é aquecer e gastar bateria com o app parado. Agora:
        // gira 0,2 ms e dorme até alguém submeter.
        const u64 now = monotonic_ns();
        if (idleSince == 0) idleSince = now;
        if (now - idleSince < kSpinNs) {
            std::this_thread::yield();
            continue;
        }
        self->park();
        idleSince = 0;
    }
    // Última leitura de `self`: depois disto o stop() pode destruir o objeto.
    self->liveWorkers_.fetch_sub(1, std::memory_order_acq_rel);
}

JobHandle JobSystem::submit(JobPriority prio, JobFn fn, void* userData) noexcept {
    if (!running_.load(std::memory_order_acquire) || !fn) return JobHandle{};

    Task t;
    t.fn = fn;
    t.userData = userData;
    t.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    t.prio = prio;

    // `pending_` sobe ANTES do push: um worker que pegue a tarefa entre o push
    // e o incremento não pode levar o contador abaixo de zero.
    pending_.fetch_add(1, std::memory_order_seq_cst);
    if (!queues_[static_cast<u8>(prio)].try_push(t)) {
        pending_.fetch_sub(1, std::memory_order_seq_cst);
        AUREA_LOG_WARN("JobSystem: fila %u cheia, submissao recusada",
                       static_cast<unsigned>(prio));
        return JobHandle{};
    }
    if (sleepers_.load(std::memory_order_seq_cst) > 0) {
        std::lock_guard<std::mutex> lk(idleMutex_);
        idleCv_.notify_one();
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
        Task t;
        if (pop_any(t)) {
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
            JobContext ctx(t.prio, 0, &stop_);
            t.fn(t.userData, ctx);
            activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
            completed_.fetch_add(1, std::memory_order_relaxed);
        } else {
            std::this_thread::yield();
        }
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
        if (!pop_any(t)) break;
        activeTasks_.fetch_add(1, std::memory_order_acq_rel);
        JobContext ctx(t.prio, 0, &stop_);
        t.fn(t.userData, ctx);
        activeTasks_.fetch_sub(1, std::memory_order_acq_rel);
        completed_.fetch_add(1, std::memory_order_relaxed);
        ++done;
    }
}

u32 JobSystem::queue_depth(JobPriority p) const noexcept {
    return queues_[static_cast<u8>(p)].size();
}

} // namespace aurea
