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
