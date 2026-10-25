// =============================================================================
//  Aurea / tests / Effects3DSheetGpu.inl (incluído por test_gpu.cpp)
//
//  0.3.1:
//   - Aurora e Constelação numa camada 3D são efeitos 3D como o Particular: a
//     câmera não move a camada (ela fica chapada, cobrindo o quadro); o efeito
//     é que vive no mundo e acompanha a câmera.
//   - Modelo 3D com efeitos respeita a profundidade dos outros objetos 3D: o
//     mais perto da câmera aparece, em qualquer ordem de camadas.
// =============================================================================

namespace {

/// Fração de pixels com alfa (quase) cheio.
f32 opaque_fraction(const FloatImage& img) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) n += img.v(x, y).w > 0.99f ? 1u : 0u;
    return static_cast<f32>(n) / static_cast<f32>(std::max(1u, img.width * img.height));
}

LayerId sheet_camera(Scene& s, Vec3 position) {
    const LayerId cam = s.comp->add_layer(LayerKind::Camera, "camera");
    Layer* c = s.comp->layer(cam);
    c->start = FrameIndex{0};
    c->end = s.comp->duration();
    c->transform.anchor = Vec3{0, 0, 0};
    c->transform.position = position;
    c->camera.fov = 50.0f;
    c->camera.active = true;
    return cam;
}

} // namespace

AUREA_TEST(EffectPackGpu, AuroraOnA3DLayerStaysASheetAndTheSkyFollowsTheCamera) {
    // A camada 3D com Aurora sai IGUAL à mesma camada 2D (que a câmera não
    // move e cujo céu segue a câmera). Antes a câmera movia o plano dela.
    AUREA_REQUIRE_GPU();
    Scene s(160, 90);
    const auto id = s.image(uniform_image(160, 90, 0, 0, 0), 80, 45);
    auto& fx = s.add_effect(id, effect_keys::kAurora);
    fx.params[1].constant = ParamValue::scalar(160.0f);
    const LayerId cam = sheet_camera(s, Vec3{80, 45, -140});
    s.comp->layer(cam)->transform.rotation = Vec3{8, 30, 0};
    const FloatImage flat = s.render(FrameIndex{10});
    s.comp->layer(id)->threeD = true;
    const FloatImage sheet = s.render(FrameIndex{10});
    s.comp->layer(cam)->transform.rotation = Vec3{0, 0, 0};
    const FloatImage straight = s.render(FrameIndex{10});
    std::printf("\n    aurora: 3D x 2D %.4f, girar %.4f, cobertura %.3f", max_abs_diff(flat, sheet),
                max_abs_diff(straight, sheet), opaque_fraction(sheet));
    AUREA_CHECK_MSG(max_abs_diff(flat, sheet) < 0.01f, "camada 3D com Aurora = folha chapada (a câmera não move a camada)");
    AUREA_CHECK_MSG(opaque_fraction(sheet) > 0.999f, "a folha cobre o quadro com a câmera girada");
    AUREA_CHECK_MSG(max_abs_diff(straight, sheet) > 0.005f, "girar a câmera gira o céu");
}

AUREA_TEST(EffectPackGpu, ConstellationOnA3DLayerLivesInTheWorldAndTheLayerStays) {
    AUREA_REQUIRE_GPU();
    Scene s(160, 90);
    const auto id = s.solid(160, 90, Vec4{0, 0, 0, 1}, 80, 45);
    s.add_effect(id, effect_keys::kConstellation);
    const LayerId cam = sheet_camera(s, Vec3{80, 45, -140});
    s.comp->layer(cam)->transform.rotation = Vec3{0, 25, 0};
    const FloatImage flat = s.render(FrameIndex{0});
    s.comp->layer(id)->threeD = true;
    const FloatImage sheet = s.render(FrameIndex{0});
    s.comp->layer(cam)->transform.rotation = Vec3{0, 0, 0};
    const FloatImage straight = s.render(FrameIndex{0});
    s.comp->layer(cam)->transform.position = Vec3{120, 45, -140};
    const FloatImage moved = s.render(FrameIndex{0});
    std::printf("\n    constelacao: 3D x 2D %.4f, girar %.4f, andar %.4f, cobertura %.3f", max_abs_diff(flat, sheet),
                max_abs_diff(straight, sheet), max_abs_diff(straight, moved), opaque_fraction(sheet));
    AUREA_CHECK(straight.mean().x + straight.mean().y + straight.mean().z > 0.002f);   // a rede aparece
    AUREA_CHECK_MSG(opaque_fraction(sheet) > 0.999f, "a câmera girada não tira a camada do lugar");
    AUREA_CHECK_MSG(opaque_fraction(moved) > 0.999f, "a câmera andando não tira a camada do lugar");
    AUREA_CHECK_MSG(max_abs_diff(flat, sheet) < 0.05f, "camada 3D com Constelação = a rede no mundo, vista pela câmera");
    AUREA_CHECK_MSG(max_abs_diff(straight, sheet) > 0.01f, "a rede vive no mundo: a câmera girando muda a vista");
    AUREA_CHECK_MSG(max_abs_diff(straight, moved) > 0.01f, "a rede vive no mundo: a câmera andando muda a vista");
}

