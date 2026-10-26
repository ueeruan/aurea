// =============================================================================
//  CheckpointManager (Export Engine 2.0, 0.3.2): export retomável de ponta a
//  ponta com o sink FFmpeg real e a GPU de verdade.
//
//  - interrompido no quadro N (suspensão do app) e retomado: os quadros
//    decodificados, a lista de PTS, a contagem, a duração e as amostras de
//    áudio batem com o export sem interrupção. Antes do ponto de retomada os
//    pacotes são os mesmos (quadros idênticos); depois, a sessão nova do
//    encoder diverge só no ruído de quantização (PSNR alto);
//  - cauda do registro corrompida (o processo morreu escrevendo): continua do
//    GOP anterior; o app que reabre recebe o mesmo ponto pela consulta;
//  - projeto ou opções mudaram: recomeça do zero e o checkpoint antigo sai;
//  - cancelar (o usuário) numa sessão retomada: nenhum arquivo publicado e
//    nenhum checkpoint sobrando;
//  - pouco espaço: o export sai pelo MP4 direto, sem checkpoint (diagnóstico);
//  - o encoder falha no meio com o MESMO encoder: o mesmo start_export continua;
//  - o hash das fontes no "Preparando" é cancelável.
// =============================================================================
#if defined(AUREA_HAS_FFMPEG)
#include "aurea/export/ExportCheckpoint.hpp"
#include "aurea/export/ExportPacketLog.hpp"
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

const char* const kCheckpointDir = "build/checkpoint-tests/state";
const char* const kCheckpointOut = "build/checkpoint-tests";

/// Falha simulada do encoder no meio do vídeo: a próxima sessão do sink
/// devolve EncodeFailed no write_video do quadro `ckptFailAt` (uma vez).
std::atomic<u32> ckptFailAt{0};
class FailingSink final : public ExportSink {
public:
    explicit FailingSink(std::unique_ptr<ExportSink> inner) : inner_(std::move(inner)), failAt_(ckptFailAt.exchange(0)) {}
    void set_cancel_flag(const std::atomic<bool>* f) noexcept override { inner_->set_cancel_flag(f); }
    void set_heartbeat(std::atomic<u64>* b) noexcept override { inner_->set_heartbeat(b); }
    void set_diagnostics(std::shared_ptr<ExportDiagnostics> d) noexcept override { inner_->set_diagnostics(std::move(d)); }
    Status open(const char* p, const VideoStreamConfig& v, const AudioStreamConfig* a) noexcept override {
        frame_ = v.resumeFrame;
        return inner_->open(p, v, a);
    }
    Status write_video(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts) noexcept override {
        if (failAt_ && frame_ == failAt_) { failAt_ = 0; return Status{Errc::EncodeFailed, "falha simulada do encoder no meio"}; }
        ++frame_;
        return inner_->write_video(y, ys, uv, uvs, pts);
    }
    Status write_audio(const i16* pcm, u32 frames, i64 pts) noexcept override { return inner_->write_audio(pcm, frames, pts); }
    Status finish() noexcept override { return inner_->finish(); }
    Status end_video() noexcept override { return inner_->end_video(); }
    Status begin_assembly(const AudioStreamConfig* a) noexcept override { return inner_->begin_assembly(a); }
    Status replay_spilled_audio() noexcept override { return inner_->replay_spilled_audio(); }
    void set_assembly_counter(std::atomic<u64>* f) noexcept override { inner_->set_assembly_counter(f); }
    u32 checkpoint_frame() const noexcept override { return inner_->checkpoint_frame(); }
    void close_checkpoint() noexcept override { inner_->close_checkpoint(); }
    Status validate_output(const ExportOutputValidation& e) noexcept override { return inner_->validate_output(e); }
    const char* validation_warnings() const noexcept override { return inner_->validation_warnings(); }
    void abort() noexcept override { inner_->abort(); }
    EncoderInfo encoder_info() const noexcept override { return inner_->encoder_info(); }
private:
    std::unique_ptr<ExportSink> inner_;
    u32 failAt_ = 0, frame_ = 0;
};
std::unique_ptr<ExportSink> make_failing_sink(void* user) noexcept {
    auto inner = make_ffmpeg_export_sink(user);
    if (!inner) return nullptr;
    return std::unique_ptr<ExportSink>(new (std::nothrow) FailingSink(std::move(inner)));
}

