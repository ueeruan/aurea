#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/text/FontManager.hpp"
#include <cmath>
#include <limits>

using namespace aurea;

namespace {
struct MorphExposureFixture {
    test::MockBackend backend;
    EffectRegistry effects;
    Renderer renderer;
    Project project;
    Composition* comp = nullptr;
    std::shared_ptr<const scene3d::SceneAsset> model;

    explicit MorphExposureFixture(u32 vertexCount) {
        register_builtin_effects(effects);
        (void)renderer.initialize(backend, effects);
        project = std::move(*Project::create_new(320, 180, 30, "morph exposure"));
        comp = project.timeline().composition(project.timeline().root());
        auto source = std::make_shared<scene3d::SceneAsset>();
        scene3d::Primitive primitive;
        primitive.positions.resize(vertexCount);
        for (u32 i = 0; i < vertexCount; ++i)
            primitive.positions[i] = Vec3{static_cast<f32>(i % 2), static_cast<f32>((i / 2) % 2), 0};
        primitive.indices = {0, 1, 2};
        primitive.bounds.min = Vec3{0, 0, 0}; primitive.bounds.max = Vec3{1, 1, 0};
        primitive.morphTargets.resize(1);
        primitive.morphTargets[0].positions.assign(vertexCount, Vec3{1, 0, 0});
        scene3d::Mesh mesh; mesh.bounds = primitive.bounds;
        mesh.primitives.push_back(std::move(primitive)); mesh.morphWeights = {0};
        source->meshes.push_back(std::move(mesh));
        scene3d::Node node; node.mesh = 0; source->nodes.push_back(std::move(node));
        source->roots = {0}; source->bounds = source->meshes[0].bounds;
        scene3d::Animation animation; animation.duration = 1;
        scene3d::AnimSampler sampler; sampler.times = {0, 1}; sampler.values = {0, 1}; sampler.components = 1;
        animation.samplers.push_back(std::move(sampler));
        animation.channels.push_back({0, scene3d::AnimPath::Weights, 0});
        source->animations.push_back(std::move(animation));
        model = std::move(source);
        renderer.set_model_lookup([](void* context, AssetId) {
            return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(context);
        }, &model);
        Asset asset; asset.kind = AssetKind::Model3D;
        const auto aid = project.add_asset(std::move(asset));
        const auto id = comp->add_layer(LayerKind::Model3D, "deforming mesh");
        auto* layer = comp->layer(id);
        layer->model.scene = aid; layer->model.animationClip = 0; layer->model.unitScale = 60;
        layer->transform.position = Vec3{160, 90, 0}; layer->threeD = true; layer->motionBlur = true;
        comp->motion_blur().enabled = true;
        comp->motion_blur().samples = 16; comp->motion_blur().adaptiveLimit = 64;
    }

    void prepare(FrameSnapshot& snapshot, bool finalQuality) {
        RenderSettings settings; settings.finalQuality = finalQuality;
        renderer.prepare(*comp, project, FrameIndex{15}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1.f, snapshot);
    }

    CompositionId wrap_in_parent(u32 copies) {
        const auto child = project.timeline().root();
        const auto parent = project.timeline().create_composition("exposure parent", 320, 180, 30);
        project.timeline().set_root(parent);
        (void)project.timeline().set_current(parent);
        comp = project.timeline().composition(parent);
        for (u32 i = 0; i < copies; ++i) {
            const auto id = comp->add_layer(LayerKind::Composition, "shared model child");
            comp->layer(id)->nested.composition = child;
        }
        return child;
    }

    LayerId add_text(u32 glyphs, bool animated) {
        const auto id = comp->add_layer(LayerKind::Text, "exposure text");
        auto* layer = comp->layer(id);
        layer->text.content.assign(glyphs, 'W');
        layer->text.size = 5;
        layer->motionBlur = animated;
        if (animated) {
            TextAnimator animator;
            animator.props = kTextPropPosition;
            layer->text.animators.push_back(std::move(animator));
        }
        return id;
    }
};
}

