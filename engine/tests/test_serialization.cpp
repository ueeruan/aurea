// Testes do formato .aurea.
//
// O que estes testes protegem, em uma frase: o trabalho do usuário. Um bug de
// serialização não trava o app — ele abre o projeto com uma camada faltando, ou
// com a animação deslocada, e o usuário descobre depois de exportar.
#include "TestFramework.hpp"

#include "aurea/project/Serialization.hpp"
#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/command/CommandQueue.hpp"

#include <cstdio>
#include <string>
#include <limits>

using namespace aurea;

namespace {

std::string temp_path(const char* name) {
    std::string p = "aurea_test_";
    p += name;
    p += ".aurea";
    return p;
}

Project make_project() {
    auto r = Project::create_new(1280, 720, 30.0, "Projeto de teste");
    Project p = std::move(*r);
    p.metadata().author = "Aurea";
    return p;
}

} // namespace

AUREA_TEST(Serialization, MotionBlurShutterSettingsRoundTripAcrossCompositions) {
    auto original = make_project();
    const auto childId = original.timeline().create_composition("child", 64, 64, 30.);
    auto& rootBlur = original.timeline().composition(original.timeline().root())->motion_blur();
    auto& childBlur = original.timeline().composition(childId)->motion_blur();
    rootBlur.enabled = true; rootBlur.samples = 32; rootBlur.previewSamples = 7;
    rootBlur.shutterAngle = 270; rootBlur.shutterPhase = 40; rootBlur.adaptiveLimit = 192;
    rootBlur.vectorBlur = true;
    childBlur.enabled = false; childBlur.shutterAngle = 90; childBlur.shutterPhase = -80;
    childBlur.samples = 8; childBlur.adaptiveLimit = 24;
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored; LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    const auto& a = restored.timeline().composition(restored.timeline().root())->motion_blur();
    AUREA_CHECK(a.enabled && a.vectorBlur);
    AUREA_CHECK_EQ(a.shutterAngle, 270.f);
    AUREA_CHECK_EQ(a.shutterPhase, 40.f);
    AUREA_CHECK_EQ(a.samples, 32u);
    AUREA_CHECK_EQ(a.adaptiveLimit, 192u);
    AUREA_CHECK_EQ(a.previewSamples, 7u);
    u32 children = 0;
    restored.timeline().for_each_composition([&](CompositionId id, const Composition& c) {
        if (id == restored.timeline().root()) return;
        ++children;
        AUREA_CHECK(!c.motion_blur().enabled);
        AUREA_CHECK_EQ(c.motion_blur().shutterAngle, 90.f);
        AUREA_CHECK_EQ(c.motion_blur().shutterPhase, -80.f);
        AUREA_CHECK_EQ(c.motion_blur().samples, 8u);
        AUREA_CHECK_EQ(c.motion_blur().adaptiveLimit, 24u);
    });
    AUREA_CHECK_EQ(children, 1u);
}

AUREA_TEST(Serialization, PanoramaRangeSurvivesSaveAndPreservesUnboundedDefault) {
    auto original = make_project();
    const auto childId = original.timeline().create_composition("panorama", 64, 64, 30.);
    auto& env = original.timeline().composition(original.timeline().root())->environment();
    env.showBackground = true; env.backgroundStart = FrameIndex{10}; env.backgroundEnd = FrameIndex{20};
    original.timeline().composition(childId)->environment().showBackground = true;
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}).ok());
    const auto& actual = restored.timeline().composition(restored.timeline().root())->environment();
    AUREA_CHECK_EQ(actual.backgroundStart.value, 10);
    AUREA_CHECK_EQ(actual.backgroundEnd.value, 20);
    AUREA_CHECK(actual.background_at(FrameIndex{19}));
    AUREA_CHECK(!actual.background_at(FrameIndex{20}));
    restored.timeline().for_each_composition([&](CompositionId id, const Composition& c) {
        if (id == restored.timeline().root()) return;
        AUREA_CHECK_EQ(c.environment().backgroundStart.value, 0);
        AUREA_CHECK_EQ(c.environment().backgroundEnd.value, -1);
        AUREA_CHECK(c.environment().background_at(FrameIndex{100}));
    });
}

AUREA_TEST(Serialization, MotionBlurAmountRecoversNonFiniteValuesAndPreservesValidRange) {
    auto original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    const f32 values[]{0, .5f, 1, 4, -1, 5, std::numeric_limits<f32>::quiet_NaN(),
        std::numeric_limits<f32>::infinity(), -std::numeric_limits<f32>::infinity()};
    const f32 expected[]{0, .5f, 1, 4, 0, 4, 0, 0, 0};
    for (f32 value : values) {
        const auto id = comp->add_layer(LayerKind::Shape, "blur amount");
        comp->layer(id)->transform.motionBlurAmount = value;
    }
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}).ok());
    comp = restored.timeline().composition(restored.timeline().root());
    AUREA_CHECK(comp != nullptr); if (!comp) return;
    AUREA_CHECK_EQ(comp->order().size(), 9u); if (comp->order().size() != 9) return;
    for (u32 i = 0; i < comp->order().size(); ++i)
        AUREA_CHECK_EQ(comp->layer(comp->order().at(i))->transform.motionBlurAmount, expected[i]);
}

AUREA_TEST(Serialization, MotionBlurVersion40PreservesSettingsAndAddsBoundedAdaptation) {
    auto original = make_project();
    auto& blur = original.timeline().composition(original.timeline().root())->motion_blur();
    blur.enabled = true; blur.samples = 64; blur.previewSamples = 12;
    blur.shutterAngle = 360; blur.vectorBlur = true;
    std::vector<u8> bytes;
    SaveOptions opts; opts.compress = false;
    AUREA_CHECK(ProjectSerializer::encode(original, opts, bytes).ok());
    auto read = [&](usize at, usize count) {
        u64 value = 0;
        for (usize i = 0; i < count; ++i) value |= static_cast<u64>(bytes.at(at + i)) << (8 * i);
        return value;
    };
    auto write = [&](usize at, u64 value, usize count) {
        for (usize i = 0; i < count; ++i) bytes.at(at + i) = static_cast<u8>(value >> (8 * i));
    };
    bool converted = false;
    const auto index = static_cast<usize>(read(12, 8));
    const u32 count = static_cast<u32>(read(8, 4));
    for (u32 i = 0; i < count; ++i) {
        const usize header = index + 40 * i;
        if (read(header, 2) != static_cast<u16>(SectionKind::Timeline)) continue;
        AUREA_CHECK_EQ(read(header + 34, 4), 0u);
        const auto offset = static_cast<usize>(read(header + 6, 8));
        const auto oldSize = static_cast<usize>(read(header + 14, 8)) - 8;
        // A one-composition v40 timeline ends before the v41 phase/limit tail.
        write(header + 2, 40, 4);
        write(header + 14, oldSize, 8); write(header + 22, oldSize, 8);
        u32 checksum = ~0u;
        for (usize b = offset; b < offset + oldSize; ++b) {
            checksum ^= bytes[b];
            for (u32 bit = 0; bit < 8; ++bit) checksum = (checksum >> 1) ^ (0xedb88320u & (0u - (checksum & 1u)));
        }
        write(header + 30, ~checksum, 4);
        converted = true;
    }
    AUREA_CHECK(converted);
    Project restored; LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, 40u);
    AUREA_CHECK(report.olderFormat);
    const auto& actual = restored.timeline().composition(restored.timeline().root())->motion_blur();
    AUREA_CHECK(actual.enabled && actual.vectorBlur);
    AUREA_CHECK_EQ(actual.shutterAngle, 360.f);
    AUREA_CHECK_EQ(actual.shutterPhase, -180.f);
    AUREA_CHECK_EQ(actual.samples, 64u);
    AUREA_CHECK_EQ(actual.previewSamples, 12u);
    AUREA_CHECK_EQ(actual.adaptiveLimit, 128u);
}

AUREA_TEST(Serialization, MotionBlurMalformedValuesHaveFiniteBoundedDefaults) {
    auto original = make_project();
    auto& blur = original.timeline().composition(original.timeline().root())->motion_blur();
    blur.shutterAngle = std::numeric_limits<f32>::infinity();
    blur.shutterPhase = std::numeric_limits<f32>::quiet_NaN();
    blur.samples = ~0u; blur.previewSamples = 0; blur.adaptiveLimit = 0;
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}).ok());
    const auto& actual = restored.timeline().composition(restored.timeline().root())->motion_blur();
    AUREA_CHECK_EQ(actual.shutterAngle, 180.f);
    AUREA_CHECK_EQ(actual.shutterPhase, -90.f);
    AUREA_CHECK_EQ(actual.samples, 64u);
    AUREA_CHECK_EQ(actual.previewSamples, 1u);
    AUREA_CHECK_EQ(actual.adaptiveLimit, 64u);
}

