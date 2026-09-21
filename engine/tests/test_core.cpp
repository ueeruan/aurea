// Testes das estruturas de base: handles, arena, orçamento de memória, fila de
// comandos, histórico, jobs e agendamento adaptativo.
#include "TestFramework.hpp"

#include "aurea/core/Handle.hpp"
#include "aurea/memory/Arena.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/command/CommandQueue.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/render/RenderScheduler.hpp"

#include <atomic>
#include <memory>
#include <thread>

using namespace aurea;

// -----------------------------------------------------------------------------
// Handles
// -----------------------------------------------------------------------------
struct Thing { int value = 0; };
struct ThingTag;
using ThingId = Handle<ThingTag>;

AUREA_TEST(Handle, CreateAndResolve) {
    SlotTable<Thing, ThingTag> table;
    const auto id = table.create();
    AUREA_CHECK(id.valid());
    AUREA_CHECK(table.get(id) != nullptr);
    AUREA_CHECK_EQ(table.count(), static_cast<u32>(1));
}

AUREA_TEST(Handle, DestroyInvalidatesOldHandle) {
    SlotTable<Thing, ThingTag> table;
    const auto a = table.create();
    (void)table.destroy(a);
    AUREA_CHECK(table.get(a) == nullptr);
    AUREA_CHECK(!table.contains(a));
}

AUREA_TEST(Handle, SlotReuseDoesNotReviveOldHandle) {
    // O ponto do handle de geração: reusar o slot não faz um handle antigo
    // voltar a apontar para nada. Sem isto, a UI que segurava um id de uma
    // camada apagada passaria a mexer numa camada diferente — silenciosamente.
    SlotTable<Thing, ThingTag> table;
    const auto old = table.create();
    (void)table.destroy(old);
    const auto fresh = table.create();
    AUREA_CHECK(old.index == fresh.index);      // mesmo slot
    AUREA_CHECK(old.generation != fresh.generation);
    AUREA_CHECK(table.get(old) == nullptr);     // handle antigo continua morto
    AUREA_CHECK(table.get(fresh) != nullptr);
}

AUREA_TEST(Handle, PackUnpackRoundTrip) {
    SlotTable<Thing, ThingTag> table;
    const auto id = table.create();
    const u64 packed = id.pack();
    const auto back = decltype(id)::unpack(packed);
    AUREA_CHECK(back == id);
}

AUREA_TEST(Handle, ForEachVisitsOnlyAlive) {
    SlotTable<Thing, ThingTag> table;
    const auto a = table.create();
    const auto b = table.create();
    const auto c = table.create();
    (void)table.destroy(b);

    u32 seen = 0;
    table.for_each([&](decltype(a), const Thing&) { ++seen; });
    AUREA_CHECK_EQ(seen, static_cast<u32>(2));
    (void)a; (void)c;
}

AUREA_TEST(OrderedIds, MoveToRepositions) {
    SlotTable<Thing, ThingTag> table;
    const auto a = table.create();
    const auto b = table.create();
    const auto c = table.create();

    OrderedIds<ThingId> order;
    order.push_back(a);
    order.push_back(b);
    order.push_back(c);

    AUREA_CHECK(order.move_to(c, 0));
    AUREA_CHECK(order.at(0) == c);
    AUREA_CHECK(order.at(1) == a);
    AUREA_CHECK(order.at(2) == b);
    AUREA_CHECK_EQ(order.index_of(c), 0);
}

// -----------------------------------------------------------------------------
// Arena
// -----------------------------------------------------------------------------
AUREA_TEST(Arena, AllocIsAligned) {
    Arena arena;
    for (int i = 0; i < 100; ++i) {
        void* p = arena.alloc(static_cast<usize>(i * 7 + 1));
        AUREA_CHECK(p != nullptr);
        AUREA_CHECK((reinterpret_cast<uintptr_t>(p) % 16) == 0);
    }
}

AUREA_TEST(Arena, ResetReusesBlock) {
    Arena arena;
    void* first = arena.alloc(128);
    const usize cap = arena.capacity();
    arena.reset();
    void* second = arena.alloc(128);
    // O mesmo bloco é reaproveitado: a arena de frame é resetada 60 vezes por
    // segundo e não pode realocar a cada vez.
    AUREA_CHECK(first == second);
    AUREA_CHECK_EQ(arena.capacity(), cap);
}