AUREA_TEST(MotionBlur, MorphOnlyAnimationEvaluatesEveryExposureTime) {
    MorphExposureFixture f(4);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u); if (snapshot.scenes.empty()) return;
    const auto& frames = snapshot.scenes[0].blurFrames;
    AUREA_CHECK(frames.size() >= 16); if (frames.empty()) return;
    f32 previous = -1;
    for (const auto& frame : frames) {
        AUREA_CHECK_EQ(frame.instances.size(), 1u); if (frame.instances.empty()) return;
        const auto& instance = frame.instances[0];
        AUREA_CHECK(!instance.sharedPose);
        AUREA_CHECK_EQ(instance.pose().morphWeights.size(), 1u);
        if (instance.pose().morphWeights.empty()) return;
        AUREA_CHECK_EQ(instance.pose().morphWeights[0].size(), 1u);
        if (instance.pose().morphWeights[0].empty()) return;
        const f32 weight = instance.pose().morphWeights[0][0];
        AUREA_CHECK(weight > previous);
        AUREA_CHECK_NEAR(weight, (15. + frame.subFrame) / 30., .00001);
        AUREA_CHECK_NEAR(instance.world.col[3].x, frames.front().instances[0].world.col[3].x, .00001);
        previous = weight;
    }
}

AUREA_TEST(MotionBlur, DenseMorphUploadsAreAdmittedBeforeSampleExpansion) {
    // The pose has only one node. Counting CPU pose vectors alone used to admit
    // 16+ independent multi-megabyte GPU vertex uploads retained by one fence.
    MorphExposureFixture f(100000);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u); if (snapshot.scenes.empty()) return;
    AUREA_CHECK(snapshot.scenes[0].blurFrames.empty());
    AUREA_CHECK_EQ(snapshot.scenes[0].instances.size(), 1u);
    f.prepare(snapshot, false);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u); if (snapshot.scenes.empty()) return;
    const auto& frames = snapshot.scenes[0].blurFrames;
    AUREA_CHECK(frames.size() >= 2);
    AUREA_CHECK(frames.size() <= 5);
}

AUREA_TEST(MotionBlur, NestedExposuresShareOneBudgetAndResetForTheNextFrame) {
    // Each child individually fits sixteen ~1.9 MiB morph uploads. They share
    // the same model asset, but their submitted uploads coexist at the fence.
    MorphExposureFixture f(32000);
    f.wrap_in_parent(2);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.nested.size(), 2u); if (snapshot.nested.size() != 2) return;
    AUREA_CHECK_EQ(snapshot.nested[0]->scenes.size(), 1u);
    AUREA_CHECK_EQ(snapshot.nested[1]->scenes.size(), 1u);
    if (snapshot.nested[0]->scenes.empty() || snapshot.nested[1]->scenes.empty()) return;
    AUREA_CHECK_EQ(snapshot.nested[0]->scenes[0].blurFrames.size(), 16u);
    AUREA_CHECK(snapshot.nested[1]->scenes[0].blurFrames.empty());
    f.comp->layer(f.comp->order().at(1))->visible = false;
    f.prepare(snapshot, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.nested.size(), 1u); if (snapshot.nested.empty()) return;
    AUREA_CHECK_EQ(snapshot.nested[0]->scenes[0].blurFrames.size(), 16u);
}

AUREA_TEST(MotionBlur, GlyphAndModelExposuresUseTheSameFrameAllowance) {
    MorphExposureFixture f(32000);
    const auto textId = f.add_text(2000, true);
    AUREA_CHECK(text::FontManager::instance().font_for(f.comp->layer(textId)->text) != nullptr);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK(snapshot.glyphs.size() >= 2000u * 16u);
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u); if (snapshot.scenes.empty()) return;
    AUREA_CHECK(snapshot.scenes[0].blurFrames.empty());
    f.comp->layer(textId)->visible = false;
    f.prepare(snapshot, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes[0].blurFrames.size(), 16u);
}

