// =============================================================================
//  0.5.2: preenchimento com TEXTURA do texto (TextData::texture, timeline v69)
//  e o efeito Textura (aurea.stylize.texture_overlay). Arquivo, projeto
//  antigo, API do motor e catálogo. Incluído no fim de test_serialization.cpp.
// =============================================================================
#include "aurea/text/TextTexture.hpp"
#include "aurea/effects/EffectRegistry.hpp"

namespace {
TextTexture every_field_texture(u64 image) {
    TextTexture t;
    t.kind = kTextTextureImage;
    t.mapping = 1;
    t.stroke = true;
    t.scale = 2.5f;
    t.rotation = -33.0f;
    t.offset = Vec2{0.25f, -1.5f};
    t.contrast = 0.4f;
    t.brightness = -0.2f;
    t.tint = Vec4{0.9f, 0.4f, 0.1f, 0.75f};
    t.speed = 2.0f;
    t.relief = 0.8f;
    t.lightAngle = 45.0f;
    t.seed = 77;
    t.image = image;
    return t;
}
} // namespace

AUREA_TEST(TextTexture, Version69RoundTripKeepsEveryFieldAndPlainTextStaysFlat) {
    AUREA_CHECK(kTimelineSectionVersion >= 69u);
    auto original = make_project();
    auto* root = original.timeline().composition(original.timeline().root());
    Asset a; a.kind = AssetKind::Image; a.sourcePath = "textura.png"; a.video.width = 8; a.video.height = 4;
    const AssetId image = original.add_asset(std::move(a));
    const auto plainId = root->add_layer(LayerKind::Text, "liso");
    const auto goldId = root->add_layer(LayerKind::Text, "ouro");
    const auto imageId = root->add_layer(LayerKind::Text, "imagem");
    root->layer(goldId)->text.texture.kind = static_cast<u8>(ProceduralTexture::Gold);
    root->layer(goldId)->text.texture.relief = 0.65f;
    root->layer(imageId)->text.texture = every_field_texture(image.pack());
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored; LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    AUREA_CHECK(!report.partial);
    const auto* r = restored.timeline().composition(restored.timeline().root());
    AUREA_CHECK(r->layer(plainId)->text.texture == TextTexture{});
    AUREA_CHECK_EQ(r->layer(goldId)->text.texture.kind, static_cast<u8>(ProceduralTexture::Gold));
    AUREA_CHECK_NEAR(r->layer(goldId)->text.texture.relief, 0.65f, 1e-6f);
    AUREA_CHECK(r->layer(imageId)->text.texture == every_field_texture(image.pack()));
    // A imagem do preenchimento conta como usada (não some na limpeza do salvar).
    const auto unused = restored.unreferenced_assets();
    AUREA_CHECK(std::find(unused.begin(), unused.end(), image) == unused.end());
    // Gravar de novo dá os mesmos bytes (nada se perde nem muda na volta).
    std::vector<u8> again;
    AUREA_CHECK(ProjectSerializer::encode(restored, SaveOptions{}, again).ok());
    AUREA_CHECK(again == bytes);
}

AUREA_TEST(TextTexture, OldProjectsOpenWithFlatTextAndSaveTheSameWay) {
    Project restored; LoadReport report;
    const auto path = std::string(AUREA_TEST_DATA_DIR) + "/manual-editing/manual-editing.aurea";
    AUREA_CHECK(ProjectSerializer::load(restored, path, LoadOptions{}, &report).ok());
    AUREA_CHECK(report.timelineVersion > 0 && report.timelineVersion < 69u);
    u32 texts = 0;
    auto all_flat = [&](const Project& p) {
        bool flat = true;
        p.timeline().for_each_composition([&](CompositionId, const Composition& comp) {
            comp.layers().for_each([&](LayerId, const Layer& layer) {
                if (layer.kind == LayerKind::Text) ++texts;
                flat &= layer.text.texture == TextTexture{};
            });
        });
        return flat;
    };
    AUREA_CHECK(all_flat(restored));
    // Salvo na v69 e aberto de novo: continua cor lisa, mesmos bytes.
    std::vector<u8> bytes, again;
    AUREA_CHECK(ProjectSerializer::encode(restored, SaveOptions{}, bytes).ok());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load_bytes(reopened, bytes.data(), bytes.size(), LoadOptions{}, nullptr).ok());
    AUREA_CHECK(all_flat(reopened));
    AUREA_CHECK(ProjectSerializer::encode(reopened, SaveOptions{}, again).ok());
    AUREA_CHECK(again == bytes);
    std::printf("camadas de texto no projeto antigo: %u; ", texts / 2);
}

