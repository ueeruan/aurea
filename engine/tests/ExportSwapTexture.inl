// Export (OfflineFrameRenderer) de "Trocar textura 3D" + imagem 3D, com e sem
// motion blur (relatos 1.2.0). Os quadros capturados são NV12: o quadriculado
// vermelho/azul da troca aparece como Cb alto (azul); o cubo padrão (vermelho)
// quase não tem Cb alto, e a imagem verde tem Cb e Cr baixos.
#include "ImageIO.hpp"

namespace export_swap_test {

struct Chroma { u32 blue = 0, red = 0, green = 0; };

Chroma chroma(const std::vector<u8>& nv12, u32 w, u32 h) {
    Chroma c;
    const u8* uv = nv12.data() + static_cast<usize>(w) * h;
    for (u32 y = 0; y < h / 2; ++y)
        for (u32 x = 0; x + 1 < w; x += 2) {
            const int cb = uv[static_cast<usize>(y) * w + x], cr = uv[static_cast<usize>(y) * w + x + 1];
            if (cb > 160 && cr < 150) ++c.blue;
            if (cr > 170 && cb < 140) ++c.red;
            if (cb < 110 && cr < 110) ++c.green;
        }
    return c;
}

std::string checker_png(const char* name) {
    aurea::test::Image8 img;
    img.width = img.height = 64;
    img.rgba.resize(64u * 64u * 4u);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) {
            u8* p = &img.rgba[(static_cast<usize>(y) * 64 + x) * 4];
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            p[0] = a ? 230 : 20; p[1] = 30; p[2] = a ? 20 : 230; p[3] = 255;
        }
    const std::string path = std::filesystem::absolute(std::string("tests/build/") + name).string();
    AUREA_CHECK(aurea::test::write_png(path, img));
    return path;
}

} // namespace export_swap_test

AUREA_TEST(OfflineExportGpu, SwappedTextureAndImagePlanesSurviveMotionBlurInExport) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace export_swap_test;
    const std::string png = checker_png("aurea_teste_troca_export.png");
    for (int blur : {0, 1}) {
        for (int swap : {0, 1}) {
            SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
            Rig r(cfg, 30, 6, 1);
            AUREA_CHECK(r.ok); if (!r.ok) return;
            r.comp()->layer(r.video_layer())->visible = false;
            // Imagem verde 3D na mesma cena (plano do grupo compartilhado).
            std::vector<u8> px(64u * 64u * 4u);
            for (usize i = 0; i < px.size(); i += 4) { px[i] = 20; px[i + 1] = 220; px[i + 2] = 20; px[i + 3] = 255; }
            const auto image = r.e.import_image(px.data(), 64, 64, "verde");
            AUREA_CHECK(image.ok()); if (!image.ok()) return;
            Layer* il = r.comp()->layer(LayerId::unpack(*image));
            il->threeD = true;
            il->transform.position = Vec3{60, 40, 150};
            const auto cube = r.e.add_shape3d(0);   // Cubo
            AUREA_CHECK(cube.ok()); if (!cube.ok()) return;
            const LayerId cid = LayerId::unpack(*cube);
            auto& track = r.comp()->layer(cid)->tracks.get_or_create(TrackProperty::PositionX);
            track.set(FrameIndex{0}, 140); track.set(FrameIndex{5}, 180);
            if (swap) {
                EffectInstance* fx = r.add_effect(cid, effect_keys::kPart3DTexture);
                AUREA_CHECK(fx != nullptr); if (!fx) return;
                const u32 fxId = fx->id;
                AUREA_CHECK(r.e.set_effect_image(*cube, fxId, png.c_str()).ok());
                for (auto& e : r.comp()->layer(cid)->effects)
                    if (e.id == fxId) e.params[2].constant = ParamValue::scalar(1.0f);   // todas as partes
            }
            if (blur) {
                AUREA_CHECK(r.e.set_motion_blur(*cube, true));
                AUREA_CHECK(r.e.set_motion_blur(*image, true));
                AUREA_CHECK(r.e.set_composition_motion_blur(true));
            }
            r.cap.keepFrames = true;
            const auto result = run_export(r, 180, 30, false, 60);
            AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
            if (!result.finished || r.cap.frames.empty()) return;
            const Chroma c = chroma(r.cap.frames[3], r.cap.video.width, r.cap.video.height);
            std::printf("\n    export troca=%d desfoque=%d: azul %u vermelho %u verde %u", swap, blur, c.blue, c.red, c.green);
            if (swap) AUREA_CHECK_MSG(c.blue > 300u, blur ? "troca perdida no export com desfoque" : "troca perdida no export");
            else AUREA_CHECK(c.blue < 100u);
            AUREA_CHECK_MSG(c.green > 100u, "imagem 3D sumiu no export");
        }
    }
    std::remove(png.c_str());
}
