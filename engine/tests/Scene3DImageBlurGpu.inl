// Imagens na cena 3D com motion blur e "Trocar textura 3D" (relatos 1.2.0):
// 1. imagens 3D somem com o desfoque ligado;
// 2. a textura trocada não aparece no cubo;
// 3. a troca deixa de valer com a forma numa cena 3D com outras camadas.
#include "aurea/scene3d/Shape3D.hpp"
namespace img3d_test {
using aurea::scene3d::Shape3DKind;

struct Counts { u32 lit = 0, red = 0, green = 0, blue = 0; };

Counts count(const Image8& img) {
    Counts c;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) {
            const u8* p = img.at(x, y);
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) ++c.lit;
            if (p[0] > p[1] + 40 && p[0] > p[2] + 40) ++c.red;
            if (p[1] > p[0] + 40 && p[1] > p[2] + 40) ++c.green;
            if (p[2] > p[0] + 40 && p[2] > p[1] + 40) ++c.blue;
        }
    return c;
}

/// Quadriculado vermelho/azul 64×64 num PNG temporário (a imagem da troca).
std::string checker_file(const char* name) {
    Image8 img;
    img.width = img.height = 64;
    img.rgba.resize(64u * 64u * 4u);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) {
            u8* p = &img.rgba[(static_cast<usize>(y) * 64 + x) * 4];
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            p[0] = a ? 230 : 20; p[1] = 30; p[2] = a ? 20 : 230; p[3] = 255;
        }
    const std::string path = std::string("tests/build/") + name;
    AUREA_CHECK(write_png(path, img));
    return path;
}

/// Imagem verde 3D (POSIÇÃO do centro em px da composição).
u64 green_image(Engine& e, f32 x, f32 y, f32 z) {
    std::vector<u8> px(64u * 64u * 4u);
    for (usize i = 0; i < px.size(); i += 4) { px[i] = 20; px[i + 1] = 220; px[i + 2] = 20; px[i + 3] = 255; }
    const Result<u64> id = e.import_image(px.data(), 64, 64, "verde");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return 0;
    Layer* l = current_comp(e)->layer(LayerId::unpack(*id));
    l->threeD = true;
    l->transform.position = Vec3{x, y, z};
    return *id;
}

u32 add_effect(Engine& e, u64 layer, const char* key) {
    Command c;
    c.type = CommandType::EffectAdd;
    c.effect_add.layer = LayerId::unpack(layer);
    c.effect_add.effectType = effect_type_id(key);
    c.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(c).ok());
    const Layer* l = current_comp(e)->layer(LayerId::unpack(layer));
    return l && !l->effects.empty() ? l->effects.back().id : 0u;
}

void set_param(Engine& e, u64 layer, u32 effect, u32 param, f32 value) {
    Command c;
    c.type = CommandType::EffectSetParam;
    c.effect_param.layer = LayerId::unpack(layer);
    c.effect_param.effect = EffectId{effect, 0};
    c.effect_param.paramIndex = param;
    c.effect_param.value = value;
    AUREA_CHECK(e.apply_command(c).ok());
}

/// Câmera que anda em X (para o desfoque ter o que borrar).
void moving_camera(Engine& e) {
    const u64 cam = add_test_camera(e, Vec3{160, 90, -216}, kDefaultFov180);
    Layer* c = current_comp(e)->layer(LayerId::unpack(cam));
    auto& track = c->tracks.get_or_create(TrackProperty::PositionX);
    track.set(FrameIndex{0}, 130.0f, Interpolation::Linear);
    track.set(FrameIndex{20}, 190.0f, Interpolation::Linear);
}

void blur_everything(Engine& e) {
    const auto& order = current_comp(e)->order();
    for (u32 i = 0; i < order.size(); ++i) (void)e.set_motion_blur(order.at(i).pack(), true);
    (void)e.set_composition_motion_blur(true);
}

/// Captura (prévia ou final). Falha = imagem vazia + a mensagem do motor:
/// a captura já preenche largura/altura, e count() de uma imagem com tamanho
/// e sem pixels lia o endereço 0.
bool grab(Engine& e, bool preview, Image8& img) {
    const Status s = preview ? e.capture_preview_frame_rgba(320, img.rgba, img.width, img.height)
                             : e.capture_frame_rgba(320, img.rgba, img.width, img.height);
    if (!s.ok()) {
        std::printf("\n    captura %s falhou: codigo %d %s", preview ? "previa" : "final", s.raw(), s.message().data());
        img = Image8{};
    }
    return s.ok();
}

} // namespace img3d_test