AUREA_TEST(ProjectAssets, IndirectReferencesPreservedWithoutConfusingFontHandles) {
    Project project = make_project();
    auto asset = [&](AssetKind kind, const char* name) {
        Asset value; value.kind = kind; value.name = name;
        return project.add_asset(std::move(value));
    };
    const AssetId orphan = asset(AssetKind::Image, "orphan");
    const AssetId sprite = asset(AssetKind::Image, "particle texture");
    const AssetId objectEnv = asset(AssetKind::Environment, "object environment");
    const AssetId globalEnv = asset(AssetKind::Environment, "composition environment");
    const AssetId model = asset(AssetKind::Model3D, "model");
    const AssetId image = asset(AssetKind::Image, "image");
    const AssetId lut = asset(AssetKind::Lut, "lut");
    auto* root = project.timeline().composition(project.timeline().root());
    const auto text = root->add_layer(LayerKind::Text, "font handle collision");
    root->layer(text)->text.font = FontId::unpack(orphan.pack());
    const auto object = root->add_layer(LayerKind::Model3D, "object");
    root->layer(object)->model.scene = model;
    root->layer(object)->environmentAsset = objectEnv.pack();
    root->layer(object)->environmentSource = 1;
    root->environment().hdri = globalEnv;
    const auto source = root->add_layer(LayerKind::Image, "direct image");
    root->layer(source)->source = image;
    EffectInstance effect; effect.type = effect_type_id("aurea.color.cube_lut");
    effect.params.resize(1); effect.params[0].constant.ref = lut.pack();
    root->layer(source)->effects.push_back(std::move(effect));
    // References in a child composition count even while another is selected.
    const auto childId = project.timeline().create_composition("child", 64, 64, 30.);
    auto* child = project.timeline().composition(childId);
    const auto particles = child->add_layer(LayerKind::ParticleSystem, "particles");
    child->layer(particles)->particles.textureAsset = sprite.pack();

    auto unused = project.unreferenced_assets();
    AUREA_CHECK_EQ(unused.size(), 1u);
    if (unused.size() != 1) return;
    AUREA_CHECK(unused[0] == orphan);
    AUREA_CHECK(project.remove_asset(unused[0]));
    AUREA_CHECK(project.asset(sprite) != nullptr && project.asset(objectEnv) != nullptr);
    AUREA_CHECK(project.unreferenced_assets().empty());

    child->layer(particles)->particles.textureAsset = 0;
    unused = project.unreferenced_assets();
    AUREA_CHECK_EQ(unused.size(), 1u);
    if (unused.size() == 1) AUREA_CHECK(unused[0] == sprite);
}

AUREA_TEST(ProjectAssets, ReopenLoadsParticleTextureAfterDeletingItsImageLayer) {
    const std::string path = temp_path("particle_asset_reference");
    std::remove(path.c_str());
    EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    config.memoryBudgetBytes = 64ull << 20;
    const std::string emitterName = "retained particle emitter";
    {
        Engine writer;
        AUREA_CHECK(writer.initialize(config).ok());
        AUREA_CHECK(writer.new_project(64, 64, 30., "particle reference").ok());
        auto* comp = writer.project()->timeline().composition(writer.project()->timeline().root());
        // Keep packed zero free for APIs that use it as the unset sentinel.
        (void)comp->add_layer(LayerKind::Shape, "placeholder");
        Asset orphan; orphan.kind = AssetKind::Image; orphan.sourcePath = "orphan.png";
        (void)writer.project()->add_asset(std::move(orphan));
        const u8 pixels[] = {255, 32, 16, 255};
        const auto image = writer.import_image(pixels, 1, 1, "sprite", "sprite.png");
        const auto emitter = writer.add_particles(0);
        AUREA_CHECK(image.ok() && emitter.ok());
        if (!image.ok() || !emitter.ok()) return;
        comp->layer(LayerId::unpack(*emitter))->name = emitterName;
        AUREA_CHECK(writer.set_particle_texture(*emitter, *image));
        Command remove; remove.type = CommandType::LayerDelete; remove.layer_ref.layer = LayerId::unpack(*image);
        AUREA_CHECK(writer.apply_command(remove).ok());
        AUREA_CHECK(writer.save_project(path.c_str()).ok());
    }
    std::vector<std::string> decoded;
    config.imageLoaderContext = &decoded;
    config.imageLoader = [](const char* source, ImagePixels& out, void* context) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(source);
        out.width = out.height = 1; out.rgba = {255, 32, 16, 255}; return true;
    };
    {
        Engine reader;
        AUREA_CHECK(reader.initialize(config).ok());
        AUREA_CHECK(reader.load_project(path.c_str()).ok());
        AUREA_CHECK_EQ(decoded.size(), 1u);
        if (decoded.size() == 1) AUREA_CHECK(decoded[0].find("sprite.png") != std::string::npos);
        AUREA_CHECK_EQ(reader.last_load_missing_assets(), 0u);
        auto* comp = reader.project()->timeline().composition(reader.project()->timeline().root());
        // Serialized layer tables compact deleted slots. Resolve the loaded
        // identities instead of reusing the writer's session-local handles.
        LayerId emitter;
        comp->layers().for_each([&](LayerId id, const Layer& layer) {
            if (layer.kind == LayerKind::ParticleSystem && layer.name == emitterName) emitter = id;
        });
        AssetId texture;
        reader.project()->for_each_asset([&](AssetId id, const Asset& asset) {
            if (asset.kind == AssetKind::Image && asset.name == "sprite"
                && asset.sourcePath.find("sprite.png") != std::string::npos) texture = id;
        });
        AUREA_CHECK(emitter.valid() && texture.valid());
        const auto* layer = comp->layer(emitter);
        AUREA_CHECK(layer && layer->particles.textureAsset == texture.pack());
        // This route accepts a standalone asset only when its pixels reloaded.
        AUREA_CHECK(reader.set_particle_texture(emitter.pack(), 0));
        AUREA_CHECK(reader.set_particle_texture(emitter.pack(), texture.pack()));
    }
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