std::unique_ptr<Rig> checkpoint_rig(const SyntheticConfig& cfg, i64 frames, const char* source = "sintetico") {
    std::filesystem::create_directories(kCheckpointDir);
    gRigResumable = true;
    gRigCheckpointDir = kCheckpointDir;
    auto r = std::make_unique<Rig>(cfg, 30, frames, 3, nullptr, nullptr, 0, false, source,
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 30000, &make_failing_sink);
    gRigResumable = false;
    gRigCheckpointDir.clear();
    return r;
}

SyntheticConfig checkpoint_config() {
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 192; cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.audioRate = 44100; cfg.audioSeconds = 5; cfg.decodeCostUs = 15000; cfg.frameCount = 150;
    return cfg;
}

ExportSettings checkpoint_settings() {
    ExportSettings settings; settings.height = 192; settings.fps = 30; settings.dither = false;
    settings.keyframeIntervalFrames = 15;
    return settings;
}

Engine::ExportProgress wait_checkpoint(Rig& r, u32 untilFrames = ~0u) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    for (;;) {
        const auto p = r.e.export_progress();
        if (p.finished || p.framesDone >= untilFrames || std::chrono::steady_clock::now() > deadline) return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

/// Arquivos do CheckpointManager na pasta (registro, PCM, manifesto).
/// A suspensao (o app foi para segundo plano). Engine::suspend devolve Timeout
/// quando os quadros do export que ela acabou de parar ainda estao na GPU e
/// pede para tentar de novo (o export ja parou; os recursos nativos nao foram
/// suspensos). O teste repete como a plataforma deveria, com limite.
bool suspend_engine(Rig& r) {
    for (int attempt = 0; attempt < 400; ++attempt) {
        const Status s = r.e.suspend();
        if (s.ok()) return true;
        if (s.code() != Errc::Timeout) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
std::vector<std::string> checkpoint_files() {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(kCheckpointDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("aurea-ckpt-", 0) == 0) names.push_back(name);
    }
    return names;
}
std::string checkpoint_log() {
    for (const auto& name : checkpoint_files())
        if (name.size() > 10 && name.compare(name.size() - 10, 10, ".aurea-pkt") == 0) return std::string(kCheckpointDir) + "/" + name;
    return {};
}
/// Temporários da montagem/partida que nunca podem sobrar ao lado da saída.
bool staged_leftovers(const std::string& output) {
    std::error_code ec;
    const std::string base = std::filesystem::path(output).filename().string();
    for (std::filesystem::directory_iterator it(kCheckpointOut, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(base, 0) == 0 && name != base) return true;
    }
    return false;
}
void clear_checkpoints() {
    (void)export_checkpoint::remove_others(kCheckpointDir, {});
}
f64 psnr(const std::vector<u8>& a, const std::vector<u8>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    f64 mse = 0;
    for (usize k = 0; k < a.size(); ++k) { const f64 d = static_cast<f64>(a[k]) - b[k]; mse += d * d; }
    mse /= static_cast<f64>(a.size());
    return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}
/// Offsets dos registros (cabeçalho de 24 bytes + dados) de cada quadro.
std::vector<u64> packet_offsets(const std::string& log) {
    std::vector<u64> offsets;
    std::vector<u8> bytes;
    if (!fileio::read_all(log, bytes, 512ull << 20) || bytes.size() < 16) return offsets;
    auto get32 = [&](u64 at) { return u32(bytes[at]) | u32(bytes[at + 1]) << 8 | u32(bytes[at + 2]) << 16 | u32(bytes[at + 3]) << 24; };
    u64 at = 16ull + get32(12) + 8ull;
    while (at + 24 <= bytes.size()) {
        const u32 size = get32(at + 8);
        if (at + 24 + size > bytes.size()) break;
        offsets.push_back(at);
        at += 24 + size;
    }
    return offsets;
}

} // namespace

AUREA_TEST(CheckpointGpu, InterruptedExportResumesWithTheSameFramesPtsDurationAndAudio) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 120;
    const ExportSettings settings = checkpoint_settings();
    // Referência: o mesmo export, sem interrupção.
    const std::string reference = std::string(kCheckpointOut) + "/reference.mp4";
    (void)fileio::remove_file(reference);
    NativeExportDetail ref;
    {
        auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
        AUREA_CHECK(r->e.start_export(settings, reference.c_str()).ok());
        const auto p = wait_checkpoint(*r);
        AUREA_CHECK(p.finished); AUREA_CHECK_EQ(p.result, Errc::Ok);
        AUREA_CHECK((p.flags & Engine::kExportResumed) == 0);
        AUREA_CHECK(r->e.export_diagnostics().find("\"checkpoint\":\"on\"") != std::string::npos);
        AUREA_CHECK(checkpoint_files().empty());   // pronto: nada sobra
        const auto decoded = verify_native_export(reference, kFrames, cfg.width, cfg.height, 30, true, &ref);
        AUREA_CHECK(decoded.ok); if (!decoded.ok) return;
    }
    const std::string movie = std::string(kCheckpointOut) + "/resumed.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    // 1) O app vai para segundo plano no quadro ~70: a suspensão interrompe e o
    //    checkpoint fica (não é o usuário cancelando).
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    const auto mid = wait_checkpoint(*r, 70);
    AUREA_CHECK(!mid.finished && mid.framesDone >= 70);
    AUREA_CHECK(suspend_engine(*r));
    const auto suspended = r->e.export_progress();
    AUREA_CHECK(suspended.finished); AUREA_CHECK_EQ(suspended.result, Errc::Cancelled);
    AUREA_CHECK((suspended.flags & Engine::kExportResumable) != 0);
    AUREA_CHECK(!fileio::exists(movie));
    AUREA_CHECK(!staged_leftovers(movie));
    AUREA_CHECK(!checkpoint_log().empty());
    AUREA_CHECK(r->e.resume().ok());
    // 2) A consulta do app (o mesmo pedido) acha o ponto: o último GOP íntegro.
    const auto candidate = r->e.export_resume_candidate(settings);
    AUREA_CHECK(candidate.valid);
    AUREA_CHECK(candidate.resumeFrame >= 45 && candidate.resumeFrame % 15 == 0);
    AUREA_CHECK_EQ(candidate.totalFrames, kFrames);
    AUREA_CHECK_EQ(candidate.safeMode, 0u);
    // 3) O MESMO export continua do checkpoint e publica o arquivo inteiro.
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    const auto started = r->e.export_progress();
    AUREA_CHECK_EQ(started.framesDone, candidate.resumeFrame);
    AUREA_CHECK((started.flags & Engine::kExportResumed) != 0);
    AUREA_CHECK(r->e.export_diagnostics().find("\"checkpoint\":\"resumed\"") != std::string::npos);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    if (done.result != Errc::Ok) std::printf("\n    retomada falhou: %s\n", done.message);
    AUREA_CHECK_EQ(done.framesDone, kFrames);
    AUREA_CHECK(checkpoint_files().empty());
    AUREA_CHECK(!staged_leftovers(movie));
    NativeExportDetail got;
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true, &got);
    AUREA_CHECK(decoded.ok); if (!decoded.ok) return;
    AUREA_CHECK_EQ(got.grayFrames.size(), ref.grayFrames.size());
    AUREA_CHECK(got.ptsUs == ref.ptsUs);
    AUREA_CHECK_EQ(got.videoDurationUs, ref.videoDurationUs);
    AUREA_CHECK_EQ(got.audioSamples, ref.audioSamples);
    f64 worst = 99.0;
    for (usize i = 0; i < std::min(got.grayFrames.size(), ref.grayFrames.size()); ++i) {
        if (i < candidate.resumeFrame) AUREA_CHECK(got.grayFrames[i] == ref.grayFrames[i]);
        worst = std::min(worst, psnr(got.grayFrames[i], ref.grayFrames[i]));
    }
    std::printf("\n    retomada no quadro %u de %u; pior PSNR(Y) contra o export sem interrupcao: %.1f dB",
                candidate.resumeFrame, kFrames, worst);
    AUREA_CHECK(worst >= 38.0);
}