AUREA_TEST(Arena, GrowsWhenFull) {
    Arena arena(1024);
    void* a = arena.alloc(4096);
    AUREA_CHECK(a != nullptr);
    AUREA_CHECK(arena.capacity() >= 4096);
}

AUREA_TEST(Arena, MakeConstructsObject) {
    Arena arena;
    auto* v = arena.make<int>(42);
    AUREA_CHECK(v != nullptr);
    AUREA_CHECK_EQ(*v, 42);
}

// -----------------------------------------------------------------------------
// MemoryManager
// -----------------------------------------------------------------------------
namespace {
struct FakeCache : IMemoryReclaimable {
    MemoryClass cls = MemoryClass::Thumbnails;
    usize bytes = 0;
    usize reclaim(usize target) noexcept override {
        const usize freed = bytes < target ? bytes : target;
        bytes -= freed;
        return freed;
    }
    MemoryClass memory_class() const noexcept override { return cls; }
    const char* debug_name() const noexcept override { return "fake"; }
};
} // namespace

AUREA_TEST(Memory, ReserveWithinBudgetSucceeds) {
    MemoryManager mgr;
    mgr.set_budget(MemoryClass::GpuTextures, 1024 * 1024);
    auto r = mgr.try_reserve(MemoryClass::GpuTextures, 512 * 1024);
    AUREA_CHECK(r.valid());
    AUREA_CHECK_EQ(mgr.used(MemoryClass::GpuTextures), static_cast<usize>(512 * 1024));
}

AUREA_TEST(Memory, OverBudgetIsRejectedAndCounted) {
    MemoryManager mgr;
    mgr.set_budget(MemoryClass::GpuTextures, 1024);
    auto r = mgr.try_reserve(MemoryClass::GpuTextures, 4096);
    AUREA_CHECK(!r.valid());
    AUREA_CHECK_EQ(mgr.rejection_count(), static_cast<u64>(1));
}

AUREA_TEST(Memory, ReclaimsFromRegisteredCache) {
    MemoryManager mgr;
    mgr.set_budget(MemoryClass::Thumbnails, 1024);
    FakeCache cache;
    cache.bytes = 4096;
    (void)mgr.register_reclaimable(&cache);
    mgr.commit(MemoryClass::Thumbnails, 1024);

    auto r = mgr.try_reserve(MemoryClass::Thumbnails, 512);
    AUREA_CHECK(r.valid());
    AUREA_CHECK(cache.bytes < 4096);   // o cache foi efetivamente liberado
}

AUREA_TEST(Memory, PersistentNeverRejected) {
    // O projeto aberto não pode falhar por orçamento: falhar aqui perderia o
    // trabalho do usuário. Se não cabe, o teto está errado.
    MemoryManager mgr;
    mgr.set_budget(MemoryClass::Persistent, 1);
    auto r = mgr.try_reserve(MemoryClass::Persistent, 4096);
    AUREA_CHECK(r.valid());
    AUREA_CHECK_EQ(mgr.rejection_count(), static_cast<u64>(0));
}

AUREA_TEST(Memory, ReservationReleaseReturnsBudget) {
    MemoryManager mgr;
    mgr.set_budget(MemoryClass::Audio, 8192);
    {
        auto r = mgr.try_reserve(MemoryClass::Audio, 4096);
        AUREA_CHECK(r.valid());
        AUREA_CHECK_EQ(mgr.used(MemoryClass::Audio), static_cast<usize>(4096));
    }
    AUREA_CHECK_EQ(mgr.used(MemoryClass::Audio), static_cast<usize>(0));
}

AUREA_TEST(Memory, PersistentRegistrationIsRefused) {
    MemoryManager mgr;
    FakeCache cache;
    cache.cls = MemoryClass::Persistent;
    AUREA_CHECK(!mgr.register_reclaimable(&cache).ok());
}

