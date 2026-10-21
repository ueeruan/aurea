// =============================================================================
//  MOTION TILE NÃO SOME (0.1.8 BETA 2). Pedido do dono: "qualquer efeito que
//  tiver o motion tile, ele deve aparecer no motion tile e o motion tile não
//  deve sumir".
//
//  Uma camada pequena com Motion Tile a 100% é uma PAREDE que cobre o quadro:
//    1. com qualquer efeito do catálogo DEPOIS dela, o quadro fica pelo menos
//       tão coberto quanto uma foto do tamanho do quadro com o mesmo efeito;
//    2. o efeito (antes ou depois do Motion Tile) aparece nos ladrilhos de
//       fora, não só no do centro.
// =============================================================================
#include "aurea/project/Serialization.hpp"
#if defined(AUREA_TEST_VULKAN)
namespace tilewall {

/// Fração do quadro com alfa > 0,5 (fundo transparente).
inline f32 coverage(const FloatImage& img) {
    u32 n = 0;
    for (usize i = 3; i < img.px.size(); i += 4) n += img.px[i] > 0.5f ? 1u : 0u;
    return img.px.empty() ? 0.0f : static_cast<f32>(n) / static_cast<f32>(img.px.size() / 4);
}

/// Pixels que mudaram entre `a` e `b`, dentro (central) e fora (outer) da
/// caixa [x0, x1) × [y0, y1).
struct SplitChange { u32 central = 0, outer = 0; };
inline SplitChange split_change(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    SplitChange c;
    for (u32 y = 0; y < a.height && y < b.height; ++y) for (u32 x = 0; x < a.width && x < b.width; ++x) {
        const f32* p = a.at(x, y);
        const f32* q = b.at(x, y);
        bool diff = false;
        for (int k = 0; k < 4; ++k) diff |= std::fabs(p[k] - q[k]) > 0.004f;
        if (!diff) continue;
        if (x >= x0 && x < x1 && y >= y0 && y < y1) ++c.central; else ++c.outer;
    }
    return c;
}

/// Efeitos fora deste teste, com o motivo.
inline const char* skipped(const Effect& fx) {
    if (const char* why = adjfx::not_for_adjustment(fx)) return why;
    const char* key = fx.info().key;
    if (std::strncmp(key, "aurea.audio.", 12) == 0) return "audio";
    // Depth/IA: o mapa vem da FONTE da camada (assíncrono); não há parede nele.
    if (std::strncmp(key, "aurea.ai.", 9) == 0) return "ia local";
    return nullptr;
}

/// Efeitos cuja ÁREA é medida na caixa da camada por definição (o tamanho
/// natural dela, não o quadro): transições/recortes que escondem parte da
/// camada, a esfera/cilindro/bolas do tamanho dela, a lente do tamanho dela e
/// as grades de deformação presas aos cantos dela. Numa camada pequena com
/// ladrilhos eles agem na caixa — a cobertura não se compara à de uma foto do
/// tamanho do quadro (o resto da parede segue com os ladrilhos).
inline const char* box_sized(const char* key) {
    static const char* const kBox[] = {
        "aurea.transform.crop", "aurea.transition.iris_wipe", "aurea.transition.box_wipe",
        "aurea.transition.radial_scale_wipe", "aurea.transition.block_dissolve", "aurea.glitch.cross",
        "aurea.transition.wipe_plasma", "aurea.transition.burn_film", "aurea.distort.page_turn",
        "aurea.distort.sphere_wrap", "aurea.distort.cylinder_wrap", "aurea.stylize.ball_grid",
        "aurea.distort.fisheye", "aurea.distort.mesh_warp", "aurea.distort.puppet",
        // 0.3.0: a lente do rastreio (k1/k2 na meia altura da camada); longe da caixa o
        // polinômio da lente sai do quadro filmado, como no Fisheye.
        "aurea.distort.lens_distortion_match",
    };
    for (const char* k : kBox) if (std::strcmp(k, key) == 0) return k;
    return nullptr;
}

/// Efeitos LOCAIS: desenham/deformam numa posição da camada (um ponto, uma
/// linha), não em toda a imagem — só o ladrilho do centro muda, por definição.
inline bool local_effect(const char* key) {
    static const char* const kLocal[] = {
        "aurea.generate.audio_spectrum", "aurea.generate.spectrum_analyzer", "aurea.distort.liquid_glass",
        "aurea.distort.smear", "aurea.distort.split", "aurea.transition.burn_film",
    };
    for (const char* k : kLocal) if (std::strcmp(k, key) == 0) return true;
    return false;
}

inline void dump(const std::string& name, const FloatImage& img) { adjfx::dump(name.c_str(), img); }

/// Camada 32×32 no centro de um quadro 96×96, fundo transparente.
inline LayerId small_layer(Scene& s) {
    s.comp->set_transparent_background(true);
    return s.image(reference_image(32, 32), 48, 48);
}

inline void add_catalog(Scene& s, LayerId id, const Effect& fx, const ParameterRegistry& params) {
    Layer* l = s.comp->layer(id);
    EffectInstance e;
    e.id = l->alloc_effect_id();
    e.type = fx.type_id();
    adjfx::configure(fx, params, e);
    l->effects.push_back(std::move(e));
}

inline void add_tile(Scene& s, LayerId id) {
    (void)s.add_effect(id, effect_keys::kMotionTile);
}

} // namespace tilewall

