// =============================================================================
//  Fotos que "não importavam" (relato SM-A566B, build 2182: "depois de umas
//  quatro imagens, 'Não foi possível importar a imagem'"). Cada foto de celular
//  ficava residente inteira (até 4096 px ≈ 48 MB) num espaço de no máximo
//  256 MB: a 6ª era recusada. E nenhuma plataforma aplicava o EXIF.
// =============================================================================
#include "aurea/media/ImageResample.hpp"

namespace {
std::vector<u8> photo_pixels(u32 w, u32 h) {
    std::vector<u8> px(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* p = px.data() + (static_cast<usize>(y) * w + x) * 4;
            p[0] = static_cast<u8>(x); p[1] = static_cast<u8>(y); p[2] = 90; p[3] = 255;
        }
    return px;
}
} // namespace

AUREA_TEST(PhotoImport, TwelvePhonePhotosFitWhereTheFifthUsedToFail) {
    // 768 MB de orçamento = espaço de imagens de 256 MB (o teto do aparelho).
    Engine e; auto cfg = headless_config(); cfg.memoryBudgetBytes = 768ull << 20;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30., "fotos").ok());
    const u32 w = 4080, h = 3060;   // 12,5 MP: a câmera principal em modo padrão
    const auto px = photo_pixels(w, h);
    u32 imported = 0;
    for (u32 i = 0; i < 12; ++i) {
        const auto id = e.import_image(px.data(), w, h, "foto", "content://fotos/1", 1);
        if (!id.ok()) break;
        ++imported;
        bridge::LayerDetailPOD d;
        AUREA_CHECK(e.query_layer_detail(*id, d));
        // Mesma proporção, nunca abaixo do piso.
        AUREA_CHECK(d.sourceWidth >= image::kShrinkFloorSide);
        AUREA_CHECK_NEAR(static_cast<f64>(d.sourceWidth) / d.sourceHeight, 4.0 / 3.0, 0.01);
        if (i == 0) AUREA_CHECK(static_cast<u64>(d.sourceWidth) * d.sourceHeight <= image::pixel_cap(1080, 1920));
    }
    AUREA_CHECK_EQ(imported, 12u);
}

AUREA_TEST(PhotoImport, BudgetStillRefusesWhenEvenTheFloorDoesNotFit) {
    Engine e; auto cfg = headless_config(); cfg.memoryBudgetBytes = 3ull << 20;   // espaço de 1 MB
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30., "cheio").ok());
    const auto px = photo_pixels(2000, 1500);
    const auto r = e.import_image(px.data(), 2000, 1500, "foto", nullptr, 1);
    AUREA_CHECK(!r.ok() && r.status().code() == Errc::BudgetExceeded);
    AUREA_CHECK_EQ(e.project()->asset_count(), 0u);
}

AUREA_TEST(PhotoImport, ExifOrientationTurnsTheLayer) {
    // Pixels crus 4×2; EXIF 6 (90° horário) = retrato 2×4 na tela.
    std::vector<u8> raw(4 * 2 * 4);
    for (u32 i = 0; i < 8; ++i) { raw[i * 4] = static_cast<u8>(i); raw[i * 4 + 3] = 255; }
    std::vector<u8> out(raw.size());
    const struct { u8 o; u8 first; u8 second; } table[] = {
        {1, 0, 1}, {2, 3, 2}, {3, 7, 6}, {4, 4, 5}, {5, 0, 4}, {6, 4, 0}, {7, 7, 3}, {8, 3, 7}};
    for (const auto& t : table) {
        AUREA_CHECK(image::orient(raw.data(), 4, 2, t.o, out.data()));
        AUREA_CHECK_EQ(out[0], t.first);    // canto superior esquerdo na tela
        AUREA_CHECK_EQ(out[4], t.second);   // o vizinho à direita
    }
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "exif").ok());
    const auto id = e.import_image(raw.data(), 4, 2, "retrato", nullptr, 6);
    AUREA_CHECK(id.ok());
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(*id, d));
    AUREA_CHECK_EQ(d.sourceWidth, 2u);
    AUREA_CHECK_EQ(d.sourceHeight, 4u);
}