// -----------------------------------------------------------------------------
// CommandQueue
// -----------------------------------------------------------------------------
AUREA_TEST(CommandQueue, PushPopPreservesOrder) {
    auto q = std::make_unique<CommandQueue>();
    for (u32 i = 0; i < 100; ++i) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.time = FrameIndex{static_cast<i64>(i)};
        AUREA_CHECK(q->push(c) != kInvalidIndex);
    }
    AUREA_CHECK_EQ(q->available(), static_cast<u32>(100));

    for (u32 i = 0; i < 100; ++i) {
        Command out;
        AUREA_CHECK(q->pop(out));
        AUREA_CHECK_EQ(out.keyframe.time.value, static_cast<i64>(i));
    }
    Command empty;
    AUREA_CHECK(!q->pop(empty));
}

AUREA_TEST(CommandQueue, StringBlobRoundTrip) {
    auto q = std::make_unique<CommandQueue>();
    u32 offset = 0, length = 0;
    AUREA_CHECK(q->push_string("Camada de video", 15, offset, length));

    Command c;
    c.type = CommandType::LayerCreate;
    c.stringOffset = offset;
    c.stringLength = length;
    AUREA_CHECK(q->push(c) != kInvalidIndex);

    Command out;
    AUREA_CHECK(q->pop(out));
    const char* s = q->string_at(out.stringOffset, out.stringLength);
    AUREA_CHECK_EQ(std::strncmp(s, "Camada de video", 15), 0);
}

AUREA_TEST(CommandQueue, FullQueueRejectsWithoutBlocking) {
    // A fila cheia devolve falha em vez de bloquear: a UI nunca pode travar
    // esperando o motor. Um comando perdido é recuperável; um app travado não.
    auto q = std::make_unique<CommandQueue>();
    Command c;
    c.type = CommandType::Nop;
    u32 accepted = 0;
    for (u32 i = 0; i < CommandQueue::kCapacity + 100; ++i) {
        if (q->push(c) == kInvalidIndex) break;
        ++accepted;
    }
    AUREA_CHECK_EQ(accepted, CommandQueue::kCapacity);
    AUREA_CHECK_EQ(q->dropped_count(), static_cast<u64>(1));
}

AUREA_TEST(CommandQueue, DrainReturnsCount) {
    auto q = std::make_unique<CommandQueue>();
    for (u32 i = 0; i < 10; ++i) {
        Command c;
        c.type = CommandType::Nop;
        (void)q->push(c);
    }
    u32 seen = 0;
    const u32 n = q->drain([&seen](const Command&) { ++seen; });
    AUREA_CHECK_EQ(n, static_cast<u32>(10));
    AUREA_CHECK_EQ(seen, static_cast<u32>(10));
}

AUREA_TEST(Command, SizeIsContract) {
    // O tamanho é contrato de ABI entre a UI e o motor, e é o que faz cada slot
    // da fila ocupar linhas de cache inteiras. Mudar o payload sem revisar isto
    // quebra a suposição em silêncio.
    AUREA_CHECK_EQ(sizeof(Command), static_cast<usize>(128));
    AUREA_CHECK(std::is_trivially_copyable_v<Command>);
}

// -----------------------------------------------------------------------------
// JobSystem
// -----------------------------------------------------------------------------
namespace {
std::atomic<int> g_jobCounter{0};
void increment_job(void*, JobContext&) { g_jobCounter.fetch_add(1); }

std::atomic<int> g_sliceHits{0};
void slice_job(void*, u32, JobContext&) { g_sliceHits.fetch_add(1); }
} // namespace

AUREA_TEST(Jobs, StartAndSubmit) {
    JobSystem jobs;
    AUREA_CHECK(jobs.start(2).ok());
    AUREA_CHECK(jobs.running());

    g_jobCounter.store(0);
    for (int i = 0; i < 32; ++i) {
        AUREA_CHECK(jobs.submit(JobPriority::Normal, increment_job, nullptr).valid());
    }
    jobs.wait_idle(JobPriority::Normal);

    // Roda as que sobrarem na thread chamadora: `wait_idle` observa a fila, e
    // sem o pump os itens ainda enfileirados nunca contariam.
    jobs.pump(4096);
    AUREA_CHECK_EQ(g_jobCounter.load(), 32);
    jobs.stop();
}

AUREA_TEST(Jobs, ParallelForCoversEveryIndex) {
    JobSystem jobs;
    AUREA_CHECK(jobs.start(3).ok());

    g_sliceHits.store(0);
    const bool ok = jobs.parallel_for(JobPriority::Critical, 100, slice_job, nullptr, 0);
    AUREA_CHECK(ok);

    for (int i = 0; i < 200 && g_sliceHits.load() < 100; ++i) {
        std::this_thread::yield();
    }
    AUREA_CHECK_EQ(g_sliceHits.load(), 100);
    jobs.stop();
}

