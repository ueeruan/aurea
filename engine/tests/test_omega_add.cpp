// =============================================================================
//  Omega 0.3.2, área 08 (adicionar e exportar): as regras do motor.
//  - P1-2: lote de importação (duração da imagem, corte do vídeo, mesmo
//    início | em sequência) num passo de desfazer só;
//  - P1-5: grupo vazio; P1-6: projeto inserido como grupo;
//  - P1-8: fps Baixo | Reduzido | Cheio em centésimos, truncado (REA);
//  - P1-9: qualidade em porcentagem (20 → 0,6 · 60 → 1,0 · 100 → 1,5).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/export/BitratePolicy.hpp"
#include "aurea/export/ExportRules.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;

namespace {

/// Vídeo sintético de 2 s a 30 fps (60 quadros), sem decodificador.
class OmegaProbeFactory final : public VideoSourceFactory {
public:
    bool probe(const char*, MediaProbe& out) override {
        out.hasVideo = true;
        out.hasAudio = false;
        out.video.codedWidth = 320; out.video.codedHeight = 180;
        out.video.fps = 30.; out.video.durationUs = 2'000'000;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
};

EngineConfig omega_config(VideoSourceFactory* factory) {
    EngineConfig cfg;
    cfg.workerCount = 1;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    cfg.mediaFactory = factory;
    return cfg;
}

Composition* current(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }
const Layer* layer_of(Engine& e, u64 id) { return current(e)->layer(LayerId::unpack(id)); }

void seek(Engine& e, i64 frame) {
    Command s; s.type = CommandType::PlaybackSeek; s.seek.time = tick_at(FrameIndex{frame}, 30.);
    AUREA_CHECK(e.apply_command(s).ok());
}

void undo(Engine& e) {
    Command u; u.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(u).ok());
}

/// Duas imagens (45 e 30 quadros) e um vídeo cortado de 0,5 s a 1,5 s, nessa ordem.
std::vector<Engine::MediaBatchItem> batch_items(const std::vector<u8>& pixels) {
    std::vector<Engine::MediaBatchItem> items(3);
    items[0].rgba = pixels.data(); items[0].width = 4; items[0].height = 4; items[0].name = "A"; items[0].durationFrames = 45;
    items[1].video = true; items[1].source.sourcePath = "synthetic-video"; items[1].source.displayName = "V";
    items[1].sourceInUs = 500'000; items[1].sourceOutUs = 1'500'000;
    items[2].rgba = pixels.data(); items[2].width = 4; items[2].height = 4; items[2].name = "B"; items[2].durationFrames = 30;
    return items;
}

} // namespace

AUREA_TEST(OmegaAdd, FpsOptionsTruncateInHundredthsWithoutAMinimum) {
    // Projeto > 15 fps: 1/4 e 1/2.
    AUREA_CHECK_EQ(export_fps_option(3000u, ExportFpsOption::Low), 750u);       // 30 → 7,5
    AUREA_CHECK_EQ(export_fps_option(3000u, ExportFpsOption::Reduced), 1500u);
    AUREA_CHECK_EQ(export_fps_option(3000u, ExportFpsOption::Full), 3000u);
    AUREA_CHECK_EQ(export_fps_option(2500u, ExportFpsOption::Low), 625u);       // 25 → 6,25
    AUREA_CHECK_EQ(export_fps_option(2500u, ExportFpsOption::Reduced), 1250u);
    AUREA_CHECK_EQ(export_fps_option(2997u, ExportFpsOption::Low), 749u);       // trunca, não arredonda
    AUREA_CHECK_EQ(export_fps_option(2997u, ExportFpsOption::Reduced), 1498u);
    AUREA_CHECK_EQ(export_fps_option(1501u, ExportFpsOption::Low), 375u);
    // Projeto ≤ 15 fps: 1/3 e 2/3.
    AUREA_CHECK_EQ(export_fps_option(1200u, ExportFpsOption::Low), 400u);       // 12 → 4
    AUREA_CHECK_EQ(export_fps_option(1200u, ExportFpsOption::Reduced), 800u);
    AUREA_CHECK_EQ(export_fps_option(1500u, ExportFpsOption::Low), 500u);
    AUREA_CHECK_EQ(export_fps_option(1500u, ExportFpsOption::Reduced), 1000u);
    AUREA_CHECK_EQ(export_fps_option(100u, ExportFpsOption::Low), 33u);         // sem mínimo
    // Número estável das pontes.
    AUREA_CHECK_EQ(export_fps_option(3000u, 0u), 750u);
    AUREA_CHECK_EQ(export_fps_option(3000u, 1u), 1500u);
    AUREA_CHECK_EQ(export_fps_option(3000u, 2u), 3000u);
    AUREA_CHECK_EQ(export_fps100(29.97), 2997u);
    AUREA_CHECK_EQ(export_fps100(23.976), 2398u);
    AUREA_CHECK_EQ(export_fps100(0.0), 0u);
}