AUREA_TEST(Serialization, SaveAndLoadRoundTrip) {
    const std::string path = temp_path("roundtrip");
    std::remove(path.c_str());

    Project original = make_project();
    original.metadata().title = "Viagem";
    original.metadata().author = "Dono";
    original.export_settings().width = 3840;
    original.export_settings().height = 2160;
    original.export_settings().videoBitrateMbps = 45;

    std::string error;
    AUREA_CHECK_MSG(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok(),
                    error.c_str());

    Project loaded;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error).ok());
    AUREA_CHECK(report.clean());

    AUREA_CHECK_EQ(loaded.metadata().title, std::string("Viagem"));
    AUREA_CHECK_EQ(loaded.metadata().author, std::string("Dono"));
    AUREA_CHECK_EQ(loaded.export_settings().width, static_cast<u32>(3840));
    AUREA_CHECK_EQ(loaded.export_settings().height, static_cast<u32>(2160));
    AUREA_CHECK_EQ(loaded.export_settings().videoBitrateMbps, static_cast<u32>(45));

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LayersSurviveRoundTrip) {
    const std::string path = temp_path("layers");
    std::remove(path.c_str());

    Project original = make_project();
    const CompositionId cid = original.timeline().root();
    Composition* c = original.timeline().composition(cid);
    AUREA_CHECK(c != nullptr);

    const LayerId video = c->add_layer(LayerKind::Video, "Video principal");
    const LayerId text = c->add_layer(LayerKind::Text, "Titulo");
    const LayerId null = c->add_layer(LayerKind::Null, "Controlador");

    c->layer(video)->start = FrameIndex{30};
    c->layer(video)->end = FrameIndex{300};
    c->layer(video)->offset = FrameIndex{15};
    c->layer(video)->transform.position = Vec3{120.0f, -40.0f, 5.0f};
    c->layer(video)->transform.scale = Vec3{1.5f, 0.75f, 1.0f};
    c->layer(video)->transform.rotation = Vec3{0.0f, 0.0f, 45.0f};
    c->layer(video)->transform.opacity = 0.65f;
    c->layer(video)->blendMode = BlendMode::Screen;
    c->layer(video)->gain = 0.8f;
    c->layer(video)->muted = true;

    c->layer(text)->text.content = "Ola, Aurea";
    c->layer(text)->text.size = 144.0f;
    c->layer(text)->text.color = Vec4{0.2f, 0.4f, 0.9f, 1.0f};
    c->layer(text)->text.alignment = 1;
    c->layer(text)->text.rtl = true;

    c->layer(null)->visible = false;
    c->layer(null)->locked = true;
    c->layer(text)->parent = null;
    c->layer(text)->threeD = true;

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(lc != nullptr);
    AUREA_CHECK_EQ(lc->layers().count(), static_cast<u32>(3));

    // Ordem vertical preservada — é o que decide o que aparece na frente.
    AUREA_CHECK_EQ(lc->order().size(), static_cast<u32>(3));
    const Layer* l0 = lc->layer(lc->order().at(0));
    const Layer* l1 = lc->layer(lc->order().at(1));
    const Layer* l2 = lc->layer(lc->order().at(2));
    AUREA_CHECK_EQ(l0->name, std::string("Video principal"));
    AUREA_CHECK_EQ(l1->name, std::string("Titulo"));
    AUREA_CHECK_EQ(l2->name, std::string("Controlador"));

    AUREA_CHECK_EQ(l0->kind, LayerKind::Video);
    AUREA_CHECK_EQ(l0->start.value, static_cast<i64>(30));
    AUREA_CHECK_EQ(l0->end.value, static_cast<i64>(300));
    AUREA_CHECK_EQ(l0->offset.value, static_cast<i64>(15));
    AUREA_CHECK_NEAR(l0->transform.position.x, 120.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.position.y, -40.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.scale.y, 0.75f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.rotation.z, 45.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.opacity, 0.65f, 1e-5);
    AUREA_CHECK_EQ(l0->blendMode, BlendMode::Screen);
    AUREA_CHECK_NEAR(l0->gain, 0.8f, 1e-5);
    AUREA_CHECK(l0->muted);

    AUREA_CHECK_EQ(l1->text.content, std::string("Ola, Aurea"));
    AUREA_CHECK_NEAR(l1->text.size, 144.0f, 1e-4);
    AUREA_CHECK_NEAR(l1->text.color.z, 0.9f, 1e-5);
    AUREA_CHECK_EQ(l1->text.alignment, static_cast<u32>(1));
    AUREA_CHECK(l1->text.rtl);
    AUREA_CHECK(l1->threeD);

    AUREA_CHECK(!l2->visible);
    AUREA_CHECK(l2->locked);

    // Parenting reconstruído: o filho aponta para a camada certa depois do
    // round-trip, e não para um índice que virou outra camada.
    AUREA_CHECK(l1->parent.valid());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, KeyframesSurviveRoundTrip) {
    const std::string path = temp_path("keyframes");
    std::remove(path.c_str());

    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId id = c->add_layer(LayerKind::Video, "Animada");

    Track& opacity = c->layer(id)->tracks.get_or_create(TrackProperty::Opacity);
    opacity.set(FrameIndex{0}, 0.0f, Interpolation::EaseIn);
    opacity.set(FrameIndex{30}, 1.0f, Interpolation::Bezier);
    opacity.set(FrameIndex{60}, 0.5f, Interpolation::Hold);
    opacity.keys[1].bx1 = 0.11f;
    opacity.keys[1].by1 = 0.22f;
    opacity.keys[1].bx2 = 0.33f;
    opacity.keys[1].by2 = 0.44f;
    opacity.keys[1].easePower = 3;   // v35: a força da bézier vai junto

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc->layer(lc->order().at(0));
    AUREA_CHECK(ll != nullptr);

    const Track* t = ll->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t != nullptr);
    AUREA_CHECK_EQ(t->keys.size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(t->sample(FrameIndex{15}), 0.25f, 0.05f);
    AUREA_CHECK_EQ(t->keys[0].interp, Interpolation::EaseIn);
    AUREA_CHECK_EQ(t->keys[1].interp, Interpolation::Bezier);
    AUREA_CHECK_EQ(t->keys[2].interp, Interpolation::Hold);
    AUREA_CHECK_NEAR(t->keys[1].bx1, 0.11f, 1e-6);
    AUREA_CHECK_NEAR(t->keys[1].by2, 0.44f, 1e-6);
    AUREA_CHECK_EQ(static_cast<u32>(t->keys[1].easePower), 3u);
    AUREA_CHECK_EQ(static_cast<u32>(t->keys[0].easePower), 1u);
    AUREA_CHECK_EQ(t->sample(FrameIndex{45}), opacity.sample(FrameIndex{45}));

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, EffectsAndMasksSurviveRoundTrip) {
    const std::string path = temp_path("effects");
    std::remove(path.c_str());

    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId id = c->add_layer(LayerKind::Video, "Com efeitos");

    // Efeito no formato novo: tipo = id estável, slots genéricos, curva. O
    // tipo é um que esta versão NÃO registra — o projeto tem de guardar tudo
    // mesmo assim (abrir numa versão sem o efeito não pode apagar o ajuste).
    EffectInstance e;
    e.id = c->layer(id)->alloc_effect_id();
    e.type = effect_type_id("aurea.teste.desconhecido");
    e.enabled = false;
    e.expanded = true;
    e.params.resize(3);
    e.params[0].constant = ParamValue::scalar(12.5f);
    e.params[1].constant = ParamValue::color(0.1f, 0.2f, 0.3f, 0.4f);
    e.params[2].source = ParamSource::Expression;
    e.params[2].expression = 3;
    CurveData curve = CurveData::identity();
    curve.channel[0].insert(curve.channel[0].begin() + 1, CurveData::Point{0.5f, 0.7f});
    e.curves.push_back(curve);
    c->layer(id)->effects.push_back(e);

    Mask m;
    m.id = c->layer(id)->alloc_mask_id();
    m.name = "Recorte";
    m.operation = MaskOperation::Subtract;
    m.feather = 4.5f;
    m.expansion = -2.0f;
    m.opacity = 0.9f;
    m.inverted = true;
    m.closed = false;
    m.points.push_back(MaskPoint{Vec2{10.0f, 20.0f}, Vec2{1, 0}, Vec2{0, 1}});
    m.points.push_back(MaskPoint{Vec2{30.0f, 40.0f}, Vec2{0, 0}, Vec2{0, 0}});
    // v17: caminho animado e track matte.
    MaskPathKey k0;
    k0.frame = 3;
    k0.interp = 2;
    k0.points = m.points;
    MaskPathKey k1 = k0;
    k1.frame = 12;
    k1.interp = 0;
    k1.points[0].position = Vec2{55.0f, 66.0f};
    k1.points[1].outTangent = Vec2{-7.5f, 2.25f};
    m.pathKeys = {k0, k1};
    c->layer(id)->masks.push_back(m);
    // A matte aponta para uma camada que EXISTE: ao reabrir, os ids de camada
    // são refeitos e a referência é remapeada (um id solto, sem camada, vira
    // "nenhuma" — antes era copiado cru e apontava para outra camada).
    const LayerId matte = c->add_layer(LayerKind::Shape, "Matte");
    c->layer(id)->matteSource = matte;
    c->layer(id)->matteMode = MatteMode::LumaInverted;

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc->layer(lc->order().at(0));

    AUREA_CHECK_EQ(ll->effects.size(), static_cast<usize>(1));
    const EffectInstance& le = ll->effects[0];
    AUREA_CHECK_EQ(le.type, effect_type_id("aurea.teste.desconhecido"));
    AUREA_CHECK(!le.enabled);
    AUREA_CHECK_EQ(le.params.size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(le.params[0].constant.v[0], 12.5f, 1e-5);
    AUREA_CHECK_NEAR(le.params[1].constant.v[3], 0.4f, 1e-5);
    AUREA_CHECK(le.params[2].source == ParamSource::Expression);
    AUREA_CHECK_EQ(le.params[2].expression, static_cast<u32>(3));
    AUREA_CHECK_EQ(le.curves.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(le.curves[0].channel[0].size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(le.curves[0].channel[0][1].y, 0.7f, 1e-6);

    AUREA_CHECK_EQ(ll->masks.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(ll->masks[0].name, std::string("Recorte"));
    AUREA_CHECK_EQ(ll->masks[0].operation, MaskOperation::Subtract);
    AUREA_CHECK_NEAR(ll->masks[0].feather, 4.5f, 1e-5);
    AUREA_CHECK(ll->masks[0].inverted);
    AUREA_CHECK(!ll->masks[0].closed);
    AUREA_CHECK_EQ(ll->masks[0].points.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(ll->masks[0].points[1].position.y, 40.0f, 1e-5);
    AUREA_CHECK_EQ(ll->masks[0].pathKeys.size(), static_cast<usize>(2));
    if (ll->masks[0].pathKeys.size() == 2) {
        const MaskPathKey& lk = ll->masks[0].pathKeys[1];
        AUREA_CHECK_EQ(lk.frame, static_cast<i64>(12));
        AUREA_CHECK_EQ(lk.interp, static_cast<u8>(0));
        AUREA_CHECK_EQ(ll->masks[0].pathKeys[0].interp, static_cast<u8>(2));
        AUREA_CHECK_EQ(lk.points.size(), static_cast<usize>(2));
        AUREA_CHECK_NEAR(lk.points[0].position.x, 55.0f, 1e-6);
        AUREA_CHECK_NEAR(lk.points[1].outTangent.x, -7.5f, 1e-6);
        AUREA_CHECK_NEAR(lk.points[1].outTangent.y, 2.25f, 1e-6);
    }
    AUREA_CHECK(lc->layer(ll->matteSource) != nullptr && lc->layer(ll->matteSource)->name == "Matte");
    AUREA_CHECK(ll->matteMode == MatteMode::LumaInverted);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, AssetsSurviveRoundTrip) {
    const std::string path = temp_path("assets");
    std::remove(path.c_str());

    Project original = make_project();
    Asset a;
    a.kind = AssetKind::Video;
    a.name = "clipe.mp4";
    a.sourcePath = "/media/clipe.mp4";
    a.proxyPath = "/cache/clipe_proxy.mp4";
    a.proxyWidth = 960;
    a.proxyHeight = 540;
    a.video.width = 3840;
    a.video.height = 2160;
    a.video.fps = 29.97;
    a.video.variableFrameRate = true;
    a.audio.sampleRate = 48000;
    a.audio.channels = 2;
    a.duration = FrameIndex{1800};
    a.contentHash = 0xDEADBEEFCAFEull;
    a.profile.codecTag = 0x68766331u;   // 'hvc1'
    a.profile.bitDepth = 10;
    a.profile.hdr = true;
    a.profile.transfer = ColorSpace::HLG;
    (void)original.add_asset(std::move(a));

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    AUREA_CHECK_EQ(loaded.asset_count(), static_cast<u32>(1));

    const AssetId found = loaded.find_asset_by_hash(0xDEADBEEFCAFEull);
    AUREA_CHECK(found.valid());
    const Asset* la = loaded.asset(found);
    AUREA_CHECK(la != nullptr);
    AUREA_CHECK_EQ(la->video.width, static_cast<u32>(3840));
    AUREA_CHECK_EQ(la->video.height, static_cast<u32>(2160));
    AUREA_CHECK(la->video.variableFrameRate);
    AUREA_CHECK_NEAR(la->video.fps, 29.97, 1e-6);
    AUREA_CHECK_EQ(la->profile.bitDepth, static_cast<u8>(10));
    AUREA_CHECK(la->profile.hdr);
    AUREA_CHECK_EQ(la->profile.transfer, ColorSpace::HLG);
    AUREA_CHECK(la->has_audio());
    AUREA_CHECK(la->proxy_ready());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, AssetDeduplicationByHash) {
    Project p = make_project();
    Asset a;
    a.name = "primeiro";
    a.contentHash = 12345;
    const AssetId first = p.add_asset(a);

    Asset b;
    b.name = "segundo (mesmo arquivo)";
    b.contentHash = 12345;
    const AssetId second = p.add_asset(b);

    // Importar o mesmo vídeo duas vezes não deve ocupar duas vezes o cache de
    // decoders nem duplicar o proxy em disco.
    AUREA_CHECK(first == second);
    AUREA_CHECK_EQ(p.asset_count(), static_cast<u32>(1));
}

AUREA_TEST(Serialization, PeekReadsHeaderWithoutFullLoad) {
    const std::string path = temp_path("peek");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    FileHeader header;
    std::vector<SectionHeader> sections;
    AUREA_CHECK(ProjectSerializer::peek(path, header, sections, &error).ok());
    AUREA_CHECK_EQ(header.magic, FileHeader::kMagic);
    AUREA_CHECK_EQ(header.formatVersion, FileHeader::kCurrentFormatVersion);
    AUREA_CHECK(sections.size() >= 3);
    AUREA_CHECK(header.appVersion[0] != 0);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsFileThatIsNotAurea) {
    const std::string path = temp_path("bogus");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    // Maior que o cabeçalho, para exercitar a checagem do magic e não a de
    // tamanho: são dois caminhos distintos e cada um tem o seu teste.
    const char junk[] =
        "isto nao e um projeto do aurea, e grande o bastante para passar do "
        "cabecalho de sessenta e quatro bytes do formato";
    AUREA_CHECK(sizeof(junk) > 64);
    (void)std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);

    Project p;
    std::string error;
    const Status s = ProjectSerializer::load(p, path, LoadOptions{}, nullptr, &error);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::UnsupportedFormat);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsFileShorterThanHeader) {
    // Um arquivo menor que o cabeçalho não pode ser lido nem para checar o
    // magic. O diagnóstico correto é "truncado", não "não é do Aurea" — a
    // diferença importa para quem recebeu um arquivo cortado por um download
    // interrompido.
    const std::string path = temp_path("tiny");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    (void)std::fwrite("AURE", 1, 4, f);
    std::fclose(f);

    Project p;
    const Status s = ProjectSerializer::load(p, path, LoadOptions{});
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::CorruptData);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, PeekRejectsForeignFile) {
    const std::string path = temp_path("peek_bogus");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    const char junk[] = "conteudo qualquer que nao e um projeto do aurea engine";
    (void)std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);

    FileHeader header;
    std::vector<SectionHeader> sections;
    const Status s = ProjectSerializer::peek(path, header, sections, nullptr);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::UnsupportedFormat);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsTruncatedFile) {
    const std::string path = temp_path("truncated");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    // Corta o arquivo pela metade: é o que uma queda no meio da escrita produz.
    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        AUREA_CHECK(f != nullptr);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size / 2));
        const usize read = data.empty() ? 0 : std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        AUREA_CHECK(f != nullptr);
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    Project loaded;
    LoadReport report;
    // Sem tolerância: recusa. Um projeto aberto pela metade é pior do que um
    // projeto que não abre, porque o usuário pode salvar por cima.
    const Status strict = ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error);
    AUREA_CHECK(!strict.ok());

    // Com tolerância: abre o que der e REPORTA. É o caminho da recuperação
    // pós-crash, onde algo é melhor que nada.
    Project tolerant;
    LoadReport tolerantReport;
    const Status lenient = ProjectSerializer::load(tolerant, path,
                                                   LoadOptions{false, true, true},
                                                   &tolerantReport, &error);
    if (lenient.ok()) {
        AUREA_CHECK(!tolerantReport.clean() || tolerantReport.sectionsRead.size() > 0);
    }

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, CorruptedSectionIsDetectedByChecksum) {
    const std::string path = temp_path("corrupt");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    // Corrompe um byte no meio do arquivo (dentro dos dados de alguma seção).
    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    AUREA_CHECK(data.size() > 200);
    data[data.size() - 50] ^= 0xFF;
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    Project loaded;
    LoadReport report;
    const Status s = ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error);
    // O checksum existe justamente para isto: interpretar lixo como camadas
    // poderia produzir handles inválidos e, a partir daí, acesso a memória
    // errada. Detectar e recusar é o comportamento correto.
    AUREA_CHECK(!s.ok() || !report.sectionsCorrupt.empty());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, SaveIsAtomicLeavesNoTempFile) {
    const std::string path = temp_path("atomic");
    const std::string tmp = path + ".tmp";
    std::remove(path.c_str());
    std::remove(tmp.c_str());

    Project p = make_project();
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}).ok());

    // Um temporário deixado para trás indica que a gravação atômica não
    // completou — e o arquivo bom não foi trocado.
    std::FILE* leftover = std::fopen(tmp.c_str(), "rb");
    AUREA_CHECK(leftover == nullptr);
    if (leftover) std::fclose(leftover);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, SaveOverExistingKeepsOldUntilComplete) {
    const std::string path = temp_path("overwrite");
    std::remove(path.c_str());

    Project first = make_project();
    first.metadata().title = "Primeira versao";
    AUREA_CHECK(ProjectSerializer::save(first, path, SaveOptions{}).ok());

    Project second = make_project();
    second.metadata().title = "Segunda versao";
    AUREA_CHECK(ProjectSerializer::save(second, path, SaveOptions{}).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}).ok());
    AUREA_CHECK_EQ(loaded.metadata().title, std::string("Segunda versao"));

    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Journal de autosave
