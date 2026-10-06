#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/text/FontManager.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <thread>

using namespace aurea;

AUREA_TEST(TransformStability, AnimatedTextDragUpdatesTheDisplayedPositionAtFrame54) {
    Engine e; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(1080, 1080, 30, nullptr).ok());
    const auto text = e.add_text("Text"); AUREA_CHECK(text.ok()); if (!text.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*text));
    layer->start = FrameIndex{20}; layer->offset = FrameIndex{7}; layer->end = FrameIndex{120};
    layer->transform.position = {540, 515, 0};
    for (const auto property : {TrackProperty::PositionX, TrackProperty::PositionY}) {
        auto& track = layer->tracks.get_or_create(property);
        const f32 base = property == TrackProperty::PositionX ? 540.f : 515.f;
        track.set(FrameIndex{7}, base); track.set(FrameIndex{61}, base);
    }
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{54}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    const FrameIndex local = layer->local_time(FrameIndex{54});
    AUREA_CHECK_EQ(local.value, i64{41});
    Command drag[2];
    for (u32 axis = 0; axis < 2; ++axis) {
        drag[axis].type = CommandType::KeyframeInsert;
        drag[axis].keyframe.track = {LayerId::unpack(*text), static_cast<TrackProperty>(axis), kInvalidIndex, 0};
        drag[axis].keyframe.time = local;
        drag[axis].keyframe.value = axis == 0 ? 640.f : 615.f;
        drag[axis].keyframe.onlyIfChanged = true;
    }
    AUREA_CHECK_EQ(e.submit_commands(drag, 2, nullptr, 0), 2u);
    AUREA_CHECK_EQ(e.commands().available(), 2u);
    // The UI submits asynchronously; its next preview frame applies the batch.
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(e.commands().available(), 0u);
    bridge::LayerDetailPOD detail{}; AUREA_CHECK(e.query_layer_detail(*text, detail));
    AUREA_CHECK_NEAR(detail.position[0], 640.f, 1e-4f);
    AUREA_CHECK_NEAR(detail.position[1], 615.f, 1e-4f);
    for (const auto property : {TrackProperty::PositionX, TrackProperty::PositionY}) {
        const auto* track = layer->tracks.find(property);
        AUREA_CHECK_EQ(track->keys.size(), usize{3});
        AUREA_CHECK(track->find_exact(local) != kInvalidIndex);
        AUREA_CHECK(track->find_exact(FrameIndex{54}) == kInvalidIndex);
    }
    e.shutdown();
}

AUREA_TEST(TransformStability, ManualDragReportsExpressionWithoutMutatingFormulaOrKeys) {
    Engine e; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(1080, 1080, 30, nullptr).ok());
    const auto text = e.add_text("Text"); AUREA_CHECK(text.ok()); if (!text.ok()) return;
    constexpr auto property = TrackProperty::PositionX;
    AUREA_CHECK(e.set_expression(*text, static_cast<u32>(property), kInvalidIndex, 0, "540").ok());
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{54}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    Command drag; drag.type = CommandType::KeyframeInsert;
    drag.keyframe.track = {LayerId::unpack(*text), property, kInvalidIndex, 0};
    drag.keyframe.time = FrameIndex{54}; drag.keyframe.value = 640; drag.keyframe.onlyIfChanged = true;
    const auto history = e.read_status().undoDepth;
    const auto blocked = e.apply_command(drag);
    AUREA_CHECK_EQ(blocked.code(), Errc::InvalidState);
    AUREA_CHECK(blocked.detail().find("expressao") != std::string_view::npos);
    AUREA_CHECK_EQ(e.read_status().undoDepth, history);
    const auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK(comp->layer(LayerId::unpack(*text))->tracks.find(property)->keys.empty());
    Engine::ExpressionInfo expression;
    AUREA_CHECK(e.query_expression(*text, static_cast<u32>(property), kInvalidIndex, 0, expression));
    AUREA_CHECK(expression.exists && expression.enabled && expression.source == "540");
    // Explicit edits can still edit the underlying animation intentionally.
    drag.keyframe.onlyIfChanged = false;
    AUREA_CHECK(e.apply_command(drag).ok());
    bridge::LayerDetailPOD detail{}; AUREA_CHECK(e.query_layer_detail(*text, detail));
    AUREA_CHECK_NEAR(detail.position[0], 540.f, 1e-4f);
    // Only an explicit expression toggle returns the property to manual input.
    AUREA_CHECK(e.set_expression_enabled(*text, static_cast<u32>(property), kInvalidIndex, 0, false));
    drag.keyframe.onlyIfChanged = true; drag.keyframe.value = 650;
    AUREA_CHECK(e.apply_command(drag).ok());
    AUREA_CHECK(e.query_layer_detail(*text, detail));
    AUREA_CHECK_NEAR(detail.position[0], 650.f, 1e-4f);
    AUREA_CHECK(e.query_expression(*text, static_cast<u32>(property), kInvalidIndex, 0, expression));
    AUREA_CHECK(expression.exists && !expression.enabled && expression.source == "540");
    e.shutdown();
}