AUREA_TEST(Jobs, RunInlineExecutesImmediately) {
    JobSystem jobs;
    g_jobCounter.store(0);
    jobs.run_inline(increment_job, nullptr);
    AUREA_CHECK_EQ(g_jobCounter.load(), 1);
}

AUREA_TEST(Jobs, SubmitAfterStopIsRefused) {
    JobSystem jobs;
    (void)jobs.start(1);
    jobs.stop();
    AUREA_CHECK(!jobs.submit(JobPriority::Normal, increment_job, nullptr).valid());
}

// -----------------------------------------------------------------------------
// Preview adaptativo
// -----------------------------------------------------------------------------
AUREA_TEST(Scheduler, BudgetFollowsDisplayRefresh) {
    // O orçamento é derivado da taxa real do display. Assumir 60 num display de
    // 120 Hz desperdiça metade da margem disponível.
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1080, 60.0f);
    AUREA_CHECK_NEAR(ctrl.budget().totalMs, 16.667f, 0.01f);

    ctrl.configure(1920, 1080, 120.0f);
    AUREA_CHECK_NEAR(ctrl.budget().totalMs, 8.333f, 0.01f);
}

AUREA_TEST(Scheduler, SustainedOverBudgetStepsDown) {
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1080, 60.0f);
    const u32 before = ctrl.current_denominator();

    FrameStats slow;
    slow.cpuMs = 40.0f;   // muito acima dos 16,67 ms
    for (int i = 0; i < 40; ++i) (void)ctrl.update(slow, ThermalState{});

    AUREA_CHECK(ctrl.current_denominator() > before);
}

AUREA_TEST(Scheduler, SingleSlowFrameDoesNotStepDown) {
    // O primeiro frame depois de um seek é sempre caro: decodifica, aloca e
    // compila. Reagir a ele faria a resolução cair sem motivo.
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1080, 60.0f);

    FrameStats oneSlow;
    oneSlow.cpuMs = 200.0f;
    (void)ctrl.update(oneSlow, ThermalState{});

    AUREA_CHECK_EQ(ctrl.state().current, PreviewScale::Auto);
    AUREA_CHECK_EQ(ctrl.change_count(), static_cast<u32>(0));
}

AUREA_TEST(Scheduler, ManualScaleIsNeverOverridden) {
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1080, 60.0f);
    ctrl.set_user_scale(PreviewScale::Full);
    AUREA_CHECK(!ctrl.auto_mode());

    FrameStats slow;
    slow.cpuMs = 100.0f;
    for (int i = 0; i < 200; ++i) (void)ctrl.update(slow, ThermalState{});

    // O usuário mandou resolução cheia. O automático para de mexer — e a UI
    // avisa se ficar lento, em vez de desobedecer.
    AUREA_CHECK_EQ(ctrl.current_denominator(), static_cast<u32>(1));
}

AUREA_TEST(Scheduler, ThermalSeverityForcesStepDown) {
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1080, 60.0f);

    ThermalState hot;
    hot.level = ThermalState::Level::Critical;

    FrameStats fine;
    fine.cpuMs = 5.0f;
    for (int i = 0; i < 5; ++i) (void)ctrl.update(fine, hot);

    // No nível crítico desce mesmo com o frame dentro do orçamento: o aparelho
    // VAI estrangular, e descer depois do estrangulamento é tarde.
    AUREA_CHECK(ctrl.current_denominator() > 1);
}

AUREA_TEST(Scheduler, RenderSizeIsEven) {
    // Resolução ímpar quebra o alinhamento de bloco 2x2 dos codecs de hardware
    // na hora de codificar o resultado.
    DeviceCapabilities caps;
    AdaptiveResolutionController ctrl(caps);
    ctrl.configure(1920, 1081, 60.0f);
    ctrl.set_user_scale(PreviewScale::Half);
    AUREA_CHECK_EQ(ctrl.render_width() % 2, static_cast<u32>(0));
    AUREA_CHECK_EQ(ctrl.render_height() % 2, static_cast<u32>(0));
}

// O cache de frames decodificados e o prefetch estão em test_media.cpp.