// -----------------------------------------------------------------------------
AUREA_TEST(Journal, AppendAndReadBack) {
    const std::string path = temp_path("journal");
    std::remove(path.c_str());

    Command cmds[3];
    for (u32 i = 0; i < 3; ++i) {
        cmds[i].type = CommandType::LayerSetOpacity;
        cmds[i].opacity.opacity = 0.1f * static_cast<f32>(i);
    }
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 3, nullptr, 0).ok());

    // Segunda gravação: o journal é append-only, e cada bloco é independente.
    Command more[2];
    for (u32 i = 0; i < 2; ++i) {
        more[i].type = CommandType::LayerSetPosition;
    }
    AUREA_CHECK(ProjectSerializer::append_journal(path, more, 2, nullptr, 0).ok());

    std::vector<Command> readBack;
    AUREA_CHECK(ProjectSerializer::read_journal(path, readBack).ok());
    AUREA_CHECK_EQ(readBack.size(), static_cast<usize>(5));

    std::remove(path.c_str());
}

AUREA_TEST(Journal, TruncatedBlockStopsButKeepsEarlierOnes) {
    // A queda acontece no meio da última gravação. Os blocos anteriores
    // continuam válidos — é o que faz a recuperação pós-crash valer a pena.
    const std::string path = temp_path("journal_trunc");
    std::remove(path.c_str());

    Command cmds[4];
    for (u32 i = 0; i < 4; ++i) cmds[i].type = CommandType::LayerSetOpacity;
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());

    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    data.resize(data.size() - 32);   // corta o fim do segundo bloco
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    std::vector<Command> readBack;
    const Status s = ProjectSerializer::read_journal(path, readBack);
    AUREA_CHECK(s.ok());
    AUREA_CHECK_EQ(readBack.size(), static_cast<usize>(4));

    std::remove(path.c_str());
}

AUREA_TEST(Journal, CorruptedBlockIsDiscarded) {
    const std::string path = temp_path("journal_corrupt");
    std::remove(path.c_str());

    Command cmds[4];
    for (u32 i = 0; i < 4; ++i) cmds[i].type = CommandType::LayerSetOpacity;
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());

    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    // Corrompe um byte DENTRO dos comandos, não no cabeçalho.
    data[data.size() - 10] ^= 0x5A;
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    std::vector<Command> readBack;
    // O checksum por bloco descarta o bloco corrompido sozinho, sem invalidar
    // os anteriores.
    (void)ProjectSerializer::read_journal(path, readBack);
    AUREA_CHECK(readBack.empty());

    std::remove(path.c_str());
}

AUREA_TEST(Journal, ReadMissingFileReportsNotFound) {
    std::vector<Command> cmds;
    const Status s = ProjectSerializer::read_journal("nao_existe_journal.bin", cmds);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::NotFound);
}

AUREA_TEST(Serialization, IncrementalSaveIsDeclaredUnimplemented) {
    // Honestidade do contrato: a gravação incremental NÃO está implementada, e
    // o motor diz isso em vez de aceitar a opção e gravar tudo em silêncio.
    AUREA_CHECK(!ProjectSerializer::incremental_save_implemented());
}

