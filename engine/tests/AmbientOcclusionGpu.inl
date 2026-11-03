// 0.4.6 ("realismo nível Blender"): oclusão ambiente de tela (GTAO). Dois
// cubos encostados: com a AO, o contato escurece a luz ambiente; sem os
// cubos se tocarem, a imagem quase não muda; o fundo nunca escurece.
#include "aurea/scene3d/Shape3D.hpp"
namespace ao_test {

f64 mean_luma(const Image8& img, u32 x0, u32 y0, u32 x1, u32 y1) {
    f64 sum = 0.0;
    u32 n = 0;
    for (u32 y = y0; y < y1 && y < img.height; ++y)
        for (u32 x = x0; x < x1 && x < img.width; ++x) {
            const u8* p = img.at(x, y);
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    return n ? sum / n : 0.0;
}

Image8 two_cubes(bool ao, bool touching) {
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    rig.e.renderer().scene_renderer().set_ambient_occlusion(ao);
    const Result<u64> big = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Grande");
    const Result<u64> small = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Pequeno");
    AUREA_CHECK(big.ok() && small.ok());
    if (!big.ok() || !small.ok()) return {};
    Composition* comp = current_comp(rig.e);
    Layer* b = comp->layer(LayerId::unpack(*big));
    Layer* s = comp->layer(LayerId::unpack(*small));
    b->transform.position = Vec3{160, 90, 60};
    b->transform.scale = Vec3{1.4f, 1.4f, 1.4f};   // ~113 px (o cubo nasce com 45% do lado menor)
    b->transform.rotation = Vec3{0, 0, 0};          // face da frente para a câmera (z ≈ 3)
    // Encravado na face do grande (vinco em volta) ou bem na frente dele.
    s->transform.position = touching ? Vec3{160, 90, -8} : Vec3{160, 90, -260};
    s->transform.scale = Vec3{0.55f, 0.55f, 0.55f};
    s->transform.rotation = Vec3{30, 45, 0};
    seek_frame(rig.e, 0);
    Image8 img = rig.capture(320);
    (void)write_png(std::string("tests/build/ao_") + (ao ? "on" : "off") + (touching ? "_junto" : "_longe") + ".png", img);
    std::printf("\n    [ao=%d] gtao rodou: %d", ao ? 1 : 0, rig.e.renderer().scene_renderer().stats().ambientOcclusion ? 1 : 0);
    return img;
}

} // namespace ao_test

AUREA_TEST(Scene3DAmbientOcclusionGpu, ContactDarkensAmbientLightOnly) {
    AUREA_REQUIRE_GPU();
    using namespace ao_test;
    const Image8 off = two_cubes(false, true), on = two_cubes(true, true);
    AUREA_CHECK(off.width == 320 && on.width == 320);
    if (off.width != 320 || on.width != 320) return;
    // Em volta do contato (a face do grande perto do pequeno).
    const f64 lOff = mean_luma(off, 100, 30, 220, 150), lOn = mean_luma(on, 100, 30, 220, 150);
    std::printf("\n    luma media: sem AO %.2f, com AO %.2f", lOff, lOn);
    AUREA_CHECK_MSG(lOn < lOff - 0.3, "a AO escurece o contato entre os cubos (320x180: raio de poucos px)");
    AUREA_CHECK_MSG(lOn > lOff * 0.7, "a AO nao apaga a cena (so a luz ambiente)");
    // Cubos separados: a AO quase não muda nada (sem halo no fundo).
    const Image8 apartOff = two_cubes(false, false), apartOn = two_cubes(true, false);
    const f64 aOff = mean_luma(apartOff, 100, 30, 220, 150), aOn = mean_luma(apartOn, 100, 30, 220, 150);
    std::printf("\n    separados: sem AO %.2f, com AO %.2f", aOff, aOn);
    AUREA_CHECK_MSG(aOn > aOff - (lOff - lOn) * 0.8, "sem contato, bem menos escurecimento");
    // O canto da imagem (fundo) é igual com e sem AO.
    AUREA_CHECK(std::fabs(mean_luma(off, 0, 0, 16, 16) - mean_luma(on, 0, 0, 16, 16)) < 0.5);
}

// 0.4.6: neblina volumétrica com raios de luz — liga, o passe roda, a cena
// clareia na direção da cor da neblina; desligada, nada muda.
AUREA_TEST(Scene3DAmbientOcclusionGpu, VolumetricFogVeilsTheSceneWhenOn) {
    AUREA_REQUIRE_GPU();
    using namespace ao_test;
    auto shot = [](bool fog, bool& ran) {
        using aurea::scene3d::Shape3DKind;
        Scene3DRig rig(320, 180);
        const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
        AUREA_CHECK(cube.ok());
        (void)rig.e.add_light(0);
        if (fog) AUREA_CHECK(rig.e.set_scene3d_fog(true, 0.004f, 1.0f, 0.8f, 0.8f, 0.85f));
        seek_frame(rig.e, 0);
        Image8 img = rig.capture(320);
        ran = rig.e.renderer().scene_renderer().stats().volumetricFog;
        return img;
    };
    bool ranOff = true, ranOn = false;
    const Image8 off = shot(false, ranOff), on = shot(true, ranOn);
    AUREA_CHECK(!ranOff && ranOn);
    const f64 lOff = mean_luma(off, 140, 70, 180, 110), lOn = mean_luma(on, 140, 70, 180, 110);
    std::printf("\n    centro: sem neblina %.2f, com neblina %.2f", lOff, lOn);
    AUREA_CHECK_MSG(std::fabs(lOn - lOff) > 2.0, "a neblina muda o objeto visto atraves dela");
}

// 0.4.6: sombra das luzes LOCAIS (atlas de faces). Luz pontual à esquerda do
// cubo pequeno, parede atrás: com "projeta sombra", a parede à direita do
// cubo escurece; sem, não.
AUREA_TEST(Scene3DAmbientOcclusionGpu, PointLightCastsShadowOnTheWall) {
    AUREA_REQUIRE_GPU();
    using namespace ao_test;
    auto shot = [](bool cast, Vec3 lightPos = Vec3{40, 90, -40}, const char* tag = "", bool sideWall = false) {
        using aurea::scene3d::Shape3DKind;
        Scene3DRig rig(320, 180);
        const Result<u64> wall = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Parede");
        const Result<u64> box = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Caixa");
        const Result<u64> light = rig.e.add_light(1);
        AUREA_CHECK(wall.ok() && box.ok() && light.ok());
        if (!wall.ok() || !box.ok() || !light.ok()) return Image8{};
        Composition* comp = current_comp(rig.e);
        Layer* w = comp->layer(LayerId::unpack(*wall));
        Layer* b = comp->layer(LayerId::unpack(*box));
        Layer* l = comp->layer(LayerId::unpack(*light));
        w->transform.position = sideWall ? Vec3{280, 90, 100} : Vec3{160, 90, 200};
        w->transform.scale = sideWall ? Vec3{0.2f, 4.0f, 6.0f} : Vec3{6.0f, 4.0f, 0.2f};
        w->transform.rotation = Vec3{0, 0, 0};
        b->transform.position = Vec3{160, 90, 60};
        b->transform.scale = Vec3{0.5f, 0.5f, 0.5f};
        b->transform.rotation = Vec3{0, 0, 0};
        l->transform.position = lightPos;
        l->light.castShadows = cast;
        l->light.intensity = 4.0f;
        seek_frame(rig.e, 0);
        Image8 img = rig.capture(320);
        (void)write_png(std::string("tests/build/sombra_local_") + tag + (cast ? "on" : "off") + ".png", img);
        return img;
    };
    const Image8 off = shot(false), on = shot(true);
    AUREA_CHECK(off.width == 320 && on.width == 320);
    if (off.width != 320 || on.width != 320) return;
    // A sombra cai à direita do cubo na parede.
    const f64 sOff = mean_luma(off, 200, 70, 260, 110), sOn = mean_luma(on, 200, 70, 260, 110);
    // O lado esquerdo da parede (iluminado) não muda.
    const f64 lOff = mean_luma(off, 20, 70, 60, 110), lOn = mean_luma(on, 20, 70, 60, 110);
    std::printf("\n    sombra: sem %.2f, com %.2f | lado aceso: sem %.2f, com %.2f", sOff, sOn, lOff, lOn);
    AUREA_CHECK_MSG(sOn < sOff - 3.0, "a luz pontual projeta a sombra do cubo na parede");
    AUREA_CHECK_MSG(std::fabs(lOn - lOff) < 2.0, "fora da sombra, nada muda (sem acne)");
}

// 0.4.6: reflexos de tela (SSR). Piso metálico liso sob um cubo vermelho:
// com SSR, o piso logo abaixo do cubo fica mais vermelho (o reflexo); o piso
// áspero não liga o passe.
AUREA_TEST(Scene3DAmbientOcclusionGpu, ScreenReflectionsMirrorTheCubeOnASmoothFloor) {
    AUREA_REQUIRE_GPU();
    using namespace ao_test;
    auto shot = [](bool ssr, f32 floorRough, bool& ran) {
        using aurea::scene3d::Shape3DKind;
        Scene3DRig rig(320, 180);
        rig.e.renderer().scene_renderer().set_screen_reflections(ssr);
        rig.e.renderer().scene_renderer().set_ambient_occlusion(false);
        const Result<u64> floor = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Piso");
        const Result<u64> box = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
        AUREA_CHECK(floor.ok() && box.ok());
        if (!floor.ok() || !box.ok()) return Image8{};
        aurea::scene3d::Shape3DSpec fs{}, bs{};
        AUREA_CHECK(rig.e.query_shape3d(*floor, fs) && rig.e.query_shape3d(*box, bs));
        fs.metallic = 1.0f; fs.roughness = floorRough;
        for (auto& part : fs.parts) part.color = Vec4{0.8f, 0.8f, 0.8f, 1.0f};
        bs.metallic = 0.0f; bs.roughness = 0.9f;
        for (auto& part : bs.parts) part.color = Vec4{1.0f, 0.05f, 0.05f, 1.0f};
        AUREA_CHECK(rig.e.set_shape3d(*floor, fs).ok() && rig.e.set_shape3d(*box, bs).ok());
        Composition* comp = current_comp(rig.e);
        Layer* f = comp->layer(LayerId::unpack(*floor));
        Layer* b = comp->layer(LayerId::unpack(*box));
        f->transform.position = Vec3{160, 150, 120};
        f->transform.scale = Vec3{8.0f, 0.1f, 8.0f};
        f->transform.rotation = Vec3{0, 0, 0};
        b->transform.position = Vec3{160, 110, 120};
        b->transform.scale = Vec3{0.6f, 0.6f, 0.6f};
        b->transform.rotation = Vec3{0, 0, 0};
        seek_frame(rig.e, 0);
        Image8 img = rig.capture(320);
        ran = rig.e.renderer().scene_renderer().stats().screenReflections;
        (void)write_png(std::string("tests/build/ssr_") + (ssr ? "on" : "off") + (floorRough < 0.5f ? "_liso" : "_aspero") + ".png", img);
        return img;
    };
    bool ranOff = true, ranOn = false, ranRough = true;
    const Image8 off = shot(false, 0.05f, ranOff), on = shot(true, 0.05f, ranOn);
    (void)shot(true, 0.9f, ranRough);
    AUREA_CHECK(!ranOff && ranOn && !ranRough);
    AUREA_CHECK(off.width == 320 && on.width == 320);
    if (off.width != 320 || on.width != 320) return;
    // Vermelho relativo no piso logo abaixo do cubo.
    auto redness = [](const Image8& img, u32 x0, u32 y0, u32 x1, u32 y1) {
        f64 r = 0, g = 0;
        for (u32 y = y0; y < y1; ++y)
            for (u32 x = x0; x < x1; ++x) { r += img.at(x, y)[0]; g += img.at(x, y)[1]; }
        return (r - g) / static_cast<f64>((x1 - x0) * (y1 - y0));
    };
    const f64 rOff = redness(off, 140, 150, 180, 175), rOn = redness(on, 140, 150, 180, 175);
    std::printf("\n    vermelho no piso: sem SSR %.2f, com SSR %.2f", rOff, rOn);
    AUREA_CHECK_MSG(rOn > rOff + 4.0, "o piso liso reflete o cubo vermelho");
}

