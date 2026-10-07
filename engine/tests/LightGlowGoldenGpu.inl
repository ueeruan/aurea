// =============================================================================
//  Brilho e Brilho profundo: referências do desenho anterior (build 2140).
//
//  As PNGs em tests/data/golden/light/ foram geradas ANTES do algoritmo novo
//  (AUREA_WRITE_LIGHT_GOLDENS=1). Uma instância de projeto antigo — salva com
//  a contagem de parâmetros de antes, gravada e reaberta pelo Engine — tem de
//  continuar idêntica a elas (≤ 2/255). Instâncias novas usam o algoritmo do
//  app antigo e são conferidas pelo comportamento (identidade, halo fora da
//  caixa, texto).
// =============================================================================
namespace light_golden {

// Contagem de parâmetros salva pelas builds até a 2143.
constexpr u32 kGlowSavedCount = 4;
constexpr u32 kDeepGlowSavedCount = 15;

struct Setting { const char* id; ParamValue value; };
struct Case {
    const char* key;
    const char* file;
    u32 savedCount;
    std::vector<Setting> values;
};

std::vector<Case> cases() {
    using PV = ParamValue;
    return {
        // Os padrões de antes (60 % / 30 px / 1): é o que um projeto antigo
        // gravou — os padrões declarados agora são os da instância nova.
        {effect_keys::kGlow, "glow_default", kGlowSavedCount,
         {{"threshold", PV::scalar(60)}, {"radius", PV::scalar(30)}, {"intensity", PV::scalar(1)}}},
        {effect_keys::kGlow, "glow_wide", kGlowSavedCount,
         {{"threshold", PV::scalar(30)}, {"radius", PV::scalar(60)}, {"intensity", PV::scalar(2)},
          {"color", PV::color(1.0f, 0.6f, 0.2f, 1)}}},
        {effect_keys::kGlow, "glow_tight", kGlowSavedCount,
         {{"threshold", PV::scalar(80)}, {"radius", PV::scalar(12)}, {"intensity", PV::scalar(4)}}},
        {effect_keys::kDeepGlow, "deep_glow_default", kDeepGlowSavedCount, {}},
        {effect_keys::kDeepGlow, "deep_glow_only", kDeepGlowSavedCount,
         {{"threshold", PV::scalar(40)}, {"core_radius", PV::scalar(20)}, {"halo_radius", PV::scalar(120)},
          {"core_intensity", PV::scalar(2)}, {"halo_intensity", PV::scalar(1.2f)},
          {"only_glow", PV::boolean(true)}}},
        {effect_keys::kDeepGlow, "deep_glow_screen", kDeepGlowSavedCount,
         {{"screen_halo", PV::boolean(true)}, {"tint_core", PV::boolean(true)},
          {"glow_color", PV::color(0.4f, 0.7f, 1.0f, 1)}, {"exposure", PV::scalar(1)},
          {"optical_falloff", PV::boolean(false)}}},
    };
}

std::string golden_path(const char* file) {
    return std::string(AUREA_TEST_DATA_DIR) + "/golden/light/" + file + ".png";
}

Composition* current(Scene3DRig& rig) {
    return rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
}

/// Texto "GLOW" à esquerda e uma imagem com mancha clara à direita.
void build_scene(Scene3DRig& rig, u64& text, u64& image) {
    auto t = rig.e.add_text("GLOW");
    AUREA_CHECK(t.ok());
    text = t.ok() ? *t : 0;
    auto px = uniform_image(96, 64, 20, 30, 60);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 96; ++x) {
        const f32 dx = (static_cast<f32>(x) - 48.0f) / 30.0f, dy = (static_cast<f32>(y) - 32.0f) / 20.0f;
        const f32 r = std::sqrt(dx * dx + dy * dy);
        if (r < 1.0f) {
            u8* p = &px.rgba[(usize(y) * 96 + x) * 4];
            const f32 k = 1.0f - r;
            p[0] = static_cast<u8>(std::min(255.0f, 120.0f + 135.0f * k * 1.6f));
            p[1] = static_cast<u8>(std::min(255.0f, 60.0f + 195.0f * k * k));
            p[2] = static_cast<u8>(std::min(255.0f, 30.0f + 225.0f * k * k * k));
        }
    }
    // Com caminho no disco: o projeto salvo reabre a imagem.
    const std::string png = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_light_mancha.png";
    Image8 file; file.width = 96; file.height = 64; file.rgba = px.rgba;
    AUREA_CHECK(write_png(png, file));
    auto i = rig.e.import_image(px.rgba.data(), 96, 64, "mancha", png.c_str());
    AUREA_CHECK(i.ok());
    image = i.ok() ? *i : 0;
    Composition* comp = current(rig);
    comp->set_background(Color{0, 0, 0, 1});
    comp->set_transparent_background(false);
    if (Layer* l = comp->layer(LayerId::unpack(text))) {
        l->transform.position.x = 90.0f; l->transform.position.y = 90.0f;
    }
    if (Layer* l = comp->layer(LayerId::unpack(image))) {
        // A importação enquadra a imagem no quadro: um terço disso, à direita.
        l->transform.position.x = 250.0f; l->transform.position.y = 90.0f;
        l->transform.scale.x /= 3.0f; l->transform.scale.y /= 3.0f;
    }
}