AUREA_TEST(TextTexture, SanitizeRepairsHandEditedValues) {
    TextTexture t;
    t.kind = 200;
    t.scale = std::numeric_limits<f32>::quiet_NaN();
    t.relief = 7.0f;
    t.tint = Vec4{2.0f, -1.0f, 0.5f, std::numeric_limits<f32>::infinity()};
    t.seed = 123456;
    t.image = 99;
    const TextTexture s = sanitized(t);
    AUREA_CHECK_EQ(s.kind, u8{0});
    AUREA_CHECK_EQ(s.scale, 1.0f);
    AUREA_CHECK_EQ(s.relief, 1.0f);
    AUREA_CHECK_EQ(s.tint.x, 1.0f);
    AUREA_CHECK_EQ(s.tint.y, 0.0f);
    AUREA_CHECK_EQ(s.tint.w, 0.0f);
    AUREA_CHECK_EQ(s.seed, 9999u);
    AUREA_CHECK_EQ(s.image, u64{0});   // sem tipo imagem, sem asset
    AUREA_CHECK(!s.active());
    TextTexture img; img.kind = kTextTextureImage;
    AUREA_CHECK(!img.active());        // imagem sem asset: cor lisa
    img.image = 5;
    AUREA_CHECK(img.active());
    AUREA_CHECK_EQ(text_texture_phase(img, 2.5), 2.5f);
    img.speed = 0.0f;
    AUREA_CHECK_EQ(text_texture_phase(img, 2.5), 0.0f);
}

AUREA_TEST(TextTexture, EngineApiSetsQueriesUndoesAndRefusesLockedOrNonText) {
    Engine e;
    EngineConfig ec; ec.workerCount = 1; ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok() && e.new_project(640, 360, 30.0, nullptr).ok());
    const auto added = e.add_text("OURO");
    if (!added.ok()) { std::printf("(sem fonte no host: pulado) "); e.shutdown(); return; }
    const u64 id = *added;
    f32 v[kTextTextureValues + 1]{};
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues + 1));
    AUREA_CHECK_EQ(v[0], 0.0f);
    AUREA_CHECK_EQ(v[kTextTextureValues], 0.0f);
    const f32 want[kTextTextureValues] = {5, 1, 1, 1.5f, 30, 0.5f, -0.25f, 0.2f, -0.1f, 1, 0.8f, 0.6f, 0.5f, 0, 0.7f, 90, 12};
    AUREA_CHECK(e.set_text_texture(id, want, kTextTextureValues));
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues));
    for (u32 i = 0; i < kTextTextureValues; ++i) AUREA_CHECK_NEAR(v[i], want[i], 1e-5f);
    // Fora da faixa: preso, não recusado; NaN: recusado.
    f32 wild[kTextTextureValues];
    std::copy(want, want + kTextTextureValues, wild);
    wild[3] = 500.0f; wild[14] = 3.0f;
    AUREA_CHECK(e.set_text_texture(id, wild, kTextTextureValues));
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues));
    AUREA_CHECK_EQ(v[3], 20.0f);
    AUREA_CHECK_EQ(v[14], 1.0f);
    wild[4] = std::numeric_limits<f32>::quiet_NaN();
    AUREA_CHECK(!e.set_text_texture(id, wild, kTextTextureValues));
    AUREA_CHECK(!e.set_text_texture(id, want, kTextTextureValues - 1));
    // Desfazer volta à textura anterior e depois à cor lisa.
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues));
    AUREA_CHECK_NEAR(v[3], 1.5f, 1e-5f);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues));
    AUREA_CHECK_EQ(v[0], 0.0f);
    // Camada que não é texto e camada travada recusam.
    const auto nullLayer = e.add_null(false);
    AUREA_CHECK(nullLayer.ok());
    if (nullLayer.ok()) AUREA_CHECK(!e.set_text_texture(*nullLayer, want, kTextTextureValues));
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->layer(LayerId::unpack(id))->locked = true;
    AUREA_CHECK(!e.set_text_texture(id, want, kTextTextureValues));
    // Imagem ilegível: recusa sem mexer no texto.
    comp->layer(LayerId::unpack(id))->locked = false;
    AUREA_CHECK(!e.set_text_texture_image(id, "nao_existe_textura.png").ok());
    AUREA_CHECK(e.query_text_texture(id, v, kTextTextureValues));
    AUREA_CHECK_EQ(v[0], 0.0f);
    e.shutdown();
}

AUREA_TEST(TextTexture, OverlayEffectIsInTheCatalogWithAStableParameterContract) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const EffectTypeId type = reg.find_key("aurea.stylize.texture_overlay");
    AUREA_CHECK(type != EffectTypeId{});
    const Effect* fx = reg.find(type);
    const ParameterRegistry* params = reg.params(type);
    AUREA_CHECK(fx && params);
    if (!fx || !params) return;
    AUREA_CHECK(std::string(fx->info().name) == "Textura");
    const char* const ids[] = {"texture", "blend_mode", "opacity", "scale", "rotation", "speed", "inside_alpha",
                               "relief", "contrast", "seed", "texture_layer"};
    AUREA_CHECK_EQ(params->count(), 11u);
    for (u32 i = 0; i < 11 && i < params->count(); ++i) AUREA_CHECK_MSG(std::string(params->at(i).id) == ids[i], ids[i]);
    AUREA_CHECK_EQ(params->at(0).enumCount, 15u);
    AUREA_CHECK_EQ(params->at(1).enumCount, 6u);
    AUREA_CHECK_EQ(fx->input_layer_param(), 10);
    // A chave antiga dos efeitos de textura que já existiam continua lá (não duplicamos).
    AUREA_CHECK(reg.find_key("aurea.stylize.texturize") != EffectTypeId{});
    AUREA_CHECK(reg.find_key(effect_keys::kGrain) != EffectTypeId{});
    AUREA_CHECK(reg.find_key(effect_keys::kHalftone) != EffectTypeId{});
}
