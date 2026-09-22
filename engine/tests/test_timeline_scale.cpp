// Fase 8D — timeline em escala, medida no host.
//
// O que a UI pede ao motor a cada mudança de modelo (query_layers +
// keyframes de todas as camadas), com 1000 clipes e 10.000 keyframes; o custo
// da waveform que a timeline repinta; e a criação de 5000 palavras de legenda.
// Os números saem no log (a bancada não tem celular: o host dá a ORDEM de
// grandeza e o antes/depois; o relatório diz isso). As checagens são de
// CONTRATO, não de tempo — tempo em CI compartilhado não é determinístico.
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/text/Captions.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

EngineConfig scale_config() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

f64 ms_since(u64 t0) { return static_cast<f64>(monotonic_ns() - t0) / 1e6; }

/// 1000 clipes; 9 keyframes em cada um (+ 1 camada com 1000) = 10.000 keyframes.
void build_scale_project(Engine& e, u32 layers, u32 keysPerLayer, u32 heavyKeys) {
    for (u32 i = 0; i < layers; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = (i % 3 == 0) ? LayerKind::Text : LayerKind::Shape;
        AUREA_CHECK(e.apply_command(c).ok());
    }
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    for (u32 i = 0; i < comp->order().size(); ++i) {
        Layer* l = comp->layer(comp->order().at(i));
        l->start = FrameIndex{static_cast<i64>(i) * 7};
        l->end = FrameIndex{static_cast<i64>(i) * 7 + 300};
        const u32 nk = i == 0 ? heavyKeys : keysPerLayer;
        for (u32 k = 0; k < nk; ++k) {
            Keyframe kf;
            kf.time = FrameIndex{static_cast<i64>(k) * 3};
            kf.value = static_cast<f32>(k % 2);
            // Referência pedida a cada vez: criar a 2ª trilha pode realocar o vetor.
            l->tracks.get_or_create((k % 2) ? TrackProperty::PositionX : TrackProperty::Opacity).keys.push_back(kf);
        }
    }
}

} // namespace