AUREA_TEST(OmegaAdd, QualityPercentMapsTheOldLevelsAndInterpolates) {
    AUREA_CHECK_NEAR(export_quality_factor_percent(20), 0.6, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_percent(40), 0.8, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_percent(60), 1.0, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_percent(80), 1.25, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_percent(100), 1.5, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_percent(5), 0.6, 1e-9);     // preso em 20
    AUREA_CHECK_NEAR(export_quality_factor_percent(150), 1.5, 1e-9);   // preso em 100
    // Códigos antigos continuam valendo (checkpoint e pontes antigas).
    AUREA_CHECK_NEAR(export_quality_factor_code(0), 0.6, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_code(1), 1.0, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_code(2), 1.5, 1e-9);
    AUREA_CHECK_NEAR(export_quality_factor_code(60), 1.0, 1e-9);
    const auto bps = [](u32 code) { return export_video_bitrate_bps_code(1920, 1080, 30.0, ExportCodec::H264, code); };
    AUREA_CHECK_EQ(bps(60), export_video_bitrate_bps(1920, 1080, 30.0, ExportCodec::H264, ExportQuality::Normal));
    AUREA_CHECK_EQ(bps(20), export_video_bitrate_bps(1920, 1080, 30.0, ExportCodec::H264, ExportQuality::Low));
    AUREA_CHECK_EQ(bps(100), export_video_bitrate_bps(1920, 1080, 30.0, ExportCodec::H264, ExportQuality::High));
    AUREA_CHECK(bps(40) < bps(60) && bps(60) < bps(80));
    // A taxa manual vence a qualidade.
    AUREA_CHECK_EQ(export_video_bitrate_bps_code(1920, 1080, 30.0, ExportCodec::H264, 20, 10), 10'000'000u);
}

AUREA_TEST(OmegaAdd, SameStartBatchStacksInSelectionOrderAndUndoesOnce) {
    OmegaProbeFactory factory;
    Engine e; AUREA_CHECK(e.initialize(omega_config(&factory)).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "lote mesmo inicio").ok());
    current(e)->set_duration(FrameIndex{900});
    seek(e, 300);
    const u32 depth = e.read_status().undoDepth;
    std::vector<u8> pixels(4 * 4 * 4, 255);
    Status error;
    const auto ids = e.import_media_batch(batch_items(pixels), Engine::MediaBatchPlacement::SameStart, &error);
    AUREA_CHECK(error.ok());
    AUREA_CHECK_EQ(ids.size(), usize{3});
    if (ids.size() != 3) { e.shutdown(); return; }
    const Layer* a = layer_of(e, ids[0]);
    const Layer* v = layer_of(e, ids[1]);
    const Layer* b = layer_of(e, ids[2]);
    AUREA_CHECK(a && v && b);
    if (!a || !v || !b) { e.shutdown(); return; }
    AUREA_CHECK_EQ(a->start.value, 300ll); AUREA_CHECK_EQ(a->end.value, 345ll);
    AUREA_CHECK_EQ(v->start.value, 300ll); AUREA_CHECK_EQ(v->end.value, 330ll);
    AUREA_CHECK_EQ(v->offset.value, 15ll);                       // origem desloca 0,5 s
    AUREA_CHECK_EQ(v->local_time(FrameIndex{300}).value, 15ll);
    AUREA_CHECK_EQ(b->start.value, 300ll); AUREA_CHECK_EQ(b->end.value, 330ll);
    // A 1ª escolhida embaixo, as seguintes acima.
    const Composition* comp = current(e);
    AUREA_CHECK(comp->z_index_of(LayerId::unpack(ids[0])) < comp->z_index_of(LayerId::unpack(ids[1])));
    AUREA_CHECK(comp->z_index_of(LayerId::unpack(ids[1])) < comp->z_index_of(LayerId::unpack(ids[2])));
    AUREA_CHECK_EQ(e.read_status().undoDepth, depth + 1);
    AUREA_CHECK_EQ(e.selection_count(), 3u);
    undo(e);
    AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    e.shutdown();
}