AUREA_TEST(PhotoImport, ShrunkAndTurnedPhotoReopensAtTheSavedSize) {
    const auto file = std::filesystem::temp_directory_path() / "aurea-photo-reopen.aurea";
    static constexpr u32 kW = 4000, kH = 3000;
    u32 savedW = 0, savedH = 0;
    {
        Engine writer; AUREA_CHECK(writer.initialize(headless_config()).ok());
        AUREA_CHECK(writer.new_project(640, 360, 30., "reabrir").ok());
        const auto px = photo_pixels(kW, kH);
        const auto id = writer.import_image(px.data(), kW, kH, "foto", "fotos/retrato.jpg", 6);
        AUREA_CHECK(id.ok());
        bridge::LayerDetailPOD d; AUREA_CHECK(writer.query_layer_detail(*id, d));
        savedW = d.sourceWidth; savedH = d.sourceHeight;
        AUREA_CHECK(savedW < savedH);                  // retrato
        AUREA_CHECK(savedH < kW);                      // reduzida (teto 4 MP)
        AUREA_CHECK(writer.save_project(file.string().c_str()).ok());
    }
    // A plataforma devolve a foto CRUA no tamanho canônico + a orientação.
    auto cfg = headless_config();
    cfg.imageLoader = [](const char*, ImagePixels& out, void*) {
        out.width = kW; out.height = kH; out.orientation = 6;
        out.rgba = photo_pixels(kW, kH);
        return true;
    };
    Engine reader; AUREA_CHECK(reader.initialize(cfg).ok());
    AUREA_CHECK(reader.load_project(file.string().c_str()).ok());
    AUREA_CHECK_EQ(reader.last_load_missing_assets(), 0u);
    const auto* c = reader.project()->timeline().composition(reader.project()->timeline().root());
    LayerId lid{}; c->layers().for_each([&](LayerId l, const Layer&) { lid = l; });
    std::vector<u8> thumb(64 * 64 * 4); u32 tw = 0;
    AUREA_CHECK(reader.query_thumbnail(lid.pack(), 0, 8, thumb.data(), static_cast<u32>(thumb.size()), &tw) > 0);
    bridge::LayerDetailPOD d; AUREA_CHECK(reader.query_layer_detail(lid.pack(), d));
    AUREA_CHECK_EQ(d.sourceWidth, savedW);
    AUREA_CHECK_EQ(d.sourceHeight, savedH);
    std::error_code error; std::filesystem::remove(file, error);
}

AUREA_TEST(PhotoImport, AreaResampleKeepsStraightAlphaEdges) {
    // Metade vermelha opaca, metade transparente PRETA: a média não escurece.
    std::vector<u8> src(2 * 1 * 4, 0);
    src[0] = 255; src[3] = 255;
    u8 dst[4] = {};
    AUREA_CHECK(image::resample_area(src.data(), 2, 1, dst, 1, 1));
    AUREA_CHECK_EQ(dst[0], 255);
    AUREA_CHECK_EQ(dst[3], 128);
    AUREA_CHECK(image::same_picture_larger(4000, 3000, 2309, 1732));
    AUREA_CHECK(!image::same_picture_larger(4000, 3000, 1732, 2309));
    AUREA_CHECK(!image::same_picture_larger(2000, 1500, 2309, 1732));
    const auto fit = image::fit_to_room(4000, 3000, 20ull << 20, 64);
    AUREA_CHECK(fit.width > 0 && static_cast<u64>(fit.width) * fit.height * 4 + 64 <= (10ull << 20));
}