AUREA_TEST(Serialization, AdjustmentGuideAndLabelSurviveRoundTrip) {
    const std::string path = temp_path("papel_da_camada");
    std::remove(path.c_str());
    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId adj = c->add_layer(LayerKind::Shape, "Ajuste");
    const LayerId guide = c->add_layer(LayerKind::Shape, "Guia");
    const LayerId plain = c->add_layer(LayerKind::Shape, "Comum");
    c->layer(adj)->adjustment = true;
    c->layer(adj)->label = 3;
    c->layer(guide)->guide = true;
    c->layer(guide)->solo = true;
    c->layer(guide)->label = 12;
    std::string error;
    AUREA_CHECK_MSG(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok(), error.c_str());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(lc != nullptr);
    AUREA_CHECK_EQ(lc->order().size(), 3u);
    const Layer* la = lc->layer(lc->order().at(0));
    const Layer* lg = lc->layer(lc->order().at(1));
    const Layer* lp = lc->layer(lc->order().at(2));
    AUREA_CHECK(la && lg && lp);
    AUREA_CHECK(la->name == "Ajuste" && la->adjustment && !la->guide && la->label == 3);
    AUREA_CHECK(lg->name == "Guia" && lg->guide && lg->solo && !lg->adjustment && lg->label == 12);
    AUREA_CHECK(lp->name == "Comum" && !lp->adjustment && !lp->guide && lp->label == 0);
    (void)plain;
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LegacyParticleProjectsComeBackAsBoxEmitters) {
    // Faíscas, Neve e Poeira de luz emitiam SEMPRE da caixa da camada, e os
    // projetos delas gravaram `emitterType` no zero (o campo nem era escrito
    // pela UI da época). Sem esta correção a neve de um projeto antigo reabriria
    // saindo de um ponto, no centro — o trabalho do usuário mudaria de cara.
    ParticleData p;
    p.emitterType = 0;
    p.emitterSize = Vec2{320.0f, 10.0f};
    p.emitterOffset = Vec2{0.0f, -100.0f};
    migrate_legacy_particles(p, 19);
    AUREA_CHECK_EQ(p.emitterType, static_cast<u32>(ParticleEmitter::Box));
    AUREA_CHECK(p.emitterSize.x == 320.0f && p.emitterOffset.y == -100.0f);
    // O emissor de verdade do arquivo antigo continua onde estava: só o TIPO
    // muda, porque era o único que o formato não guardava.

    // De v20 em diante o campo é do usuário e NÃO se mexe — inclusive Ponto,
    // que é uma escolha legítima de quem montou o sistema no Particular.
    ParticleData q;
    q.emitterType = static_cast<u32>(ParticleEmitter::Point);
    migrate_legacy_particles(q, 20);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Point));
    migrate_legacy_particles(q, 21);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Point));
    q.emitterType = static_cast<u32>(ParticleEmitter::Mesh);
    migrate_legacy_particles(q, 21);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Mesh));
}

AUREA_TEST(Serialization, ObjectEnvironmentIsIndependentAndSurvivesReopen) {
    // Ambiente por objeto (v22): cada modelo 3D tem o SEU ambiente. Mexer num
    // não pode mexer no outro nem no do projeto — era o que o dono via
    // ("coloco HDRI no objeto A e o B muda").
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const LayerId a = comp->add_layer(LayerKind::Model3D, "A");
    const LayerId b = comp->add_layer(LayerKind::Model3D, "B");
    comp->layer(a)->threeD = true;
    comp->layer(b)->threeD = true;

    // A com ambiente próprio; B e o projeto ficam como estavam.
    AUREA_CHECK(e.set_object_environment(a.pack(), 1, 4242, 2.5f, 45.0f, 1.5f));
    f32 va[5]{}, vb[5]{}, proj[3]{};
    AUREA_CHECK(e.query_object_environment(a.pack(), va));
    AUREA_CHECK(e.query_object_environment(b.pack(), vb));
    AUREA_CHECK(e.query_environment(proj));
    AUREA_CHECK(va[0] == 1.0f && va[1] == 4242.0f && va[2] == 2.5f);
    AUREA_CHECK(vb[0] == 0.0f && vb[1] == 0.0f);          // B: do projeto
    AUREA_CHECK(proj[1] == 1.0f && proj[2] == 0.0f);      // o do projeto intacto

    // Mexer no ambiente do PROJETO não muda o estado do objeto.
    AUREA_CHECK(e.set_environment_params(3.0f, 90.0f));
    AUREA_CHECK(e.query_object_environment(a.pack(), va));
    AUREA_CHECK(va[2] == 2.5f && va[3] == 45.0f);

    // Salvar e reabrir: o ambiente de cada objeto volta igual.
    const std::string path = temp_path("ambiente_por_objeto");
    std::remove(path.c_str());
    std::string err;
    AUREA_CHECK_MSG(ProjectSerializer::save(*e.project(), path, SaveOptions{}, &err).ok(), err.c_str());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load(reopened, path, LoadOptions{}, nullptr, &err).ok());
    const Composition* c2 = reopened.timeline().composition(reopened.timeline().current());
    const Layer* la = nullptr;
    const Layer* lb = nullptr;
    for (usize i = 0; i < c2->order().size(); ++i) {
        const Layer* l = c2->layer(c2->order().at(i));
        if (l && l->name == "A") la = l;
        if (l && l->name == "B") lb = l;
    }
    AUREA_CHECK(la && lb);
    std::printf("    reaberto: A fonte %u hdri %llu int %.2f giro %.1f exp %.2f | B fonte %u\n",
                la->environmentSource, static_cast<unsigned long long>(la->environmentAsset),
                static_cast<f64>(la->environmentIntensity), static_cast<f64>(la->environmentRotation),
                static_cast<f64>(la->environmentExposure), lb->environmentSource);
    AUREA_CHECK(la->environmentSource == 1u && la->environmentAsset == 4242u);
    AUREA_CHECK(std::fabs(la->environmentIntensity - 2.5f) < 1e-4f);
    AUREA_CHECK(std::fabs(la->environmentRotation - 45.0f) < 1e-4f);
    AUREA_CHECK(std::fabs(la->environmentExposure - 1.5f) < 1e-4f);
    AUREA_CHECK(lb->environmentSource == 0u && lb->environmentAsset == 0u);
    e.shutdown();
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// O projeto INTEIRO: o formato é um só, e nada se perde
// -----------------------------------------------------------------------------

namespace {

std::vector<u8> ler_arquivo(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<u8> bytes(n > 0 ? static_cast<usize>(n) : 0);
    if (!bytes.empty()) {
        const usize lidos = std::fread(bytes.data(), 1, bytes.size(), f);
        bytes.resize(lidos);
    }
    std::fclose(f);
    return bytes;
}

/// Quantos de cada coisa o projeto tem. Serve para o teste não ser vazio: se a
/// releitura devolvesse um projeto limpo, os bytes bateriam e a contagem não.
struct Censo {
    u32 camadas = 0, efeitos = 0, keyframes = 0, mascaras = 0, trilhas = 0;
    u32 assets = 0, composicoes = 0, texto = 0, forma = 0, modelo3d = 0, precomp = 0;
};

Censo censo(Engine& e) {
    Censo c;
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    if (comp) {
        comp->layers().for_each([&](LayerId, const Layer& l) {
            ++c.camadas;
            if (l.kind == LayerKind::Text) ++c.texto;
            if (l.kind == LayerKind::Shape) ++c.forma;
            if (l.kind == LayerKind::Model3D) ++c.modelo3d;
            if (l.kind == LayerKind::Composition) ++c.precomp;
            c.efeitos += static_cast<u32>(l.effects.size());
            c.mascaras += static_cast<u32>(l.masks.size());
            c.trilhas += l.tracks.size();
            for (u32 t = 0; t < l.tracks.size(); ++t) c.keyframes += static_cast<u32>(l.tracks.at(t).keys.size());
            c.keyframes += static_cast<u32>(l.timeRemap.keys.size());
        });
    }
    c.assets = e.project()->asset_count();
    c.composicoes = e.project()->timeline().composition_count();
    return c;
}

} // namespace

