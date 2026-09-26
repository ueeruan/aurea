// Testes da fachada: o contrato que a bridge JNI e a ObjC++ enxergam.
//
// Rodam headless — sem janela e sem GPU. Isso é deliberado: a timeline, a
// animação, os comandos e a serialização precisam funcionar sem backend
// gráfico, e é o que permite testá-los no CI.
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/project/FileIO.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <filesystem>
#include <chrono>

using namespace aurea;

namespace {

EngineConfig headless_config() {
    EngineConfig cfg;
    // Sem backend: config.backend nulo. Timeline, comandos e serialização
    // funcionam sem GPU.
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

} // namespace

AUREA_TEST(Engine, InitializeHeadlessSucceeds) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Ready);
    e.shutdown();
    AUREA_CHECK_EQ(e.state(), EngineState::Uninitialized);
}

AUREA_TEST(Engine, DoubleInitializeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.initialize(headless_config()).ok());
    e.shutdown();
}

AUREA_TEST(Engine, NewProjectCreatesComposition) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 60.0, "Meu projeto").ok());

    const Project* p = e.project();
    AUREA_CHECK(p != nullptr);
    AUREA_CHECK(p->timeline().root().valid());
    AUREA_CHECK(p->timeline().current().valid());

    const Composition* c = p->timeline().composition(p->timeline().root());
    AUREA_CHECK(c != nullptr);
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1920));
    AUREA_CHECK_EQ(c->height(), static_cast<u32>(1080));
    AUREA_CHECK_NEAR(c->fps(), 60.0, 1e-9);
    e.shutdown();
}

AUREA_TEST(Engine, KeyframeEasingQueryKeepsTrackAddressAndLocalTime) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    const auto layer = LayerId::unpack(*added);
    Command insert;
    insert.type = CommandType::KeyframeInsert;
    insert.keyframe.track = TrackRef{layer, TrackProperty::ShapeParam, kInvalidIndex, 5};
    insert.keyframe.time = FrameIndex{18};
    insert.keyframe.value = 320;
    AUREA_CHECK(e.apply_command(insert).ok());
    Command easing;
    easing.type = CommandType::KeyframeSetInterpolation;
    easing.keyframe_interp.track = insert.keyframe.track;
    easing.keyframe_interp.time = FrameIndex{18};
    easing.keyframe_interp.interp = Interpolation::Bezier;
    easing.keyframe_interp.bx1 = 0.2f; easing.keyframe_interp.by1 = -0.3f;
    easing.keyframe_interp.bx2 = 0.8f; easing.keyframe_interp.by2 = 1.4f;
    AUREA_CHECK(e.apply_command(easing).ok());
    float values[4]{};
    AUREA_CHECK(e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 18, values));
    AUREA_CHECK_NEAR(values[0], 0.2f, 0.0001f);
    AUREA_CHECK_NEAR(values[1], -0.3f, 0.0001f);
    AUREA_CHECK_NEAR(values[2], 0.8f, 0.0001f);
    AUREA_CHECK_NEAR(values[3], 1.4f, 0.0001f);
    AUREA_CHECK(!e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 4, 18, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 19, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, 999, kInvalidIndex, 5, 18, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, 0, kInvalidIndex, 0, 18, nullptr));
    insert.keyframe.time = FrameIndex{48}; insert.keyframe.value = 640;
    AUREA_CHECK(e.apply_command(insert).ok());
    float samples[3]{};
    AUREA_CHECK_EQ(e.query_track_curve(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 18, 48, samples, 3), 3u);
    AUREA_CHECK_NEAR(samples[0], 320, 0.001f);
    AUREA_CHECK_NEAR(samples[2], 640, 0.001f);
    AUREA_CHECK_EQ(e.query_track_curve(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 4, 18, 48, samples, 3), 0u);
    AUREA_CHECK_EQ(e.query_track_curve(*added, 999, kInvalidIndex, 5, 18, 48, samples, 3), 0u);
    e.shutdown();
}

AUREA_TEST(Engine, ObjectHdriImportPreservesSceneAndAssetIdentity) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const auto object = comp->add_layer(LayerKind::Model3D, "objeto");
    const auto path = (std::filesystem::temp_directory_path() / ("aurea_object_hdri_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hdr")).string();
    FILE* file = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(file != nullptr);
    const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    std::fwrite(header, 1, sizeof(header) - 1, file);
    const u8 pixel[4] = {128, 64, 32, 129};
    for (int n = 0; n < 4; ++n) std::fwrite(pixel, 1, 4, file);
    std::fclose(file);
    const auto scene = e.import_hdri(path.c_str());
    const auto own = e.import_hdri(path.c_str(), object.pack());
    std::remove(path.c_str());
    AUREA_CHECK(scene.ok()); AUREA_CHECK(own.ok());
    AUREA_CHECK_EQ(comp->environment().hdri.pack(), *scene);
    f32 values[5]{}; u64 asset = 0;
    AUREA_CHECK(e.query_object_environment(object.pack(), values, &asset));
    AUREA_CHECK_EQ(asset, *own); AUREA_CHECK_EQ(values[0], 1.f);
    const u64 precise = (1ull << 40) | 123456789ull;
    AUREA_CHECK(e.set_object_environment(object.pack(), 1, precise, 1, 0, 1));
    AUREA_CHECK(e.query_object_environment(object.pack(), values, &asset));
    AUREA_CHECK_EQ(asset, precise);
    e.shutdown();
}