AUREA_TEST(Gpu, MotionTileKeepsTheWallWithEveryCatalogEffect) {
    AUREA_REQUIRE_GPU();
    std::vector<std::string> broken;
    const char* only = std::getenv("AUREA_TILE_ONLY");
    for (u32 i = 0; i < gpu().effects.count(); ++i) {
        const Effect& fx = gpu().effects.at(i);
        const char* key = fx.info().key;
        if (only && *only && std::strcmp(only, key) != 0) continue;
        if (tilewall::skipped(fx)) continue;
        if (fx.type_id() == effect_type_id(effect_keys::kMotionTile)) continue;
        const ParameterRegistry& params = gpu().effects.params_at(i);
        for (i64 t : {i64{0}, i64{12}}) {
            const FrameIndex frame{t};
            // Referência: uma foto do tamanho do quadro com o efeito.
            f32 reference = 0.0f;
            {
                Scene s(96, 96);
                s.comp->set_transparent_background(true);
                const LayerId full = s.image(reference_image(96, 96), 48, 48);
                tilewall::add_catalog(s, full, fx, params);
                reference = tilewall::coverage(s.render(frame));
            }
            FloatImage wall, after, before;
            {
                Scene s(96, 96);
                const LayerId id = tilewall::small_layer(s);
                tilewall::add_tile(s, id);
                wall = s.render(frame);
            }
            {
                Scene s(96, 96);
                const LayerId id = tilewall::small_layer(s);
                tilewall::add_tile(s, id);
                tilewall::add_catalog(s, id, fx, params);
                after = s.render(frame);
            }
            {
                Scene s(96, 96);
                const LayerId id = tilewall::small_layer(s);
                tilewall::add_catalog(s, id, fx, params);
                tilewall::add_tile(s, id);
                before = s.render(frame);
            }
            const std::string where = std::string(key) + " (quadro " + std::to_string(t) + ")";
            tilewall::dump(std::string("tile_") + key + "_t" + std::to_string(t) + "_depois", after);
            tilewall::dump(std::string("tile_") + key + "_t" + std::to_string(t) + "_antes", before);
            const f32 covAfter = tilewall::coverage(after);
            // 1. A parede não some: com o efeito depois, pelo menos a cobertura
            //    da foto cheia com o mesmo efeito (folga de 3% do quadro).
            if (!tilewall::box_sized(key) && covAfter + 0.03f < reference)
                broken.push_back(where + ": parede sumiu com o efeito depois (" + std::to_string(covAfter) +
                                 " < foto cheia " + std::to_string(reference) + ")");
            // 2. O efeito aparece fora do ladrilho central.
            const tilewall::SplitChange a = tilewall::split_change(wall, after, 32, 32, 64, 64);
            if (!tilewall::local_effect(key) && a.central > 40 && a.outer == 0)
                broken.push_back(where + ": efeito DEPOIS so no ladrilho central");
            const tilewall::SplitChange b = tilewall::split_change(wall, before, 32, 32, 64, 64);
            if (!tilewall::local_effect(key) && b.central > 40 && b.outer == 0)
                broken.push_back(where + ": efeito ANTES so no ladrilho central");
        }
    }
    for (const std::string& b : broken) std::printf("\n    motion tile: %s", b.c_str());
    AUREA_CHECK_MSG(broken.empty(), "o Motion Tile some ou o efeito nao chega aos ladrilhos");
}
// Os contextos em que a parede sumia ou podia sumir: camada fora do quadro,
// filha de um null com Motion Tile, pré-composição, camada de ajuste, prévia
// reduzida, export, efeitos animados antes/depois (tremor, onda, cor) em todo
// ladrilho, scrub e reabrir o projeto. Cada caso cobre o quadro inteiro.
AUREA_TEST(Gpu, MotionTileWallSurvivesEveryContext) {
    AUREA_REQUIRE_GPU();
    auto full = [](const FloatImage& img, const char* what) {
        const f32 c = tilewall::coverage(img);
        std::printf("\n    %s: cobertura %.4f", what, c);
        tilewall::dump(std::string("tile_ctx_") + what, img);
        AUREA_CHECK_MSG(c > 0.999f, what);
    };
    // 1. A camada original FORA do quadro (o retângulo dela não toca a tela):
    //    os ladrilhos ainda cobrem — com e sem efeito de cor depois.
    for (const bool color : {false, true}) {
        Scene s(96, 96);
        const LayerId id = tilewall::small_layer(s);
        s.comp->layer(id)->transform.position = Vec3{-70.0f, 140.0f, 0};
        tilewall::add_tile(s, id);
        if (color) s.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = 0.5f;
        full(s.render(), color ? "fora_do_quadro_com_cor" : "fora_do_quadro");
    }
    // 2. Filha de um null com Motion Tile, levada para fora do quadro.
    {
        Scene s(96, 96);
        const LayerId id = tilewall::small_layer(s);
        const LayerId parent = s.comp->add_layer(LayerKind::Null, "null");
        s.comp->layer(parent)->transform.position = Vec3{48, 48, 0};
        tilewall::add_tile(s, parent);
        s.comp->layer(id)->parent = parent;
        s.comp->layer(id)->transform.position = Vec3{-90.0f, 10.0f, 0};
        full(s.render(), "filha_de_null_fora");
    }
    // 3. Pré-composição pequena com Motion Tile.
    {
        Scene s(96, 96);
        s.comp->set_transparent_background(true);
        auto& timeline = s.project.timeline();
        const CompositionId childId = timeline.create_composition("ladrilho", 32, 32, 30.);
        Composition* child = timeline.composition(childId);
        child->set_transparent_background(true);
        const LayerId shapeId = child->add_layer(LayerKind::Shape, "quadrado");
        Layer* shape = child->layer(shapeId);
        shape->shape.bounds = Rect{0, 0, 32, 32};
        shape->shape.fillColor = Vec4{0.9f, 0.4f, 0.1f, 1};
        shape->transform.anchor = Vec3{16, 16, 0};
        shape->transform.position = Vec3{16, 16, 0};
        const LayerId nestedId = s.comp->add_layer(LayerKind::Composition, "pre");
        Layer* nested = s.comp->layer(nestedId);
        nested->nested.composition = childId;
        nested->transform.anchor = Vec3{16, 16, 0};
        nested->transform.position = Vec3{48, 48, 0};
        nested->transform.scale = Vec3{0.5f, 0.5f, 1};
        tilewall::add_tile(s, nestedId);
        full(s.render(), "pre_composicao");
    }
    // 4. Camada de ajuste com Motion Tile a 50%: a composição abaixo vira 4×4.
    {
        Scene s(96, 96);
        s.comp->set_transparent_background(true);
        (void)s.image(reference_image(96, 96), 48, 48);
        const LayerId adj = s.solid(96, 96, Vec4{1, 0, 1, 1}, 48, 48);
        s.comp->layer(adj)->adjustment = true;
        auto& tile = s.add_effect(adj, effect_keys::kMotionTile);
        tile.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
        tile.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
        const FloatImage img = s.render();
        full(img, "camada_de_ajuste");
        // O disco branco do centro aparece em cada quarto (4 cópias).
        u32 white = 0;
        // Ladrilhos de 48 px centrados: discos em (48,48), (0,48), (96,48), (48,0)...
        for (const auto& [x, y] : {std::pair<u32, u32>{52, 52}, {4, 52}, {92, 52}, {52, 4}, {52, 92}})
            white += img.v(x, y).x > 0.9f && img.v(x, y).y > 0.9f ? 1u : 0u;
        AUREA_CHECK_EQ(white, 5u);
    }
    // 4b. Texto com Motion Tile (o texto é recortado à caixa dos glifos):
    //     fora do quadro, com efeito antes e depois — letras nos 4 quartos.
    for (int variant = 0; variant < 3; ++variant) {
        Scene s(96, 96);
        s.comp->set_transparent_background(true);
        const LayerId textId = s.comp->add_layer(LayerKind::Text, "ladrilho de texto");
        Layer* text = s.comp->layer(textId);
        text->text.content = "AB";
        text->text.size = 18;
        text->transform.position = variant == 0 ? Vec3{48, 48, 0} : Vec3{-60, 140, 0};
        if (variant == 2) s.add_effect(textId, effect_keys::kGaussianBlur).params[0].constant.v[0] = 1.5f;
        auto& tile = s.add_effect(textId, effect_keys::kMotionTile);
        tile.params[motion_tile::kMirror].constant.v[0] = 1.0f;
        if (variant == 1) s.add_effect(textId, "aurea.light.glow");
        const FloatImage img = s.render();
        tilewall::dump("tile_ctx_texto_" + std::to_string(variant), img);
        u32 quarters = 0;
        for (u32 qy = 0; qy < 2; ++qy) for (u32 qx = 0; qx < 2; ++qx) {
            u32 ink = 0;
            for (u32 y = qy * 48; y < qy * 48 + 48; ++y) for (u32 x = qx * 48; x < qx * 48 + 48; ++x) ink += img.v(x, y).w > 0.5f ? 1u : 0u;
            quarters += ink > 10 ? 1u : 0u;
        }
        std::printf("\n    texto %d: quartos com letras %u", variant, quarters);
        AUREA_CHECK_EQ(quarters, 4u);
    }
    // 5. Efeitos ANIMADOS antes e depois (tremor, onda, cor animada): em todo
    //    ladrilho, em vários quadros, na prévia reduzida e no export; o scrub
    //    volta ao mesmo quadro.
    {
        Scene s(96, 96);
        const LayerId id = tilewall::small_layer(s);
        Layer* l = s.comp->layer(id);
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(FrameIndex{0}, 48.0f);
        px.set(FrameIndex{30}, 200.0f);   // sai do quadro andando
        // Onda ANTES (bordas repetidas: a caixa segue cheia) e DEPOIS, as duas
        // andando com o tempo.
        auto& wave = s.add_effect(id, effect_keys::kWaveWarp);
        wave.params[0].constant.v[0] = 6.0f;
        wave.params[2].constant.v[0] = 3.0f;
        wave.params[6].constant = ParamValue::scalar(0.0f);
        tilewall::add_tile(s, id);
        auto& waveAfter = s.add_effect(id, effect_keys::kWaveWarp);
        waveAfter.params[0].constant.v[0] = 5.0f;
        waveAfter.params[2].constant.v[0] = 2.0f;
        s.add_effect(id, effect_keys::kWiggle);
        s.add_effect(id, effect_keys::kShake);
        s.add_effect(id, effect_keys::kHueSaturation).params[0].constant.v[0] = 90.0f;
        for (const i64 t : {i64{0}, i64{7}, i64{19}, i64{29}}) {
            const FloatImage img = s.render(FrameIndex{t});
            full(img, ("animado_t" + std::to_string(t)).c_str());
            full(s.render(FrameIndex{t}, 2), ("animado_meia_t" + std::to_string(t)).c_str());
            const FloatImage exported = s.render(FrameIndex{t}, 1, true);
            f32 diff = 0;
            for (usize k = 0; k < img.px.size(); ++k) diff = std::max(diff, std::fabs(img.px[k] - exported.px[k]));
            AUREA_CHECK_MSG(diff <= 2.0f / 255.0f, "export difere da previa");
        }
        const FloatImage a = s.render(FrameIndex{19});
        (void)s.render(FrameIndex{3});
        const FloatImage b = s.render(FrameIndex{19});
        f32 diff = 0;
        for (usize k = 0; k < a.px.size(); ++k) diff = std::max(diff, std::fabs(a.px[k] - b.px[k]));
        AUREA_CHECK(diff <= 1e-4f);
        // 6. Reabrir o projeto: o mesmo quadro.
        const std::string path = std::string(AUREA_GOLDEN_DIR) + "/../../build/motion_tile_reopen.aurea";
        std::string error;
        AUREA_CHECK_MSG(ProjectSerializer::save(s.project, path, SaveOptions{}, &error).ok(), error.c_str());
        Project loaded;
        AUREA_CHECK_MSG(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok(), error.c_str());
        s.project = std::move(loaded);
        s.comp = s.project.timeline().composition(s.project.timeline().root());
        gpu().renderer.release_project_resources();
        const FloatImage reopened = s.render(FrameIndex{19});
        diff = 0;
        for (usize k = 0; k < a.px.size() && k < reopened.px.size(); ++k) diff = std::max(diff, std::fabs(a.px[k] - reopened.px[k]));
        std::printf("\n    reaberto: diferenca %.5f", diff);
        AUREA_CHECK(diff <= 1e-3f);
        std::remove(path.c_str());
    }
}
#endif // AUREA_TEST_VULKAN