AUREA_TEST(CheckpointGpu, CorruptLogTailResumesFromThePreviousGop) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 120;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/corrupt-tail.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    (void)wait_checkpoint(*r, 75);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    const std::string log = checkpoint_log();
    AUREA_CHECK(!log.empty()); if (log.empty()) return;
    export_log::ScanResult before;
    AUREA_CHECK(export_log::scan(log, before).ok());
    AUREA_CHECK(before.resumeFrame >= 30);
    // O processo "morreu" escrevendo: um byte do GOP que termina no ponto de
    // retomada não confere mais e o manifesto ficou "rodando".
    const std::vector<u64> offsets = packet_offsets(log);
    const u32 damaged = before.resumeFrame - 3;
    AUREA_CHECK(offsets.size() > damaged); if (offsets.size() <= damaged) return;
    {
        std::FILE* f = fileio::open_file(log, "rb+");
        AUREA_CHECK(f != nullptr); if (!f) return;
        std::fseek(f, static_cast<long>(offsets[damaged] + 24 + 1), SEEK_SET);
        const int c = std::fgetc(f);
        std::fseek(f, static_cast<long>(offsets[damaged] + 24 + 1), SEEK_SET);
        std::fputc(c ^ 0x5a, f);
        std::fclose(f);
    }
    export_checkpoint::Manifest m;
    AUREA_CHECK(export_checkpoint::read_manifest(log, m));
    m.state = export_checkpoint::SessionState::Running;
    AUREA_CHECK(export_checkpoint::write_manifest(log, m).ok());
    const u32 previousGop = before.resumeFrame - 15;
    const auto candidate = r->e.export_resume_candidate(settings);
    AUREA_CHECK(candidate.valid);
    AUREA_CHECK_EQ(candidate.resumeFrame, previousGop);
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, previousGop);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    if (done.result != Errc::Ok) std::printf("\n    retomada da cauda corrompida falhou: %s\n", done.message);
    AUREA_CHECK(checkpoint_files().empty());
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true);
    AUREA_CHECK(decoded.ok);
}