namespace {
struct HdriPathFixture {
    std::filesystem::path root;
    bool owns = false;
    HdriPathFixture() {
        const auto temporary = std::filesystem::temp_directory_path();
        root = temporary / ("aurea_hdri_portability_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code error;
        owns = std::filesystem::create_directory(root, error);
    }
    ~HdriPathFixture() {
        // Delete only the unique directory this fixture actually created.
        std::error_code error;
        if (owns && root.is_absolute() && root.parent_path() == std::filesystem::temp_directory_path(error)
            && root.filename().string().rfind("aurea_hdri_portability_", 0) == 0)
            std::filesystem::remove_all(root, error);
    }
    static std::string utf8(const std::filesystem::path& path) {
        const auto s = path.generic_u8string();
        return {reinterpret_cast<const char*>(s.data()), s.size()};
    }
    static bool write(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        FILE* f = fileio::open_file(utf8(path), "wb");
        if (!f) return false;
        const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
        const u8 pixel[4] = {128, 32, 16, 129};
        bool ok = std::fwrite(header, 1, sizeof(header) - 1, f) == sizeof(header) - 1;
        for (int n = 0; n < 4; ++n) ok = std::fwrite(pixel, 1, 4, f) == 4 && ok;
        return std::fclose(f) == 0 && ok;
    }
};
} // namespace

AUREA_TEST(Engine, HdriInternalPathSurvivesSaveAndRelocatedDocuments) {
    HdriPathFixture fixture;
    AUREA_CHECK(fixture.owns);
    const auto oldDocs = fixture.root / "old" / "Documents";
    const auto newDocs = fixture.root / "new" / "Documents";
    const auto media = oldDocs / "modelos" / "environment.hdr";
    const auto companion = newDocs / "modelos" / "environment.hdr";
    AUREA_CHECK(HdriPathFixture::write(media));
    AUREA_CHECK(HdriPathFixture::write(companion));
    const auto projectPath = HdriPathFixture::utf8(fixture.root / "portable.aurea");
    u64 assetId = 0;
    {
        Engine e;
        auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(oldDocs);
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, "portable HDRI").ok());
        const auto imported = e.import_hdri(HdriPathFixture::utf8(media).c_str());
        AUREA_CHECK(imported.ok());
        assetId = *imported;
        const Asset* asset = e.project()->asset(AssetId::unpack(assetId));
        AUREA_CHECK(asset && asset->sourcePath == "docs:modelos/environment.hdr");
        AUREA_CHECK(e.save_project(projectPath.c_str()).ok());
        e.shutdown();
    }
    std::error_code error;
    AUREA_CHECK(std::filesystem::remove(media, error)); // old sandbox is unavailable
    {
        Engine e;
        auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(newDocs);
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.load_project(projectPath.c_str()).ok());
        const Asset* asset = e.project()->asset(AssetId::unpack(assetId));
        AUREA_CHECK(asset && asset->sourcePath == "docs:modelos/environment.hdr");
        const auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        AUREA_CHECK_EQ(comp->environment().hdri.pack(), assetId);
        // The real Radiance decoder consumes the saved reference through the
        // same resolver as hdri_lookup; no private API or fake media loader.
        const std::string savedPath = asset->sourcePath;
        AUREA_CHECK(e.import_hdri(savedPath.c_str()).ok());
        AUREA_CHECK(e.save_project(projectPath.c_str()).ok());
        AUREA_CHECK(e.load_project(projectPath.c_str()).ok());
        e.shutdown();
    }
}

AUREA_TEST(Engine, HdriLegacyAndroidPathResolvesOnlyExistingContainedCompanion) {
    HdriPathFixture fixture;
    AUREA_CHECK(fixture.owns);
    const auto docs = fixture.root / "Documents";
    AUREA_CHECK(HdriPathFixture::write(docs / "modelos" / "environment.hdr"));
    AUREA_CHECK(HdriPathFixture::write(fixture.root / "outside.hdr"));
    AUREA_CHECK(HdriPathFixture::write(fixture.root / "Documents-other" / "external.hdr"));
    Engine e;
    auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(docs);
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "legacy HDRI").ok());
    const char* legacy = "/data/user/0/com.aurea.aurea/files/modelos/environment.hdr";
    auto imported = e.import_hdri(legacy);
    AUREA_CHECK(imported.ok());
    AUREA_CHECK(e.project()->asset(AssetId::unpack(*imported))->sourcePath == "docs:modelos/environment.hdr");
    AUREA_CHECK(e.import_hdri("/data/data/com.aurea.aurea/files/modelos/environment.hdr").ok());
    // Simulate the original serialized Android reference, without rewriting
    // an external golden fixture. Loading preserves it until a new import.
    e.project()->asset(AssetId::unpack(*imported))->sourcePath = legacy;
    const auto path = HdriPathFixture::utf8(fixture.root / "legacy.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    const auto stored = e.project()->asset(AssetId::unpack(*imported))->sourcePath;
    AUREA_CHECK_EQ(stored, std::string(legacy));
    AUREA_CHECK(e.import_hdri(stored.c_str()).ok());
    for (const char* invalid : {
        "/data/user/0/com.aurea.aurea/files/modelos/missing.hdr",
        "/data/user/0/com.other.application/files/modelos/environment.hdr",
        "/data/user/0/com.aurea.aurea.evil/files/modelos/environment.hdr",
        "/data/user/0/com.aurea.aurea/files/../outside.hdr",
        "/data/user/0/com.aurea.aurea/files/..\\outside.hdr",
        "docs:../outside.hdr", "docs:..\\outside.hdr",
        "docs:/modelos/environment.hdr", "docs:C:/outside.hdr",
        "docs:modelos/environment.hdr:stream"}) {
        AUREA_CHECK(!e.import_hdri(invalid).ok());
    }
    AUREA_CHECK(e.import_hdri("docs:modelos\\environment.hdr").ok());
    const auto outside = HdriPathFixture::utf8(fixture.root / "Documents-other" / "external.hdr");
    const auto external = e.import_hdri(outside.c_str());
    AUREA_CHECK(external.ok());
    AUREA_CHECK_EQ(e.project()->asset(AssetId::unpack(*external))->sourcePath, outside);
    std::error_code error;
    std::filesystem::create_symlink(fixture.root / "outside.hdr", docs / "escape.hdr", error);
    if (!error) {
        AUREA_CHECK(!e.import_hdri("docs:escape.hdr").ok());
        AUREA_CHECK(!e.import_hdri("/data/user/0/com.aurea.aurea/files/escape.hdr").ok());
    } else {
        std::printf("    symlink containment not exercised: host denied symlink creation\n");
    }
    e.shutdown();
}

AUREA_TEST(Engine, CommandsThroughTheQueueReachTheModel) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    // O caminho REAL da UI: escreve na fila, o motor drena no frame. Testar
    // pelo atalho `apply_command` não cobriria o contrato da fila.
    const char name[] = "Camada do usuario";
    u32 offset = 0, length = 0;
    AUREA_CHECK(e.commands().push_string(name, sizeof(name) - 1, offset, length));

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.stringOffset = offset;
    create.stringLength = length;

    Command opacity;
    opacity.type = CommandType::LayerSetOpacity;
    opacity.opacity.layer = LayerId{};
    opacity.opacity.opacity = 0.4f;

    const Command batch[1] = {create};
    AUREA_CHECK_EQ(e.submit_commands(batch, 1), static_cast<u32>(1));

    AUREA_CHECK(e.render_frame().ok());

    const Project* p = e.project();
    const Composition* c = p->timeline().composition(p->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    const Layer* l = c->layer(c->order().at(0));
    AUREA_CHECK(l != nullptr);
    AUREA_CHECK_EQ(l->name, std::string("Camada do usuario"));

    (void)opacity;
    e.shutdown();
}

