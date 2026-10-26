#include "TestFramework.hpp"
#include "aurea/export/ExportPacketLog.hpp"
#include "aurea/project/FileIO.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;
namespace plog = aurea::export_log;

namespace {
struct LogFile {
    std::string path;
    explicit LogFile(const char* name) : path(std::string("aurea_test_pktlog_") + name) { fileio::remove_file(path); }
    ~LogFile() { fileio::remove_file(path); }
};
plog::StreamInfo stream(u64 key = 77) {
    plog::StreamInfo info;
    info.codec = 0; info.width = 64; info.height = 36;
    info.rateNum = 30; info.rateDen = 1; info.timebaseNum = 1; info.timebaseDen = 30000;
    info.sessionKey = key;
    info.extradata = {1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 9, 9, 9, 9};
    return info;
}
std::vector<u8> payload(u32 frame) {
    std::vector<u8> data(100 + frame * 7 % 50);
    for (usize i = 0; i < data.size(); ++i) data[i] = static_cast<u8>(frame * 31 + i);
    return data;
}
// GOP de 10 quadros: 0, 10, 20... são keyframes.
bool key(u32 frame) { return frame % 10 == 0; }
void write_frames(plog::Writer& writer, u32 from, u32 to) {
    for (u32 f = from; f < to; ++f) {
        const auto data = payload(f);
        AUREA_CHECK(writer.append(f, data.data(), data.size(), key(f)).ok());
    }
}
}

AUREA_TEST(ExportPacketLog, RoundTripKeepsEveryPacketAndTheEncoderParameters) {
    LogFile file("roundtrip");
    plog::Writer writer;
    AUREA_CHECK(writer.create(file.path, stream()).ok());
    write_frames(writer, 0, 35);
    AUREA_CHECK_EQ(writer.durable_frames(), 30u); // fsync antes do keyframe 30
    AUREA_CHECK(writer.close().ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 35u);
    AUREA_CHECK_EQ(scan.resumeFrame, 30u);
    AUREA_CHECK_EQ(scan.keyframes, 4u);
    AUREA_CHECK(scan.info.same_stream(stream()));
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    AUREA_CHECK(reader.info().extradata == stream().extradata);
    plog::Packet packet; bool end = false; u32 count = 0;
    while (reader.next(packet, end).ok() && !end) {
        AUREA_CHECK_EQ(packet.frame, count);
        AUREA_CHECK_EQ(packet.key, key(count));
        AUREA_CHECK(packet.data == payload(count));
        ++count;
    }
    AUREA_CHECK(end);
    AUREA_CHECK_EQ(count, 35u);
}

AUREA_TEST(ExportPacketLog, TruncatedTailResumesAtTheLastCompleteGop) {
    LogFile file("truncated");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 27);
        AUREA_CHECK(writer.close().ok());
    }
    // O app morreu no meio da escrita do quadro 26: o arquivo termina no meio dele.
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    bytes.resize(bytes.size() - 40);
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 26u);
    AUREA_CHECK_EQ(scan.resumeFrame, 20u);
    // Continua com uma sessão nova: o quadro 20 precisa ser keyframe.
    plog::Writer writer;
    AUREA_CHECK(writer.resume(file.path, stream(), scan).ok());
    AUREA_CHECK_EQ(writer.frames(), 20u);
    const auto notKey = payload(20);
    AUREA_CHECK_EQ(writer.append(20, notKey.data(), notKey.size(), false).code(), Errc::CorruptData);
    AUREA_CHECK(writer.resume(file.path, stream(), scan).ok());
    write_frames(writer, 20, 40);
    AUREA_CHECK(writer.close().ok());
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 40u);
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    plog::Packet packet; bool end = false; u32 count = 0;
    while (reader.next(packet, end).ok() && !end) { AUREA_CHECK(packet.data == payload(count)); ++count; }
    AUREA_CHECK_EQ(count, 40u);
}

AUREA_TEST(ExportPacketLog, CorruptPacketDiscardsItsGopAndEverythingAfter) {
    LogFile file("corrupt");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 30);
        AUREA_CHECK(writer.close().ok());
    }
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    // Um byte trocado no fim do arquivo (dentro do quadro 29... ou antes):
    // o pacote atingido e tudo depois dele saem; volta ao keyframe anterior.
    bytes[bytes.size() - 500] ^= 0x5a;
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK(scan.validFrames < 30u);
    AUREA_CHECK(scan.resumeFrame <= scan.validFrames);
    AUREA_CHECK_EQ(scan.resumeFrame % 10, 0u);
    plog::Reader reader;
    AUREA_CHECK(reader.open(file.path).ok());
    plog::Packet packet; bool end = false; Status last = OkStatus; u32 good = 0;
    for (;;) { last = reader.next(packet, end); if (!last.ok() || end) break; ++good; }
    AUREA_CHECK_EQ(last.code(), Errc::CorruptData); // a leitura nunca entrega o pacote ruim
    AUREA_CHECK_EQ(good, scan.validFrames);
}