// Bug 1: imagens 3D (planos da cena) somem com o motion blur ligado.
AUREA_TEST(Scene3DImageBlurGpu, ImagesInA3DSceneSurviveMotionBlur) {
    AUREA_REQUIRE_GPU();
    using namespace img3d_test;
    using aurea::scene3d::Shape3DKind;
    // withModel: cubo 3D na mesma cena (grupo compartilhado com os planos).
    for (int withModel : {0, 1}) {
        for (int preview : {0, 1}) {
            auto shot = [&](bool blur) {
                Scene3DRig rig(320, 180);
                moving_camera(rig.e);
                (void)green_image(rig.e, 90, 90, 0);
                (void)green_image(rig.e, 230, 90, 100);
                if (withModel) {
                    const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
                    AUREA_CHECK(cube.ok());
                }
                if (blur) blur_everything(rig.e);
                seek_frame(rig.e, 10);
                Image8 img;
                AUREA_CHECK(grab(rig.e, preview != 0, img));
                return count(img);
            };
            const Counts off = shot(false), on = shot(true);
            std::printf("\n    imagens 3D (modelo %d, %s): verde sem desfoque %u, com desfoque %u",
                        withModel, preview ? "previa" : "final", off.green, on.green);
            AUREA_CHECK(off.green > 2000u);
            // O rastro espalha a imagem, mas ela continua lá (não some).
            AUREA_CHECK(on.green * 10u > off.green * 6u);
        }
    }
}

// Bug 1 (variante): a IMAGEM 3D que anda sozinha (planos com blurMatrices,
// reamostrados no relógio do grupo) continua visível com o desfoque.
AUREA_TEST(Scene3DImageBlurGpu, MovingImagePlaneSurvivesMotionBlur) {
    AUREA_REQUIRE_GPU();
    using namespace img3d_test;
    using aurea::scene3d::Shape3DKind;
    for (int preview : {0, 1}) {
        auto shot = [&](bool blur) {
            Scene3DRig rig(320, 180);
            const u64 img = green_image(rig.e, 100, 90, 0);
            Layer* l = current_comp(rig.e)->layer(LayerId::unpack(img));
            auto& track = l->tracks.get_or_create(TrackProperty::PositionX);
            track.set(FrameIndex{0}, 70.0f, Interpolation::Linear);
            track.set(FrameIndex{20}, 130.0f, Interpolation::Linear);
            const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
            AUREA_CHECK(cube.ok());
            if (cube.ok()) current_comp(rig.e)->layer(LayerId::unpack(*cube))->transform.position.x = 240.0f;
            if (blur) blur_everything(rig.e);
            seek_frame(rig.e, 10);
            Image8 out;
            AUREA_CHECK(grab(rig.e, preview != 0, out));
            return count(out);
        };
        const Counts off = shot(false), on = shot(true);
        std::printf("\n    imagem 3D animada (%s): verde sem desfoque %u, com desfoque %u",
                    preview ? "previa" : "final", off.green, on.green);
        AUREA_CHECK(off.green > 2000u);
        AUREA_CHECK(on.green * 10u > off.green * 6u);
    }
}

