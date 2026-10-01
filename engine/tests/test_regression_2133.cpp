#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include <filesystem>

using namespace aurea;

AUREA_TEST(Regression2133, ExportRangeFollowsClipsWithoutChangingTheProject) {
    Engine e; EngineConfig cfg; cfg.disableAutosave = true; cfg.workerCount = 1;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->set_duration(FrameIndex{1020}); // Former footage was 34 seconds.
    const auto text = comp->add_layer(LayerKind::Text, "five seconds");
    comp->layer(text)->end = FrameIndex{150};
    const auto control = comp->add_layer(LayerKind::Null, "parent");
    AUREA_CHECK_EQ(e.query_export_duration(), i64{150});
    AUREA_CHECK_EQ(e.query_export_duration(false), i64{1020});
    comp->layer(text)->start = FrameIndex{60}; // Preserve the gap at the start.
    comp->layer(text)->offset = FrameIndex{90};
    comp->layer(text)->speed = 2;
    AUREA_CHECK_EQ(e.query_export_duration(), i64{150});
    comp->layer(text)->timeRemapEnabled = true;
    comp->layer(text)->timeRemap.set(FrameIndex{90}, 120);
    comp->layer(text)->timeRemap.set(FrameIndex{180}, 0);
    AUREA_CHECK_EQ(e.query_export_duration(), i64{150});
    const auto audio = comp->add_layer(LayerKind::Audio, "longer audio");
    comp->layer(audio)->end = FrameIndex{240};
    AUREA_CHECK_EQ(e.query_export_duration(), i64{240});
    comp->layer(audio)->muted = true; // Mute/visibility do not trim a clip.
    AUREA_CHECK_EQ(e.query_export_duration(), i64{240});
    AUREA_CHECK(comp->remove_layer(audio));
    const auto nested = comp->add_layer(LayerKind::Composition, "precomp");
    comp->layer(nested)->start = FrameIndex{270}; comp->layer(nested)->end = FrameIndex{360};
    AUREA_CHECK_EQ(e.query_export_duration(), i64{360});
    comp->set_duration(FrameIndex{300}); // An explicit composition limit still wins.
    AUREA_CHECK_EQ(e.query_export_duration(), i64{300});
    AUREA_CHECK(comp->remove_layer(nested));
    comp->set_duration(FrameIndex{1020});
    const char* path = "build/reference/animator-export-2133/range.aurea";
    std::filesystem::create_directories("build/reference/animator-export-2133");
    AUREA_CHECK(e.save_project(path).ok());
    AUREA_CHECK(e.load_project(path).ok());
    AUREA_CHECK_EQ(e.query_export_duration(), i64{150});
    AUREA_CHECK_EQ(e.query_export_duration(false), i64{1020});
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK(comp->remove_layer(text));
    AUREA_CHECK(comp->remove_layer(control));
    AUREA_CHECK_EQ(e.query_export_duration(), i64{1020}); // Background-only project.
    e.shutdown();
}