AUREA_TEST(ExportPacketLog, AnotherExportOrEncoderNeverContinuesTheLog) {
    LogFile file("mismatch");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream(1)).ok());
        write_frames(writer, 0, 25);
        AUREA_CHECK(writer.close().ok());
    }
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    plog::Writer writer;
    AUREA_CHECK_EQ(writer.resume(file.path, stream(2), scan).code(), Errc::UnsupportedFormat);
    auto otherSps = stream(1); otherSps.extradata.back() ^= 1;
    AUREA_CHECK_EQ(writer.resume(file.path, otherSps, scan).code(), Errc::UnsupportedFormat);
    // Nada foi cortado pelas tentativas recusadas.
    plog::ScanResult again;
    AUREA_CHECK(plog::scan(file.path, again).ok());
    AUREA_CHECK_EQ(again.validFrames, 25u);
}

AUREA_TEST(ExportPacketLog, LogWithoutACompleteGopRestartsFromZero) {
    LogFile file("short");
    {
        plog::Writer writer;
        AUREA_CHECK(writer.create(file.path, stream()).ok());
        write_frames(writer, 0, 6);
        AUREA_CHECK(writer.close().ok());
    }
    plog::ScanResult scan;
    AUREA_CHECK(plog::scan(file.path, scan).ok());
    AUREA_CHECK_EQ(scan.validFrames, 6u);
    AUREA_CHECK_EQ(scan.resumeFrame, 0u);
    plog::Writer writer;
    AUREA_CHECK_EQ(writer.resume(file.path, stream(), scan).code(), Errc::InvalidState);
    // Cabeçalho corrompido: não é um registro.
    std::vector<u8> bytes;
    AUREA_CHECK(fileio::read_all(file.path, bytes, 1u << 20));
    bytes[20] ^= 0xff;
    AUREA_CHECK(fileio::write_atomic(file.path, bytes.data(), bytes.size()).ok());
    AUREA_CHECK_EQ(plog::scan(file.path, scan).code(), Errc::CorruptData);
    AUREA_CHECK_EQ(plog::scan(file.path + ".nao-existe", scan).code(), Errc::NotFound);
}

// =============================================================================
//  CheckpointManager (export/ExportCheckpoint.hpp): identidade e manifesto.
// =============================================================================
#include "aurea/export/ExportCheckpoint.hpp"
#include "aurea/project/Serialization.hpp"
#include <filesystem>

namespace ckpt = aurea::export_checkpoint;
namespace {
std::vector<u8> encoded(const Project& project) {
    std::vector<u8> bytes;
    SaveOptions save; save.writeThumbnail = false;
    (void)ProjectSerializer::encode(project, save, bytes);
    return bytes;
}
Project checkpoint_project() { auto made = Project::create_new(320, 180, 30, "checkpoint"); return std::move(*made); }
}

// O que não aparece no vídeo (datas, playhead, zoom/seleção, ajustes guardados)
// não muda a chave; o conteúdo e as configurações pedidas mudam. O degrau da
// escada não entra no pedido (o manifesto é que o informa).
AUREA_TEST(ExportCheckpoint, ProjectAndRequestKeysIgnoreUiStateButNotContent) {
    Project p = checkpoint_project();
    const u64 base = ckpt::project_key(encoded(p));
    AUREA_CHECK(base != 0);
    p.metadata().modifiedUnixMs += 123456;
    p.timeline().set_playhead(FrameIndex{17});
    p.editor_settings().timelineZoom = 3.0f;
    p.export_settings().quality = 2;
    AUREA_CHECK_EQ(ckpt::project_key(encoded(p)), base);
    // Gravar e reabrir (o app morreu e voltou): a mesma chave.
    {
        Project reopened;
        const std::vector<u8> bytes = encoded(p);
        AUREA_CHECK(ProjectSerializer::load_bytes(reopened, bytes.data(), bytes.size(), LoadOptions{}, nullptr).ok());
        AUREA_CHECK_EQ(ckpt::project_key(encoded(reopened)), base);
    }
    Composition* comp = p.timeline().composition(p.timeline().root());
    AUREA_CHECK(comp != nullptr); if (!comp) return;
    (void)comp->add_layer(LayerKind::Shape, "novo");
    const u64 edited = ckpt::project_key(encoded(p));
    AUREA_CHECK(edited != base && edited != 0);
    AUREA_CHECK_EQ(ckpt::project_key({}), u64{0});
    ExportSettings s; s.height = 1080;
    const CompositionId root = p.timeline().root();
    const u64 request = ckpt::request_key(edited, root, s);
    ExportSettings rung = s; rung.safeMode = 3;
    AUREA_CHECK_EQ(ckpt::request_key(edited, root, rung), request);
    ExportSettings faster = s; faster.quality = 2;
    AUREA_CHECK(ckpt::request_key(edited, root, faster) != request);
    ExportSettings hevc = s; hevc.videoCodec = ExportCodec::HEVC;
    AUREA_CHECK(ckpt::request_key(edited, root, hevc) != request);
    AUREA_CHECK(ckpt::request_key(base, root, s) != request);
    AUREA_CHECK_EQ(ckpt::request_key(0, root, s), u64{0});
}

