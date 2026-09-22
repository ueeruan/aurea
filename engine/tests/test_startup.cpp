// =============================================================================
//  Abertura do motor (Fase 8I, §59–63 e §176).
//
//  Mede, na GPU REAL do host, o que a abertura do app paga dentro do motor:
//  Engine::initialize a frio (sem cache de pipeline em disco) e a quente (com
//  o cache que a execução anterior gravou), e quantos pipelines são
//  compilados antes do primeiro quadro.
//
//  Limite honesto: o driver da NVIDIA tem o PRÓPRIO cache de shader em disco,
//  fora do Aurea. "A frio" aqui é "sem o cache do Aurea"; no celular, onde o
//  driver costuma não ter cache global, a diferença é maior.
// =============================================================================
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN)

#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/Effect.hpp"
#include "aurea/render/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

namespace fs = std::filesystem;

std::string startup_dir(const char* name) {
    std::error_code ec;
    fs::path p = fs::temp_directory_path(ec) / "aurea_abertura" / name;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}

std::string cache_file(const std::string& dir) { return dir + "/aurea_pipeline_cache.bin"; }

struct Opened {
    bool ok = false;
    f64 initMs = 0.0;
    f64 firstFrameMs = 0.0;   ///< projeto novo + forma + texto + 1o quadro lido
    u32 prewarmed = 0;
};

/// Sobe um motor com GPU real e cache em `dir`, mede e desliga (o desligar
/// grava o cache, como o app ao sair).
Opened open_engine(const std::string& dir, Engine& e) {
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.backendConfig.framesInFlight = 2;
    ec.cacheDirectory = dir;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    Opened o;
    const auto t0 = std::chrono::steady_clock::now();
    o.ok = e.initialize(ec).ok() && e.gpu() != nullptr;
    o.initMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!o.ok) return o;
    o.prewarmed = e.renderer().pipelines_prewarmed();
    // O primeiro quadro do caso comum (forma + texto): o que a abertura
    // preguiçosa não pode empurrar para cá.
    const auto t1 = std::chrono::steady_clock::now();
    o.ok = e.new_project(1280, 720, 30.0, "abertura").ok() && e.add_shape(0).ok() && e.add_text("Aurea").ok();
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    o.ok = o.ok && e.capture_frame_rgba(256, rgba, w, h).ok();
    o.firstFrameMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t1).count();
    return o;
}

bool gpu_available() {
    static const bool ok = [] {
        vk::Backend b;
        BackendConfig c;
        c.framesInFlight = 1;
        const bool up = b.initialize(c).ok();
        b.shutdown();
        return up;
    }();
    return ok;
}

} // namespace