AUREA_TEST(Regression2130, AutoKeySkipsUnchangedAxesAndKeepsManualHoldKeys) {
    Engine e; EngineConfig config; config.workerCount=1; config.disableAutosave=true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(640,360,30,nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const auto id = comp->add_layer(LayerKind::Null, "animated");
    auto* l = comp->layer(id);
    l->threeD = true; l->start = FrameIndex{20}; l->offset = FrameIndex{7};
    l->transform.position = {100, 200, 300};
    auto insert = [&](TrackProperty property, FrameIndex time, f32 value, bool automatic) {
        Command cmd; cmd.type = CommandType::KeyframeInsert;
        cmd.keyframe.track = {id, property, kInvalidIndex, 0};
        cmd.keyframe.time = time; cmd.keyframe.value = value; cmd.keyframe.onlyIfChanged = automatic;
        AUREA_CHECK(e.apply_command(cmd).ok());
    };
    insert(TrackProperty::PositionX, FrameIndex{7}, 100, false);
    insert(TrackProperty::PositionX, FrameIndex{27}, 200, false);
    const auto before = e.read_status().undoDepth;
    // Local time includes the layer's start and trim; Auto-Key compares at that time.
    const auto local = l->local_time(FrameIndex{30});
    AUREA_CHECK_EQ(local.value, i64{17});
    for (int repeat=0; repeat<12; ++repeat) {
        insert(TrackProperty::PositionX, local, 150, true);
        insert(TrackProperty::PositionY, local, 200, true);
        insert(TrackProperty::PositionZ, local, 300, true);
    }
    AUREA_CHECK_EQ(l->tracks.find(TrackProperty::PositionX)->keys.size(), usize{2});
    AUREA_CHECK(!l->tracks.find(TrackProperty::PositionY));
    AUREA_CHECK(!l->tracks.find(TrackProperty::PositionZ));
    AUREA_CHECK_EQ(e.read_status().undoDepth, before);
    // Deleting the last key may leave an empty track. Rendering uses the layer
    // transform again, not that track's default zero.
    l->tracks.get_or_create(TrackProperty::PositionY);
    insert(TrackProperty::PositionY, local, 200, true);
    AUREA_CHECK(l->tracks.find(TrackProperty::PositionY)->keys.empty());
    AUREA_CHECK_EQ(e.read_status().undoDepth, before);
    insert(TrackProperty::PositionX, local, 175, true);
    auto* track = l->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK_EQ(track->keys.size(), usize{3});
    track->keys[1].interp = Interpolation::Bezier;
    track->keys[1].bx1 = .2f; track->keys[1].by1 = .6f;
    insert(TrackProperty::PositionX, local, 180, true);
    AUREA_CHECK_EQ(track->keys.size(), usize{3});
    AUREA_CHECK(track->keys[1].interp == Interpolation::Bezier);
    AUREA_CHECK_NEAR(track->keys[1].by1, .6f, 1e-6f);
    insert(TrackProperty::PositionX, FrameIndex{37}, 200, false);
    AUREA_CHECK_EQ(track->keys.size(), usize{4}); // explicit hold is intentional
    e.shutdown();
}

AUREA_TEST(Regression2130, TextUsesSelectedFontAndParagraphBoxForItsCenter) {
    Engine e; EngineConfig config; config.workerCount=1; config.disableAutosave=true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(1280,720,30,nullptr).ok());
    const auto id = e.add_text("MMMM\nM"); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const auto fontPath = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/fonts/Roboto-Regular.ttf";
    AUREA_CHECK(std::filesystem::exists(fontPath));
    AUREA_CHECK(e.set_text_font(*id, "Roboto", 400, false, fontPath.string()));
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* l = comp->layer(LayerId::unpack(*id));
    const auto position = l->transform.position;
    auto verifyCenter = [&] {
        const auto font = text::FontManager::instance().font_for(l->text);
        AUREA_CHECK(static_cast<bool>(font)); if (!font) return;
        const auto extent = text::measure(*font, l->text);
        const f32 pad = l->text.strokeWidth > 0 ? l->text.strokeWidth + 2 : 2;
        const f32 width = std::ceil(extent.width + 2*pad), height = std::ceil(extent.height + 2*pad);
        AUREA_CHECK_NEAR(l->transform.anchor.x, width*.5f, 1e-5f);
        AUREA_CHECK_NEAR(l->transform.anchor.y, height*.5f, 1e-5f);
        AUREA_CHECK(l->transform.position == position);
        bridge::LayerDetailPOD detail{}; AUREA_CHECK(e.query_layer_detail(*id,detail));
        AUREA_CHECK_EQ(detail.sourceWidth, static_cast<u32>(width));
        AUREA_CHECK_EQ(detail.sourceHeight, static_cast<u32>(height));
    };
    verifyCenter();
    f32 style[20]{}; AUREA_CHECK(e.query_text_style(*id, style, 20));
    style[0]=2; style[1]=800; style[2]=300; style[18]=1.8f; style[19]=12;
    AUREA_CHECK(e.set_text_style(*id,style,20)); verifyCenter();
    Command align; align.type=CommandType::TextSetAlignment;
    align.text_align={LayerId::unpack(*id),1}; AUREA_CHECK(e.apply_command(align).ok()); verifyCenter();
    const auto font = text::FontManager::instance().font_for(l->text);
    text::TextLayout left, center;
    auto data=l->text; data.alignment=0; AUREA_CHECK(text::layout_quads(*font,data,2,left));
    data.alignment=1; AUREA_CHECK(text::layout_quads(*font,data,2,center));
    AUREA_CHECK_EQ(left.quads.size(),center.quads.size());
    // Both preview and export consume these glyphs. Shorter lines move further right.
    AUREA_CHECK(center.quads.front().penX > left.quads.front().penX);
    AUREA_CHECK(center.quads.back().penX > center.quads.front().penX);
    e.shutdown();
}

