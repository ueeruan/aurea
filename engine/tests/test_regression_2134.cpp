#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "aurea/memory/Arena.hpp"
#include "aurea/render/Renderer.hpp"

#include <cstring>
#include <cstdio>

using namespace aurea;

AUREA_TEST(Regression2134, FrameArenaReleasesOverflowBetweenFrames) {
    Arena arena(1024);
    for (u32 frame = 0; frame < 120; ++frame) {
        u8* blocks[12]{};
        for (u32 i = 0; i < 12; ++i) {
            blocks[i] = static_cast<u8*>(arena.alloc(900, 64));
            AUREA_CHECK(blocks[i] != nullptr);
            if (!blocks[i]) return;
            std::memset(blocks[i], static_cast<int>(i + 1), 900);
        }
        // Growing may not invalidate any earlier draw's uniforms.
        for (u32 i = 0; i < 12; ++i) {
            AUREA_CHECK_EQ(blocks[i][0], static_cast<u8>(i + 1));
            AUREA_CHECK_EQ(blocks[i][899], static_cast<u8>(i + 1));
        }
        arena.reset();
        AUREA_CHECK_EQ(arena.used(), static_cast<usize>(0));
        AUREA_CHECK_EQ(arena.reserved_bytes(), arena.capacity());
        if (arena.reserved_bytes() != arena.capacity()) break;
    }
    arena.release();
    AUREA_CHECK_EQ(arena.reserved_bytes(), static_cast<usize>(0));
}

AUREA_TEST(Regression2134, MotionBlurMemoryDoesNotMultiplyBySampleCount) {
    for (bool scene : {false, true}) {
        u64 bytes[2]{};
        u32 textures[2]{};
        for (u32 test = 0; test < 2; ++test) {
            const u32 samples = test == 0 ? 2u : 64u;
            test::MockBackend backend;
            backend.mapBuffers = true;
            EffectRegistry effects;
            register_builtin_effects(effects);
            Renderer renderer;
            AUREA_CHECK(renderer.initialize(backend, effects).ok());
            FrameSnapshot snapshot;
            snapshot.compWidth = 1920; snapshot.compHeight = 1080;
            RenderLayer layer;
            layer.source.width = 1920; layer.source.height = 1080;
            layer.compFromLayer = Mat4::identity();
            if (scene) {
                layer.source.kind = LayerSource::Kind::Scene3D;
                snapshot.scenes.resize(1);
                auto& group = snapshot.scenes[0];
                group.camera = scene3d::default_camera(1920, 1080);
                const scene3d::SceneFrame sample = group;
                group.blurFrames.assign(samples, sample);
            } else {
                layer.source.kind = LayerSource::Kind::Text;
                layer.source.glyphCount = 1;
                layer.source.glyphSets = samples;
                snapshot.glyphs.resize(samples);
            }
            snapshot.layers.push_back(layer);
            RenderSettings settings; settings.finalQuality = true;
            FrameStats stats; RenderTimings timings;
            AUREA_CHECK(renderer.render(snapshot, settings, nullptr, stats, timings).ok());
            bytes[test] = renderer.graph_stats().transientBytes;
            textures[test] = renderer.graph_stats().physicalTextures;
            AUREA_CHECK(renderer.graph_stats().passesExecuted >= samples + 1);
            renderer.shutdown();
        }
        std::printf("    %s: 2 samples %llu bytes/%u textures; 64 samples %llu bytes/%u textures\n",
            scene ? "3D" : "text", static_cast<unsigned long long>(bytes[0]), textures[0],
            static_cast<unsigned long long>(bytes[1]), textures[1]);
        AUREA_CHECK_EQ(bytes[0], bytes[1]);
        AUREA_CHECK_EQ(textures[0], textures[1]);
    }
}