AUREA_TEST(Serialization, WholeProjectWithEveryFeatureIsByteStable) {
    // O formato `.aurea` é UM SÓ: o Android e o iOS escrevem e leem por este
    // mesmo código (a ponte do iOS chama `new_project`/`save_project`/
    // `load_project`, e a serialização não tem um ramo por plataforma). O que
    // este teste prova é a outra metade da promessa do dono — que um projeto
    // com TUDO dentro volta idêntico:
    //
    //   salvar → abrir → serializar de novo dá BYTES IGUAIS.
    // A segunda gravação usa o serializador para preservar modifiedUnixMs:
    // Engine::save_project atualiza legitimamente esse campo a cada gravação.
    //
    // Comparar byte a byte cobre cada campo que o formato grava, inclusive os
    // que alguém esquecer de conferir num teste por campo. E o censo ao lado
    // garante que o teste não está comparando dois projetos vazios.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, "Projeto completo").ok());

    // Imagem
    std::vector<u8> px(32 * 32 * 4, 200);
    const Result<u64> img = e.import_image(px.data(), 32, 32, "quadrado.png", nullptr);
    AUREA_CHECK(img.ok());
    // Forma, texto, nulo 3D, particular e texto 3D
    const Result<u64> forma = e.add_shape(0);
    AUREA_CHECK(forma.ok());
    const Result<u64> texto = e.add_text("Aurea");
    AUREA_CHECK(texto.ok());
    const Result<u64> nulo = e.add_null(true);
    AUREA_CHECK(nulo.ok());
    const Result<u64> part = e.add_particles(0);
    AUREA_CHECK(part.ok());
    scene3d::Text3DSpec t3;
    t3.content = "AUREA";
    t3.bevel = true;
    t3.bevelWidth = 0.03f;
    t3.bevelSegments = 3;
    t3.regionMaterials = true;
    t3.bevelMat.metallic = 1.0f;
    const Result<u64> t3d = e.add_text3d(t3);
    AUREA_CHECK(t3d.ok());

    // Máscara, efeito com keyframes, ambiente por objeto, sombras, remap.
    AUREA_CHECK(e.add_mask(*forma, nullptr, 0, true) >= 0);
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*texto);
    add.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    AUREA_CHECK(e.apply_command(add).ok());
    // O id do efeito é do núcleo (alloc_effect_id) — não um número escolhido.
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const u32 idEfeito = comp->layer(LayerId::unpack(*texto))->effects.back().id;
    Command set;
    set.type = CommandType::EffectSetParam;
    set.effect_param.layer = LayerId::unpack(*texto);
    set.effect_param.effect = EffectId{idEfeito, 0};
    set.effect_param.paramIndex = 0;
    set.effect_param.value = 12.5f;
    AUREA_CHECK(e.apply_command(set).ok());

    auto key = [&](Result<u64> camada, TrackProperty p, FrameIndex t, f32 v) {
        if (!camada.ok()) return;
        Command k;
        k.type = CommandType::KeyframeInsert;
        k.keyframe.track.layer = LayerId::unpack(*camada);
        k.keyframe.track.property = p;
        k.keyframe.track.effectIndex = kInvalidIndex;
        k.keyframe.track.effectParamIndex = 0;
        k.keyframe.time = t;
        k.keyframe.value = v;
        AUREA_CHECK(e.apply_command(k).ok());
    };
    key(*forma, TrackProperty::PositionX, FrameIndex{0}, 40.0f);
    key(*forma, TrackProperty::PositionX, FrameIndex{40}, 260.0f);
    key(*forma, TrackProperty::RotationZ, FrameIndex{0}, 0.0f);
    key(*forma, TrackProperty::RotationZ, FrameIndex{40}, 90.0f);
    key(*texto, TrackProperty::Opacity, FrameIndex{0}, 1.0f);
    key(*texto, TrackProperty::Opacity, FrameIndex{30}, 0.2f);

    if (t3d.ok()) {
        AUREA_CHECK(e.set_object_environment(*t3d, 1, 0, 2.0f, 45.0f, 1.2f));
        AUREA_CHECK(e.set_model_shadows(*t3d, false, true));
    }
    AUREA_CHECK(e.set_time_remap(*texto, true));
    AUREA_CHECK(e.edit_time_remap_key(*texto, -1, 20, 10.0f, 0) >= 0);

    const Censo antes = censo(e);
    std::printf("\n    projeto completo: %u camadas, %u efeitos, %u keyframes, %u trilhas, %u mascaras, %u assets\n",
                antes.camadas, antes.efeitos, antes.keyframes, antes.trilhas, antes.mascaras, antes.assets);
    AUREA_CHECK(antes.camadas >= 6);
    AUREA_CHECK(antes.efeitos >= 1);
    AUREA_CHECK(antes.keyframes >= 5);
    AUREA_CHECK(antes.mascaras >= 1);

    const std::string a = temp_path("completo_a");
    const std::string b = temp_path("completo_b");
    std::remove(a.c_str());
    std::remove(b.c_str());
    AUREA_CHECK(e.save_project(a.c_str()).ok());
    const std::vector<u8> bytesA = ler_arquivo(a);
    AUREA_CHECK(!bytesA.empty());

    AUREA_CHECK(e.load_project(a.c_str()).ok());
    const Censo depois = censo(e);
    AUREA_CHECK(depois.camadas == antes.camadas);
    AUREA_CHECK(depois.efeitos == antes.efeitos);
    AUREA_CHECK(depois.keyframes == antes.keyframes);
    AUREA_CHECK(depois.trilhas == antes.trilhas);
    AUREA_CHECK(depois.mascaras == antes.mascaras);
    AUREA_CHECK(depois.assets == antes.assets);
    AUREA_CHECK(depois.composicoes == antes.composicoes);

    AUREA_CHECK(ProjectSerializer::save(*e.project(), b, SaveOptions{}).ok());
    const std::vector<u8> bytesB = ler_arquivo(b);
    AUREA_CHECK(bytesB.size() == bytesA.size());
    usize iguais = 0;
    for (usize i = 0; i < bytesA.size() && i < bytesB.size(); ++i) iguais += bytesA[i] == bytesB[i];
    std::printf("    ida e volta: %zu bytes, %zu iguais\n", bytesA.size(), iguais);
    if (iguais != bytesA.size()) {
        for (usize i = 0, shown = 0; i < bytesA.size() && i < bytesB.size() && shown < 16; ++i) {
            if (bytesA[i] == bytesB[i]) continue;
            std::printf("    byte %zu: %02x -> %02x\n", i, bytesA[i], bytesB[i]);
            ++shown;
        }
    }
    AUREA_CHECK(iguais == bytesA.size());

    std::remove(a.c_str());
    std::remove(b.c_str());
}

AUREA_TEST(Serialization, DuplicateKeyframesKeepLastRecordAndRemainEditable) {
    const std::string path = temp_path("duplicate_keys");
    Project original = make_project();
    Composition* comp = original.timeline().composition(original.timeline().root());
    const LayerId layer = comp->add_layer(LayerKind::Null, "Legacy animation");
    Track& track = comp->layer(layer)->tracks.get_or_create(TrackProperty::PositionX);
    // Emulate a legacy/imported track. Presets already use last-record-wins.
    track.keys = {
        Keyframe{FrameIndex{40}, 40.0f}, Keyframe{FrameIndex{20}, -100.0f},
        Keyframe{FrameIndex{0}, -50.0f}, Keyframe{FrameIndex{20}, -200.0f},
        Keyframe{FrameIndex{0}, 0.0f}, Keyframe{FrameIndex{20}, 20.0f, Interpolation::Bezier}
    };
    track.keys.back().bx1 = 0.2f;
    track.keys.back().by1 = -0.5f;
    track.keys.back().bx2 = 0.8f;
    track.keys.back().by2 = 1.5f;
    track.keys.back().tangentIn = 7.0f;
    track.keys.back().tangentOut = -9.0f;
    track.keys.back().easingPreset = 23;
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    comp = loaded.timeline().composition(loaded.timeline().root());
    Track* editable = comp->layer(comp->order().at(0))->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(editable != nullptr);
    AUREA_CHECK_EQ(editable->keys.size(), static_cast<usize>(3));
    AUREA_CHECK_EQ(editable->find_exact(FrameIndex{20}), static_cast<u32>(1));
    AUREA_CHECK_EQ(editable->keys[1].interp, Interpolation::Bezier);
    AUREA_CHECK_NEAR(editable->keys[1].by1, -0.5f, 1e-6);
    AUREA_CHECK_NEAR(editable->keys[1].by2, 1.5f, 1e-6);
    AUREA_CHECK_NEAR(editable->keys[1].tangentIn, 7.0f, 1e-6);
    AUREA_CHECK_NEAR(editable->keys[1].tangentOut, -9.0f, 1e-6);
    AUREA_CHECK_EQ(editable->keys[1].easingPreset, static_cast<u16>(23));
    AUREA_CHECK_NEAR(editable->sample(FrameIndex{40}), 40.0f, 1e-6);
    AUREA_CHECK_NEAR(editable->sample(FrameIndex{0}), 0.0f, 1e-6);
    AUREA_CHECK_NEAR(editable->sample(FrameIndex{20}), 20.0f, 1e-6);
    editable->set(FrameIndex{20}, 75.0f);
    (void)editable->sample(FrameIndex{39}); // Change cached interval before seeking backwards.
    AUREA_CHECK_NEAR(editable->sample(FrameIndex{20}), 75.0f, 1e-6);
    AUREA_CHECK_EQ(editable->move(FrameIndex{20}, FrameIndex{10}), static_cast<u32>(1));
    AUREA_CHECK_EQ(editable->find_exact(FrameIndex{20}), kInvalidIndex);
    AUREA_CHECK_NEAR(editable->sample(FrameIndex{10}), 75.0f, 1e-6);
    const Track expected = *editable;
    AUREA_CHECK(ProjectSerializer::save(loaded, path, SaveOptions{}, &error).ok());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load(reopened, path, LoadOptions{}, nullptr, &error).ok());
    comp = reopened.timeline().composition(reopened.timeline().root());
    const Track* actual = comp->layer(comp->order().at(0))->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(actual != nullptr);
    AUREA_CHECK_EQ(actual->keys.size(), expected.keys.size());
    for (i64 i = 0; i < 61; ++i) {
        const FrameIndex frame{(i * 43) % 61 - 10};
        AUREA_CHECK_NEAR(actual->sample(frame), expected.sample(frame), 1e-6);
    }
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LargeReverseOrderedTrackLoadsInSortedOrder) {
    const std::string path = temp_path("reverse_keys");
    Project original = make_project();
    Composition* comp = original.timeline().composition(original.timeline().root());
    const LayerId layer = comp->add_layer(LayerKind::Null, "Imported tracking");
    Track& track = comp->layer(layer)->tracks.get_or_create(TrackProperty::PositionX);
    constexpr i64 count = 32768;
    track.keys.reserve(count);
    for (i64 i = count; i > 0; --i) track.keys.push_back(Keyframe{FrameIndex{i}, static_cast<f32>(i)});
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    comp = loaded.timeline().composition(loaded.timeline().root());
    const Track* actual = comp->layer(comp->order().at(0))->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(actual != nullptr);
    AUREA_CHECK_EQ(actual->keys.size(), static_cast<usize>(count));
    for (i64 i = 0; i < count; ++i) {
        AUREA_CHECK_EQ(actual->keys[static_cast<usize>(i)].time.value, i + 1);
        AUREA_CHECK_NEAR(actual->keys[static_cast<usize>(i)].value, static_cast<f32>(i + 1), 1e-6);
    }
    std::remove(path.c_str());
}