AUREA_TEST(Regression2130, ProcessBudgetUsesAddressSpaceNotJustPhysicalRam) {
    for (u64 mb : {0ull, 64ull, 128ull, 256ull, 384ull, 1024ull, 4096ull}) {
        AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(mb << 20, 32), std::min<u64>(mb, 256ull) << 20);
        AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(mb << 20, 64), mb << 20);
    }
    AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(std::numeric_limits<u64>::max(), 32), 256ull << 20);
}

AUREA_TEST(Regression2130, MemoryReservationsCannotWrapOrOverspendBetweenWorkers) {
    MemoryManager memory;
    const auto cls = MemoryClass::DecodedFrames;
    memory.set_budget(cls, 1024);
    auto held = memory.try_reserve(cls, 512);
    AUREA_CHECK(held.valid());
    auto overflow = memory.try_reserve(cls, std::numeric_limits<usize>::max() - 255);
    AUREA_CHECK(!overflow.valid());
    AUREA_CHECK_EQ(memory.used(cls), usize{512});
    held.release();
    for (int round = 0; round < 20; ++round) {
        std::atomic<int> ready{0};
        std::atomic<bool> start{false}, release{false};
        std::array<std::thread, 8> workers;
        for (auto& worker : workers) worker = std::thread([&] {
            ready.fetch_add(1);
            while (!start.load()) std::this_thread::yield();
            auto reservation = memory.try_reserve(cls, 600);
            ready.fetch_add(1);
            while (!release.load()) std::this_thread::yield();
        });
        while (ready.load() < 8) std::this_thread::yield();
        start = true;
        while (ready.load() < 16) std::this_thread::yield();
        AUREA_CHECK_EQ(memory.used(cls), usize{600});
        AUREA_CHECK(memory.peak(cls) <= 1024);
        release = true;
        for (auto& worker : workers) worker.join();
        AUREA_CHECK_EQ(memory.used(cls), usize{0});
    }
}

