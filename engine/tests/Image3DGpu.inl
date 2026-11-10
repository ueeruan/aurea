// PNG para 3D: o objeto nasce da imagem, muda com a receita, volta no desfazer
// e reabre igual, porque o projeto guarda a receita e não a malha.
#include "aurea/scene3d/Image3D.hpp"
namespace image3d_test {

/// Disco vermelho com furo, sobre fundo transparente.
Image8 logo(bool empty = false) {
    Image8 img;
    img.width = img.height = 512;
    img.rgba.assign(512u * 512u * 4u, 0);
    if (empty) return img;
    for (u32 y = 0; y < 512; ++y) for (u32 x = 0; x < 512; ++x) {
        const f32 dx = static_cast<f32>(x) - 256.f, dy = static_cast<f32>(y) - 256.f, r = std::sqrt(dx * dx + dy * dy);
        u8* p = &img.rgba[(static_cast<usize>(y) * 512u + x) * 4];
        p[0] = 230; p[1] = 30; p[2] = 40; p[3] = r < 220.f && r > 80.f ? 255 : 0;
    }
    return img;
}

u32 strong_red(const Image8& img) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) {
        const u8* p = img.at(x, y);
        n += p[0] > 120 && p[1] < 90 && p[2] < 90;
    }
    return n;
}

bool undo(Engine& e) {
    Command u;
    u.type = CommandType::Undo;
    return e.apply_command(u).ok();
}

} // namespace image3d_test

AUREA_TEST(Image3DGpu, PngBecomesObjectThatSurvivesEditUndoAndReopen) {
    AUREA_REQUIRE_GPU();
    using namespace image3d_test;
    const std::string base = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".");
    const std::string png = base + "/aurea_image3d_logo.png", none = base + "/aurea_image3d_vazia.png", project = base + "/aurea_image3d.aurea";
    AUREA_CHECK(write_png(png, logo()));
    AUREA_CHECK(write_png(none, logo(true)));
    Image8 created;
    {
        Scene3DRig rig(640, 360);
        scene3d::Image3DSpec spec;
        spec.imagePath = png;
        spec.depth = .25f;
        scene3d::Image3DSpec transparent;
        transparent.imagePath = none;
        const Result<u64> refused = rig.e.add_image3d(transparent, "Vazia");
        AUREA_CHECK(!refused.ok());
        if (!refused.ok()) AUREA_CHECK(refused.status().code() == Errc::OutOfRange);
        const Result<u64> id = rig.e.add_image3d(spec, "Logo");
        AUREA_CHECK(id.ok());
        if (!id.ok()) return;
        seek_frame(rig.e, 0);
        created = rig.capture(640);
        (void)write_png("tests/build/image3d_criada.png", created);
        const u32 red = strong_red(created);
        std::printf("\n    vermelho=%u", red);
        AUREA_CHECK(red > 3000u);
        scene3d::Image3DSpec now;
        AUREA_CHECK(rig.e.query_image3d(*id, now));
        // A lateral nasce na cor média da imagem, não num cinza qualquer.
        AUREA_CHECK(now.side.color.x > now.side.color.y * 3.f);
        scene3d::Text3DSpec text;
        AUREA_CHECK(!rig.e.query_text3d(*id, text));

        scene3d::Image3DSpec deeper = now;
        deeper.depth = .7f;
        AUREA_CHECK(rig.e.set_image3d(*id, deeper).ok());
        const Image8 deep = rig.capture(640);
        (void)write_png("tests/build/image3d_funda.png", deep);
        AUREA_CHECK(max_diff(created, deep) > 20);
        AUREA_CHECK(undo(rig.e));
        const Image8 back = rig.capture(640);
        AUREA_CHECK(max_diff(created, back) <= 3);
        AUREA_CHECK(rig.e.apply_image3d_preset(*id, 1));
        const Image8 gold = rig.capture(640);
        (void)write_png("tests/build/image3d_ouro.png", gold);
        AUREA_CHECK(max_diff(created, gold) > 20);
        AUREA_CHECK(undo(rig.e));
        AUREA_CHECK(rig.e.save_project(project.c_str()).ok());
    }
    {
        Scene3DRig rig(640, 360);
        AUREA_CHECK(rig.e.load_project(project.c_str()).ok());
        AUREA_CHECK_EQ(rig.e.last_load_missing_assets(), 0u);
        seek_frame(rig.e, 0);
        const Image8 reopened = rig.capture(640);
        (void)write_png("tests/build/image3d_reaberta.png", reopened);
        std::printf("\n    diferenca ao reabrir=%d", static_cast<int>(max_diff(created, reopened)));
        AUREA_CHECK(max_diff(created, reopened) <= 3);
    }
}

AUREA_TEST(Image3DGpu, ConvertingAPngLayerKeepsItsPlaceAndDropsThePlate) {
    AUREA_REQUIRE_GPU();
    using namespace image3d_test;
    const std::string png = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_image3d_convert.png";
    AUREA_CHECK(write_png(png, logo()));
    Scene3DRig rig(640, 360);
    const std::vector<u8> pixels(16u * 16u * 4u, 200);
    const Result<u64> image = rig.e.import_image(pixels.data(), 16, 16, "Logo.png", "test://logo");
    AUREA_CHECK(image.ok());
    if (!image.ok()) return;
    Composition* comp = current_comp(rig.e);
    Layer* src = comp->layer(LayerId::unpack(*image));
    src->transform.anchor = Vec3{0, 0, 0};
    src->transform.position = Vec3{100, 50, 0};
    src->transform.scale = Vec3{2, 2, 1};
    src->start = FrameIndex{3};
    src->end = FrameIndex{40};
    const Result<u64> made = rig.e.layer_to_image3d(*image, png, "Logo.png");
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    AUREA_CHECK(comp->layer(LayerId::unpack(*image)) == nullptr);
    const Layer* object = comp->layer(LayerId::unpack(*made));
    AUREA_CHECK(object != nullptr);
    if (!object) return;
    scene3d::Image3DSpec spec;
    AUREA_CHECK(rig.e.query_image3d(*made, spec));
    AUREA_CHECK_NEAR(object->transform.position.x, 116.f, .01f);   // centro da imagem de 16 px, escala 2
    AUREA_CHECK_NEAR(object->transform.position.y, 66.f, .01f);
    AUREA_CHECK_EQ(object->start.value, 3);
    AUREA_CHECK_EQ(object->end.value, 40);
    AUREA_CHECK_NEAR(object->model.unitScale, 16.f, .01f);
    AUREA_CHECK_NEAR(object->model.pivot.x, 0.f, .001f);
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(u).ok());
    AUREA_CHECK(comp->layer(LayerId::unpack(*image)) != nullptr);
    AUREA_CHECK(comp->layer(LayerId::unpack(*made)) == nullptr);
}