// =============================================================================
//  0.5.3 — relato do Discord: "null bug foto bug app crash for nothing … it's
//  just two photos and crashed" (sem log). Duas fotos no mesmo projeto pelos
//  caminhos que copiam a foto inteira: importar (grande com EXIF 6 + uma
//  pequena + um panorama extremo), desenhar (a textura de cada uma sobe pela
//  fila de envio), miniaturas da timeline e reabrir o projeto com a plataforma
//  devolvendo a foto, nada (null) ou o tamanho errado. Nenhum caminho pode
//  fechar o app; o que faltar vira placeholder.
// =============================================================================
#include "aurea/core/SafeAlloc.hpp"

AUREA_TEST(PhotoImport, SafeAllocRefusesImpossibleSizesInsteadOfTerminating) {
    std::vector<u8> v(16, 7);
    // Tamanho impossível: o resize lançaria (length_error / bad_alloc) — num
    // arquivo -fno-exceptions do celular isso era std::terminate.
    AUREA_CHECK(!try_resize_bytes(v, std::numeric_limits<usize>::max()));
    AUREA_CHECK_EQ(v.size(), 16u);
    AUREA_CHECK_EQ(v[0], 7);
    AUREA_CHECK(try_resize_bytes(v, 64));
    AUREA_CHECK_EQ(v.size(), 64u);
    const u8 src[4] = {1, 2, 3, 4};
    std::vector<u8> copy;
    AUREA_CHECK(try_assign_bytes(copy, src, 4));
    AUREA_CHECK_EQ(copy.size(), 4u);
    AUREA_CHECK_EQ(copy[3], 4);
    AUREA_CHECK(!try_assign_bytes(copy, nullptr, 4));
    AUREA_CHECK_EQ(copy.size(), 4u);
    AUREA_CHECK(try_assign_bytes(copy, nullptr, 0));
    AUREA_CHECK(copy.empty());
}