// O manifesto vai e volta; a consulta do app só aceita um checkpoint com GOP
// íntegro, do mesmo pedido, com progresso desde a última continuação e dentro
// do limite de continuações por falha. Um export novo apaga os de outra chave.
AUREA_TEST(ExportCheckpoint, ManifestCandidateAndCleanupFollowTheResumeRules) {
    const std::string dir = "aurea_test_ckpt_dir";
    std::filesystem::remove_all(dir);
    AUREA_CHECK_EQ(ckpt::directory(dir, {}, {}), dir);
    const u64 key = 0x1234abcdull, request = 0x9999ull;
    const std::string log = ckpt::log_path(dir, key);
    plog::StreamInfo info = stream(key);
    info.totalFrames = 40;
    {
        plog::Writer w;
        AUREA_CHECK(w.create(log, info).ok());
        for (u32 f = 0; f < 25; ++f) {
            const auto data = payload(f);
            AUREA_CHECK(w.append(f, data.data(), data.size(), f % 10 == 0).ok());
        }
        AUREA_CHECK(w.close().ok());
    }
    ckpt::Manifest m;
    m.sessionKey = key; m.requestKey = request; m.safeMode = 1; m.totalFrames = 40;
    m.state = ckpt::SessionState::Paused; m.failureResumes = 0; m.lastResumeFrame = 0;
    AUREA_CHECK(ckpt::write_manifest(log, m).ok());
    ckpt::Manifest back;
    AUREA_CHECK(ckpt::read_manifest(log, back));
    AUREA_CHECK_EQ(back.sessionKey, key); AUREA_CHECK_EQ(back.requestKey, request);
    AUREA_CHECK_EQ(back.safeMode, 1u); AUREA_CHECK_EQ(back.totalFrames, 40u);
    AUREA_CHECK(back.state == ckpt::SessionState::Paused);
    auto c = ckpt::find_candidate(dir, request);
    AUREA_CHECK(c.valid); AUREA_CHECK_EQ(c.resumeFrame, 20u); AUREA_CHECK_EQ(c.safeMode, 1u); AUREA_CHECK_EQ(c.totalFrames, 40u);
    AUREA_CHECK(!ckpt::find_candidate(dir, request + 1).valid);
    // A sessão que partiu do quadro 20 morreu sem fechar outro GOP: sem progresso.
    m.state = ckpt::SessionState::Running; m.lastResumeFrame = 20;
    AUREA_CHECK(ckpt::write_manifest(log, m).ok());
    AUREA_CHECK(!ckpt::find_candidate(dir, request).valid);
    // Progresso, mas as continuações por falha acabaram.
    m.lastResumeFrame = 10; m.failureResumes = ckpt::kMaxFailureResumes;
    AUREA_CHECK(ckpt::write_manifest(log, m).ok());
    AUREA_CHECK(!ckpt::find_candidate(dir, request).valid);
    m.failureResumes = ckpt::kMaxFailureResumes - 1;
    AUREA_CHECK(ckpt::write_manifest(log, m).ok());
    AUREA_CHECK(ckpt::find_candidate(dir, request).valid);
    // Limpeza: só os arquivos do CheckpointManager, exceto o que fica.
    const std::string otherLog = ckpt::log_path(dir, key + 1);
    AUREA_CHECK(fileio::write_atomic(otherLog, "x", 1).ok());
    AUREA_CHECK(fileio::write_atomic(ckpt::spill_path(otherLog), "x", 1).ok());
    AUREA_CHECK(fileio::write_atomic(dir + "/video-do-usuario.mp4", "x", 1).ok());
    AUREA_CHECK_EQ(ckpt::remove_others(dir, log), 1u);
    AUREA_CHECK(!fileio::exists(otherLog) && !fileio::exists(ckpt::spill_path(otherLog)));
    AUREA_CHECK(fileio::exists(log) && fileio::exists(ckpt::manifest_path(log)));
    AUREA_CHECK(fileio::exists(dir + "/video-do-usuario.mp4"));
    ckpt::remove(log);
    AUREA_CHECK(!fileio::exists(log) && !fileio::exists(ckpt::manifest_path(log)));
    AUREA_CHECK(!ckpt::find_candidate(dir, request).valid);
    std::filesystem::remove_all(dir);
}