AUREA_TEST(MotionBlur, NoGlyphSetIsAllocatedWhenTheRemainingAllowanceIsTooSmall) {
    MorphExposureFixture f(32000);
    f.wrap_in_parent(1);
    const auto textId = f.add_text(20000, false); // One set exceeds the child's remaining allowance.
    AUREA_CHECK(text::FontManager::instance().font_for(f.comp->layer(textId)->text) != nullptr);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK(snapshot.glyphs.empty());
    AUREA_CHECK_EQ(snapshot.nested.size(), 1u); if (snapshot.nested.empty()) return;
    AUREA_CHECK_EQ(snapshot.nested[0]->scenes[0].blurFrames.size(), 16u);
    for (const auto& layer : snapshot.layers) AUREA_CHECK(layer.source.kind != LayerSource::Kind::Text);
}

AUREA_TEST(MotionBlur, NonFiniteAmountKeepsTheCenterCameraDuringAnotherModelsExposure) {
    MorphExposureFixture f(4);
    const auto moving = f.comp->order().at(0);
    const auto sharp = f.comp->add_layer(LayerKind::Model3D, "invalid amount");
    f.comp->layer(sharp)->model = f.comp->layer(moving)->model;
    f.comp->layer(sharp)->transform = f.comp->layer(moving)->transform;
    f.comp->layer(sharp)->threeD = true;
    f.comp->layer(sharp)->motionBlur = true;
    f.comp->layer(sharp)->transform.motionBlurAmount = 0;
    const auto cameraId = f.comp->add_layer(LayerKind::Camera, "animated camera");
    auto* camera = f.comp->layer(cameraId);
    camera->transform.position = Vec3{160, 90, -1000};
    auto& focal = camera->tracks.get_or_create(TrackProperty::FocalLength);
    focal.set(FrameIndex{0}, 24); focal.set(FrameIndex{30}, 72);
    FrameSnapshot snapshot; f.prepare(snapshot, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u); if (snapshot.scenes.empty()) return;
    const auto& frames = snapshot.scenes[0].blurFrames;
    AUREA_CHECK(frames.size() >= 16); if (frames.empty()) return;
    const auto findSharp = [&](const scene3d::SceneFrame& frame) -> const scene3d::SceneInstance* {
        for (const auto& instance : frame.instances) if (instance.layerKey == sharp.pack()) return &instance;
        return nullptr;
    };
    const auto* center = findSharp(frames.front());
    AUREA_CHECK(center != nullptr); if (!center) return;
    const Mat4 expected = center->sampleViewProj;
    for (f32 amount : {std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity(), -std::numeric_limits<f32>::infinity()}) {
        f.comp->layer(sharp)->transform.motionBlurAmount = amount;
        f.prepare(snapshot, true);
        AUREA_CHECK(!f.renderer.take_incomplete());
        AUREA_CHECK(!snapshot.scenes[0].blurFrames.empty());
        for (const auto& frame : snapshot.scenes[0].blurFrames) {
            const auto* instance = findSharp(frame);
            AUREA_CHECK(instance != nullptr); if (!instance) continue;
            AUREA_CHECK(instance->cameraOverride);
            for (u32 c = 0; c < 4; ++c) {
                const auto& a = instance->sampleViewProj.col[c];
                const auto& b = expected.col[c];
                AUREA_CHECK(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) && std::isfinite(a.w));
                AUREA_CHECK_NEAR(a.x, b.x, .00001); AUREA_CHECK_NEAR(a.y, b.y, .00001);
                AUREA_CHECK_NEAR(a.z, b.z, .00001); AUREA_CHECK_NEAR(a.w, b.w, .00001);
            }
        }
    }
}