AUREA_TEST(CheckpointGpu, ChangedProjectOrOptionsRestartFromZeroAndUserCancelLeavesNothing) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 120;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/changed.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    auto interrupt = [&] {
        AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
        (void)wait_checkpoint(*r, 50);
        AUREA_CHECK(suspend_engine(*r));
        AUREA_CHECK(r->e.resume().ok());
        AUREA_CHECK(r->e.export_resume_candidate(settings).valid);
    };
    // Opções diferentes (qualidade): outro pedido, outro checkpoint; o antigo sai.
    interrupt();
    const std::string oldLog = checkpoint_log();
    ExportSettings other = settings;
    other.quality = 2;
    AUREA_CHECK(!r->e.export_resume_candidate(other).valid);
    AUREA_CHECK(r->e.start_export(other, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, 0u);
    AUREA_CHECK((r->e.export_progress().flags & Engine::kExportResumed) == 0);
    AUREA_CHECK(!fileio::exists(oldLog));
    // Cancelar (o usuário) não é pausa: nada publicado e nenhum checkpoint.
    (void)wait_checkpoint(*r, 20);
    AUREA_CHECK(r->e.cancel_export().ok());
    const auto cancelled = wait_checkpoint(*r);
    AUREA_CHECK_EQ(cancelled.result, Errc::Cancelled);
    AUREA_CHECK((cancelled.flags & Engine::kExportResumable) == 0);
    AUREA_CHECK(checkpoint_files().empty());
    AUREA_CHECK(!fileio::exists(movie));
    // Projeto editado depois da interrupção: recomeça do zero.
    interrupt();
    r->comp()->layer(r->video_layer())->transform.opacity = .5f;
    AUREA_CHECK(!r->e.export_resume_candidate(settings).valid);
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, 0u);
    (void)wait_checkpoint(*r, 20);
    AUREA_CHECK(r->e.cancel_export().ok());
    (void)wait_checkpoint(*r);
    AUREA_CHECK(checkpoint_files().empty());
}