AUREA_TEST(Engine, SubmitCommandsAcceptsPartialBatch) {
    // A fila nunca bloqueia: se encher, o motor aceita o que couber e a UI
    // reenvia o resto. Nenhum comando é perdido em silêncio.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    std::vector<Command> flood(CommandQueue::kCapacity + 500);
    for (auto& c : flood) c.type = CommandType::Nop;

    const u32 accepted = e.submit_commands(flood.data(), static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted <= static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted > 0);
    e.shutdown();
}

AUREA_TEST(Engine, SaveAppliesQueuedEditsBeforeSnapshotWithoutRendering) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "Queued save").ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    if (!added.ok()) { e.shutdown(); return; }
    const auto path = (std::filesystem::temp_directory_path() /
        ("aurea_queued_save_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".aurea")).string();

    // Both bridges submit to this queue. Saving immediately after an edit
    // must include it even when no preview frame or thumbnail has run.
    for (u32 pass = 0; pass < 2; ++pass) {
        const f32 rotation = pass == 0 ? 15.0f : 30.0f;
        const f32 opacity = pass == 0 ? 0.75f : 0.5f;
        Command edits[2];
        edits[0].type = CommandType::LayerSetRotation;
        edits[0].rotation.layer = LayerId::unpack(*added);
        edits[0].rotation.rx = 0; edits[0].rotation.ry = 0; edits[0].rotation.rz = rotation;
        edits[1].type = CommandType::LayerSetOpacity;
        edits[1].opacity.layer = LayerId::unpack(*added);
        edits[1].opacity.opacity = opacity;
        AUREA_CHECK_EQ(e.submit_commands(edits, 2), 2u);
        AUREA_CHECK_EQ(e.commands().available(), 2u);
        AUREA_CHECK((pass == 0 ? e.save_project(path.c_str()) : e.save_project()).ok());
        AUREA_CHECK_EQ(e.commands().available(), 0u);

        // A separate engine ensures queued in-memory edits cannot disguise
        // a stale file when it is reopened (including the pathless overload).
        Engine reopened;
        AUREA_CHECK(reopened.initialize(headless_config()).ok());
        AUREA_CHECK(reopened.load_project(path.c_str()).ok());
        const Composition* comp = reopened.project()->timeline().composition(reopened.project()->timeline().current());
        AUREA_CHECK(comp != nullptr);
        if (comp) {
            AUREA_CHECK_EQ(comp->layers().count(), 1u);
            const Layer* layer = comp->order().size() ? comp->layer(comp->order().at(0)) : nullptr;
            AUREA_CHECK(layer != nullptr);
            if (layer) {
                AUREA_CHECK_NEAR(layer->transform.rotation.z, rotation, 0.0001f);
                AUREA_CHECK_NEAR(layer->transform.opacity, opacity, 0.0001f);
            }
        }
        reopened.shutdown();
    }
    e.shutdown();
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

AUREA_TEST(Engine, AddLayerViaCommandThenQueryRows) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 5; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = (i % 2) ? LayerKind::Text : LayerKind::Video;
        AUREA_CHECK(e.apply_command(c).ok());
    }

    bridge::LayerRow rows[16];
    char names[512];
    const u32 n = e.query_layers(rows, 16, names, sizeof(names));
    AUREA_CHECK_EQ(n, static_cast<u32>(5));

    // A lista vem da FRENTE para o fundo: é o que a UI mostra, e inverter na UI
    // seria a mesma regra em dois lugares — uma chance de divergirem.
    AUREA_CHECK(rows[0].zIndex < rows[4].zIndex);
    for (u32 i = 0; i < n; ++i) {
        AUREA_CHECK(rows[i].nameLength > 0);
    }
    e.shutdown();
}

AUREA_TEST(Engine, QueryLayersRespectsCapacity) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 20; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = LayerKind::Video;
        (void)e.apply_command(c);
    }

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(4));
    e.shutdown();
}

AUREA_TEST(Engine, LayerVisibilityFlagIsReported) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.correlationId = 7;
    (void)e.apply_command(create);

    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command hide;
    hide.type = CommandType::LayerSetVisible;
    hide.layer_visible.layer = id;
    hide.layer_visible.visible = false;
    AUREA_CHECK(e.apply_command(hide).ok());

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(1));
    AUREA_CHECK_EQ(rows[0].flags & 1u, static_cast<u32>(0));   // bit 0 = visivel
    e.shutdown();
}

AUREA_TEST(Engine, LayerSplitProducesTwoAdjacentLayers) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId original = c->order().at(0);
    c->layer(original)->start = FrameIndex{0};
    c->layer(original)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = original;
    split.layer_split.at = FrameIndex{40};
    AUREA_CHECK(e.apply_command(split).ok());

    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(2));

    const Layer* first = c->layer(original);
    AUREA_CHECK_EQ(first->start.value, static_cast<i64>(0));
    AUREA_CHECK_EQ(first->end.value, static_cast<i64>(40));

    // A segunda metade continua de onde a primeira parou. Sem ajustar o offset,
    // a segunda repetiria o começo do vídeo — o erro clássico de corte.
    LayerId second{};
    c->layers().for_each([&](LayerId id, const Layer&) {
        if (!(id == original)) second = id;
    });
    AUREA_CHECK(second.valid());
    const Layer* s = c->layer(second);
    AUREA_CHECK_EQ(s->start.value, static_cast<i64>(40));
    AUREA_CHECK_EQ(s->end.value, static_cast<i64>(100));
    AUREA_CHECK_EQ(s->offset.value, static_cast<i64>(40));
    e.shutdown();
}

AUREA_TEST(Engine, SplitOutsideRangeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{0};
    c->layer(id)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{500};
    AUREA_CHECK(!e.apply_command(split).ok());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, ParentingCycleIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command createA;
    createA.type = CommandType::LayerCreate;
    createA.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createA);
    Command createB;
    createB.type = CommandType::LayerCreate;
    createB.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createB);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId a = c->order().at(0);
    const LayerId b = c->order().at(1);

    Command parent;
    parent.type = CommandType::LayerSetParent;
    parent.layer_parent.layer = a;
    parent.layer_parent.parent = b;
    AUREA_CHECK(e.apply_command(parent).ok());

    // Fechar o ciclo faria a avaliação de transform entrar em laço infinito.
    Command cycle;
    cycle.type = CommandType::LayerSetParent;
    cycle.layer_parent.layer = b;
    cycle.layer_parent.parent = a;
    AUREA_CHECK(!e.apply_command(cycle).ok());
    e.shutdown();
}