AUREA_TEST(Gpu, ModelWithEffectsKeepsItsDepthAgainstTheOtherModels) {
    // Dois triângulos 3D sobrepostos, um vermelho (camada de baixo) e um verde
    // (de cima), os dois com Inverter (vermelho -> ciano, verde -> magenta).
    // Ligar o efeito tira o modelo do grupo 3D compartilhado; o mais perto da
    // câmera continua aparecendo, seja qual for a ordem das camadas.
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    ModelImport mi; mi.path = write_triangle_gltf(true, 1, 1, 1);
    const auto bottomId = rig.e.import_model(mi); AUREA_CHECK(bottomId.ok()); if (!bottomId.ok()) return;
    auto* comp = current_comp(rig.e);
    auto* bottom = comp->layer(LayerId::unpack(*bottomId));
    bottom->model.unitScale = 140;
    bottom->transform.position = Vec3{160, 90, 0};
    const auto topLayer = comp->add_layer(LayerKind::Model3D, "de cima");
    auto* top = comp->layer(topLayer);
    top->model = bottom->model; top->transform = bottom->transform;
    top->threeD = true; top->end = comp->duration();
    const u64 topId = topLayer.pack();
    // Vermelho embaixo, verde em cima (cor por instância).
    AUREA_CHECK(rig.e.set_material_param(*bottomId, 0, 1, 0).ok());
    AUREA_CHECK(rig.e.set_material_param(*bottomId, 0, 2, 0).ok());
    AUREA_CHECK(rig.e.set_material_param(topId, 0, 0, 0).ok());
    AUREA_CHECK(rig.e.set_material_param(topId, 0, 2, 0).ok());
    for (const u64 layer : {*bottomId, topId}) {
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(layer);
        add.effect_add.effectType = effect_type_id(effect_keys::kInvert);
        add.effect_add.index = kInvalidIndex;
        AUREA_CHECK(rig.e.submit_commands(&add, 1) == 1);
    }
    (void)rig.capture(64);        // aplica a fila de comandos
    comp = current_comp(rig.e);   // os comandos podem trocar a composição de lugar
    bottom = comp->layer(LayerId::unpack(*bottomId));
    top = comp->layer(topLayer);
    std::printf("\n    efeitos: baixo %zu, cima %zu", bottom ? bottom->effects.size() : 0u, top ? top->effects.size() : 0u);
    AUREA_CHECK(!bottom->effects.empty() && !top->effects.empty());
    if (bottom->effects.empty() || top->effects.empty()) return;
    auto center = [&](const char* what) {
        const Image8 img = rig.capture(320);
        const usize o = (static_cast<usize>(img.height / 2) * img.width + img.width / 2) * 4;
        std::printf("\n    %s: centro %u %u %u", what, img.rgba[o], img.rgba[o + 1], img.rgba[o + 2]);
        return std::array<u8, 3>{img.rgba[o], img.rgba[o + 1], img.rgba[o + 2]};
    };
    // 1) Efeito no de CIMA (magenta), que está ATRÁS: aparece o vermelho da frente.
    bottom->effects[0].enabled = false;
    top->effects[0].enabled = true;
    top->transform.position.z = 300;
    const auto behind = center("magenta atras");
    AUREA_CHECK_MSG(behind[0] > 200 && behind[2] < 40, "o modelo com efeito atrás fica atrás");
    // 2) Efeito no de BAIXO (ciano), que está NA FRENTE: aparece o ciano.
    bottom->effects[0].enabled = true;
    top->effects[0].enabled = false;
    top->transform.position.z = 0;
    bottom->transform.position.z = -120;   // a câmera padrão fica a ~247 px
    const auto front = center("ciano na frente");
    AUREA_CHECK_MSG(front[2] > 200 && front[0] < 40, "o modelo com efeito na frente fica na frente");
    // 3) Sem efeito nenhum (grupo compartilhado de sempre): o vermelho da frente.
    bottom->effects[0].enabled = false;
    const auto plain = center("sem efeito");
    AUREA_CHECK(plain[0] > 200 && plain[1] < 40);
}