EffectInstance* add_fx(Scene3DRig& rig, u64 layer, const Case& c) {
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(layer);
    add.effect_add.effectType = effect_type_id(c.key);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    Layer* l = current(rig)->layer(LayerId::unpack(layer));
    if (!l || l->effects.empty()) return nullptr;
    EffectInstance& fx = l->effects.back();
    const ParameterRegistry* specs = rig.e.effects().params(fx.type);
    for (const Setting& s : c.values) {
        const u32 p = specs ? specs->find(s.id) : kInvalidIndex;
        AUREA_CHECK(p != kInvalidIndex && p < fx.params.size());
        if (p != kInvalidIndex && p < fx.params.size()) fx.params[p].constant = s.value;
    }
    return &fx;
}

/// Renderiza o caso como um projeto antigo: os parâmetros cortados na contagem
/// de antes, gravado e reaberto pelo Engine (o caminho da migração).
Image8 render_legacy(const Case& c, i32* algorithmAfterLoad) {
    Scene3DRig rig(320, 180, true);
    u64 text = 0, image = 0;
    build_scene(rig, text, image);
    for (u64 id : {text, image}) {
        if (EffectInstance* fx = add_fx(rig, id, c)) {
            if (fx->params.size() > c.savedCount) fx->params.resize(c.savedCount);
        }
    }
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                             "/aurea_light_legacy_" + c.file + ".aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    seek_frame(rig.e, 0);
    if (algorithmAfterLoad) {
        *algorithmAfterLoad = -1;
        const ParameterRegistry* specs = rig.e.effects().params(effect_type_id(c.key));
        const u32 p = specs ? specs->find("algorithm") : kInvalidIndex;
        const Layer* l = current(rig)->layer(LayerId::unpack(text));
        if (p != kInvalidIndex && l && !l->effects.empty() && p < l->effects.back().params.size()) {
            *algorithmAfterLoad = static_cast<i32>(std::lround(l->effects.back().params[p].constant.v[0]));
        }
    }
    Image8 out = rig.capture(320);
    std::remove(path.c_str());
    return out;
}

bool has_new_algorithm(const char* key) {
    Scene3DRig rig(16, 16);
    const ParameterRegistry* specs = rig.e.effects().params(effect_type_id(key));
    return specs && specs->find("algorithm") != kInvalidIndex;
}

} // namespace light_golden

AUREA_TEST(LightGlowGoldenGpu, LegacyInstancesMatchBuild2140Goldens) {
    AUREA_REQUIRE_GPU();
    using namespace light_golden;
    const bool write = std::getenv("AUREA_WRITE_LIGHT_GOLDENS") != nullptr;
    if (write) std::filesystem::create_directories(std::string(AUREA_TEST_DATA_DIR) + "/golden/light");
    for (const Case& c : cases()) {
        i32 algorithm = -1;
        const Image8 img = render_legacy(c, &algorithm);
        AUREA_CHECK_EQ(img.width, 320u);
        if (write) {
            if (img.rgba.empty()) continue;
            AUREA_CHECK(write_png(golden_path(c.file), img));
            std::printf("    golden %s escrita\n", c.file);
            continue;
        }
        Image8 golden;
        AUREA_CHECK(read_png(golden_path(c.file), golden));
        AUREA_CHECK_EQ(golden.width, img.width);
        AUREA_CHECK_EQ(golden.height, img.height);
        if (golden.rgba.size() != img.rgba.size()) continue;
        const u32 diff = max_diff(golden, img);
        std::printf("    %-18s legado: diff max %u/255, algoritmo=%d\n", c.file, diff, algorithm);
        AUREA_CHECK(diff <= 2u);
        // Instância salva sem o slot do algoritmo abre no desenho legado.
        if (has_new_algorithm(c.key)) AUREA_CHECK_EQ(algorithm, 0);
    }
}