AUREA_TEST(OmegaAdd, SequenceBatchFollowsTheSelectionOrderAndUndoesOnce) {
    OmegaProbeFactory factory;
    Engine e; AUREA_CHECK(e.initialize(omega_config(&factory)).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "lote sequencia").ok());
    current(e)->set_duration(FrameIndex{900});
    seek(e, 300);
    std::vector<u8> pixels(4 * 4 * 4, 255);
    const auto ids = e.import_media_batch(batch_items(pixels), Engine::MediaBatchPlacement::Sequence);
    AUREA_CHECK_EQ(ids.size(), usize{3});
    if (ids.size() != 3) { e.shutdown(); return; }
    AUREA_CHECK_EQ(layer_of(e, ids[0])->start.value, 300ll); AUREA_CHECK_EQ(layer_of(e, ids[0])->end.value, 345ll);
    AUREA_CHECK_EQ(layer_of(e, ids[1])->start.value, 345ll); AUREA_CHECK_EQ(layer_of(e, ids[1])->end.value, 375ll);
    AUREA_CHECK_EQ(layer_of(e, ids[1])->offset.value, 15ll);   // o corte anda junto (arranjo mantém a origem)
    AUREA_CHECK_EQ(layer_of(e, ids[2])->start.value, 375ll); AUREA_CHECK_EQ(layer_of(e, ids[2])->end.value, 405ll);
    AUREA_CHECK(current(e)->duration().value >= 405);
    undo(e);
    AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    e.shutdown();
}

AUREA_TEST(OmegaAdd, TrimmedFirstClipShrinksTheCompositionInTheImportStep) {
    OmegaProbeFactory factory;
    Engine e; AUREA_CHECK(e.initialize(omega_config(&factory)).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "corte").ok());
    seek(e, 0);
    const u32 depth = e.read_status().undoDepth;
    VideoImport request; request.sourcePath = "synthetic-video"; request.displayName = "clip";
    const auto id = e.import_video(request);
    AUREA_CHECK(id.ok()); if (!id.ok()) { e.shutdown(); return; }
    AUREA_CHECK_EQ(current(e)->duration().value, 60ll);           // o primeiro clipe adota a duração
    AUREA_CHECK(e.apply_import_timing(*id, 0, 0, 1'000'000).ok());
    AUREA_CHECK_EQ(layer_of(e, *id)->end.value, 30ll);
    AUREA_CHECK_EQ(current(e)->duration().value, 30ll);           // sem rabo vazio
    AUREA_CHECK_EQ(e.read_status().undoDepth, depth + 1);         // o mesmo passo da importação
    // Corte fora da fonte fica preso nela.
    AUREA_CHECK(e.apply_import_timing(*id, 0, 1'900'000, 9'000'000).ok());
    AUREA_CHECK_EQ(layer_of(e, *id)->offset.value, 57ll);
    AUREA_CHECK_EQ(layer_of(e, *id)->end.value - layer_of(e, *id)->start.value, 3ll);
    undo(e);
    AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    e.shutdown();
}