AUREA_TEST(Engine, ParentingKeepsChildInPlace) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    auto pid = e.add_null(false);
    auto cid = e.add_null(false);
    AUREA_CHECK(pid.ok() && cid.ok());
    Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* par = c->layer(LayerId::unpack(*pid));
    Layer* ch = c->layer(LayerId::unpack(*cid));
    AUREA_CHECK(par && ch && par->kind == LayerKind::Null);
    par->transform.position = Vec3{300, 200, 0};
    par->transform.rotation = Vec3{0, 0, 30};
    par->transform.scale = Vec3{2, 2, 1};
    ch->transform.position = Vec3{900, 500, 0};
    ch->transform.rotation = Vec3{0, 0, -10};
    const FrameIndex t0{0};
    auto corner = [&](const Mat4& m, f32 x, f32 y) { return m * Vec4{x, y, 0, 1}; };
    const Mat4 before = layer_world_matrix(*c, *ch, t0);
    bridge::LayerDetailPOD d0, d1;
    AUREA_CHECK(e.query_layer_detail(*cid, d0));
    AUREA_CHECK((d0.geomFlags & bridge::kGeomCornersValid) != 0);
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*cid);
    pc.layer_parent.parent = LayerId::unpack(*pid);
    AUREA_CHECK(e.apply_command(pc).ok());
    const Mat4 after = layer_world_matrix(*c, *ch, t0);
    f32 worst = 0;
    for (f32 x : {0.0f, 100.0f}) for (f32 y : {0.0f, 100.0f}) {
        const Vec4 a = corner(before, x, y), b = corner(after, x, y);
        worst = std::max({worst, std::fabs(a.x - b.x), std::fabs(a.y - b.y)});
    }
    std::printf("    filho: escala local %.3f rot %.2f; desvio %.4f px\n", ch->transform.scale.x, ch->transform.rotation.z, worst);
    AUREA_CHECK(worst < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.scale.x - 0.5f) < 1e-3f);
    AUREA_CHECK(std::fabs(ch->transform.rotation.z + 40.0f) < 1e-2f);
    AUREA_CHECK(ch->transform.rotation.x == 0.0f && ch->transform.rotation.y == 0.0f);
    // O palco recebe os mesmos cantos (mundo) e o afim do pai.
    AUREA_CHECK(e.query_layer_detail(*cid, d1));
    f32 cw = 0;
    for (int i = 0; i < 8; ++i) cw = std::max(cw, std::fabs(d0.corners[i] - d1.corners[i]));
    AUREA_CHECK(cw < 0.05f);
    AUREA_CHECK(std::fabs(d1.parentAffine[0] - 2.0f * std::cos(30.0f * 3.14159265f / 180.0f)) < 1e-3f);
    AUREA_CHECK(std::fabs(d1.parentAffine[4] - (300.0f - 2.0f * (50.0f * std::cos(0.5235988f) - 50.0f * std::sin(0.5235988f)))) < 0.05f);
    // Mover o pai arrasta o filho.
    par->transform.position.x += 50;
    const Vec4 moved = corner(layer_world_matrix(*c, *ch, t0), 0, 0);
    AUREA_CHECK(std::fabs(moved.x - corner(after, 0, 0).x - 50.0f) < 0.05f);
    par->transform.position.x -= 50;
    // Soltar o pai: volta ao mesmo lugar, transform original.
    pc.layer_parent.parent = LayerId{};
    AUREA_CHECK(e.apply_command(pc).ok());
    const Mat4 freed = layer_world_matrix(*c, *ch, t0);
    f32 worst2 = 0;
    for (f32 x : {0.0f, 100.0f}) for (f32 y : {0.0f, 100.0f}) {
        const Vec4 a = corner(before, x, y), b = corner(freed, x, y);
        worst2 = std::max({worst2, std::fabs(a.x - b.x), std::fabs(a.y - b.y)});
    }
    AUREA_CHECK(worst2 < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.position.x - 900.0f) < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.scale.x - 1.0f) < 1e-3f);
    e.shutdown();
}

AUREA_TEST(Engine, ParentSurvivesSaveAndReopenAfterReorder) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    auto a = e.add_null(false);   // slot 0
    auto b = e.add_null(false);   // slot 1
    auto c = e.add_null(false);   // slot 2
    AUREA_CHECK(a.ok() && b.ok() && c.ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->layer(LayerId::unpack(*a))->name = "filho";
    comp->layer(LayerId::unpack(*c))->name = "pai";
    // Ordem vertical diferente da de criação: o pai vai para o fundo.
    Command ro;
    ro.type = CommandType::LayerReorder;
    ro.layer_reorder.layer = LayerId::unpack(*c);
    ro.layer_reorder.newIndex = 0;
    AUREA_CHECK(e.apply_command(ro).ok());
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*a);
    pc.layer_parent.parent = LayerId::unpack(*c);
    AUREA_CHECK(e.apply_command(pc).ok());
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_pai.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* child = nullptr;
    comp->layers().for_each([&](LayerId, const Layer& l) { if (l.name == "filho") child = &l; });
    AUREA_CHECK(child != nullptr);
    const Layer* par = child ? comp->layer(child->parent) : nullptr;
    AUREA_CHECK(par != nullptr);
    AUREA_CHECK(par && par->name == "pai");
    e.shutdown();
    std::remove(path.c_str());
}

AUREA_TEST(Engine, SelectionIsSortedAndDeduplicated) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const u64 ids[] = {50, 10, 50, 30};
    e.set_selection(ids, 4);
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(3));
    AUREA_CHECK(e.is_selected(10));
    AUREA_CHECK(e.is_selected(30));
    AUREA_CHECK(e.is_selected(50));
    AUREA_CHECK(!e.is_selected(20));

    u64 out[8];
    AUREA_CHECK_EQ(e.get_selection(out, 8), static_cast<u32>(3));
    AUREA_CHECK_EQ(out[0], static_cast<u64>(10));

    e.clear_selection();
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(0));
    e.shutdown();
}

AUREA_TEST(Engine, SeekUpdatesPlayhead) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = TickNs{1'000'000'000};   // 1 segundo
    AUREA_CHECK(e.apply_command(seek).ok());

    // 1 s a 30 fps = frame 30.
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(30));
    e.shutdown();
}

AUREA_TEST(Engine, PlayPauseControlsClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.project()->timeline().playing());

    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.project()->timeline().playing());
    e.shutdown();
}