AUREA_TEST(TimelineScale, QueriesWith1000LayersAnd10kKeyframes) {
    Engine e;
    AUREA_CHECK(e.initialize(scale_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
    // 999 camadas × 9 + 1 × 1009 = 10.000 keyframes.
    build_scale_project(e, 1000, 9, 1009);

    std::vector<bridge::LayerRow> rows(4096);
    std::vector<char> names(256 * 1024);
    std::vector<bridge::KeyframeRow> keys(16384);

    constexpr u32 kIters = 50;
    u32 n = 0;
    u64 t0 = monotonic_ns();
    for (u32 it = 0; it < kIters; ++it) n = e.query_layers(rows.data(), 4096, names.data(), static_cast<u32>(names.size()));
    const f64 layersMs = ms_since(t0) / kIters;
    AUREA_CHECK_EQ(n, 1000u);

    // O caminho da UI hoje: uma consulta de keyframes POR camada.
    u64 total = 0;
    t0 = monotonic_ns();
    for (u32 it = 0; it < kIters; ++it) {
        total = 0;
        for (u32 i = 0; i < n; ++i) total += e.query_keyframes(rows[i].id, keys.data(), 16384);
    }
    const f64 perLayerMs = ms_since(t0) / kIters;
    AUREA_CHECK_EQ(total, 10000ull);

    // Fase 8D: tudo numa consulta (1 travamento do modelo, 1 travessia de JNI).
    std::vector<bridge::KeyframeIndexRow> index(4096);
    u32 layersOut = 0, all = 0;
    t0 = monotonic_ns();
    for (u32 it = 0; it < kIters; ++it) all = e.query_all_keyframes(index.data(), 4096, keys.data(), 16384, &layersOut);
    const f64 allMs = ms_since(t0) / kIters;
    AUREA_CHECK_EQ(all, 10000u);
    AUREA_CHECK_EQ(layersOut, n);
    // Mesmo conteúdo e mesma ordem da consulta por camada.
    std::vector<bridge::KeyframeRow> one(16384);
    u32 cursor = 0;
    bool same = true;
    for (u32 i = 0; i < n; ++i) {
        const u32 c = e.query_keyframes(rows[i].id, one.data(), 16384);
        same = same && index[i].layerId == rows[i].id && index[i].count == c
            && std::memcmp(one.data(), keys.data() + cursor, c * sizeof(bridge::KeyframeRow)) == 0;
        cursor += c;
    }
    AUREA_CHECK(same);
    // Buffer pequeno: nada escrito, o total volta para a UI crescer o buffer.
    index[0].count = 12345;
    AUREA_CHECK_EQ(e.query_all_keyframes(index.data(), 4096, keys.data(), 100, &layersOut), 10000u);
    AUREA_CHECK_EQ(index[0].count, 12345u);
    AUREA_CHECK_EQ(e.query_all_keyframes(index.data(), 10, keys.data(), 16384, &layersOut), 10000u);
    AUREA_CHECK_EQ(layersOut, 1000u);
    std::printf("    query_layers(1000): %.3f ms | keyframes: 1000 consultas %.3f ms, consulta unica %.3f ms\n",
                layersMs, perLayerMs, allMs);
    e.shutdown();
}

AUREA_TEST(TimelineScale, OneLayerWith10kKeyframes) {
    Engine e;
    AUREA_CHECK(e.initialize(scale_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
    build_scale_project(e, 1, 0, 10000);
    bridge::LayerRow row;
    AUREA_CHECK_EQ(e.query_layers(&row, 1, nullptr, 0), 1u);
    std::vector<bridge::KeyframeRow> keys(16384);
    constexpr u32 kIters = 200;
    u32 got = 0;
    const u64 t0 = monotonic_ns();
    for (u32 it = 0; it < kIters; ++it) got = e.query_keyframes(row.id, keys.data(), 16384);
    std::printf("    query_keyframes(1 camada, 10000 kf): %.3f ms\n", ms_since(t0) / kIters);
    AUREA_CHECK_EQ(got, 10000u);
    e.shutdown();
}

AUREA_TEST(TimelineScale, Captions5000Words) {
    SyntheticConfig cfg;
    cfg.audioRate = 48000;
    cfg.audioSeconds = 1800.0;
    cfg.frameCount = 30 * 1800;
    SyntheticFactory f(cfg);
    EngineConfig ec = scale_config();
    ec.mediaFactory = &f;
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "s";
    vi.displayName = "s";
    auto vid = e.import_video(vi);
    AUREA_CHECK(vid.ok());
    if (!vid.ok()) return;
    // 5000 palavras, ~2,6 palavras/s, pausas a cada 12 palavras.
    std::vector<text::CaptionWord> words;
    words.reserve(5000);
    f64 t = 0.5;
    for (u32 i = 0; i < 5000; ++i) {
        text::CaptionWord w;
        w.text = (i % 7 == 0) ? "palavra" : (i % 3 == 0 ? "de" : "legenda");
        w.start = t;
        w.end = t + 0.3;
        t += (i % 12 == 11) ? 1.0 : 0.38;
        words.push_back(w);
    }
    text::CaptionOptions o;
    o.maxWords = 3;
    o.highlight = true;
    u64 t0 = monotonic_ns();
    auto made = e.create_captions(*vid, words, o);
    const f64 createMs = ms_since(t0);
    AUREA_CHECK(made.ok());
    const u32 count = made.ok() ? *made : 0;
    AUREA_CHECK(count >= 1000);   // até 3 palavras e 18 letras por linha

    std::vector<bridge::LayerRow> rows(4096);
    std::vector<char> names(256 * 1024);
    std::vector<bridge::KeyframeRow> keys(65536);
    t0 = monotonic_ns();
    const u32 n = e.query_layers(rows.data(), 4096, names.data(), static_cast<u32>(names.size()));
    u64 kf = 0;
    for (u32 i = 0; i < n; ++i) kf += e.query_keyframes(rows[i].id, keys.data(), 65536);
    const f64 refreshMs = ms_since(t0);
    AUREA_CHECK_EQ(n, count + 1);
    std::printf("    create_captions(5000 palavras): %u camadas em %.1f ms | releitura da timeline: %u camadas, %llu kf em %.2f ms\n",
                count, createMs, n, static_cast<unsigned long long>(kf), refreshMs);
    e.shutdown();
}