AUREA_TEST(OmegaAdd, EmptyGroupIsAnEmptyTransparentCompositionAtThePlayhead) {
    Engine e; AUREA_CHECK(e.initialize(omega_config(nullptr)).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "grupo vazio").ok());
    current(e)->set_duration(FrameIndex{300});
    seek(e, 30);
    const auto group = e.add_empty_group("Grupo vazio");
    AUREA_CHECK(group.ok()); if (!group.ok()) { e.shutdown(); return; }
    const Layer* g = layer_of(e, *group);
    AUREA_CHECK(g && g->kind == LayerKind::Composition);
    if (!g) { e.shutdown(); return; }
    AUREA_CHECK_EQ(g->start.value, 30ll);
    AUREA_CHECK_EQ(g->end.value, 300ll);
    AUREA_CHECK_EQ(g->local_time(FrameIndex{30}).value, 0ll);
    const Composition* child = e.project()->timeline().composition(g->nested.composition);
    AUREA_CHECK(child != nullptr);
    if (!child) { e.shutdown(); return; }
    AUREA_CHECK(child->transparent_background());
    AUREA_CHECK_EQ(child->order().size(), 0u);
    AUREA_CHECK_EQ(child->width(), 640u);
    AUREA_CHECK_EQ(e.selection_count(), 1u);
    // Outro grupo vazio ganha um nome próprio.
    const auto second = e.add_empty_group("Grupo vazio");
    AUREA_CHECK(second.ok());
    if (second.ok()) AUREA_CHECK(layer_of(e, *second)->name == "Grupo vazio 2");
    undo(e);   // tira o segundo
    // Abrir, pôr uma camada dentro e desfazer: o grupo continua.
    AUREA_CHECK(e.open_precomp(*group));
    const auto inside = e.add_shape(0);
    AUREA_CHECK(inside.ok());
    AUREA_CHECK_EQ(e.read_status().layerCount, 1u);
    undo(e);
    AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    AUREA_CHECK(e.close_precomp());
    AUREA_CHECK_EQ(e.read_status().layerCount, 1u);
    undo(e);   // tira o grupo
    AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    e.shutdown();
}

AUREA_TEST(OmegaAdd, ProjectInsertedAsGroupKeepsItsLayersAndRefusesItself) {
    namespace fs = std::filesystem;
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path dir = fs::temp_directory_path() / ("aurea-omega-add-" + stamp);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string element = (dir / "elemento.aurea").string();
    const std::string host = (dir / "principal.aurea").string();
    Engine e; AUREA_CHECK(e.initialize(omega_config(nullptr)).ok());
    // O "elemento": 320×180, duas formas.
    AUREA_CHECK(e.new_project(320, 180, 30., "Elemento").ok());
    AUREA_CHECK(e.add_shape(0).ok());
    AUREA_CHECK(e.add_shape(10).ok());
    AUREA_CHECK(e.save_project(element.c_str()).ok());
    // O projeto que recebe: 640×360.
    AUREA_CHECK(e.new_project(640, 360, 30., "Principal").ok());
    AUREA_CHECK(e.save_project(host.c_str()).ok());
    seek(e, 12);
    const auto self = e.insert_project_as_layer(host.c_str());
    AUREA_CHECK(!self.ok());
    const auto inserted = e.insert_project_as_layer(element.c_str());
    AUREA_CHECK(inserted.ok());
    if (inserted.ok()) {
        const Layer* g = layer_of(e, *inserted);
        AUREA_CHECK(g && g->kind == LayerKind::Composition);
        if (g) {
            AUREA_CHECK_EQ(g->start.value, 12ll);
            AUREA_CHECK_NEAR(g->transform.scale.x, 2.0f, 1e-4f);   // 320 cabe em 640
            const Composition* child = e.project()->timeline().composition(g->nested.composition);
            AUREA_CHECK(child != nullptr);
            if (child) {
                AUREA_CHECK_EQ(child->order().size(), 2u);
                AUREA_CHECK(g->nested.composition != e.project()->timeline().current());
            }
        }
        AUREA_CHECK_EQ(e.read_status().layerCount, 1u);
        undo(e);
        AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
    }
    e.shutdown();
    fs::remove_all(dir, ec);
}
