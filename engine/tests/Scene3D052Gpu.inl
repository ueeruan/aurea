// 0.5.2: a luz AMBIENTE (a "Ambiente" do AE) também ilumina os modelos PBR.
// Antes ela só acendia as camadas 2D com "Aceita luzes"; com só uma ambiente
// na cena o modelo ainda recebia a luz-chave padrão. Agora: com a ambiente
// sai a luz-chave; a ambiente clareia o difuso por igual e tinge com a cor
// dela. Cena sem luz ambiente: igual a antes (as referências 3D conferem).
// Também o chão molhado. Incluído depois de AmbientOcclusionGpu.inl (usa ao_test::mean_luma).
namespace ambient_light_test {

struct Shot {
    Image8 img;
    f64 luma = 0, red = 0, green = 0;
};

/// mode: 0 sem luz nenhuma (luz-chave padrão), 1 ambiente com força 0,
/// 2 ambiente branca com força 1, 3 ambiente vermelha com força 1.
Shot cube_with_ambient(int mode) {
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.set_scene3d_tonemap(0, 1.f));   // curva neutra: o limiar é medido nela
    const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(cube.ok());
    if (!cube.ok()) return {};
    Composition* comp = current_comp(rig.e);
    Layer* c = comp->layer(LayerId::unpack(*cube));
    c->transform.position = Vec3{160, 90, 60};
    c->transform.scale = Vec3{1.4f, 1.4f, 1.4f};
    c->transform.rotation = Vec3{0, 0, 0};          // face da frente para a câmera
    if (mode > 0) {
        const Result<u64> amb = rig.e.add_light(3);
        AUREA_CHECK(amb.ok());
        if (!amb.ok()) return {};
        Layer* l = current_comp(rig.e)->layer(LayerId::unpack(*amb));
        AUREA_CHECK(l && l->light.kind == LightKind::Ambient);
        if (!l) return {};
        const f32 intensity = mode == 1 ? 0.0f : 1.0f;
        const Vec3 color = mode == 3 ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{1.0f, 1.0f, 1.0f};
        l->light.intensity = intensity;
        l->light.color = Vec4{color.x, color.y, color.z, 1.0f};
        if (Track* t = l->tracks.find(TrackProperty::LightIntensity)) t->staticValue = intensity;
        if (Track* t = l->tracks.find(TrackProperty::LightColorR)) t->staticValue = color.x;
        if (Track* t = l->tracks.find(TrackProperty::LightColorG)) t->staticValue = color.y;
        if (Track* t = l->tracks.find(TrackProperty::LightColorB)) t->staticValue = color.z;
    }
    seek_frame(rig.e, 0);
    Shot s;
    s.img = rig.capture(320);
    (void)write_png(std::string("tests/build/luz_ambiente_") + std::to_string(mode) + ".png", s.img);
    if (s.img.width != 320) return s;
    // A face da frente do cubo (centro da imagem).
    s.luma = ao_test::mean_luma(s.img, 140, 70, 180, 110);
    u32 n = 0;
    for (u32 y = 70; y < 110; ++y)
        for (u32 x = 140; x < 180; ++x) {
            const u8* p = s.img.at(x, y);
            s.red += p[0];
            s.green += p[1];
            ++n;
        }
    if (n) { s.red /= n; s.green /= n; }
    return s;
}

} // namespace ambient_light_test

AUREA_TEST(Scene3DAmbientLightGpu, AmbientLightLightsModelsAndReplacesTheDefaultKeyLight) {
    AUREA_REQUIRE_GPU();
    using namespace ambient_light_test;
    const Shot none = cube_with_ambient(0), dark = cube_with_ambient(1), white = cube_with_ambient(2), red = cube_with_ambient(3);
    AUREA_CHECK(none.img.width == 320 && dark.img.width == 320 && white.img.width == 320 && red.img.width == 320);
    if (none.img.width != 320 || dark.img.width != 320 || white.img.width != 320 || red.img.width != 320) return;
    std::printf("\n    face da frente: sem luz %.1f | ambiente 0 %.1f | ambiente branca %.1f | vermelha R %.1f G %.1f (ambiente 0: R %.1f G %.1f)",
                none.luma, dark.luma, white.luma, red.red, red.green, dark.red, dark.green);
    // Uma ambiente na cena tira a luz-chave padrão (força 0 = só o ambiente do grupo).
    AUREA_CHECK_MSG(dark.luma < none.luma - 5.0, "com uma luz ambiente a luz-chave padrao sai");
    // A ambiente acende o modelo...
    AUREA_CHECK_MSG(white.luma > dark.luma + 15.0, "a luz ambiente ilumina o modelo PBR");
    // ...e tinge com a cor dela: o vermelho sobe e passa bem à frente do verde
    // (o tone map neutro comprime o canal claro, então o verde não fica parado).
    AUREA_CHECK_MSG(red.red - dark.red > 15.0, "a ambiente vermelha avermelha o modelo");
    AUREA_CHECK_MSG((red.red - red.green) > (dark.red - dark.green) + 20.0, "a ambiente vermelha puxa o modelo para o vermelho");
}

// 0.5.2 chão molhado: as poças mudam o chão (mais escuro, espelhando); voltar
// a 0 devolve exatamente o chão seco (o shader soma zero).
AUREA_TEST(Scene3DWetFloorGpu, PuddlesChangeTheFloorAndDryIsUnchanged) {
    AUREA_REQUIRE_GPU();
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.set_scene3d_tonemap(0, 1.f));
    AUREA_CHECK(rig.e.set_scene_floor(1, .5f, .5f, .5f, .5f, .3f));
    const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    const Result<u64> sun = rig.e.add_light(0);
    AUREA_CHECK(cube.ok() && sun.ok());
    if (!cube.ok() || !sun.ok()) return;
    seek_frame(rig.e, 0);
    const Image8 dry = rig.capture(320);
    AUREA_CHECK(rig.e.set_scene_floor_wetness(0.7f, 120.0f));
    const Image8 wet = rig.capture(320);
    AUREA_CHECK(rig.e.set_scene_floor_wetness(0.0f, 120.0f));
    const Image8 dryAgain = rig.capture(320);
    (void)write_png("tests/build/chao_seco.png", dry);
    (void)write_png("tests/build/chao_molhado.png", wet);
    AUREA_CHECK(dry.width == 320 && wet.width == 320 && dryAgain.width == 320);
    if (dry.width != 320 || wet.width != 320 || dryAgain.width != 320) return;
    // A metade de baixo (o chão diante da câmera).
    f64 wetDiff = 0.0;
    u32 dryDiff = 0, n = 0;
    for (u32 y = dry.height / 2; y < dry.height; ++y)
        for (u32 x = 0; x < dry.width; ++x) {
            const u8 *a = dry.at(x, y), *b = wet.at(x, y), *c = dryAgain.at(x, y);
            for (u32 k = 0; k < 3; ++k) {
                wetDiff += std::abs(static_cast<int>(a[k]) - b[k]);
                dryDiff = std::max<u32>(dryDiff, static_cast<u32>(std::abs(static_cast<int>(a[k]) - c[k])));
            }
            ++n;
        }
    wetDiff /= std::max(1u, n * 3);
    std::printf("\n    chao: diferenca media molhado %.2f, seco de novo max %u", wetDiff, dryDiff);
    AUREA_CHECK_MSG(wetDiff > 2.0, "as pocas mudam o chao");
    AUREA_CHECK_MSG(dryDiff <= 1, "sem pocas o chao volta a ser o seco");
}