AUREA_TEST(Engine, ExtendingFiveSecondClipExtendsPlaybackAndUndoRestoresBoth) {
    for (bool ripple : {false, true}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
        auto& timeline = e.project()->timeline();
        auto* c = timeline.composition(timeline.current());
        Command duration; duration.type = CommandType::CompositionSetDuration;
        duration.comp_duration.comp = timeline.current(); duration.comp_duration.duration = FrameIndex{150};
        AUREA_CHECK(e.apply_command(duration).ok());
        const auto id = c->add_layer(LayerKind::Shape, "five seconds");
        c->set_edit_mode(ripple);
        Command trim; trim.type = CommandType::LayerSetTimeRange;
        trim.layer_range.layer = id; trim.layer_range.start = FrameIndex{0}; trim.layer_range.end = FrameIndex{900};
        AUREA_CHECK(e.apply_command(trim).ok());
        AUREA_CHECK_EQ(e.read_status().duration.value, 900);
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{600}, 30);
        AUREA_CHECK(e.apply_command(seek).ok());
        AUREA_CHECK_EQ(e.read_status().playhead.value, 600);
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(undo).ok());
        c = timeline.composition(timeline.current());
        AUREA_CHECK_EQ(c->duration().value, 150);
        AUREA_CHECK_EQ(c->layer(id)->end.value, 150);
        e.shutdown();
    }
}

AUREA_TEST(Engine, CompositionResolutionKeepsVideoFramingAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
    auto& timeline = e.project()->timeline();
    auto* c = timeline.composition(timeline.current());
    const auto id = c->add_layer(LayerKind::Video, "video");
    c->layer(id)->transform.position = Vec3{640, 360, 0};
    c->layer(id)->transform.scale = Vec3{0.5f, 0.5f, 1};
    Command size; size.type = CommandType::CompositionSetSize;
    size.comp_size.comp = timeline.current(); size.comp_size.width = 640; size.comp_size.height = 360;
    AUREA_CHECK(e.apply_command(size).ok());
    AUREA_CHECK_NEAR(c->layer(id)->transform.position.x / c->width(), 0.5f, 0.001f);
    AUREA_CHECK_NEAR(c->layer(id)->transform.scale.x, 0.25f, 0.001f);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    c = timeline.composition(timeline.current());
    AUREA_CHECK_EQ(c->width(), 1280u);
    AUREA_CHECK_NEAR(c->layer(id)->transform.position.x, 640, 0.001f);
    AUREA_CHECK_NEAR(c->layer(id)->transform.scale.x, 0.5f, 0.001f);
    e.shutdown();
}

AUREA_TEST(Engine, CompositionSizeBeyondDeviceIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());

    Command tooBig;
    tooBig.type = CommandType::CompositionSetSize;
    tooBig.comp_size.comp = e.project()->timeline().current();
    tooBig.comp_size.width = 16384;
    tooBig.comp_size.height = 16384;
    // Acima do que o aparelho decodifica, o preview não acompanha e o export
    // não fecha. Recusar com código claro é melhor que aceitar e o usuário
    // descobrir no export.
    AUREA_CHECK(!e.apply_command(tooBig).ok());
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1280));
    e.shutdown();
}

AUREA_TEST(Engine, SeekMovesThePlayheadAndRenderKeepsIt) {
    // Parado, o playhead é o que o usuário pediu: renderizar não o move.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(10));
    AUREA_CHECK_EQ(e.read_status().playhead.value, static_cast<i64>(10));
    e.shutdown();
}

AUREA_TEST(Engine, PlayAdvancesThePlayheadFromTheClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.read_status().playing);
    // O relógio anda em tempo real: 120 ms depois, pelo menos 2 frames.
    const u64 t0 = monotonic_ns();
    while (monotonic_ns() - t0 < 120'000'000ull) {}
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.project()->timeline().playhead().value >= 2);
    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.read_status().playing);
    e.shutdown();
}

AUREA_TEST(Engine, AnimationSkipsLayersOutsideTheirRange) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{1000};
    c->layer(id)->end = FrameIndex{2000};

    AUREA_CHECK(e.render_frame().ok());

    bridge::LayerRow rows[4];
    // A camada existe na timeline, mas o motor não a avaliou — ela está fora
    // do tempo. Avaliar camadas inativas é trabalho jogado fora a 60 Hz.
    AUREA_CHECK_EQ(e.query_layers(rows, 4, nullptr, 0), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, StatusReflectsProjectState) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Status").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);
    (void)e.render_frame();

    const EngineStatus st = e.read_status();
    AUREA_CHECK_EQ(st.layerCount, static_cast<u32>(1));
    AUREA_CHECK_EQ(st.duration.value, static_cast<i64>(300));
    AUREA_CHECK(!st.playing);
    AUREA_CHECK(st.previewDenominator >= 1);
    e.shutdown();
}

AUREA_TEST(Engine, TelemetryReportsWorkersAndQueues) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const EngineTelemetry t = e.read_telemetry();
    AUREA_CHECK_EQ(t.workerCount, static_cast<u32>(2));
    AUREA_CHECK_EQ(t.commandsDropped, static_cast<u64>(0));
    e.shutdown();
}

AUREA_TEST(Engine, ExportWithoutEncoderIsRefusedNotFaked) {
    // Sem GPU ou sem encoder da plataforma, o export é recusado na hora — nada
    // de "ok" seguido de um arquivo vazio que o usuário acharia que é o
    // trabalho dele.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    ExportSettings settings;
    const Status s = e.start_export(settings, "saida.mp4");
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::NotSupported);
    AUREA_CHECK(!e.export_progress().running);

    std::FILE* f = std::fopen("saida.mp4", "rb");
    AUREA_CHECK(f == nullptr);
    if (f) std::fclose(f);
    e.shutdown();
}

AUREA_TEST(Engine, SuspendAndResumeKeepProject) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Sobrevive").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    AUREA_CHECK(e.suspend().ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Suspended);

    AUREA_CHECK(e.resume().ok());

    // Suspender NÃO perde trabalho: o app pode ser morto em background a
    // qualquer momento, e o projeto tem que estar lá quando ele voltar.
    AUREA_CHECK(e.project() != nullptr);
    AUREA_CHECK_EQ(e.project()->metadata().title, std::string("Sobrevive"));
    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, RenderWithoutProjectFails) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.render_frame().ok());
    e.shutdown();
}

AUREA_TEST(Engine, UnknownEffectTypeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command addEffect;
    addEffect.type = CommandType::EffectAdd;
    addEffect.effect_add.layer = id;
    addEffect.effect_add.effectType = 4242;   // nao registrado
    AUREA_CHECK(!e.apply_command(addEffect).ok());
    AUREA_CHECK_EQ(c->layer(id)->effects.size(), static_cast<usize>(0));
    e.shutdown();
}