// Bug 3: a troca vale com a forma numa cena 3D com OUTRAS formas e imagens
// (grupo compartilhado, instancing, desfoque), na prévia e no final; e a
// forma vizinha sem troca sai idêntica com e sem a troca no cubo.
AUREA_TEST(Scene3DImageBlurGpu, SwapSurvivesASharedSceneWithOtherShapes) {
    AUREA_REQUIRE_GPU();
    using namespace img3d_test;
    using aurea::scene3d::Shape3DKind;
    const std::string png = checker_file("aurea_teste_troca_vizinhos.png");
    auto region = [](const Image8& img, u32 x0, u32 x1) {
        Counts c;
        for (u32 y = 0; y < img.height; ++y)
            for (u32 x = x0; x < x1 && x < img.width; ++x) {
                const u8* p = img.at(x, y);
                if (p[0] > 8 || p[1] > 8 || p[2] > 8) ++c.lit;
                if (p[0] > p[1] + 40 && p[0] > p[2] + 40) ++c.red;
                if (p[1] > p[0] + 40 && p[1] > p[2] + 40) ++c.green;
                if (p[2] > p[0] + 40 && p[2] > p[1] + 40) ++c.blue;
            }
        return c;
    };
    for (int blur : {0, 1}) {
        for (int preview : {0, 1}) {
            Image8 shots[2];
            for (int swap : {0, 1}) {
                Scene3DRig rig(320, 180);
                if (blur) moving_camera(rig.e);
                (void)green_image(rig.e, 160, 30, 150);
                const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
                const Result<u64> other = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo vizinho");
                AUREA_CHECK(cube.ok() && other.ok());
                if (!cube.ok() || !other.ok()) return;
                current_comp(rig.e)->layer(LayerId::unpack(*cube))->transform.position.x = 90.0f;
                current_comp(rig.e)->layer(LayerId::unpack(*other))->transform.position.x = 230.0f;
                if (swap) {
                    const u32 fx = add_effect(rig.e, *cube, effect_keys::kPart3DTexture);
                    AUREA_CHECK(rig.e.set_effect_image(*cube, fx, png.c_str()).ok());
                    set_param(rig.e, *cube, fx, 2, 1.0f);
                }
                if (blur) blur_everything(rig.e);
                seek_frame(rig.e, 10);
                Image8& out = shots[swap];
                AUREA_CHECK(grab(rig.e, preview != 0, out));
            }
            const Counts left = region(shots[1], 0, 160), right0 = region(shots[0], 160, 320),
                         right1 = region(shots[1], 160, 320), all = count(shots[1]);
            std::printf("\n    vizinhos (desfoque %d, %s): cubo trocado vermelho %u azul %u | vizinho lit %u/%u azul %u/%u vermelho %u/%u | verde %u",
                        blur, preview ? "previa" : "final", left.red, left.blue, right0.lit, right1.lit,
                        right0.blue, right1.blue, right0.red, right1.red, all.green);
            if (const char* dump = std::getenv("AUREA_DUMP_3DIMG"); dump && *dump == '1') {
                const std::string tag = std::string("tests/build/vizinhos_b") + std::to_string(blur) + (preview ? "_previa" : "_final");
                (void)write_png(tag + "_sem.png", shots[0]);
                (void)write_png(tag + "_com.png", shots[1]);
            }
            AUREA_CHECK(left.red > 200u && left.blue > 200u);
            // O vizinho não pega a textura: a metade dele sai igual à do quadro
            // sem troca (o cubo padrão já tem faces azuladas pela luz).
            AUREA_CHECK(right1.blue <= right0.blue + 20u && right0.blue <= right1.blue + 20u);
            AUREA_CHECK(right1.red + 50u > right0.red && right0.red + 50u > right1.red);
            AUREA_CHECK(right1.lit + 50u > right0.lit && right0.lit + 50u > right1.lit);
            AUREA_CHECK(all.green > 100u);                   // a imagem 3D continua na cena
        }
    }
    std::remove(png.c_str());
}

// Bug 2/3: Trocar textura 3D no cubo — sozinho, com desfoque e numa cena 3D
// com imagens (grupo compartilhado).
AUREA_TEST(Scene3DImageBlurGpu, SwappedTextureShowsAloneWithBlurAndInASharedScene) {
    AUREA_REQUIRE_GPU();
    using namespace img3d_test;
    using aurea::scene3d::Shape3DKind;
    const std::string png = checker_file("aurea_teste_troca_cena.png");
    enum Case { Alone, Blurred, WithImage, WithImageBlurred, Count };
    const char* names[Count] = {"sozinho", "com desfoque", "com imagem 3D", "com imagem 3D e desfoque"};
    for (int k = 0; k < Count; ++k) {
        for (int swap : {0, 1}) {
            Scene3DRig rig(320, 180);
            const bool image = k == WithImage || k == WithImageBlurred;
            const bool blur = k == Blurred || k == WithImageBlurred;
            if (blur) moving_camera(rig.e);
            if (image) (void)green_image(rig.e, 60, 40, 200);
            const Result<u64> cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
            AUREA_CHECK(cube.ok());
            if (!cube.ok()) return;
            if (swap) {
                const u32 fx = add_effect(rig.e, *cube, effect_keys::kPart3DTexture);
                AUREA_CHECK(rig.e.set_effect_image(*cube, fx, png.c_str()).ok());
                set_param(rig.e, *cube, fx, 0, 5.0f);   // Parte 5 (como no print)
                set_param(rig.e, *cube, fx, 2, 1.0f);   // todas as partes
            }
            if (blur) blur_everything(rig.e);
            seek_frame(rig.e, 10);
            const Counts c = count(rig.capture(320));
            std::printf("\n    troca de textura %s (%s): vermelho %u azul %u verde %u", names[k],
                        swap ? "com troca" : "sem troca", c.red, c.blue, c.green);
            if (swap) AUREA_CHECK_MSG(c.red > 200u && c.blue > 200u, names[k]);
            else AUREA_CHECK(c.blue < 50u);
            if (image) AUREA_CHECK_MSG(c.green > 100u, names[k]);
        }
    }
    std::remove(png.c_str());
}