AUREA_TEST(Regression2130, ObjectEnvironmentEditsLeaveGlobalLightingUntouched) {
    Engine e; EngineConfig config; config.workerCount=1; config.disableAutosave=true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(640,360,30,nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const auto id = comp->add_layer(LayerKind::Model3D, "object");
    const auto original = comp->environment();
    AUREA_CHECK(e.set_object_environment(id.pack(),1,0,1.25f,72.f,2.5f));
    f32 values[5]{};
    AUREA_CHECK(e.query_object_environment(id.pack(),values));
    AUREA_CHECK_NEAR(values[2],1.25f,1e-6f);
    AUREA_CHECK_NEAR(values[3],72.f,1e-6f);
    AUREA_CHECK_NEAR(values[4],2.5f,1e-6f);
    AUREA_CHECK_EQ(comp->environment().intensity,original.intensity);
    e.shutdown();
}

// Beta 2026-10-05: "não consigo mover a camada depois de pôr keyframe" — texto
// com várias chaves de posição, cabeçote entre elas, arrastar no palco não
// fazia nada. A UI decidia keyframe × valor parado pelo detalhe que tinha
// lido; com ele atrasado mandava LayerSetPosition, que a animação esconde, ou
// a chave ia para o cabeçote DELA, não para o quadro mostrado. O gesto agora
// manda kAutoKey* e o motor decide com as trilhas vivas e o quadro na tela.
namespace {
Command stage_gesture(LayerId layer, TrackProperty property, f32 value, u32 flags) {
    Command c; c.type = CommandType::KeyframeInsert;
    c.keyframe.track = {layer, property, kInvalidIndex, 0};
    c.keyframe.time = FrameIndex{0};   // ignorado com kAutoKeyAtPlayhead
    c.keyframe.value = value;
    c.keyframe.onlyIfChanged = flags;
    return c;
}
constexpr u32 kStageDrag = kAutoKeyOnlyIfChanged | kAutoKeyAtPlayhead | kAutoKeyStaticWhenUnanimated;
} // namespace

AUREA_TEST(TransformStability, StageDragOnKeyedTextMovesTheShownFrameEvenWithAStaleUiSnapshot) {
    Engine e; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(1080, 1080, 30, nullptr).ok());
    const auto text = e.add_text("Keys"); AUREA_CHECK(text.ok()); if (!text.ok()) return;
    const LayerId id = LayerId::unpack(*text);
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(id);
    layer->start = FrameIndex{20}; layer->offset = FrameIndex{7}; layer->end = FrameIndex{120};
    layer->transform.position = {540, 515, 0};
    // Várias chaves de posição (losango), como no relato.
    const struct { i64 t; f32 x, y; } keys[] = {{7, 540, 515}, {37, 740, 515}, {67, 740, 815}};
    for (const auto& k : keys) {
        for (u32 axis = 0; axis < 2; ++axis) {
            Command key; key.type = CommandType::KeyframeInsert;
            key.keyframe.track = {id, static_cast<TrackProperty>(axis), kInvalidIndex, 0};
            key.keyframe.time = FrameIndex{k.t}; key.keyframe.value = axis == 0 ? k.x : k.y;
            AUREA_CHECK(e.apply_command(key).ok());
        }
    }
    // Cabeçote entre a 1ª e a 2ª chave: timeline 45 = local 32.
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{45}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    const FrameIndex shown = layer->local_time(FrameIndex{45});
    AUREA_CHECK_EQ(shown.value, i64{32});
    bridge::LayerDetailPOD detail{}; AUREA_CHECK(e.query_layer_detail(*text, detail));
    const f32 x0 = detail.position[0], y0 = detail.position[1];
    AUREA_CHECK_NEAR(x0, 540.f + 200.f * 25.f / 30.f, 1e-3f);

    // O caminho antigo com o retrato atrasado ("não animada"): o valor parado
    // muda, mas a trilha manda — a camada fica onde estava. Era o bug.
    Command stale; stale.type = CommandType::LayerSetPosition;
    stale.position = {id, x0 + 120.f, y0 + 60.f, 0.f};
    AUREA_CHECK(e.apply_command(stale).ok());
    AUREA_CHECK(e.query_layer_detail(*text, detail));
    AUREA_CHECK_NEAR(detail.position[0], x0, 1e-3f);
    AUREA_CHECK_NEAR(detail.position[1], y0, 1e-3f);

    // O gesto novo: assíncrono como nas UIs, aplicado no próximo quadro.
    Command drag[2] = {stage_gesture(id, TrackProperty::PositionX, x0 + 120.f, kStageDrag),
                       stage_gesture(id, TrackProperty::PositionY, y0 + 60.f, kStageDrag)};
    AUREA_CHECK_EQ(e.submit_commands(drag, 2, nullptr, 0), 2u);
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.query_layer_detail(*text, detail));
    AUREA_CHECK_NEAR(detail.position[0], x0 + 120.f, 1e-3f);
    AUREA_CHECK_NEAR(detail.position[1], y0 + 60.f, 1e-3f);
    for (const auto property : {TrackProperty::PositionX, TrackProperty::PositionY}) {
        const Track* track = layer->tracks.find(property);
        AUREA_CHECK(track != nullptr); if (!track) continue;
        // Chave no quadro MOSTRADO (local 32), nem no 0 do comando nem no 45 da timeline.
        AUREA_CHECK_EQ(track->keys.size(), usize{4});
        AUREA_CHECK(track->find_exact(shown) != kInvalidIndex);
        AUREA_CHECK(track->find_exact(FrameIndex{0}) == kInvalidIndex);
        AUREA_CHECK(track->find_exact(FrameIndex{45}) == kInvalidIndex);
        // As outras poses ficam.
        AUREA_CHECK_NEAR(track->value_or(FrameIndex{67}, 0.f), property == TrackProperty::PositionX ? 740.f : 815.f, 1e-4f);
    }
    // Soltar no mesmo lugar não grava nada nem empilha desfazer.
    const auto depth = e.read_status().undoDepth;
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::PositionX, x0 + 120.f, kStageDrag)).ok());
    AUREA_CHECK_EQ(e.read_status().undoDepth, depth);
    AUREA_CHECK_EQ(layer->tracks.find(TrackProperty::PositionX)->keys.size(), usize{4});
    e.shutdown();
}