AUREA_TEST(PhotoImport, TwoPhotosImportDrawThumbAndReopenWithoutCrashing) {
    const auto file = std::filesystem::temp_directory_path() / "aurea-two-photos.aurea";
    // Foto de celular 12,5 MP em retrato pelo EXIF 6 (pixels crus deitados) e
    // uma foto pequena (PNG/WebP chegam igual: RGBA8 reto já decodificado).
    static constexpr u32 kBigW = 4080, kBigH = 3060, kSmallW = 640, kSmallH = 480;
    u64 bigId = 0, smallId = 0, panoId = 0;
    {
        auto* mock = new aurea::test::MockBackend();
        Engine e; auto cfg = headless_config(); cfg.backend = mock; cfg.memoryBudgetBytes = 768ull << 20;
        AUREA_CHECK(e.initialize(cfg).ok());
        AUREA_CHECK(e.new_project(1080, 1920, 30., "duas fotos").ok());
        int window = 0;
        AUREA_CHECK(e.attach_surface(&window, 108, 192).ok());
        const auto big = photo_pixels(kBigW, kBigH);
        const auto small = photo_pixels(kSmallW, kSmallH);
        const auto a = e.import_image(big.data(), kBigW, kBigH, "IMG_0001.jpg", "content://media/picker/0/1", 6);
        AUREA_CHECK(a.ok()); if (!a.ok()) return;
        AUREA_CHECK(e.render_frame().ok());   // a 1ª textura sobe antes da 2ª foto chegar
        const auto b = e.import_image(small.data(), kSmallW, kSmallH, "IMG_0002.png", "content://media/picker/0/2", 1);
        AUREA_CHECK(b.ok()); if (!b.ok()) return;
        bigId = *a; smallId = *b;
        // Panorama extremo: a miniatura da timeline pedia uma tira gigante.
        const auto pano = photo_pixels(8192, 16);
        const auto c = e.import_image(pano.data(), 8192, 16, "pano.webp", "content://media/picker/0/3", 1);
        AUREA_CHECK(c.ok()); if (c.ok()) panoId = *c;
        for (u32 i = 0; i < 4; ++i) AUREA_CHECK(e.render_frame().ok());
        bridge::LayerDetailPOD d;
        AUREA_CHECK(e.query_layer_detail(bigId, d));
        AUREA_CHECK(d.sourceWidth < d.sourceHeight);   // retrato (EXIF aplicado)
        AUREA_CHECK(e.query_layer_detail(smallId, d));
        AUREA_CHECK_EQ(d.sourceWidth, kSmallW);
        std::vector<u8> thumb(1 << 20); u32 tw = 0;
        AUREA_CHECK(e.query_thumbnail(bigId, 0, 48, thumb.data(), static_cast<u32>(thumb.size()), &tw) > 0);
        AUREA_CHECK(e.query_thumbnail(smallId, 0, 48, thumb.data(), static_cast<u32>(thumb.size()), &tw) > 0);
        if (panoId) {
            tw = 0;
            const u32 bytes = e.query_thumbnail(panoId, 0, 48, thumb.data(), static_cast<u32>(thumb.size()), &tw);
            AUREA_CHECK(bytes > 0);
            AUREA_CHECK(tw <= 4096u);   // a tira nunca passa de 4096 de largura
        }
        AUREA_CHECK(e.save_project(file.string().c_str()).ok());
        e.shutdown();
    }
    // Reabrir: a plataforma devolve a 1ª foto crua (com EXIF), nada para a 2ª
    // ("null": decodeImage falhou) e depois um tamanho que não confere.
    struct Loader { u32 calls = 0; u32 mode = 0; } loader;
    auto cfg = headless_config();
    cfg.memoryBudgetBytes = 768ull << 20;
    cfg.imageLoaderContext = &loader;
    cfg.imageLoader = [](const char* src, ImagePixels& out, void* opaque) {
        auto& l = *static_cast<Loader*>(opaque); ++l.calls;
        const std::string s = src ? src : "";
        if (s.find("/1") != std::string::npos) {
            out.width = kBigW; out.height = kBigH; out.orientation = 6;
            out.rgba = photo_pixels(kBigW, kBigH);
            return true;
        }
        if (l.mode == 0) return false;               // null
        out.width = kSmallW; out.height = kSmallH;   // tamanho errado: falta 1 linha
        out.rgba.assign(static_cast<usize>(kSmallW) * (kSmallH - 1) * 4, 9);
        return true;
    };
    for (u32 mode : {0u, 1u}) {
        loader.mode = mode; loader.calls = 0;
        auto* mock = new aurea::test::MockBackend();
        cfg.backend = mock;
        Engine r; AUREA_CHECK(r.initialize(cfg).ok());
        AUREA_CHECK(r.load_project(file.string().c_str()).ok());
        AUREA_CHECK(loader.calls >= 2u);
        AUREA_CHECK(r.last_load_missing_assets() >= 1u);   // a 2ª vira placeholder
        int window = 0;
        AUREA_CHECK(r.attach_surface(&window, 108, 192).ok());
        for (u32 i = 0; i < 3; ++i) AUREA_CHECK(r.render_frame().ok());
        // Os ids podem mudar ao reabrir: acha as camadas pela origem.
        u64 reBig = 0, reSmall = 0;
        const auto* comp = r.project()->timeline().composition(r.project()->timeline().root());
        comp->layers().for_each([&](LayerId lid, const Layer& layer) {
            const Asset* asset = r.project()->asset(layer.source);
            if (!asset) return;
            if (asset->sourcePath == "content://media/picker/0/1") reBig = lid.pack();
            if (asset->sourcePath == "content://media/picker/0/2") reSmall = lid.pack();
        });
        AUREA_CHECK(reBig != 0 && reSmall != 0);
        std::vector<u8> thumb(1 << 20); u32 tw = 0;
        AUREA_CHECK(r.query_thumbnail(reBig, 0, 48, thumb.data(), static_cast<u32>(thumb.size()), &tw) > 0);
        AUREA_CHECK_EQ(r.query_thumbnail(reSmall, 0, 48, thumb.data(), static_cast<u32>(thumb.size()), &tw), 0u);
        r.shutdown();
    }
    std::error_code error; std::filesystem::remove(file, error);
}