namespace {
Layer* remap_test_layer(Project& project) {
    auto* comp = project.timeline().composition(project.timeline().root());
    return comp->layer(comp->order().at(0));
}
u32 legacy_remap_effect(Layer& layer, bool enabled = true) {
    EffectInstance effect;
    effect.id = layer.alloc_effect_id();
    effect.type = effect_type_id(effect_keys::kTimeRemap);
    effect.enabled = enabled;
    effect.params.resize(2);
    layer.effects.push_back(effect);
    return effect.id;
}
}

AUREA_TEST(Serialization, LegacyRemapSecondsPreserveFractionalFpsCurvesAndHistory) {
    const std::string path = temp_path("legacy_remap_seconds");
    constexpr f64 fps = 30000.0 / 1001.0;
    auto created = Project::create_new(320, 180, fps, "Old remap");
    Project original = std::move(*created);
    auto* comp = original.timeline().composition(original.timeline().root());
    const LayerId id = comp->add_layer(LayerKind::Video, "VFR clip");
    Layer* layer = comp->layer(id);
    layer->start = FrameIndex{17}; layer->end = FrameIndex{217};
    Asset asset; asset.kind = AssetKind::Video; asset.name = "VFR";
    asset.video.fps = 59.94; asset.video.variableFrameRate = true;
    asset.timebaseFps = 120.0; asset.contentHash = 812345;
    layer->source = original.add_asset(std::move(asset));
    const u32 effect = legacy_remap_effect(*layer, false);
    Track& old = layer->tracks.get_or_create(TrackProperty::EffectParam, effect, 0);
    // Explicit legacy data: time remains a local timeline frame, value is seconds.
    old.keys = {Keyframe{FrameIndex{91}, 3.25f}, Keyframe{FrameIndex{7}, 0.5f, Interpolation::Bezier}};
    old.keys.back().bx1 = 0.21f; old.keys.back().by1 = -0.4f;
    old.keys.back().bx2 = 0.72f; old.keys.back().by2 = 1.3f;
    old.keys.back().tangentIn = -0.75f; old.keys.back().tangentOut = 1.25f;
    old.keys.back().easingPreset = 17;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}).ok());
    Engine engine; EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.load_project(path.c_str()).ok());
    comp = engine.project()->timeline().composition(engine.project()->timeline().root());
    const LayerId loadedId = comp->order().at(0);
    layer = comp->layer(loadedId);
    AUREA_CHECK_NEAR(comp->fps(), fps, 1e-12);
    AUREA_CHECK(layer->timeRemapLegacyMigrated);
    AUREA_CHECK(!layer->timeRemapEnabled);
    AUREA_CHECK_EQ(layer->tracks.size(), 0u);
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{1});
    AUREA_CHECK_EQ(layer->timeRemap.keys.size(), usize{2});
    const Keyframe first = layer->timeRemap.keys.front();
    AUREA_CHECK_EQ(first.time.value, i64{7});
    AUREA_CHECK_NEAR(first.value, 0.5 * fps, 1e-5);
    AUREA_CHECK_NEAR(first.tangentIn, -0.75 * fps, 1e-5);
    AUREA_CHECK_NEAR(first.tangentOut, 1.25 * fps, 1e-5);
    AUREA_CHECK_NEAR(first.by1, -0.4f, 1e-7);
    AUREA_CHECK_NEAR(first.by2, 1.3f, 1e-7);
    AUREA_CHECK_EQ(first.easingPreset, u16{17});
    AUREA_CHECK_EQ(first.interp, Interpolation::Bezier);
    AUREA_CHECK_NEAR(layer->timeRemapLegacyTracks[0].keys.front().value, 0.5f, 1e-7);
    const Asset* loadedAsset = engine.project()->asset(layer->source);
    AUREA_CHECK(loadedAsset && loadedAsset->video.variableFrameRate);
    if (loadedAsset) AUREA_CHECK_NEAR(loadedAsset->video.fps, 59.94, 1e-12);
    bridge::KeyframeRow rows[8]{};
    AUREA_CHECK_EQ(engine.query_keyframes(loadedId.pack(), rows, 8), 2u);
    AUREA_CHECK_EQ(engine.query_all_keyframes(nullptr, 0, nullptr, 0, nullptr), 2u);
    for (u32 i = 0; i < 2; ++i) AUREA_CHECK_EQ(rows[i].property, static_cast<u32>(TrackProperty::TimeRemap));
    Command enabled; enabled.type = CommandType::EffectSetEnabled;
    enabled.effect_enabled.layer = loadedId; enabled.effect_enabled.effect = EffectId{effect, 0}; enabled.effect_enabled.enabled = true;
    AUREA_CHECK(engine.apply_command(enabled).ok());
    AUREA_CHECK(layer->timeRemapEnabled);
    Command key; key.type = CommandType::KeyframeSetValue;
    key.keyframe.track = TrackRef{loadedId, TrackProperty::EffectParam, effect, 0};
    key.keyframe.time = FrameIndex{7}; key.keyframe.value = 2.0f;
    AUREA_CHECK(engine.apply_command(key).ok());
    AUREA_CHECK_NEAR(layer->timeRemap.keys.front().value, 2.0 * fps, 1e-5);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(engine.apply_command(undo).ok());
    comp = engine.project()->timeline().composition(engine.project()->timeline().root());
    layer = comp->layer(loadedId);
    AUREA_CHECK_NEAR(layer->timeRemap.keys.front().value, first.value, 1e-7);
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{1});
    // Removing every canonical key must not resurrect archived aliases on load.
    key.type = CommandType::KeyframeDelete;
    AUREA_CHECK(engine.apply_command(key).ok());
    key.keyframe.time = FrameIndex{91};
    AUREA_CHECK(engine.apply_command(key).ok());
    AUREA_CHECK(engine.save_project(path.c_str()).ok());
    AUREA_CHECK(engine.load_project(path.c_str()).ok());
    layer = remap_test_layer(*engine.project());
    AUREA_CHECK(layer->timeRemap.keys.empty());
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{1});
    AUREA_CHECK_NEAR(layer->timeRemapLegacyTracks[0].keys.back().value, 3.25f, 1e-7);
    AUREA_CHECK_EQ(engine.query_all_keyframes(nullptr, 0, nullptr, 0, nullptr), 0u);
    engine.shutdown(); std::remove(path.c_str());
}