AUREA_TEST(ExportCheckpoint, LowFreeSpaceDisablesTheLogBeforeTheDirectExportDoes) {
    const u64 video = 1000ull << 20;   // ~1 GB de vídeo estimado
    // Registro inteiro + MP4 montado (metade, a folga do caminho direto) + 64 MB.
    AUREA_CHECK(ckpt::log_fits(1100ull << 20, video, 0, 0));
    AUREA_CHECK(!ckpt::log_fits(900ull << 20, video, 0, 0));
    // O PCM do render também ocupa; o que o registro já gravou não conta de novo.
    AUREA_CHECK(!ckpt::log_fits(1100ull << 20, video, 400ull << 20, 0));
    AUREA_CHECK(ckpt::log_fits(700ull << 20, video, 0, 800ull << 20));
    // O caminho direto (Engine) recusa abaixo de video/2 + 32 MB: entre os dois,
    // o export sai sem checkpoint em vez de falhar.
    AUREA_CHECK(!ckpt::log_fits((video / 2) + (40ull << 20), video, 0, 0));
}

// Identidade LIMITADA das fontes (chave do checkpoint): custo constante por
// arquivo (~3 MiB lidos de um arquivo de 512 MB), e ainda muda quando um byte
// dentro de um bloco amostrado muda, quando o tamanho muda e quando a data muda.
// O descritor (o caminho do content:// do Android) dá a mesma amostra.
#include <chrono>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
AUREA_TEST(ExportCheckpoint, SampledSourceIdentityIsBoundedAndSeesSampledBytesSizeAndTime) {
    namespace fs = std::filesystem;
    const std::string path = "aurea_test_ckpt_identity_512mb.bin";
    struct Cleanup { std::string p; ~Cleanup() { (void)fileio::remove_file(p); } } cleanup{path};
    constexpr u64 kSize = 512ull << 20;
    {
        std::vector<u8> block(4u << 20);
        for (usize i = 0; i < block.size(); ++i) block[i] = static_cast<u8>((i * 2654435761u) >> 13);
        std::FILE* f = fileio::open_file(path, "wb");
        AUREA_CHECK(f != nullptr); if (!f) return;
        for (u64 written = 0; written < kSize; written += block.size()) {
            block[0] = static_cast<u8>(written >> 22);   // cada bloco de 4 MiB diferente
            AUREA_CHECK_EQ(std::fwrite(block.data(), 1, block.size(), f), block.size());
        }
        std::fclose(f);
    }
    std::atomic<u64> done{0};
    ckpt::SourceIdentity first;
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(ckpt::file_identity(path, first, nullptr, &done).ok());
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n    identidade amostrada de 512 MB: %.2f ms, %llu KiB lidos", ms,
                static_cast<unsigned long long>(done.load() >> 10));
    AUREA_CHECK_EQ(first.bytes, kSize);
    AUREA_CHECK_EQ(done.load(), ckpt::sampled_bytes(kSize));
    AUREA_CHECK(done.load() <= (3ull << 20));
    AUREA_CHECK(first.sampleHash != 0 && first.modified != 0);
    // Determinística; o descritor dá a mesma amostra e o mesmo tamanho.
    ckpt::SourceIdentity again;
    AUREA_CHECK(ckpt::file_identity(path, again).ok());
    AUREA_CHECK(again.sampleHash == first.sampleHash && again.modified == first.modified);
    {
#if defined(_WIN32)
        const int fd = _open(path.c_str(), _O_RDONLY | _O_BINARY);
#else
        const int fd = ::open(path.c_str(), O_RDONLY);
#endif
        AUREA_CHECK(fd >= 0);
        if (fd >= 0) {
            ckpt::SourceIdentity byFd;
            AUREA_CHECK(ckpt::fd_identity(fd, byFd).ok());
            AUREA_CHECK_EQ(byFd.bytes, first.bytes);
            AUREA_CHECK_EQ(byFd.sampleHash, first.sampleHash);
            AUREA_CHECK(byFd.modified != 0);
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
        }
    }
    std::error_code ec;
    const auto stamp = fs::last_write_time(fs::u8path(path), ec);
    AUREA_CHECK(!ec);
    auto poke = [&](u64 at, u8 value) {
        std::FILE* f = fileio::open_file(path, "rb+");
        AUREA_CHECK(f != nullptr); if (!f) return;
        std::fseek(f, static_cast<long>(at), SEEK_SET);
        std::fputc(value, f);
        std::fclose(f);
        fs::last_write_time(fs::u8path(path), stamp, ec);   // a data volta: só o byte muda
    };
    auto read_byte = [&](u64 at) {
        std::FILE* f = fileio::open_file(path, "rb");
        std::fseek(f, static_cast<long>(at), SEEK_SET);
        const int c = std::fgetc(f);
        std::fclose(f);
        return static_cast<u8>(c);
    };
    // Um byte dentro de um bloco amostrado (o centro do 5o bloco de 64 KiB).
    const u64 sampled = kSize / (ckpt::kSampleBlocks + 1) * 5;
    const u8 original = read_byte(sampled);
    poke(sampled, static_cast<u8>(original ^ 0x5a));
    ckpt::SourceIdentity changed;
    AUREA_CHECK(ckpt::file_identity(path, changed).ok());
    AUREA_CHECK_EQ(changed.bytes, first.bytes);
    AUREA_CHECK_EQ(changed.modified, first.modified);
    AUREA_CHECK(changed.sampleHash != first.sampleHash);
    AUREA_CHECK(ckpt::mix_identity(7, changed) != ckpt::mix_identity(7, first));
    poke(sampled, original);
    AUREA_CHECK(ckpt::file_identity(path, again).ok());
    AUREA_CHECK_EQ(again.sampleHash, first.sampleHash);
    // Só a data muda.
    fs::last_write_time(fs::u8path(path), stamp + std::chrono::seconds(10), ec);
    AUREA_CHECK(!ec);
    AUREA_CHECK(ckpt::file_identity(path, changed).ok());
    AUREA_CHECK_EQ(changed.sampleHash, first.sampleHash);
    AUREA_CHECK(changed.modified != first.modified);
    AUREA_CHECK(ckpt::mix_identity(7, changed) != ckpt::mix_identity(7, first));
    // O tamanho muda (um byte a mais no fim).
    {
        std::FILE* f = fileio::open_file(path, "ab");
        AUREA_CHECK(f != nullptr);
        if (f) { std::fputc(1, f); std::fclose(f); }
        fs::last_write_time(fs::u8path(path), stamp, ec);
    }
    AUREA_CHECK(ckpt::file_identity(path, changed).ok());
    AUREA_CHECK_EQ(changed.bytes, kSize + 1);
    AUREA_CHECK(changed.sampleHash != first.sampleHash);
    // Cancelado antes do primeiro bloco: nada lido.
    std::atomic<bool> cancel{true};
    done = 0;
    AUREA_CHECK_EQ(ckpt::file_identity(path, changed, &cancel, &done).code(), Errc::Cancelled);
    AUREA_CHECK_EQ(done.load(), u64{0});
    // Arquivo pequeno (abaixo de 3 MiB): lido inteiro, qualquer byte conta.
    const std::string small = "aurea_test_ckpt_identity_small.bin";
    struct SmallCleanup { std::string p; ~SmallCleanup() { (void)fileio::remove_file(p); } } smallCleanup{small};
    std::vector<u8> bytes(700000, 9);
    AUREA_CHECK(fileio::write_atomic(small, bytes.data(), bytes.size()).ok());
    ckpt::SourceIdentity a, b;
    AUREA_CHECK(ckpt::file_identity(small, a).ok());
    bytes[333333] = 10;
    AUREA_CHECK(fileio::write_atomic(small, bytes.data(), bytes.size()).ok());
    AUREA_CHECK(ckpt::file_identity(small, b).ok());
    AUREA_CHECK(a.sampleHash != b.sampleHash);
    AUREA_CHECK_EQ(ckpt::memory_sample_hash(bytes.data(), bytes.size()), ckpt::memory_sample_hash(bytes.data(), bytes.size()));
    AUREA_CHECK(ckpt::memory_sample_hash(bytes.data(), bytes.size()) != 0);
}