AUREA_TEST(Engine, MaskOperationsOnMissingMaskAreRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command maskOp;
    maskOp.type = CommandType::MaskSetOperation;
    maskOp.mask_op.layer = id;
    maskOp.mask_op.mask = MaskId{99, 1};
    maskOp.mask_op.op = MaskOperation::Subtract;
    AUREA_CHECK(!e.apply_command(maskOp).ok());
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Histórico (desfazer/refazer por snapshot da composição)
// -----------------------------------------------------------------------------
namespace {

LayerId first_layer(Engine& e) {
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    LayerId id{};
    c->layers().for_each([&](LayerId lid, const Layer&) { if (!id.valid()) id = lid; });
    return id;
}

const Layer* layer_of(Engine& e, LayerId id) {
    return e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
}

Command position_cmd(LayerId id, f32 x, f32 y) {
    Command c;
    c.type = CommandType::LayerSetPosition;
    c.position.layer = id;
    c.position.x = x;
    c.position.y = y;
    return c;
}

} // namespace

AUREA_TEST(History, UndoAndRedoRestoreTheExactValue) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(id.valid());

    AUREA_CHECK(e.apply_command(position_cmd(id, 100, 200)).ok());
    AUREA_CHECK(e.apply_command(position_cmd(id, 300, 400)).ok());
    AUREA_CHECK(e.read_status().canUndo);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 100.0f, 1e-6f);
    AUREA_CHECK(e.read_status().canRedo);

    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 300.0f, 1e-6f);

    // Desfaz tudo, inclusive a criação: a layer some; refazer a devolve com o MESMO id.
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, id) == nullptr);
    AUREA_CHECK(!e.read_status().canUndo);
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK(layer_of(e, id) != nullptr);
    e.shutdown();
}

AUREA_TEST(History, AGestureGroupUndoesAtOnce) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(e.apply_command(position_cmd(id, 10, 10)).ok());
    const u32 depthBefore = e.read_status().undoDepth;

    Command begin;
    begin.type = CommandType::UndoBeginGroup;
    AUREA_CHECK(e.apply_command(begin, "arrastar").ok());
    for (int i = 1; i <= 60; ++i) AUREA_CHECK(e.apply_command(position_cmd(id, 10.0f + i, 10.0f)).ok());
    Command end;
    end.type = CommandType::UndoEndGroup;
    AUREA_CHECK(e.apply_command(end).ok());
    AUREA_CHECK_EQ(e.read_status().undoDepth, depthBefore + 1);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 10.0f, 1e-6f);
    e.shutdown();
}

AUREA_TEST(History, SplitAndEffectAreUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command range;
    range.type = CommandType::LayerSetTimeRange;
    range.layer_range.layer = id;
    range.layer_range.start = FrameIndex{0};
    range.layer_range.end = FrameIndex{90};
    AUREA_CHECK(e.apply_command(range).ok());

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{30};
    AUREA_CHECK(e.apply_command(split).ok());
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 2u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(30));

    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = id;
    fx.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    fx.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(fx).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(1));

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(0));
    AUREA_CHECK(e.apply_command(undo).ok());
    c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 1u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(90));
    e.shutdown();
}

AUREA_TEST(History, DeleteAndFpsChangeAreUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(e.apply_command(position_cmd(id, 123, 45)).ok());
    Command range;
    range.type = CommandType::LayerSetTimeRange;
    range.layer_range.layer = id;
    range.layer_range.start = FrameIndex{0};
    range.layer_range.end = FrameIndex{90};
    AUREA_CHECK(e.apply_command(range).ok());

    // Apagar e desfazer: volta com o MESMO id e o mesmo estado.
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = id;
    AUREA_CHECK(e.apply_command(del).ok());
    AUREA_CHECK(layer_of(e, id) == nullptr);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, id) != nullptr);
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 123.0f, 1e-6f);

    // 30 -> 60 fps preserva os segundos; desfazer volta a taxa E os frames.
    Command fps;
    fps.type = CommandType::CompositionSetFps;
    fps.comp_fps.comp = e.project()->timeline().current();
    fps.comp_fps.fps = 60.0;
    AUREA_CHECK(e.apply_command(fps).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(180));
    AUREA_CHECK_EQ(e.read_status().compFps, 60.0f);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(90));
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->fps(), 30.0);
    e.shutdown();
}

AUREA_TEST(History, TrimStartKeepsContentWithOffset) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command trim;
    trim.type = CommandType::LayerSetTimeRange;
    trim.layer_range.layer = id;
    trim.layer_range.start = FrameIndex{12};
    trim.layer_range.end = FrameIndex{60};
    trim.layer_range.offset = FrameIndex{12};
    trim.layer_range.setOffset = 1;
    AUREA_CHECK(e.apply_command(trim).ok());
    const Layer* l = layer_of(e, id);
    AUREA_CHECK_EQ(l->offset.value, static_cast<i64>(12));
    // O conteúdo não andou: o frame local no instante 20 continua 20.
    AUREA_CHECK_EQ(l->local_time(FrameIndex{20}).value, static_cast<i64>(20));

    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(id.pack(), d));
    AUREA_CHECK_EQ(d.startFrame, 12);
    AUREA_CHECK_EQ(d.offsetFrames, 12);
    e.shutdown();
}

AUREA_TEST(Engine, PortraitVideoKeepsAspectInComposition) {
    // Regressão: o teto de export era aplicado por eixo e um vídeo em pé
    // (1080×1920) virava uma composição quadrada 1080×1080.
    for (const auto& [w, h] : {std::pair<u32, u32>{1080, 1920}, std::pair<u32, u32>{2160, 3840},
                               std::pair<u32, u32>{1920, 1080}, std::pair<u32, u32>{8000, 4500}}) {
        test::SyntheticConfig cfg;
        cfg.width = w;
        cfg.height = h;
        test::SyntheticFactory factory(cfg);
        EngineConfig ec = headless_config();
        ec.mediaFactory = &factory;
        Engine e;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "sintetico";
        AUREA_CHECK(e.import_video(vi).ok());
        const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        const u32 capLong = std::max(e.caps().max_export_width(), e.caps().max_export_height());
        const u32 capShort = std::min(e.caps().max_export_width(), e.caps().max_export_height());
        // Proporção do vídeo mantida (até o arredondamento para par)...
        AUREA_CHECK_NEAR(static_cast<f64>(c->width()) / c->height(), static_cast<f64>(w) / h, 0.01);
        // ...e dentro do teto do aparelho nos dois lados.
        AUREA_CHECK(std::max(c->width(), c->height()) <= capLong);
        AUREA_CHECK(std::min(c->width(), c->height()) <= capShort);
        // Vídeo que cabe no teto não é reduzido.
        if (std::max(w, h) <= capLong && std::min(w, h) <= capShort) {
            AUREA_CHECK_EQ(c->width(), w);
            AUREA_CHECK_EQ(c->height(), h);
        }
        e.shutdown();
    }
}