AUREA_TEST(TransformStability, StageDragWithoutKeysChangesTheStaticValueAndNeverCreatesATrack) {
    Engine e; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(640, 360, 30, nullptr).ok());
    const auto text = e.add_text("Static"); AUREA_CHECK(text.ok()); if (!text.ok()) return;
    const LayerId id = LayerId::unpack(*text);
    auto* layer = e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{12}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    const auto depth = e.read_status().undoDepth;
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::PositionX, 200.f, kStageDrag)).ok());
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::SkewX, 12.f, kStageDrag)).ok());
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::Opacity, 1.4f, kStageDrag)).ok());
    AUREA_CHECK_NEAR(layer->transform.position.x, 200.f, 1e-5f);
    AUREA_CHECK_NEAR(layer->transform.skewX, 12.f, 1e-5f);
    AUREA_CHECK_NEAR(layer->transform.opacity, 1.f, 1e-6f);   // presa como no LayerSetOpacity
    for (const auto property : {TrackProperty::PositionX, TrackProperty::SkewX, TrackProperty::Opacity})
        AUREA_CHECK(!layer->tracks.find(property) || layer->tracks.find(property)->keys.empty());
    AUREA_CHECK(e.read_status().undoDepth > depth);
    // Expressão ligada continua recusando o gesto, sem mexer no valor.
    AUREA_CHECK(e.set_expression(*text, static_cast<u32>(TrackProperty::PositionY), kInvalidIndex, 0, "100").ok());
    const f32 y = layer->transform.position.y;
    const auto refused = e.apply_command(stage_gesture(id, TrackProperty::PositionY, y + 50.f, kStageDrag));
    AUREA_CHECK_EQ(refused.code(), Errc::InvalidState);
    AUREA_CHECK_NEAR(layer->transform.position.y, y, 1e-5f);
    e.shutdown();
}

AUREA_TEST(TransformStability, AnimatedThreeDGroupGestureKeysEveryAxisAtTheShownFrame) {
    Engine e; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(640, 360, 30, nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const LayerId id = comp->add_layer(LayerKind::Null, "3d");
    auto* l = comp->layer(id);
    l->threeD = true; l->start = FrameIndex{10}; l->offset = FrameIndex{0};
    l->transform.position = {100, 120, 30};
    auto& x = l->tracks.get_or_create(TrackProperty::PositionX);
    x.set(FrameIndex{0}, 100); x.set(FrameIndex{40}, 300);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{30}, 30);
    AUREA_CHECK(e.apply_command(seek).ok());
    // O grupo XYZ (sem o bit 4): Y e Z, ainda parados, também ganham chave.
    constexpr u32 group = kAutoKeyOnlyIfChanged | kAutoKeyAtPlayhead;
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::PositionX, 260.f, group)).ok());
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::PositionY, 150.f, group)).ok());
    AUREA_CHECK(e.apply_command(stage_gesture(id, TrackProperty::PositionZ, 45.f, group)).ok());
    const FrameIndex shown{20};
    const std::pair<TrackProperty, f32> expected[] = {
        {TrackProperty::PositionX, 260.f}, {TrackProperty::PositionY, 150.f}, {TrackProperty::PositionZ, 45.f}};
    for (const auto& [property, value] : expected) {
        const Track* track = l->tracks.find(property);
        AUREA_CHECK(track != nullptr); if (!track) continue;
        AUREA_CHECK(track->find_exact(shown) != kInvalidIndex);
        AUREA_CHECK_NEAR(track->value_or(shown, 0.f), value, 1e-5f);
    }
    e.shutdown();
}