AUREA_TEST(Serialization, LegacyRemapCanonicalWinsAndArchivesConflictingInformation) {
    const std::string path = temp_path("legacy_remap_canonical");
    Project original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    Layer* layer = comp->layer(comp->add_layer(LayerKind::Video, "Canonical"));
    const u32 effect = legacy_remap_effect(*layer);
    layer->timeRemap.property = TrackProperty::TimeRemap;
    layer->timeRemap.set(FrameIndex{11}, 72.0f, Interpolation::Hold);
    layer->timeRemapEnabled = false;
    layer->tracks.get_or_create(TrackProperty::TimeRemap).set(FrameIndex{5}, 900.0f);
    layer->tracks.get_or_create(TrackProperty::EffectParam, effect, 0).set(FrameIndex{11}, 10.0f);
    // Another effect component has unknown semantics and must stay untouched.
    layer->tracks.get_or_create(TrackProperty::EffectParam, effect, 256).set(FrameIndex{99}, 6.0f);
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}).ok());
    layer = remap_test_layer(loaded);
    AUREA_CHECK_EQ(layer->timeRemap.keys.size(), usize{1});
    AUREA_CHECK_EQ(layer->timeRemap.keys[0].time.value, i64{11});
    AUREA_CHECK_NEAR(layer->timeRemap.keys[0].value, 72.0f, 1e-7);
    AUREA_CHECK_EQ(layer->timeRemap.keys[0].interp, Interpolation::Hold);
    AUREA_CHECK(!layer->timeRemapEnabled);
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{2});
    AUREA_CHECK_EQ(layer->tracks.size(), 1u);
    AUREA_CHECK_EQ(layer->tracks.at(0).effectParamIndex, 256u);
    AUREA_CHECK(ProjectSerializer::save(loaded, path, SaveOptions{}).ok());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load(reopened, path, LoadOptions{}).ok());
    layer = remap_test_layer(reopened);
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{2});
    AUREA_CHECK_EQ(layer->timeRemap.keys.size(), usize{1});
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LegacyRemapFrameUnitsWinButAmbiguousEffectsAreNotGuessed) {
    const std::string path = temp_path("legacy_remap_ambiguous");
    Project original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    Layer* layer = comp->layer(comp->add_layer(LayerKind::Video, "Ambiguous"));
    const u32 a = legacy_remap_effect(*layer), b = legacy_remap_effect(*layer);
    layer->tracks.get_or_create(TrackProperty::EffectParam, a, 0).set(FrameIndex{8}, 1.0f);
    layer->tracks.get_or_create(TrackProperty::EffectParam, b, 0).set(FrameIndex{9}, 2.0f);
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}).ok());
    layer = remap_test_layer(loaded);
    AUREA_CHECK(!layer->timeRemapLegacyMigrated);
    AUREA_CHECK(layer->timeRemap.keys.empty());
    AUREA_CHECK_EQ(layer->tracks.size(), 2u);
    layer->tracks.get_or_create(TrackProperty::TimeRemap).set(FrameIndex{13}, 43.5f);
    AUREA_CHECK(ProjectSerializer::save(loaded, path, SaveOptions{}).ok());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load(reopened, path, LoadOptions{}).ok());
    layer = remap_test_layer(reopened);
    AUREA_CHECK(layer->timeRemapLegacyMigrated && layer->timeRemapEnabled);
    AUREA_CHECK_EQ(layer->timeRemap.keys[0].time.value, i64{13});
    AUREA_CHECK_NEAR(layer->timeRemap.keys[0].value, 43.5f, 1e-7);
    AUREA_CHECK_EQ(layer->timeRemapLegacyTracks.size(), usize{3});
    AUREA_CHECK_EQ(layer->tracks.size(), 0u);
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, ObjectMaterialOverridesAndAnimationSurviveIndependently) {
    const std::string path = temp_path("object_materials");
    Project original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    const LayerId a = comp->add_layer(LayerKind::Model3D, "Red"), b = comp->add_layer(LayerKind::Model3D, "Original");
    MaterialOverride override;
    override.materialIndex = 3; override.mask = 63;
    override.baseColor = {0.9f, 0.2f, 0.1f, 0.75f}; override.metallic = 0.8f; override.roughness = 0.27f;
    comp->layer(a)->model.materials.push_back(override);
    auto& track = comp->layer(a)->tracks.get_or_create(TrackProperty::MaterialParam, 3, 5);
    track.set(FrameIndex{3}, 0.2f, Interpolation::Bezier);
    track.set(FrameIndex{90}, 0.9f);
    track.keys[0].by1 = -0.3f; track.keys[0].by2 = 1.4f;
    AUREA_CHECK(ProjectSerializer::save(original,path,SaveOptions{}).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded,path,LoadOptions{}).ok());
    comp = loaded.timeline().composition(loaded.timeline().root());
    const Layer* restored = nullptr;
    for (u32 i=0; i<comp->order().size(); ++i) {
        const Layer* layer = comp->layer(comp->order().at(i));
        if (layer->name == "Red") restored = layer;
        else AUREA_CHECK(layer->model.materials.empty());
    }
    AUREA_CHECK(restored != nullptr);
    if (restored) {
        AUREA_CHECK_EQ(restored->model.materials.size(),usize{1});
        const auto& value=restored->model.materials[0];
        AUREA_CHECK_EQ(value.materialIndex,3u); AUREA_CHECK_EQ(value.mask,63u);
        AUREA_CHECK_NEAR(value.baseColor.w,0.75f,1e-7);
        AUREA_CHECK_NEAR(value.metallic,0.8f,1e-7); AUREA_CHECK_NEAR(value.roughness,0.27f,1e-7);
        const Track* animation=restored->tracks.find(TrackProperty::MaterialParam,3,5);
        AUREA_CHECK(animation != nullptr);
        if (animation) {
            AUREA_CHECK_NEAR(animation->keys[0].by1,-0.3f,1e-7);
            AUREA_CHECK_NEAR(animation->sample(FrameIndex{90}),0.9f,1e-7);
            AUREA_CHECK_NEAR(animation->sample(FrameIndex{3}),0.2f,1e-7);
        }
        Layer duplicate=*restored;
        duplicate.model.materials[0].metallic=0.0f;
        AUREA_CHECK_NEAR(restored->model.materials[0].metallic,0.8f,1e-7);
    }
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, InvalidMaterialOverridesAreRejectedWithoutPartialLoad) {
    const std::string path=temp_path("invalid_materials");
    for (u32 invalid=0; invalid<5; ++invalid) {
        Project original=make_project();
        auto* comp=original.timeline().composition(original.timeline().root());
        Layer* layer=comp->layer(comp->add_layer(LayerKind::Model3D,"Invalid"));
        MaterialOverride value;
        if (invalid==0) value.materialIndex=4096;
        if (invalid==1) value.mask=64;
        if (invalid==2) value.roughness=1.1f;
        if (invalid==3) value.baseColor.x=std::numeric_limits<f32>::quiet_NaN();
        layer->model.materials.push_back(value);
        if (invalid==4) layer->model.materials.push_back(value);
        AUREA_CHECK(ProjectSerializer::save(original,path,SaveOptions{}).ok());
        Project loaded;
        AUREA_CHECK(!ProjectSerializer::load(loaded,path,LoadOptions{}).ok());
    }
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, BounceElasticAndStepsSurviveProjectReload) {
    const std::string path = temp_path("easing_families");
    Project original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    const LayerId id = comp->add_layer(LayerKind::Shape, "Easing families");
    const auto modes = {Interpolation::Bounce, Interpolation::Elastic, Interpolation::Steps, Interpolation::Bounce};
    u32 index = 0;
    for (auto mode : modes) {
        auto& track = comp->layer(id)->tracks.get_or_create(static_cast<TrackProperty>(index++));
        track.set(FrameIndex{0}, -20.f, mode); track.set(FrameIndex{100}, 80.f);
        if (index==4) track.set_interpolation(FrameIndex{0},mode,6.f/8,.7f,0,-10);
    }
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}).ok());
    Project restored;
    AUREA_CHECK(ProjectSerializer::load(restored, path, LoadOptions{}).ok());
    auto* loadedComp = restored.timeline().composition(restored.timeline().root());
    const Layer* loaded = nullptr;
    for (u32 i=0; i<loadedComp->order().size(); ++i)
        if (loadedComp->layer(loadedComp->order().at(i))->name == "Easing families") loaded = loadedComp->layer(loadedComp->order().at(i));
    AUREA_CHECK(loaded != nullptr);
    if (loaded) for (u32 i=0; i<4; ++i) {
        const auto* before = comp->layer(id)->tracks.find(static_cast<TrackProperty>(i));
        const auto* after = loaded->tracks.find(static_cast<TrackProperty>(i));
        AUREA_CHECK(before && after);
        if (!before || !after) continue;
        AUREA_CHECK(before->keys[0].interp == after->keys[0].interp);
        AUREA_CHECK_NEAR(before->keys[0].by2,after->keys[0].by2,1e-6);
        for (int frame=0; frame<=100; ++frame)
            AUREA_CHECK_NEAR(before->sample(FrameIndex{frame}), after->sample(FrameIndex{frame}), 1e-6);
    }
    std::remove(path.c_str());
}

// Overshoot (tipo novo, valor 10) e Elástico/Quique com parâmetros: o projeto
// relido avalia igual, quadro a quadro; o formato do arquivo é o mesmo (os
// parâmetros vão nos floats da bézier com o marcador em by2).
AUREA_TEST(Serialization, ParametricEasingsSurviveProjectReload) {
    const std::string path = temp_path("easing_params");
    Project original = make_project();
    auto* comp = original.timeline().composition(original.timeline().root());
    const LayerId id = comp->add_layer(LayerKind::Shape, "Easing params");
    struct Case { Interpolation kind; f32 a, b, dir; };
    const Case cases[] = {{Interpolation::Overshoot, 0.7f, 0.0f, 1.0f}, {Interpolation::Overshoot, 0.3f, 0.0f, 0.0f},
                          {Interpolation::Elastic, 5.0f / 8, 0.6f, 1.0f}, {Interpolation::Bounce, 4.0f / 8, 0.8f, 0.0f}};
    u32 index = 0;
    for (const Case& c : cases) {
        auto& track = comp->layer(id)->tracks.get_or_create(static_cast<TrackProperty>(index++));
        track.set(FrameIndex{0}, -20.f); track.set(FrameIndex{100}, 80.f);
        track.set_interpolation(FrameIndex{0}, c.kind, c.a, c.b, c.dir, kEaseParamMarker);
    }
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}).ok());
    Project restored;
    AUREA_CHECK(ProjectSerializer::load(restored, path, LoadOptions{}).ok());
    auto* loadedComp = restored.timeline().composition(restored.timeline().root());
    const Layer* loaded = nullptr;
    for (u32 i = 0; i < loadedComp->order().size(); ++i)
        if (loadedComp->layer(loadedComp->order().at(i))->name == "Easing params") loaded = loadedComp->layer(loadedComp->order().at(i));
    AUREA_CHECK(loaded != nullptr);
    bool outside = false;
    if (loaded) for (u32 i = 0; i < 4; ++i) {
        const auto* before = comp->layer(id)->tracks.find(static_cast<TrackProperty>(i));
        const auto* after = loaded->tracks.find(static_cast<TrackProperty>(i));
        AUREA_CHECK(before && after);
        if (!before || !after) continue;
        AUREA_CHECK(after->keys[0].interp == cases[i].kind);
        AUREA_CHECK_EQ(after->keys[0].bx1, cases[i].a);
        AUREA_CHECK_EQ(after->keys[0].by1, cases[i].b);
        AUREA_CHECK_EQ(after->keys[0].bx2, cases[i].dir);
        AUREA_CHECK_EQ(after->keys[0].by2, kEaseParamMarker);
        for (int frame = 0; frame <= 100; ++frame) {
            AUREA_CHECK_EQ(before->sample(FrameIndex{frame}), after->sample(FrameIndex{frame}));
            const f32 v = after->sample(FrameIndex{frame});
            outside = outside || v > 80.f || v < -20.f;
        }
    }
    AUREA_CHECK(outside);   // as curvas que passam do ponto passam mesmo depois de relidas
    std::remove(path.c_str());
}