AUREA_TEST(Engine, ImagesComeBackWhenTheProjectIsReopened) {
    // A imagem importada guarda a origem; ao reabrir, o motor pede os pixels à
    // plataforma (imageLoader). Antes, a imagem só existia na sessão.
    static u32 loads = 0;
    loads = 0;
    EngineConfig ec = headless_config();
    ec.imageLoader = [](const char* src, ImagePixels& out, void*) {
        if (std::string(src) != "content://teste/imagem") return false;
        ++loads;
        out.width = 4;
        out.height = 2;
        out.rgba.assign(4 * 2 * 4, 200);
        return true;
    };
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    std::vector<u8> px(4 * 2 * 4, 200);
    const auto id = e.import_image(px.data(), 4, 2, "foto", "content://teste/imagem");
    AUREA_CHECK(id.ok());
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_imagem.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(loads, 1u);
    // A camada volta e a miniatura (feita dos pixels recarregados) existe.
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    LayerId lid{};
    c->layers().for_each([&](LayerId l, const Layer&) { lid = l; });
    std::vector<u8> thumb(64 * 64 * 4);
    u32 w = 0;
    AUREA_CHECK(e.query_thumbnail(lid.pack(), 0, 8, thumb.data(), static_cast<u32>(thumb.size()), &w) > 0);
    AUREA_CHECK_EQ(w, 16u);
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(lid.pack(), d));
    AUREA_CHECK_EQ(d.sourceWidth, 4u);
    AUREA_CHECK_EQ(d.sourceHeight, 2u);
    e.shutdown();
    std::remove(path.c_str());
}

AUREA_TEST(Engine, QueuedStringsAreNotGluedToThePreviousOnes) {
    // As strings da fila ficam coladas no blob, sem terminador. Duas edições
    // de texto seguidas (a digitação da UI): a segunda não pode levar a
    // primeira junto.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    auto id = e.add_text("Texto");
    if (!id.ok()) { e.shutdown(); return; }   // sem fonte no host: nada a verificar
    const char* edits[] = {"Texto A", "Texto Au"};
    for (const char* s : edits) {
        u32 offset = 0, length = 0;
        AUREA_CHECK(e.commands().push_string(s, static_cast<u32>(std::strlen(s)), offset, length));
        Command c;
        c.type = CommandType::TextSetContent;
        c.layer_ref.layer = LayerId::unpack(*id);
        c.stringOffset = offset;
        c.stringLength = length;
        AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
    }
    AUREA_CHECK(e.render_frame().ok());
    TextData t;
    AUREA_CHECK(e.query_text(*id, t));
    AUREA_CHECK_EQ(t.content, std::string("Texto Au"));
    e.shutdown();
}

AUREA_TEST(Engine, BatchStringBlobOffsetsAreRebasedIntoTheQueue) {
    // O caminho da bridge: cada lote traz seu blob com deslocamentos a partir
    // de 0. Dois lotes seguidos com string: o segundo não pode ler a do primeiro.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    auto id = e.add_text("Texto");
    if (!id.ok()) { e.shutdown(); return; }
    const char* blobs[] = {"editar texto", "Texto Aurea"};
    for (int i = 0; i < 2; ++i) {
        Command c;
        c.type = i == 0 ? CommandType::UndoBeginGroup : CommandType::TextSetContent;
        c.layer_ref.layer = LayerId::unpack(*id);
        c.stringOffset = 0;
        c.stringLength = static_cast<u32>(std::strlen(blobs[i]));
        AUREA_CHECK_EQ(e.submit_commands(&c, 1, blobs[i], c.stringLength), 1u);
    }
    AUREA_CHECK(e.render_frame().ok());
    TextData t;
    AUREA_CHECK(e.query_text(*id, t));
    AUREA_CHECK_EQ(t.content, std::string("Texto Aurea"));
    e.shutdown();
}

// =============================================================================
// 7H — papel e organização da camada: ajuste, guia, etiqueta, solo, busca
// =============================================================================
AUREA_TEST(Engine, LayerRoleFlagsReachTheRowsAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok());

    AUREA_CHECK(e.set_layer_adjustment(*b, true));
    AUREA_CHECK(e.set_layer_guide(*a, true));
    AUREA_CHECK(e.set_layer_label(*a, 5));
    AUREA_CHECK(!e.set_layer_label(*a, kLayerLabelCount));   // fora da paleta: recusa
    AUREA_CHECK(e.set_layer_solo(*b, true));
    AUREA_CHECK(!e.set_layer_guide(0xDEADBEEFull, true));

    bridge::LayerRow rows[4];
    char names[256];
    const u32 n = e.query_layers(rows, 4, names, sizeof(names));
    AUREA_CHECK_EQ(n, 2u);
    // A frente primeiro: b, depois a.
    AUREA_CHECK_EQ(rows[0].id, *b);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagAdjustment) != 0);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagSolo) != 0);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagGuide) == 0);
    AUREA_CHECK((rows[1].flags & bridge::kLayerRowFlagGuide) != 0);
    AUREA_CHECK_EQ((rows[1].flags & bridge::kLayerRowLabelMask) >> bridge::kLayerRowLabelShift, 5u);
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(*b, d));
    AUREA_CHECK((d.flags & bridge::kLayerRowFlagAdjustment) != 0 && (d.flags & bridge::kLayerRowFlagSolo) != 0);

    // Cada troca é um passo de desfazer: o último (solo) volta primeiro.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*b))->solo);
    AUREA_CHECK(layer_of(e, LayerId::unpack(*b))->adjustment);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, LayerId::unpack(*a))->label, static_cast<u8>(0));
    e.shutdown();
}

AUREA_TEST(Engine, SearchLayersFoldsCaseAndAccents) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    const auto t = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok() && t.ok());
    Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    c->layer(LayerId::unpack(*a))->name = "TÍTULO Principal";
    c->layer(LayerId::unpack(*b))->name = "Fundo azul";
    // Texto: acha pelo conteúdo, não só pelo nome.
    Layer* tl = c->layer(LayerId::unpack(*t));
    tl->name = "Texto";
    tl->kind = LayerKind::Text;
    tl->text.content = "Coração de São Paulo";

    auto ids = e.search_layers("titulo");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *a);
    ids = e.search_layers("PRINCIPAL");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *a);
    ids = e.search_layers("sao paulo");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *t);
    ids = e.search_layers("CORAÇÃO");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *t);
    ids = e.search_layers("u");   // as três: da frente para o fundo
    AUREA_CHECK(ids.size() == 3 && ids[0] == *t && ids[1] == *b && ids[2] == *a);
    AUREA_CHECK(e.search_layers("").empty());
    AUREA_CHECK(e.search_layers("xyz").empty());
    e.shutdown();
}