AUREA_TEST(Startup, ColdAndWarmEngineInitializeAreMeasured) {
    if (!gpu_available()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = startup_dir("fria_quente");

    // A primeira abertura do processo ainda paga carregar o driver: fica fora
    // da conta. Depois, 5 rodadas de (fria sem arquivo → quente com o arquivo
    // que a fria gravou); vale a mediana.
    std::error_code ec;
    {
        Engine e;
        AUREA_CHECK(open_engine(dir, e).ok);
        e.shutdown();
    }
    constexpr int kRounds = 5;
    // [rodada][0 = total, 1 = gpu, 2 = renderer, 3 = resto, 4 = 1o quadro]
    f64 cold[5][kRounds] = {}, warm[5][kRounds] = {};
    u32 prewarmed = 0;
    u64 cacheBytes = 0;
    auto keep = [](f64 (&dst)[5][kRounds], int r, const Opened& o, const Engine::StartupTimings& t) {
        dst[0][r] = o.initMs;
        dst[1][r] = t.gpuMs;
        dst[2][r] = t.rendererMs;
        dst[3][r] = t.jobsMs + t.restMs;
        dst[4][r] = o.firstFrameMs;
    };
    for (int r = 0; r < kRounds; ++r) {
        fs::remove(fs::path(cache_file(dir)), ec);
        {
            Engine e;
            const Opened o = open_engine(dir, e);
            AUREA_CHECK(o.ok);
            keep(cold, r, o, e.startup_timings());
            prewarmed = o.prewarmed;
            e.shutdown();
        }
        cacheBytes = fs::file_size(cache_file(dir), ec);
        AUREA_CHECK(!ec && cacheBytes > 0);
        {
            Engine e;
            const Opened o = open_engine(dir, e);
            AUREA_CHECK(o.ok);
            keep(warm, r, o, e.startup_timings());
            e.shutdown();
        }
    }
    auto med = [](f64* v) { std::sort(v, v + kRounds); return v[kRounds / 2]; };
    for (int k = 0; k < 5; ++k) { (void)med(cold[k]); (void)med(warm[k]); }
    std::printf("\n    Engine::initialize, mediana de %d: fria %.1f ms [%.1f..%.1f], quente %.1f ms [%.1f..%.1f]\n",
                kRounds, cold[0][kRounds / 2], cold[0][0], cold[0][kRounds - 1],
                warm[0][kRounds / 2], warm[0][0], warm[0][kRounds - 1]);
    std::printf("      fria:   gpu %.1f  renderer %.1f  resto %.1f ms\n", cold[1][kRounds / 2], cold[2][kRounds / 2], cold[3][kRounds / 2]);
    std::printf("      quente: gpu %.1f  renderer %.1f  resto %.1f ms\n", warm[1][kRounds / 2], warm[2][kRounds / 2], warm[3][kRounds / 2]);
    std::printf("      %u pipelines antes do 1o quadro; cache %llu KB\n", prewarmed, static_cast<unsigned long long>(cacheBytes / 1024));
    std::printf("      1o quadro (projeto novo + forma + texto): fria %.1f ms, quente %.1f ms\n    ",
                cold[4][kRounds / 2], warm[4][kRounds / 2]);
    fs::remove_all(fs::path(dir), ec);
    // A abertura só pré-aquece o que todo projeto usa (composição, vídeo,
    // forma, vetor, texto, máscara, cor, saída); efeito e 3D ficam de fora.
    AUREA_CHECK(prewarmed >= 15 && prewarmed <= 20);
}

namespace {

using CacheLoad = GPUBackend::PipelineCacheInfo::Load;

/// Abre, lê o que o backend fez com o cache e desliga (grava).
CacheLoad open_and_close(const std::string& dir, u64 tag = 0) {
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.backendConfig.pipelineCacheTag = tag;
    ec.cacheDirectory = dir;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    if (!e.initialize(ec).ok() || !e.gpu()) return CacheLoad::None;
    const CacheLoad load = e.gpu()->pipeline_cache_info().load;
    e.shutdown();
    return load;
}

void overwrite(const std::string& path, const std::vector<u8>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

std::vector<u8> slurp(const std::string& path) {
    std::vector<u8> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    out.resize(static_cast<usize>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    out.resize(std::fread(out.data(), 1, out.size(), f));
    std::fclose(f);
    return out;
}

} // namespace

AUREA_TEST(Startup, CorruptPipelineCacheIsDiscardedAndRebuilt) {
    // §176: cache corrompido → recompila (nunca entrega lixo ao driver, nunca
    // trava a abertura). Cada caso: abre com o arquivo estragado, o motor sobe
    // mesmo assim, o arquivo é refeito e a abertura seguinte o aceita.
    if (!gpu_available()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = startup_dir("corrompido");
    const std::string file = cache_file(dir);

    AUREA_CHECK(open_and_close(dir) == CacheLoad::Missing);
    AUREA_CHECK(open_and_close(dir) == CacheLoad::Loaded);
    const std::vector<u8> good = slurp(file);
    AUREA_CHECK(good.size() > 72);

    auto broken_then_healed = [&](const char* what, const std::vector<u8>& bytes) {
        overwrite(file, bytes);
        const CacheLoad first = open_and_close(dir);
        const CacheLoad second = open_and_close(dir);
        const bool ok = first == CacheLoad::Rejected && second == CacheLoad::Loaded;
        if (!ok) std::printf("\n    caso '%s': %d depois %d", what, static_cast<int>(first), static_cast<int>(second));
        AUREA_CHECK(ok);
    };

    std::vector<u8> flipped = good;
    flipped[flipped.size() / 2] ^= 0x5A;
    broken_then_healed("byte trocado", flipped);

    std::vector<u8> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 3));
    broken_then_healed("truncado", truncated);

    // O formato de antes da Fase 8I (blob cru do driver, sem cabeçalho nosso).
    std::vector<u8> raw(good.begin() + 72, good.end());
    broken_then_healed("formato antigo", raw);

    std::vector<u8> garbage(4096);
    for (usize i = 0; i < garbage.size(); ++i) garbage[i] = static_cast<u8>(i * 131u + 7u);
    broken_then_healed("lixo", garbage);

    // App atualizado: o SPIR-V mudou, a versão do cache muda junto.
    AUREA_CHECK(open_and_close(dir, 0x1234) == CacheLoad::Rejected);
    AUREA_CHECK(open_and_close(dir, 0x1234) == CacheLoad::Loaded);
    AUREA_CHECK(open_and_close(dir) == CacheLoad::Rejected);

    // A carga anterior derrubou o processo (a marca ficou): nem lê o arquivo.
    AUREA_CHECK(open_and_close(dir) == CacheLoad::Loaded);
    overwrite(file + ".carregando", {});
    AUREA_CHECK(open_and_close(dir) == CacheLoad::CrashGuard);
    std::error_code ec;
    AUREA_CHECK(!fs::exists(fs::path(file + ".carregando"), ec));
    AUREA_CHECK(open_and_close(dir) == CacheLoad::Loaded);
    fs::remove_all(fs::path(dir), ec);
}

AUREA_TEST(Startup, ProjectPipelinesWarmWhenUsedNotAtOpen) {
    // Efeito e 3D não são compilados na abertura: entram quando o projeto os
    // tem, no primeiro quadro parado — e o playback que vem depois não
    // compila nada.
    if (!gpu_available()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = startup_dir("preguicoso");
    Engine e;
    AUREA_CHECK(open_engine(dir, e).ok);
    ShaderLibrary& lib = e.renderer().shaders();

    const EffectTypeId blur = e.effects().find_key(effect_keys::kGaussianBlur);
    const Effect* fx = e.effects().find(blur);
    AUREA_CHECK(fx != nullptr);
    std::vector<PipelineKey> keys;
    if (fx) fx->pipelines(keys, SurfaceFormat::RGBA16F);
    AUREA_CHECK(!keys.empty());
    bool anyMissing = false;
    for (const PipelineKey& k : keys) anyMissing |= !lib.has_pipeline(k);
    AUREA_CHECK(anyMissing);   // não veio da abertura

    auto shape = e.add_shape(0);
    AUREA_CHECK(shape.ok());
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*shape);
    add.effect_add.effectType = blur;
    add.effect_add.index = 0xFFFFFFFFu;
    AUREA_CHECK(e.apply_command(add).ok());
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(e.capture_frame_rgba(128, rgba, w, h).ok());
    bool allThere = true;
    for (const PipelineKey& k : keys) allThere &= lib.has_pipeline(k);
    AUREA_CHECK(allThere);

    const u32 before3d = lib.pipeline_count();
    AUREA_CHECK(e.add_null(true).ok());
    AUREA_CHECK(e.capture_frame_rgba(128, rgba, w, h).ok());
    const u32 after3d = lib.pipeline_count();
    AUREA_CHECK(after3d >= before3d + 10);   // opaco/máscara/mistura × faces + sombra + plano

    lib.mark_steady_state();
    for (int i = 0; i < 5; ++i) AUREA_CHECK(e.capture_frame_rgba(128, rgba, w, h).ok());
    AUREA_CHECK_EQ(lib.compiles_since_mark(), 0u);
    std::printf("\n    abertura %u pipelines; +%u do projeto (desfoque + 3D) no 1o quadro parado\n    ",
                e.renderer().pipelines_prewarmed(), e.renderer().pipelines_warmed_for_project());
    e.shutdown();
    std::error_code ec;
    fs::remove_all(fs::path(dir), ec);
}

#endif // AUREA_TEST_VULKAN
