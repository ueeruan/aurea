// Remover fundo (0.4.5): recorte por quadro, cache persistente, worker e a
// ação da camada. Ver include/aurea/ai/BackgroundRemoval.hpp.
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "aurea/ai/BackgroundRemoval.hpp"
#include "aurea/ai/RotoMatte.hpp"
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/render/Renderer.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace aurea;

namespace {
constexpr u32 N = ai::kBgMatteSize;
constexpr usize kCells = static_cast<usize>(N) * N;

/// Fundo com textura fixa e um quadrado claro em (x0..x1, y0..y1). `prob`
/// confiante só no miolo (a borda da rede fica `shift` células para dentro):
/// a borda final tem que encostar na aresta de COR, não na da rede.
ai::BgRaw scene(u32 x0, u32 y0, u32 x1, u32 y1, u32 shift, u8 bgBase = 40) {
    ai::BgRaw r;
    r.prob.assign(kCells, 0.02f);
    r.rgb.resize(kCells * 3);
    for (u32 y = 0; y < N; ++y) for (u32 x = 0; x < N; ++x) {
        const usize i = static_cast<usize>(y) * N + x;
        const int tex = int(((x * 73856093u) ^ (y * 19349663u)) % 9) - 4;
        const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
        r.rgb[i * 3] = static_cast<u8>(in ? 220 : bgBase + tex);
        r.rgb[i * 3 + 1] = static_cast<u8>(in ? 200 : bgBase + 60 + tex);
        r.rgb[i * 3 + 2] = static_cast<u8>(in ? 180 : bgBase + 20);
        if (x >= x0 + shift && x < x1 - shift && y >= y0 + shift && y < y1 - shift) r.prob[i] = 0.97f;
        else if (in) r.prob[i] = 0.5f;   // incerto: a geodésica decide
    }
    return r;
}

f32 at(const std::vector<f32>& m, u32 x, u32 y) { return m[static_cast<usize>(y) * N + x]; }

std::vector<f32> disk_matte(f32 cx, f32 cy, f32 r) {
    std::vector<f32> m(kCells, 0.0f);
    for (u32 y = 0; y < N; ++y) for (u32 x = 0; x < N; ++x) {
        const f32 d = std::hypot(x + .5f - cx, y + .5f - cy);
        m[static_cast<usize>(y) * N + x] = std::clamp(r - d, 0.0f, 1.0f);
    }
    return m;
}

std::filesystem::path temp_file(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

/// O status não espera o render (try_lock): tenta algumas vezes.
bool status_of(Engine& e, u64 layer, u32 effect, i64* out8) {
    for (u32 i = 0; i < 200; ++i) {
        if (e.bg_remove_status(layer, effect, out8)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool wait_idle(const ai::BgRemoveService& s, u32 ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        if (!s.progress().running) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

aurea::test::SyntheticConfig video_config(u32 frames) {
    aurea::test::SyntheticConfig c;
    c.width = 96; c.height = 64; c.frameCount = frames;
    c.pattern = aurea::test::SyntheticPattern::MovingSquare;
    return c;
}

Asset video_asset(u32 frames) {
    Asset a; a.kind = AssetKind::Video; a.video.width = 96; a.video.height = 64; a.video.fps = 30;
    a.video.frameCount.value = frames; a.contentHash = 0xB6B6;
    return a;
}
} // namespace

// Fonte do serviço: o mesmo mapa cru do Rotobrush, a chave do recorte.
namespace {
ai::BgRemoveService::Source make_source(ai::DepthMapService* fg, VideoSourceFactory* factory, u32 frames, u32 model = 0) {
    ai::BgRemoveService::Source s;
    s.fg = fg; s.factory = factory; s.asset = video_asset(frames);
    s.rawKey = 0xAB12; s.model = model;
    s.matteKey = ai::bg_matte_key(s.asset.contentHash, s.asset.sourcePath, 96, 64, frames, model);
    s.fps = 30.0; s.frameCount = frames; s.layerW = 96; s.layerH = 64;
    return s;
}
} // namespace

AUREA_TEST(BgRemove, MatteFollowsTheColorEdgeNotTheNetworkEdge) {
    const ai::BgRaw cur = scene(100, 90, 220, 230, 6);
    std::vector<f32> m;
    ai::bg_matte(nullptr, cur, nullptr, m);
    AUREA_CHECK_EQ(m.size(), kCells);
    AUREA_CHECK(at(m, 160, 160) > 0.95f);         // miolo
    AUREA_CHECK(at(m, 20, 20) < 0.05f);           // fundo
    // A rede só era confiante 6 células para dentro; a borda vai até a cor.
    AUREA_CHECK(at(m, 102, 160) > 0.5f);
    AUREA_CHECK(at(m, 97, 160) < 0.5f);
    AUREA_CHECK(at(m, 160, 227) > 0.5f);
    AUREA_CHECK(at(m, 160, 233) < 0.5f);
    // Valores já quantizados como o cache guarda: recalcular == reler.
    std::vector<u8> packed; std::vector<f32> back;
    ai::roto_pack(m, packed);
    AUREA_CHECK(ai::roto_unpack(packed, kCells, back));
    AUREA_CHECK(back == m);
}

AUREA_TEST(BgRemove, TemporalMedianFixesAOneFrameDropoutSymmetrically) {
    const ai::BgRaw good = scene(100, 90, 220, 230, 4);
    ai::BgRaw bad = good;
    // Um quadro só perde a metade de baixo do objeto (o "piscar" da rede).
    for (u32 y = 160; y < 230; ++y) for (u32 x = 100; x < 220; ++x) bad.prob[static_cast<usize>(y) * N + x] = 0.01f;
    std::vector<f32> alone, fixed, reversed, ref;
    ai::bg_matte(nullptr, bad, nullptr, alone);
    ai::bg_matte(&good, bad, &good, fixed);
    ai::bg_matte(nullptr, good, nullptr, ref);
    AUREA_CHECK(at(alone, 160, 200) < 0.5f);       // sem vizinhos o buraco aparece
    AUREA_CHECK(at(fixed, 160, 200) > 0.95f);      // com eles, não
    AUREA_CHECK(at(fixed, 20, 20) < 0.05f);
    // Janela simétrica: trocar anterior/seguinte dá o mesmo recorte.
    ai::bg_matte(&good, bad, &good, reversed);
    AUREA_CHECK(fixed == reversed);
    // Borda do clipe (sem vizinho de um lado): vale o que existe, sem quebrar.
    std::vector<f32> edge;
    ai::bg_matte(nullptr, good, &good, edge);
    AUREA_CHECK(edge == ref);
}

AUREA_TEST(BgRemove, SceneCutNeighboursDoNotBringTheOldSilhouetteBack) {
    const ai::BgRaw cur = scene(40, 40, 120, 120, 4);
    const ai::BgRaw other = scene(180, 160, 300, 300, 4, 160);   // outra cena, outra cor
    std::vector<f32> alone, withCut;
    ai::bg_matte(nullptr, cur, nullptr, alone);
    ai::bg_matte(&other, cur, &other, withCut);
    AUREA_CHECK(withCut == alone);
    AUREA_CHECK(at(withCut, 240, 230) < 0.05f);
}

AUREA_TEST(BgRemove, KeyFollowsContentModelAndAlgorithmNotThePath) {
    const u64 a = ai::bg_matte_key(0x1234, "/android/a.mp4", 1920, 1080, 300, 0);
    const u64 b = ai::bg_matte_key(0x1234, "/ios/Documents/a.mp4", 1920, 1080, 300, 0);
    AUREA_CHECK_EQ(a, b);                                   // mesma mídia em outro aparelho
    AUREA_CHECK(a != ai::bg_matte_key(0x1234, "/x", 1920, 1080, 300, 1));     // outro modelo
    AUREA_CHECK(a != ai::bg_matte_key(0x9999, "/x", 1920, 1080, 300, 0));     // outra mídia
    // Sem hash do conteúdo, o caminho identifica.
    AUREA_CHECK(ai::bg_matte_key(0, "/a.mp4", 1, 1, 1, 0) != ai::bg_matte_key(0, "/b.mp4", 1, 1, 1, 0));
    AUREA_CHECK(ai::bg_matte_key(0, "", 0, 0, 0, 0) != 0u);
}

AUREA_TEST(BgRemove, StoreRoundTripsAndRejectsDamagedFiles) {
    ai::BgMatteStore store;
    const auto m0 = disk_matte(160, 160, 60), m1 = disk_matte(100, 120, 40);
    store.put(7, 0, m0);
    store.put(7, 1, m1);
    store.put(9, 42, m1);
    AUREA_CHECK(store.dirty());
    AUREA_CHECK_EQ(store.count(7, 0, 10), i64{2});
    AUREA_CHECK_EQ(store.count(9, 0, 100), i64{1});
    std::vector<u8> bytes;
    store.encode(bytes);
    ai::BgMatteStore copy;
    AUREA_CHECK(copy.decode(bytes.data(), bytes.size()));
    std::vector<f32> got, want;
    AUREA_CHECK(copy.get(7, 1, got));
    AUREA_CHECK(store.get(7, 1, want));
    AUREA_CHECK(got == want);
    AUREA_CHECK(copy.has(9, 42));
    AUREA_CHECK(!copy.has(9, 41));
    // Determinístico: o mesmo cache dá o mesmo arquivo.
    std::vector<u8> again;
    copy.encode(again);
    AUREA_CHECK(again == bytes);
    // Um byte trocado, arquivo cortado ou outra versão: recusado inteiro.
    for (const usize pos : {usize{30}, bytes.size() / 2, bytes.size() - 3}) {
        auto bad = bytes; bad[pos] ^= 0x5A;
        ai::BgMatteStore x;
        AUREA_CHECK(!x.decode(bad.data(), bad.size()));
        AUREA_CHECK_EQ(x.frames(), usize{0});
    }
    ai::BgMatteStore cut;
    AUREA_CHECK(!cut.decode(bytes.data(), bytes.size() - 9));
    auto versioned = bytes; versioned[8] ^= 1;   // versão
    ai::BgMatteStore old;
    AUREA_CHECK(!old.decode(versioned.data(), versioned.size()));
    // Tamanho errado nunca entra no cache.
    store.put(7, 5, std::vector<f32>(10, 1.0f));
    AUREA_CHECK(!store.has(7, 5));
}

AUREA_TEST(BgRemove, StoreSurvivesSaveAndReopenPrunesAndEvicts) {
    const auto path = temp_file("aurea-bgremove-test.aurea.bgmask").string();
    (void)fileio::remove_file(path);
    const auto m = disk_matte(150, 150, 50);
    {
        ai::BgMatteStore store;
        store.attach(path);
        for (i64 f = 0; f < 5; ++f) store.put(11, f, m);
        store.put(12, 0, m);
        AUREA_CHECK(store.flush().ok());
        AUREA_CHECK(!store.dirty());
        AUREA_CHECK(fileio::exists(path));
    }
    {
        // Reabrir: leitura preguiçosa na primeira consulta.
        ai::BgMatteStore store;
        store.attach(path);
        AUREA_CHECK_EQ(store.count(11, 0, 4), i64{5});
        std::vector<f32> got;
        AUREA_CHECK(store.get(11, 3, got));
        AUREA_CHECK_EQ(got.size(), kCells);
        // Pressão de memória: grava, solta e relê sob demanda (nada perdido).
        store.put(11, 7, m);
        AUREA_CHECK(store.release_memory() > 0u);
        AUREA_CHECK_EQ(store.bytes(), usize{0});
        AUREA_CHECK(store.has(11, 7));
        AUREA_CHECK_EQ(store.count(11, 0, 7), i64{6});
        // Sem arquivo, nada sai.
        ai::BgMatteStore loose;
        loose.put(1, 0, m);
        AUREA_CHECK_EQ(loose.release_memory(), usize{0});
        AUREA_CHECK(loose.has(1, 0));
        // Mídia que saiu do projeto: a poda tira do arquivo.
        const std::vector<u64> keep{11};
        AUREA_CHECK(store.flush(&keep).ok());
    }
    {
        ai::BgMatteStore store;
        store.attach(path);
        AUREA_CHECK(store.has(11, 0));
        AUREA_CHECK(!store.has(12, 0));
        // Nada para guardar: o arquivo velho sai.
        const std::vector<u64> none;
        AUREA_CHECK(store.flush(&none).ok());
        AUREA_CHECK(!fileio::exists(path));
    }
    {
        // Arquivo corrompido: ignorado (recalcula), nunca um recorte errado.
        const u8 junk[] = {'A', 'U', 'B', 'G', 'M', 'S', 'K', '1', 1, 2, 3};
        AUREA_CHECK(fileio::write_atomic(path, junk, sizeof(junk)).ok());
        ai::BgMatteStore store;
        store.attach(path);
        AUREA_CHECK(!store.has(11, 0));
        (void)fileio::remove_file(path);
    }
    {
        // Teto de memória: sai a mídia usada há mais tempo, nunca a atual; numa
        // mídia só, os quadros mais longe do que acabou de entrar.
        ai::BgMatteStore store;
        std::vector<u8> packed;
        ai::roto_pack(m, packed);
        store.set_max_bytes(packed.size() * 6);
        for (i64 f = 0; f < 3; ++f) store.put(1, f, m);
        for (i64 f = 0; f < 4; ++f) store.put(2, f, m);
        AUREA_CHECK(store.bytes() <= packed.size() * 6);
        AUREA_CHECK_EQ(store.count(1, 0, 10), i64{0});
        AUREA_CHECK_EQ(store.count(2, 0, 10), i64{4});
        for (i64 f = 4; f < 10; ++f) store.put(2, f, m);
        AUREA_CHECK(store.has(2, 9));
        AUREA_CHECK(!store.has(2, 0));
        AUREA_CHECK(store.bytes() <= packed.size() * 6);
    }
}

AUREA_TEST(BgRemove, NativeSegmenterIsUsedOnlyForThePersonModel) {
    AUREA_CHECK_EQ(ai::bg_effective_model(ai::kBgModelPerson), ai::kBgModelAuto);   // sem nativo
    AUREA_CHECK(!Engine::bg_remove_native_available());
    static u32 calls = 0;
    ai::bg_set_native_segmenter([](void*, u32 model, const u8* rgb, u32 w, u32 h, u32 outSize, f32* out) {
        ++calls;
        if (model != ai::kBgModelPerson || !rgb || !w || !h) return false;
        for (u32 y = 0; y < outSize; ++y) for (u32 x = 0; x < outSize; ++x)
            out[y * outSize + x] = (x > outSize / 4 && x < 3 * outSize / 4) ? 1.5f : -1.0f;   // fora de 0..1: saneado
        return true;
    }, nullptr);
    AUREA_CHECK(Engine::bg_remove_native_available());
    AUREA_CHECK_EQ(ai::bg_effective_model(ai::kBgModelPerson), ai::kBgModelPerson);
    AUREA_CHECK_EQ(ai::bg_effective_model(ai::kBgModelAuto), ai::kBgModelAuto);
    ai::RotoImage image;
    image.width = 64; image.height = 48; image.rgb.assign(64 * 48 * 3, 90);
    const ai::BgRawPtr raw = ai::BgRemoveService::raw_from_native(ai::kBgModelPerson, image);
    AUREA_CHECK(raw && raw->valid());
    if (raw) {
        AUREA_CHECK_EQ(raw->prob[static_cast<usize>(N / 2) * N + N / 2], 1.0f);
        AUREA_CHECK_EQ(raw->prob[0], 0.0f);
        AUREA_CHECK_EQ(raw->rgb[0], u8{90});
    }
    AUREA_CHECK(!ai::BgRemoveService::raw_from_native(ai::kBgModelAuto, image));
    AUREA_CHECK(calls >= 2u);
    ai::bg_set_native_segmenter(nullptr, nullptr);
    AUREA_CHECK_EQ(ai::bg_effective_model(ai::kBgModelPerson), ai::kBgModelAuto);
}

AUREA_TEST(BgRemove, AnalyzeCachesEveryFrameAndMatchesOnDemandExport) {
    constexpr u32 frames = 6;
    aurea::test::SyntheticFactory factory(video_config(frames));
    ai::DepthMapService fg(true);
    auto store = ai::make_bg_matte_store();
    ai::BgRemoveService service(store);
    const auto src = make_source(&fg, &factory, frames);
    service.analyze(src, 0, frames - 1);
    AUREA_CHECK(wait_idle(service, 120'000));
    const auto p = service.progress();
    AUREA_CHECK(!p.running);
    AUREA_CHECK(!p.failed);
    AUREA_CHECK_EQ(p.done, i64{frames});
    AUREA_CHECK_EQ(p.matteKey, src.matteKey);
    AUREA_CHECK_EQ(store->count(src.matteKey, 0, frames - 1), i64{frames});
    // Export num renderizador novo (outro serviço, outro worker de rede, o
    // MESMO cache): lê o analisado, não recalcula.
    {
        ai::DepthMapService exportFg(true);
        ai::BgRemoveService exportService(store);
        const auto r = exportService.compute(make_source(&exportFg, &factory, frames), 3);
        AUREA_CHECK(r.matte != nullptr);
        AUREA_CHECK_EQ(exportService.computed_frames(), u64{0});
    }
    // Sem análise (cache vazio, quadro pedido direto, fora de ordem): o MESMO
    // recorte — a janela no tempo é fixa, não depende da ordem.
    {
        ai::DepthMapService coldFg(true);
        ai::BgRemoveService cold(ai::make_bg_matte_store());
        const auto src2 = make_source(&coldFg, &factory, frames);
        const auto direct = cold.compute(src2, 3);
        const auto analysed = service.cached(src.matteKey, 3);
        AUREA_CHECK(direct.matte && analysed);
        if (direct.matte && analysed) AUREA_CHECK(*direct.matte == *analysed);
        // Borda fina: o refinado na resolução do quadro, guardado em memória.
        auto refined = src2;
        refined.refine.radius = 4.0f;
        const auto withEdge = cold.compute(refined, 3);
        AUREA_CHECK(withEdge.refined != nullptr);
        if (withEdge.refined) {
            AUREA_CHECK_EQ(withEdge.refined->width, 96u);
            AUREA_CHECK_EQ(withEdge.refined->height, 64u);
        }
        AUREA_CHECK(cold.cached_refined(src2.matteKey, 3, refined.refine.hash()) != nullptr);
    }
}

AUREA_TEST(BgRemove, CancelStopsTheAnalysisAndKeepsWhatWasDone) {
    constexpr u32 frames = 90;
    aurea::test::SyntheticFactory factory(video_config(frames));
    ai::DepthMapService fg(true);
    auto store = ai::make_bg_matte_store();
    ai::BgRemoveService service(store);
    const auto src = make_source(&fg, &factory, frames);
    service.analyze(src, 0, frames - 1);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (store->count(src.matteKey, 0, frames - 1) < 2 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    service.cancel();
    AUREA_CHECK(!service.progress().running);
    // O quadro em curso (uma inferência não se interrompe) termina; depois, nada.
    i64 after = store->count(src.matteKey, 0, frames - 1);
    for (u32 quiet = 0; quiet < 3 && std::chrono::steady_clock::now() < until;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const i64 now = store->count(src.matteKey, 0, frames - 1);
        quiet = now == after ? quiet + 1 : 0;
        after = now;
    }
    AUREA_CHECK_EQ(store->count(src.matteKey, 0, frames - 1), after);
    AUREA_CHECK(after >= 2 && after < i64{frames});
    // Analisar de novo continua: o que já estava pronto não é refeito.
    const u64 before = service.computed_frames();
    service.analyze(src, 0, 3);
    AUREA_CHECK(wait_idle(service, 60'000));
    AUREA_CHECK(service.computed_frames() - before <= u64{4});
    AUREA_CHECK_EQ(store->count(src.matteKey, 0, 3), i64{4});
}

AUREA_TEST(BgRemove, LayerActionAddsOneEffectReadsTheSavedMasksAndRejectsOtherLayers) {
    const auto file = temp_file("aurea-bgremove-action.aurea");
    const std::string sidecar = file.string() + ".bgmask";
    (void)fileio::remove_file(sidecar);
    Engine engine;
    EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(640, 480, 30, nullptr).ok());
    std::vector<u8> pixels(160 * 120 * 4, 255);
    auto imported = engine.import_image(pixels.data(), 160, 120, "Pessoa");
    AUREA_CHECK(imported.ok()); if (!imported.ok()) return;
    const LayerId id = LayerId::unpack(*imported);
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    const usize depth = engine.history().depth();
    const i32 effect = engine.bg_remove_apply(id.pack());
    AUREA_CHECK(effect >= 0);
    AUREA_CHECK_EQ(engine.history().depth(), depth + 1);
    // Tocar de novo não duplica.
    AUREA_CHECK_EQ(engine.bg_remove_apply(id.pack()), effect);
    AUREA_CHECK_EQ(engine.history().depth(), depth + 1);
    Layer* layer = comp->layer(id);
    AUREA_CHECK(layer && layer->effects.size() == 1u);
    if (!layer || layer->effects.empty()) return;
    const ParameterRegistry* specs = engine.effects().params(layer->effects.back().type);
    AUREA_CHECK(specs && specs->count() == 11u);
    i64 st[8]{};
    AUREA_CHECK(status_of(engine, id.pack(), static_cast<u32>(effect), st));
    AUREA_CHECK_EQ(st[6], i64{1});    // uma foto = um quadro
    AUREA_CHECK_EQ(st[5], i64{0});
    AUREA_CHECK_EQ(st[7], i64{0});    // automático
    // Outro tipo de camada (texto): a ação e o efeito são recusados.
    auto text = engine.add_text("Oi");
    AUREA_CHECK(text.ok());
    if (text.ok()) {
        AUREA_CHECK_EQ(engine.bg_remove_apply(*text), -1);
        Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(*text);
        add.effect_add.effectType = effect_type_id(effect_keys::kBackgroundRemove); add.effect_add.index = kInvalidIndex;
        AUREA_CHECK(!engine.apply_command(add).ok());
    }
    const Asset* asset = engine.project()->asset(layer->source);
    AUREA_CHECK(asset != nullptr); if (!asset) return;
    // Salvar e escrever o recorte "analisado" ao lado: reabrir lê o arquivo.
    AUREA_CHECK(engine.save_project(file.string().c_str()).ok());
    {
        ai::BgMatteStore s;
        s.put(ai::bg_matte_key(asset->contentHash, asset->sourcePath, 0, 0, 1, 0), 0, disk_matte(160, 160, 50));
        AUREA_CHECK(s.flush_to(sidecar).ok());
    }
    AUREA_CHECK(engine.load_project(file.string().c_str()).ok());
    comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    u64 reopened = 0; u32 reopenedEffect = 0;
    comp->layers().for_each([&](LayerId lid, const Layer& l) {
        for (const auto& e : l.effects) if (e.type == effect_type_id(effect_keys::kBackgroundRemove)) { reopened = lid.pack(); reopenedEffect = e.id; }
    });
    AUREA_CHECK(reopened != 0u);
    i64 again[8]{};
    AUREA_CHECK(status_of(engine, reopened, reopenedEffect, again));
    AUREA_CHECK_EQ(again[4], i64{1});
    AUREA_CHECK_EQ(again[5], i64{1});
    engine.shutdown();
    (void)fileio::remove_file(file.string());
    (void)fileio::remove_file(sidecar);
    (void)fileio::remove_file(fileio::backup_path(file.string()));
}