AUREA_TEST(CheckpointGpu, UserCancelDuringAResumedExportPublishesNothingAndRemovesTheLog) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 120;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/cancel-resumed.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    (void)wait_checkpoint(*r, 50);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    const auto resumed = r->e.export_progress();
    AUREA_CHECK(resumed.framesDone > 0 && (resumed.flags & Engine::kExportResumed) != 0);
    (void)wait_checkpoint(*r, resumed.framesDone + 10);
    AUREA_CHECK(r->e.cancel_export().ok());
    const auto cancelled = wait_checkpoint(*r);
    AUREA_CHECK(cancelled.finished); AUREA_CHECK_EQ(cancelled.result, Errc::Cancelled);
    AUREA_CHECK((cancelled.flags & Engine::kExportResumable) == 0);
    AUREA_CHECK(!fileio::exists(movie));
    AUREA_CHECK(!staged_leftovers(movie));
    AUREA_CHECK(checkpoint_files().empty());
    AUREA_CHECK(!r->e.export_resume_candidate(settings).valid);
}

AUREA_TEST(CheckpointGpu, LowFreeSpaceExportsWithoutTheLogAndStillSucceeds) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 60;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/low-space.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    struct Restore { ~Restore() { export_checkpoint::set_available_bytes_for_tests(0); } } restore;
    export_checkpoint::set_available_bytes_for_tests(1);   // 1 byte livre para o checkpoint
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK(r->e.export_diagnostics().find("\"checkpoint\":\"off_low_storage\"") != std::string::npos);
    // Sem registro: a suspensão não deixa nada para continuar.
    (void)wait_checkpoint(*r, 30);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK((r->e.export_progress().flags & Engine::kExportResumable) == 0);
    AUREA_CHECK(checkpoint_files().empty());
    AUREA_CHECK(r->e.resume().ok());
    // O export inteiro pelo MP4 direto: o mesmo vídeo, validado.
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, 0u);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    AUREA_CHECK(checkpoint_files().empty());
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true);
    AUREA_CHECK(decoded.ok);
}

AUREA_TEST(CheckpointGpu, EncoderFailureMidStreamResumesWithTheSameEncoder) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 120;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/encoder-failure.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames); AUREA_CHECK(r->ok); if (!r->ok) return;
    ckptFailAt = 80;
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    const auto failed = wait_checkpoint(*r);
    AUREA_CHECK(failed.finished); AUREA_CHECK_EQ(failed.result, Errc::EncodeFailed);
    AUREA_CHECK((failed.flags & Engine::kExportResumable) != 0);
    AUREA_CHECK(!fileio::exists(movie));
    const auto candidate = r->e.export_resume_candidate(settings);
    // O último GOP inteiro antes do quadro 80 (o encoder pode segurar alguns quadros).
    AUREA_CHECK(candidate.valid && candidate.resumeFrame >= 60 && candidate.resumeFrame <= 75 &&
                candidate.resumeFrame % 15 == 0);
    // A tela repete a MESMA tentativa (mesmo degrau, mesmo encoder).
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, candidate.resumeFrame);
    AUREA_CHECK((r->e.export_progress().flags & Engine::kExportResumed) != 0);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    AUREA_CHECK(checkpoint_files().empty());
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true);
    AUREA_CHECK(decoded.ok);
}