AUREA_TEST(LightGlowGoldenGpu, NewInstancesUseOldAppLookAndBehave) {
    AUREA_REQUIRE_GPU();
    using namespace light_golden;
    if (!has_new_algorithm(effect_keys::kGlow)) return;
    for (const char* key : {effect_keys::kGlow, effect_keys::kDeepGlow}) {
        // Sem efeito: a referência do quadro e a caixa do texto.
        Scene3DRig rig(320, 180, true);
        u64 text = 0, image = 0;
        build_scene(rig, text, image);
        seek_frame(rig.e, 0);
        const Image8 plain = rig.capture(320);
        u32 x0 = plain.width, y0 = plain.height, x1 = 0, y1 = 0;
        for (u32 y = 0; y < plain.height; ++y) for (u32 x = 0; x < 180; ++x) {
            const u8* p = plain.at(x, y);
            if (std::max({p[0], p[1], p[2]}) > 32) {
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
        }
        AUREA_CHECK(x1 > x0);

        // Instância nova: algoritmo 1, olhar diferente do legado.
        const Case fresh{key, "", 0, {}};
        EffectInstance* fx = add_fx(rig, text, fresh);
        (void)add_fx(rig, image, fresh);
        AUREA_CHECK(fx != nullptr); if (!fx) return;
        const ParameterRegistry* specs = rig.e.effects().params(fx->type);
        const u32 algo = specs->find("algorithm");
        AUREA_CHECK(algo != kInvalidIndex);
        AUREA_CHECK_NEAR(fx->params[algo].constant.v[0], 1.0f, 1e-6f);
        AUREA_CHECK((specs->at(algo).flags & kParamHidden) != 0);
        seek_frame(rig.e, 0);
        const Image8 lit = rig.capture(320);
        if (const char* dump = std::getenv("AUREA_LIGHT_DUMP")) {
            (void)write_png(std::string(dump) + "/" + key + "_novo.png", lit);
        }
        Image8 golden;
        AUREA_CHECK(read_png(golden_path(std::string(key) == effect_keys::kGlow ? "glow_default" : "deep_glow_default"), golden));
        const u32 vsLegacy = max_diff(golden, lit);
        // Halo fora dos glifos e além da caixa do texto.
        u32 beyond = 0, finite = 0;
        for (u32 y = 0; y < lit.height; ++y) for (u32 x = 0; x < 180; ++x) {
            const u8* p = lit.at(x, y);
            const u8* b = plain.at(x, y);
            const bool outside = x + 4 < x0 || x > x1 + 4 || y + 4 < y0 || y > y1 + 4;
            if (outside && std::max({b[0], b[1], b[2]}) < 2 && std::max({p[0], p[1], p[2]}) > 6) ++beyond;
            finite += p[3] == 255;
        }
        std::printf("    %s novo: diff vs legado %u/255, halo além da caixa do texto=%u px\n", key, vsLegacy, beyond);
        AUREA_CHECK(vsLegacy > 8u);
        AUREA_CHECK(beyond > 150u);
        // O texto continua aceso (não some nem escurece).
        u32 brighter = 0, darker = 0;
        for (u32 y = y0; y <= y1; ++y) for (u32 x = x0; x <= x1; ++x) {
            const u8* p = lit.at(x, y); const u8* b = plain.at(x, y);
            const i32 lp = p[0] + p[1] + p[2], lb = b[0] + b[1] + b[2];
            brighter += lp > lb + 3; darker += lp + 6 < lb;
        }
        AUREA_CHECK(brighter > 50u);
        AUREA_CHECK_EQ(darker, 0u);

        // Intensidade 0 = identidade.
        const u32 amount = specs->find(std::string(key) == effect_keys::kGlow ? "intensity" : "glow_intensity");
        AUREA_CHECK(amount != kInvalidIndex);
        Composition* comp = current(rig);
        for (u64 id : {text, image}) {
            Layer* l = comp->layer(LayerId::unpack(id));
            if (l && !l->effects.empty()) l->effects.back().params[amount].constant = ParamValue::scalar(0);
        }
        seek_frame(rig.e, 0);
        const u32 identity = max_diff(plain, rig.capture(320));
        std::printf("    %s novo com intensidade 0: diff %u/255\n", key, identity);
        AUREA_CHECK(identity <= 1u);
    }
}

// Sem GPU: o slot "algorithm" ausente (instância salva) avalia 0; a instância
// nova nasce com 1, e o slot fica fora do card.
AUREA_TEST(LightGlowGoldenGpu, MissingAlgorithmSlotMeansLegacy) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    for (const char* key : {effect_keys::kGlow, effect_keys::kDeepGlow}) {
        const ParameterRegistry* specs = reg.params(effect_type_id(key));
        AUREA_CHECK(specs != nullptr); if (!specs) continue;
        const u32 a = specs->find("algorithm");
        if (a == kInvalidIndex) continue;   // antes do algoritmo novo
        EffectInstance fx;
        initialize_instance(fx, *specs);
        TrackSet tracks;
        AUREA_CHECK_NEAR(evaluate_param_f(tracks, fx, a, specs->at(a), 0.0, nullptr).v[0], 1.0f, 1e-6f);
        AUREA_CHECK((specs->at(a).flags & kParamHidden) != 0);
        fx.params.resize(a);
        AUREA_CHECK_NEAR(evaluate_param_f(tracks, fx, a, specs->at(a), 0.0, nullptr).v[0], 0.0f, 1e-6f);
    }
}
