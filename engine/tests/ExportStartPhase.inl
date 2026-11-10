// Included by test_export.cpp.
// =============================================================================
//  0.5.2 (E4 e E1): a partida do export com prazo e cancelar, e o quadro que
//  nunca fica completo. MockBackend: sem GPU real; o que se confere aqui é o
//  controle (quem espera, por quanto tempo, quem ouve o Cancelar), não pixels.
//
//  Campo: LG K61 (PowerVR GE8320) parado em "sources" 20 a 46 s sem quadro e o
//  Cancelar sem efeito; Galaxy A23 parado no mesmo quadro nos 7 degraus.
// =============================================================================
namespace export_start_phase_test {

bool wait_flag(const std::atomic<bool>& flag, u64 timeoutNs) {
    const u64 deadline = monotonic_ns() + timeoutNs;
    while (!flag.load(std::memory_order_acquire) && monotonic_ns() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return flag.load(std::memory_order_acquire);
}
/// O diagnóstico da sessão diz esta etapa (a partida está nela agora).
bool wait_stage(Engine& e, const char* stage, u64 timeoutNs) {
    const std::string want = std::string("\"sessionStage\":\"") + stage + "\"";
    const u64 deadline = monotonic_ns() + timeoutNs;
    while (monotonic_ns() < deadline) {
        if (e.export_diagnostics().find(want) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
bool wait_finished(Engine& e, u64 timeoutNs) {
    const u64 deadline = monotonic_ns() + timeoutNs;
    while (!e.export_progress().finished && monotonic_ns() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return e.export_progress().finished;
}
/// Os prazos da partida dos próximos Rig (EngineConfig::exportStart*WaitMs).
struct StartWaits {
    StartWaits(u32 lockMs, u32 openMs) { gRigStartLockWaitMs = lockMs; gRigSourceOpenWaitMs = openMs; }
    ~StartWaits() { gRigStartLockWaitMs = 0; gRigSourceOpenWaitMs = 0; }
};
struct RemoveOutput {
    std::string path;
    ~RemoveOutput() { (void)fileio::remove_file(path); }
};
ExportSettings small_settings() {
    ExportSettings s; s.height = 64; s.fps = 30; s.dither = false;
    return s;
}

// A plataforma que não devolve a abertura da fonte (provedor de nuvem baixando)
// e não olha o cancelar que recebe — o pior caso do Android.
std::atomic<bool> gStuckRelease{false}, gStuckEntered{false};
Result<std::string> stuck_resolver(const std::string& path, const std::string&, const std::atomic<bool>*) noexcept {
    gStuckEntered.store(true, std::memory_order_release);
    const u64 safety = monotonic_ns() + 20'000'000'000ull;
    while (!gStuckRelease.load(std::memory_order_acquire) && monotonic_ns() < safety)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return path;
}

} // namespace export_start_phase_test

AUREA_TEST(ExportStartPhase, CancelReachesAStartWaitingForTheRenderLock) {
    using namespace export_start_phase_test;
    RemoveOutput output{"aurea_test_start_lock_cancel.mp4"};
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 4, 2, nullptr, backend);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    std::atomic<bool> release{false}, held{false}, returned{false};
    std::thread holder([&] { r.e.hold_render_lock_for_tests(release, &held); });
    AUREA_CHECK(wait_flag(held, 2'000'000'000ull));
    std::atomic<i32> code{static_cast<i32>(Errc::InvalidState)};
    const ExportSettings settings = small_settings();
    std::thread starter([&] {
        code.store(static_cast<i32>(r.e.start_export(settings, output.path.c_str()).code()), std::memory_order_release);
        returned.store(true, std::memory_order_release);
    });
    // A partida espera o lock, e o diagnóstico diz isso (antes: a sessão anterior).
    const bool waiting = wait_stage(r.e, "render_lock", 3'000'000'000ull);
    AUREA_CHECK(waiting);
    AUREA_CHECK(!returned.load(std::memory_order_acquire));
    // O Cancelar antes do portão de cancelamento chega à partida (antes: InvalidState e nada).
    AUREA_CHECK(r.e.cancel_export().ok());
    const bool back = wait_flag(returned, 2'000'000'000ull);
    AUREA_CHECK(back);
    release.store(true, std::memory_order_release);
    holder.join();
    starter.join();
    AUREA_CHECK_EQ(static_cast<Errc>(code.load(std::memory_order_acquire)), Errc::Cancelled);
    // Lock solto: o próximo export anda do começo ao fim.
    r.cap = BenchCapture{};
    r.cap.persistFinishMarker = true;
    AUREA_CHECK(r.e.start_export(settings, output.path.c_str()).ok());
    AUREA_CHECK(wait_finished(r.e, 10'000'000'000ull));
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{4});
}

AUREA_TEST(ExportStartPhase, RenderLockThatNeverFreesEndsTheStartWithAReason) {
    using namespace export_start_phase_test;
    RemoveOutput output{"aurea_test_start_lock_deadline.mp4"};
    StartWaits waits{300, 0};
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 4, 2, nullptr, backend);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    std::atomic<bool> release{false}, held{false};
    std::thread holder([&] { r.e.hold_render_lock_for_tests(release, &held); });
    AUREA_CHECK(wait_flag(held, 2'000'000'000ull));
    const ExportSettings settings = small_settings();
    const u64 begin = monotonic_ns();
    const Status refused = r.e.start_export(settings, output.path.c_str());
    const f64 seconds = static_cast<f64>(monotonic_ns() - begin) / 1e9;
    std::printf("\n    lock preso: partida voltou em %.2f s (%d)", seconds, refused.raw());
    release.store(true, std::memory_order_release);
    holder.join();
    // Ocupado (a tela espera e refaz o mesmo degrau), com o motivo, no prazo.
    AUREA_CHECK_EQ(refused.code(), Errc::Busy);
    AUREA_CHECK(refused.detail().find("a previa nao liberou a GPU") != std::string_view::npos);
    AUREA_CHECK(seconds < 3.0);
    r.cap = BenchCapture{};
    r.cap.persistFinishMarker = true;
    AUREA_CHECK(r.e.start_export(settings, output.path.c_str()).ok());
    AUREA_CHECK(wait_finished(r.e, 10'000'000'000ull));
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Ok);
}

AUREA_TEST(ExportStartPhase, PlatformSourceOpenThatNeverReturnsHonoursCancelAndDeadline) {
    using namespace export_start_phase_test;
    for (const bool cancel : {true, false}) {
        RemoveOutput output{cancel ? "aurea_test_source_open_cancel.mp4" : "aurea_test_source_open_deadline.mp4"};
        gStuckRelease.store(false); gStuckEntered.store(false);
        StartWaits waits{0, cancel ? 0u : 400u};
        SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
        auto* backend = new MockBackend(); backend->mapBuffers = true;
        Rig r(cfg, 30, 4, 2, nullptr, backend, 0, false, "sintetico", ExportExecutionProfile::Balanced, false,
              nullptr, nullptr, false, 0, true, 0, nullptr, &stuck_resolver);
        AUREA_CHECK(r.ok); if (!r.ok) { gStuckRelease.store(true); return; }
        std::atomic<bool> returned{false};
        std::atomic<i32> code{static_cast<i32>(Errc::InvalidState)};
        std::string detail;
        const ExportSettings settings = small_settings();
        const u64 begin = monotonic_ns();
        std::thread starter([&] {
            const Status s = r.e.start_export(settings, output.path.c_str());
            detail = std::string(s.detail());
            code.store(static_cast<i32>(s.code()), std::memory_order_release);
            returned.store(true, std::memory_order_release);
        });
        AUREA_CHECK(wait_flag(gStuckEntered, 3'000'000'000ull));
        AUREA_CHECK(wait_stage(r.e, "sources", 2'000'000'000ull));
        if (cancel) AUREA_CHECK(r.e.cancel_export().ok());
        const bool back = wait_flag(returned, 3'000'000'000ull);
        const f64 seconds = static_cast<f64>(monotonic_ns() - begin) / 1e9;
        std::printf("\n    abertura presa (%s): partida voltou em %.2f s", cancel ? "cancelar" : "prazo", seconds);
        AUREA_CHECK(back);
        gStuckRelease.store(true, std::memory_order_release);   // a thread solta da plataforma termina
        starter.join();
        if (cancel) {
            AUREA_CHECK_EQ(static_cast<Errc>(code.load()), Errc::Cancelled);
        } else {
            AUREA_CHECK_EQ(static_cast<Errc>(code.load()), Errc::MediaSourceMissing);
            AUREA_CHECK(detail.find("nao abriu a tempo") != std::string::npos);
        }
        AUREA_CHECK(r.cap.hashes.empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

AUREA_TEST(ExportIncompleteSite, ElementThatNeverBuildsFinishesTheExportWithANotice) {
    // Um efeito que nunca monta nesta GPU (o shader foi recusado) deixa todo
    // quadro incompleto no mesmo ponto, sem nada a caminho. Antes: o export
    // falhava nesse quadro ("recurso de composicao indisponivel") em todo
    // degrau. Agora: o primeiro quadro espera o prazo inteiro, o ponto vira
    // "sem solução", os outros saem depois da espera curta e a tela avisa.
    using namespace export_start_phase_test;
    RemoveOutput output{"aurea_test_incomplete_site_notice.mp4"};
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    backend->failShader = "effects/camera_shake.frag";
    constexpr i64 frames = 6;
    Rig r(cfg, 30, frames, 2, nullptr, backend, 0, false, "sintetico", ExportExecutionProfile::Balanced, false,
          nullptr, nullptr, false, 0, true, /*resourceTimeoutMs*/ 1200);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    AUREA_CHECK(r.add_effect(r.video_layer(), effect_keys::kShake) != nullptr);
    r.cap.persistFinishMarker = true;
    const u64 begin = monotonic_ns();
    AUREA_CHECK(r.e.start_export(small_settings(), output.path.c_str()).ok());
    AUREA_CHECK(wait_finished(r.e, 30'000'000'000ull));
    const f64 seconds = static_cast<f64>(monotonic_ns() - begin) / 1e9;
    const Engine::ExportProgress p = r.e.export_progress();
    const std::string diagnostics = r.e.export_diagnostics();
    std::printf("\n    efeito que nunca monta: %.2f s, resultado %d, %s", seconds, static_cast<int>(p.result), diagnostics.c_str());
    AUREA_CHECK_EQ(p.result, Errc::Ok);
    AUREA_CHECK((p.flags & Engine::kExportElementsOmitted) != 0);
    AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(frames));
    AUREA_CHECK(diagnostics.find("\"unresolvedSites\":1") != std::string::npos);
    // O prazo inteiro uma vez (1,2 s), não por quadro.
    AUREA_CHECK(seconds < 1.2 * 3);
}

AUREA_TEST(ExportIncompleteSite, UploadThatArrivesLateStillWaitsAndTheFrameIsComplete) {
    // Transitório: a textura da imagem falha ao subir nas primeiras vezes e
    // chega depois (upload pendente = algo a caminho). O quadro espera, sai
    // completo e sem aviso — a rede de segurança não pega o que ainda vem.
    using namespace export_start_phase_test;
    RemoveOutput output{"aurea_test_incomplete_transient.mp4"};
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 4, 2, nullptr, backend, 0, false, "sintetico", ExportExecutionProfile::Balanced, false,
          nullptr, nullptr, false, 0, true, /*resourceTimeoutMs*/ 5000);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    std::vector<u8> rgba(32u * 32u * 4u, 200);
    AUREA_CHECK(r.e.import_image(rgba.data(), 32, 32, "atrasada").ok());
    std::atomic<u32> refused{0};
    std::atomic<bool> armed{true};
    backend->beforeTextureUpload = [&](TextureHandle texture, const void*, u32) -> Status {
        const TextureDesc d = backend->texture_desc(texture);
        if (armed.load() && d.debugName && std::strcmp(d.debugName, "imagem") == 0 && refused.load() < 3) {
            ++refused;
            return Status{Errc::OutOfDeviceMemory, "upload recusado no teste"};
        }
        return OkStatus;
    };
    r.cap.persistFinishMarker = true;
    AUREA_CHECK(r.e.start_export(small_settings(), output.path.c_str()).ok());
    AUREA_CHECK(wait_finished(r.e, 30'000'000'000ull));
    armed.store(false);
    const Engine::ExportProgress p = r.e.export_progress();
    std::printf("\n    upload atrasado: %u recusas, resultado %d", refused.load(), static_cast<int>(p.result));
    AUREA_CHECK_EQ(p.result, Errc::Ok);
    AUREA_CHECK(refused.load() >= 1);
    AUREA_CHECK((p.flags & Engine::kExportElementsOmitted) == 0);
    AUREA_CHECK(r.e.export_diagnostics().find("\"unresolvedSites\":0") != std::string::npos);
    backend->beforeTextureUpload = {};
}