// --- Fontes por URI (Android content://) --------------------------------------
// A identidade vem da plataforma (EngineConfig::exportSourceIdentity), pelo
// mesmo descritor que o export usaria. Aqui um arquivo de verdade faz o papel
// do provedor; o modo escolhe: verificável, não verificável, ou "segura até
// cancelar" (o "Preparando" lento e cancelável, sem depender de relógio).
namespace {
std::atomic<int> ckptUriMode{0};   // 0 verificável, 1 não verificável, 2 segura até cancelar
std::string ckptUriBacking;
std::atomic<bool> ckptUriEntered{false};
Status checkpoint_uri_identity(const std::string& uri, export_checkpoint::SourceIdentity& out,
                               const std::atomic<bool>* cancel) noexcept {
    if (uri.rfind("content://", 0) != 0) return Errc::NotSupported;
    ckptUriEntered = true;
    switch (ckptUriMode.load()) {
        case 1: return Status{Errc::NotSupported, "provedor sem arquivo regular (teste)"};
        case 2: {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!(cancel && cancel->load())) {
                if (std::chrono::steady_clock::now() > deadline) return Errc::Timeout;
                std::this_thread::yield();
            }
            return Errc::Cancelled;
        }
        default: break;
    }
#if defined(_WIN32)
    const int fd = _open(ckptUriBacking.c_str(), _O_RDONLY | _O_BINARY);
#else
    const int fd = ::open(ckptUriBacking.c_str(), O_RDONLY);
#endif
    if (fd < 0) return Errc::MediaSourceMissing;
    const Status s = export_checkpoint::fd_identity(fd, out, cancel);
#if defined(_WIN32)
    _close(fd);
#else
    ::close(fd);
#endif
    return s;
}
struct UriRig {
    std::string backing;
    explicit UriRig(const char* name) {
        backing = std::string(kCheckpointOut) + "/" + name;
        std::vector<u8> bytes(5u << 20);
        for (usize i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<u8>(i * 13u + 1u);
        (void)fileio::write_atomic(backing, bytes.data(), bytes.size());
        ckptUriBacking = backing; ckptUriMode = 0; ckptUriEntered = false;
        gRigSourceIdentity = &checkpoint_uri_identity;
    }
    ~UriRig() { gRigSourceIdentity = nullptr; ckptUriMode = 0; (void)fileio::remove_file(backing); }
};
const char* const kUriSource = "content://aurea.test/media/clip.mp4";
} // namespace

AUREA_TEST(CheckpointGpu, UriSourceResumesOnlyWhenThePlatformVerifiesIt) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    UriRig uri("uri-backing.bin");
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 90;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/uri-source.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames, kUriSource); AUREA_CHECK(r->ok); if (!r->ok) return;
    // Verificável (arquivo regular atrás da URI): checkpoint e retomada normais.
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK(ckptUriEntered.load());
    AUREA_CHECK(r->e.export_diagnostics().find("\"checkpoint\":\"on\"") != std::string::npos);
    (void)wait_checkpoint(*r, 50);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    AUREA_CHECK(r->e.export_resume_candidate(settings).valid);
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK(r->e.export_progress().framesDone > 0);
    (void)wait_checkpoint(*r, 70);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    // O conteúdo atrás da URI mudou (mesmo tamanho, um byte amostrado): outra
    // identidade, nada continua.
    {
        std::FILE* f = fileio::open_file(uri.backing, "rb+");
        AUREA_CHECK(f != nullptr);
        if (f) { std::fseek(f, 3, SEEK_SET); std::fputc(0x77, f); std::fclose(f); }
    }
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, 0u);
    (void)wait_checkpoint(*r, 50);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    AUREA_CHECK(!checkpoint_log().empty());
    // Não verificável (pipe, provedor virtual): o export sai inteiro pelo MP4
    // direto, sem checkpoint, e o que havia não é continuado.
    ckptUriMode = 1;
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, 0u);
    AUREA_CHECK(r->e.export_diagnostics().find("\"checkpoint\":\"off_unverifiable_source\"") != std::string::npos);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    AUREA_CHECK((done.flags & Engine::kExportResumed) == 0);
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true);
    AUREA_CHECK(decoded.ok);
}