// =============================================================================
// Ficha do catálogo de efeitos (Fase 7.3 §20, §68)
//
// `query_effect_specs` é o que o navegador mostra antes de existir camada: a
// DECLARAÇÃO dos parâmetros de um tipo. Sem camada não há valor corrente, e o
// contrato é que `value` saia com o padrão — quem lê a ficha não pode ver lixo
// de memória nem um valor que o efeito nunca teria.
// =============================================================================
AUREA_TEST(Engine, EffectSpecsDescribeEveryParameterOfAType) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());

    const EffectTypeId gaussian = effect_type_id(effect_keys::kGaussianBlur);
    std::vector<bridge::EffectParamRow> rows(16);
    std::vector<char> blob(8 * 1024);
    const u32 n = e.query_effect_specs(gaussian, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                        static_cast<u32>(blob.size()));
    AUREA_CHECK(n > 0);

    // O efeito declara os parâmetros na mesma ordem; o índice é o contrato com
    // o projeto (é ele que vai no keyframe).
    for (u32 i = 0; i < n; ++i) {
        AUREA_CHECK_EQ(rows[i].index, i);
        AUREA_CHECK(rows[i].labelLength > 0);                 // sem rótulo a UI fica muda
        AUREA_CHECK(rows[i].minValue <= rows[i].maxValue);
        for (int c = 0; c < 4; ++c) {
            AUREA_CHECK_EQ(rows[i].value[c], rows[i].defaultValue[c]);
        }
        // Sem instância não há keyframe: a ficha nunca mente dizendo "animado".
        AUREA_CHECK_EQ(rows[i].animated, 0u);
    }

    // Tipo que não existe: nenhuma linha, sem escrever nada.
    AUREA_CHECK_EQ(e.query_effect_specs(0u, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                        static_cast<u32>(blob.size())), 0u);
    // Capacidade menor que o número de parâmetros: corta em vez de estourar.
    AUREA_CHECK_EQ(e.query_effect_specs(gaussian, rows.data(), 1u, blob.data(),
                                        static_cast<u32>(blob.size())), 1u);
    e.shutdown();
}

AUREA_TEST(Engine, EffectSpecsCoverTheWholeCatalog) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());

    std::vector<bridge::EffectCatalogRow> catalog(64);
    std::vector<char> blob(16 * 1024);
    const u32 count = e.query_effect_catalog(catalog.data(), static_cast<u32>(catalog.size()), blob.data(),
                                             static_cast<u32>(blob.size()));
    AUREA_CHECK(count > 0);

    std::vector<bridge::EffectParamRow> rows(64);
    std::vector<char> specBlob(32 * 1024);
    u32 withParams = 0;
    for (u32 i = 0; i < count; ++i) {
        const u32 n = e.query_effect_specs(catalog[i].typeId, rows.data(), static_cast<u32>(rows.size()),
                                           specBlob.data(), static_cast<u32>(specBlob.size()));
        // O catálogo diz quantos parâmetros o efeito tem; a ficha tem de bater.
        AUREA_CHECK_EQ(n, catalog[i].paramCount);
        if (n > 0 && n <= rows.size()) ++withParams;
    }
    // Todo efeito do motor tem ficha — nenhum entra no catálogo mudo.
    AUREA_CHECK_EQ(withParams, count);
    e.shutdown();
}

AUREA_TEST(Engine, ParentingAnimated3DLayerSurvivesTrackStorageGrowth) {
    for (const u32 initialTracks : {1u, 15u, 16u}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
        const auto parentId = e.add_null(true);
        const auto childId = e.add_null(true);
        AUREA_CHECK(parentId.ok() && childId.ok());
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* parent = comp->layer(LayerId::unpack(*parentId));
        Layer* child = comp->layer(LayerId::unpack(*childId));
        parent->transform.position = Vec3{300, 200, 70};
        parent->transform.rotation.z = 30;
        parent->transform.scale = Vec3{2, 2, 2};
        parent->transform.anchor = Vec3{0, 0, 0};
        child->transform.position = Vec3{500, 400, 90};
        child->transform.anchor = Vec3{0, 0, 0};
        Track& x = child->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, 500.0f);
        x.set(FrameIndex{30}, 800.0f);
        for (u32 i = 1; i < initialTracks; ++i) {
            child->tracks.set_static(TrackProperty::EffectParam, static_cast<f32>(i), i, 0);
        }
        if (initialTracks == 1) {
            // History copies need not retain the default 16-track spare capacity.
            TrackSet copied = child->tracks;
            child->tracks = std::move(copied);
        }
        Vec4 expected[31];
        for (i64 frame = 0; frame <= 30; ++frame) {
            expected[frame] = layer_world_3d(*comp, *child, FrameIndex{frame}) * Vec4{0, 0, 0, 1};
        }
        Command command;
        command.type = CommandType::LayerSetParent;
        command.layer_parent.layer = LayerId::unpack(*childId);
        command.layer_parent.parent = LayerId::unpack(*parentId);
        AUREA_CHECK(e.apply_command(command).ok());
        AUREA_CHECK_EQ(child->tracks.size(), initialTracks + 2);
        for (const TrackProperty property : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ}) {
            const Track* track = child->tracks.find(property);
            AUREA_CHECK(track != nullptr);
            AUREA_CHECK_EQ(track->keys.size(), static_cast<usize>(2));
        }
        for (i64 frame = 30; frame >= 0; --frame) {
            const Vec4 actual = layer_world_3d(*comp, *child, FrameIndex{frame}) * Vec4{0, 0, 0, 1};
            AUREA_CHECK_NEAR(actual.x, expected[frame].x, 0.002f);
            AUREA_CHECK_NEAR(actual.y, expected[frame].y, 0.002f);
            AUREA_CHECK_NEAR(actual.z, expected[frame].z, 0.002f);
        }
        for (u32 i = 1; i < initialTracks; ++i) {
            AUREA_CHECK_NEAR(child->tracks.find(TrackProperty::EffectParam, i, 0)->staticValue,
                             static_cast<f32>(i), 1e-6);
        }
        e.shutdown();
    }
}