AUREA_TEST(CheckpointGpu, PreparingSourceIdentityIsCancellable) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    UriRig uri("uri-cancel.bin");
    ckptUriMode = 2;   // a plataforma só volta quando o cancelamento chegar
    auto r = checkpoint_rig(checkpoint_config(), 60, kUriSource); AUREA_CHECK(r->ok); if (!r->ok) return;
    const std::string movie = std::string(kCheckpointOut) + "/cancel-preparing.mp4";
    std::atomic<bool> sawPreparing{false};
    std::thread watcher([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (ckptUriEntered.load() && std::strncmp(r->e.export_progress().message, "preparando fontes", 17) == 0) {
                sawPreparing = true;
                (void)r->e.cancel_export();
                return;
            }
            std::this_thread::yield();
        }
    });
    const Status started = r->e.start_export(checkpoint_settings(), movie.c_str());
    watcher.join();
    AUREA_CHECK(sawPreparing.load());
    AUREA_CHECK_EQ(started.code(), Errc::Cancelled);
    AUREA_CHECK(!fileio::exists(movie));
    AUREA_CHECK(checkpoint_files().empty());
}

// iOS: o app vai para segundo plano ainda no "Preparando" de um export que
// continuaria um checkpoint. pause_export para a partida como a suspensão (a
// GPU não pode ser usada lá) e NÃO apaga o checkpoint; o mesmo export, refeito
// ao voltar, continua dele. (Cancelar do usuário apaga.)
AUREA_TEST(CheckpointGpu, PauseWhilePreparingKeepsTheCheckpointForTheSameExport) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    std::filesystem::create_directories(kCheckpointOut);
    clear_checkpoints();
    UriRig uri("uri-pause.bin");
    const SyntheticConfig cfg = checkpoint_config();
    constexpr u32 kFrames = 90;
    const ExportSettings settings = checkpoint_settings();
    const std::string movie = std::string(kCheckpointOut) + "/pause-preparing.mp4";
    (void)fileio::remove_file(movie);
    auto r = checkpoint_rig(cfg, kFrames, kUriSource); AUREA_CHECK(r->ok); if (!r->ok) return;
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    (void)wait_checkpoint(*r, 50);
    AUREA_CHECK(suspend_engine(*r));
    AUREA_CHECK(r->e.resume().ok());
    const auto saved = r->e.export_resume_candidate(settings);
    AUREA_CHECK(saved.valid && saved.resumeFrame >= 30);
    ckptUriMode = 2; ckptUriEntered = false;
    std::atomic<bool> paused{false};
    std::thread watcher([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (ckptUriEntered.load() && std::strncmp(r->e.export_progress().message, "preparando fontes", 17) == 0) {
                paused = r->e.pause_export().ok();
                return;
            }
            std::this_thread::yield();
        }
    });
    const Status started = r->e.start_export(settings, movie.c_str());
    watcher.join();
    AUREA_CHECK(paused.load());
    AUREA_CHECK_EQ(started.code(), Errc::Cancelled);
    AUREA_CHECK(!fileio::exists(movie));
    ckptUriMode = 0;
    const auto kept = r->e.export_resume_candidate(settings);
    AUREA_CHECK(kept.valid); AUREA_CHECK_EQ(kept.resumeFrame, saved.resumeFrame);
    // Voltou: o mesmo export continua do checkpoint.
    AUREA_CHECK(r->e.start_export(settings, movie.c_str()).ok());
    AUREA_CHECK_EQ(r->e.export_progress().framesDone, saved.resumeFrame);
    const auto done = wait_checkpoint(*r);
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
    AUREA_CHECK(checkpoint_files().empty());
    const auto decoded = verify_native_export(movie, kFrames, cfg.width, cfg.height, 30, true);
    AUREA_CHECK(decoded.ok);
}
#endif
