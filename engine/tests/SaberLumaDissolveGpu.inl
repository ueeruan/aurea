// Saber (SaberEffect.cpp) e Luma Dissolve (LumaDissolveEffect.cpp): o núcleo
// único desenha um traço brilhante entre os pontos e nada fora do alcance do
// brilho; o deslocamento final desenha só a parte pedida; a composição
// transparente tem alfa 0 longe do traço; máscaras e alfa da camada viram
// núcleo; cintilação/distorção são determinísticas; camada de ajuste e
// salvar/abrir. Luma Dissolve: 0% = idêntico, 100% = alfa 0, 50% pela
// luminância some os escuros primeiro, borda, mapas e outra camada.
//
// Com AUREA_SABER_PNG_DIR=<pasta>, as amostras visuais são gravadas lá.
namespace {
const ParamSpec* saber_spec(const EffectInstance& fx, const char* id, u32& index) {
    const ParameterRegistry* reg = gpu().effects.params(fx.type);
    index = reg ? reg->find(id) : kInvalidIndex;
    return reg && index != kInvalidIndex ? &reg->at(index) : nullptr;
}
void fx_set(EffectInstance& fx, const char* id, f32 x, f32 y = 0.0f, f32 z = 0.0f, f32 w = 0.0f) {
    u32 i = 0;
    const ParamSpec* spec = saber_spec(fx, id, i);
    AUREA_CHECK_MSG(spec != nullptr, id);
    if (!spec) return;
    fx.params[i].constant.v[0] = x; fx.params[i].constant.v[1] = y;
    fx.params[i].constant.v[2] = z; fx.params[i].constant.v[3] = w;
}
void fx_ref(EffectInstance& fx, const char* id, LayerId layer) {
    u32 i = 0;
    AUREA_CHECK(saber_spec(fx, id, i) != nullptr);
    fx.params[i].constant.ref = layer.pack();
    fx.params[i].constant.v[0] = static_cast<f32>(layer.index);
}
void fx_apply_preset(EffectInstance& fx, u32 preset) {
    const Effect* e = gpu().effects.find(fx.type);
    AUREA_CHECK(e != nullptr && preset < e->presets().size());
    if (!e || preset >= e->presets().size()) return;
    for (const EffectPresetValue& v : e->presets()[preset].values) fx.params[v.param].constant.v[v.component] = v.value;
}
void saber_png(const char* name, const FloatImage& img) {
    if (const char* dir = std::getenv("AUREA_SABER_PNG_DIR"); dir && *dir)
        (void)write_png(std::string(dir) + "/" + name + ".png", img.encoded());
}
f32 sl_diff(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
bool sl_finite(const FloatImage& img) {
    for (const f32 v : img.px) if (!std::isfinite(v)) return false;
    return true;
}
/// Rampa horizontal em sRGB (preta à esquerda, branca à direita), opaca.
ImagePixels gray_ramp(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = static_cast<u8>(x * 255 / (w - 1));
        }
    return px;
}
Mask rect_mask(f32 x0, f32 y0, f32 x1, f32 y1) {
    Mask m;
    m.id = 1;
    m.operation = MaskOperation::None;   // só guia o Saber, não recorta
    m.closed = true;
    for (const Vec2 p : {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}}) m.points.push_back(MaskPoint{p, {}, {}});
    return m;
}
}

AUREA_TEST(SaberGpu, SingleCoreDrawsAGlowingStrokeBetweenThePointsAndNothingFarAway) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSaber);
    fx_set(fx, "composite", 1);                    // transparente: só o sabre
    fx_set(fx, "glow_spread", 20);                 // 2,5 oitavas: alcance ~36 px
    const auto img = s.render();
    saber_png("saber_transparente", img);
    AUREA_CHECK(sl_finite(img));
    // Traço de (51, 64) a (205, 64): o miolo é branco-quente e opaco.
    const Vec4 core = img.v(128, 64);
    std::printf("    miolo %.3f %.3f %.3f a=%.3f\n", core.x, core.y, core.z, core.w);
    AUREA_CHECK(core.w > 0.95f);
    AUREA_CHECK(std::min({core.x, core.y, core.z}) > 0.6f);
    // Logo acima do núcleo o brilho é azul (a cor padrão).
    const Vec4 halo = img.v(128, 55);
    std::printf("    halo %.3f %.3f %.3f a=%.3f\n", halo.x, halo.y, halo.z, halo.w);
    AUREA_CHECK(halo.w > 0.15f);
    AUREA_CHECK(halo.z > halo.x + 0.1f);
    // Longe do traço (além do alcance do brilho): alfa 0.
    AUREA_CHECK(img.v(128, 6).w < 0.01f);
    AUREA_CHECK(img.v(6, 64).w < 0.01f);
    AUREA_CHECK(img.v(250, 64).w < 0.01f);
    // Os pontos mandam: o traço vertical acende outra coluna.
    fx_set(fx, "core_start", 0.5f, 0.1f);
    fx_set(fx, "core_end", 0.5f, 0.9f);
    const auto vertical = s.render();
    AUREA_CHECK(vertical.v(128, 20).w > 0.95f);
    AUREA_CHECK(vertical.v(60, 64).w < 0.01f);
}

AUREA_TEST(SaberGpu, EndOffsetDrawsOnlyThatPartOfTheCore) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSaber);
    fx_set(fx, "composite", 1);
    fx_set(fx, "glow_spread", 10);
    auto lit = [](const FloatImage& img) {
        u32 n = 0;
        for (u32 x = 0; x < img.width; ++x) n += img.v(x, 64).w > 0.9f ? 1u : 0u;
        return n;
    };
    const u32 full = lit(s.render());
    fx_set(fx, "end_offset", 50);
    const auto half = s.render();
    saber_png("saber_metade", half);
    const u32 part = lit(half);
    std::printf("    pixels acesos: inteiro %u, 50%% %u\n", full, part);
    AUREA_CHECK(full > 140u);
    AUREA_CHECK(part > full * 4 / 10 && part < full * 6 / 10);
    AUREA_CHECK(half.v(90, 64).w > 0.9f);
    AUREA_CHECK(half.v(175, 64).w < 0.2f);
    // Deslocamento inicial 50% + final 100%: a outra metade.
    fx_set(fx, "start_offset", 50);
    fx_set(fx, "end_offset", 100);
    const auto other = s.render();
    AUREA_CHECK(other.v(90, 64).w < 0.2f);
    AUREA_CHECK(other.v(175, 64).w > 0.9f);
    // Final = inicial: nada acende (só a camada sumiu, composição transparente).
    fx_set(fx, "end_offset", 50);
    AUREA_CHECK(s.render().v(128, 64).w < 0.05f);
}

AUREA_TEST(SaberGpu, AddCompositeKeepsTheLayerAndBlackFillsTheBox) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kSaber);   // padrão: somar sobre a camada
    fx_set(fx, "glow_spread", 20);
    const auto added = s.render();
    // Longe do traço a camada está intacta; no traço, mais clara.
    AUREA_CHECK(cc_pixel_diff(added.v(128, 6), original.v(128, 6)) < 1e-3f);
    AUREA_CHECK(added.v(128, 64).y > original.v(128, 64).y + 0.2f || added.v(128, 64).y > 0.95f);
    AUREA_CHECK(added.v(10, 10).w > 0.99f);
    // Sobre preto: a caixa inteira opaca e preta longe do traço.
    fx_set(fx, "composite", 2);
    const auto black = s.render();
    const Vec4 far = black.v(128, 6);
    AUREA_CHECK(far.w > 0.99f && far.x < 0.01f && far.y < 0.01f && far.z < 0.01f);
    AUREA_CHECK(black.v(128, 64).w > 0.99f && black.v(128, 64).y > 0.6f);
    // Somar com brilho e núcleo a 0: sai da cadeia (camada exata).
    fx_set(fx, "composite", 0);
    fx_set(fx, "glow_intensity", 0);
    fx_set(fx, "core_intensity", 0);
    AUREA_CHECK(sl_diff(s.render(), original) < 1e-6f);
}

AUREA_TEST(SaberGpu, LayerMasksBecomeTheCoreAndEndOffsetWalksTheirPath) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    s.comp->layer(layer)->masks.push_back(rect_mask(64, 24, 192, 104));
    auto& fx = s.add_effect(layer, effect_keys::kSaber);
    fx_set(fx, "core_type", 1);
    fx_set(fx, "composite", 1);
    fx_set(fx, "glow_spread", 10);
    const auto img = s.render();
    saber_png("saber_mascara", img);
    AUREA_CHECK(sl_finite(img));
    for (const auto& [x, y] : {std::pair{128u, 24u}, std::pair{192u, 64u}, std::pair{128u, 104u}, std::pair{64u, 64u}})
        AUREA_CHECK_MSG(img.v(x, y).w > 0.9f, "borda da máscara acesa");
    AUREA_CHECK(img.v(128, 64).w < 0.05f);   // o meio do retângulo fica escuro
    AUREA_CHECK(img.v(10, 10).w < 0.01f);
    // Metade do perímetro (416 px): o topo (128) e a direita (80).
    fx_set(fx, "end_offset", 50);
    const auto half = s.render();
    AUREA_CHECK(half.v(128, 24).w > 0.9f);
    AUREA_CHECK(half.v(192, 60).w > 0.9f);
    AUREA_CHECK(half.v(128, 104).w < 0.1f);
    AUREA_CHECK(half.v(64, 64).w < 0.1f);
    // Evolução de meia volta: a janela anda para a outra metade.
    fx_set(fx, "mask_evolution", 50);
    const auto walked = s.render();
    AUREA_CHECK(walked.v(128, 24).w < 0.1f);
    AUREA_CHECK(walked.v(128, 104).w > 0.9f);
    // Sem máscara: nada para desenhar (transparente = vazio).
    s.comp->layer(layer)->masks.clear();
    AUREA_CHECK(s.render().v(128, 24).w < 0.01f);
}

AUREA_TEST(SaberGpu, LayerAlphaCoreOutlinesOrFillsTheShape) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto solid = s.solid(80, 60, Vec4{1, 1, 1, 1}, 128, 64);   // x 88..168, y 34..94
    auto& fx = s.add_effect(solid, effect_keys::kSaber);
    fx_set(fx, "core_type", 2);
    fx_set(fx, "composite", 1);
    fx_set(fx, "glow_spread", 10);
    const auto outline = s.render();
    saber_png("saber_contorno", outline);
    AUREA_CHECK(outline.v(88, 64).w > 0.9f);
    AUREA_CHECK(outline.v(128, 34).w > 0.9f);
    AUREA_CHECK(outline.v(128, 64).w < 0.05f);
    AUREA_CHECK(outline.v(10, 64).w < 0.01f);
    fx_set(fx, "use_alpha", 1);
    const auto filled = s.render();
    AUREA_CHECK(filled.v(128, 64).w > 0.95f);
    // Outra camada como núcleo: a forma escondida desenha o contorno numa
    // camada de fundo (o Saber do After Effects num sólido + camada de texto).
    Scene t(256, 128);
    t.comp->set_transparent_background(true);
    const auto back = t.solid(256, 128, Vec4{0, 0, 0, 1}, 128, 64);
    const auto shape = t.solid(80, 60, Vec4{1, 1, 1, 1}, 128, 64);
    t.comp->layer(shape)->visible = false;
    auto& other = t.add_effect(back, effect_keys::kSaber);
    fx_set(other, "core_type", 2);
    fx_ref(other, "core_layer", shape);
    fx_set(other, "glow_spread", 10);
    const auto ring = t.render();
    AUREA_CHECK(ring.v(88, 64).y > 0.6f);
    AUREA_CHECK(ring.v(128, 64).y < 0.05f);
}

AUREA_TEST(SaberGpu, TextLayerBecomesANeonOutline) {
    AUREA_REQUIRE_GPU();
    Scene s(320, 180);
    const auto id = s.comp->add_layer(LayerKind::Text, "neon");
    Layer* text = s.comp->layer(id);
    text->text.content = "AUREA";
    text->text.size = 72;
    text->transform.position = Vec3{160, 90, 0};
    const auto plain = s.render();
    auto& fx = s.add_effect(id, effect_keys::kSaber);
    fx_set(fx, "core_type", 2);
    fx_set(fx, "core_size", 4);
    fx_set(fx, "composite", 1);
    fx_apply_preset(fx, 2);   // neon
    const auto neon = s.render();
    saber_png("saber_texto_neon", neon);
    AUREA_CHECK(sl_finite(neon));
    u32 lit = 0, before = 0;
    for (u32 y = 0; y < neon.height; ++y)
        for (u32 x = 0; x < neon.width; ++x) {
            lit += neon.v(x, y).x > 0.5f ? 1u : 0u;
            before += plain.v(x, y).x > 0.5f ? 1u : 0u;
        }
    std::printf("    pixels acesos: texto %u, neon %u\n", before, lit);
    AUREA_CHECK(before > 200u);
    AUREA_CHECK(lit > before / 3);
    AUREA_CHECK(sl_diff(plain, neon) > 0.3f);
    // Usar alfa: o preenchimento das letras vira núcleo.
    fx_set(fx, "use_alpha", 1);
    AUREA_CHECK(sl_diff(neon, s.render()) > 0.1f);
}

AUREA_TEST(SaberGpu, FlickerAndDistortionAreDeterministicPerFrame) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSaber);
    fx_set(fx, "composite", 1);
    fx_set(fx, "flicker_intensity", 60);
    fx_set(fx, "distortion_type", 3);   // elétrica
    fx_set(fx, "distortion_amount", 10);
    const auto a = s.render(FrameIndex{5});
    const auto b = s.render(FrameIndex{5});
    const auto exported = s.render(FrameIndex{5}, 1, true);
    const auto next = s.render(FrameIndex{6});
    saber_png("saber_eletrico", a);
    AUREA_CHECK(sl_finite(a));
    AUREA_CHECK(sl_diff(a, b) < 1e-6f);
    AUREA_CHECK(sl_diff(a, exported) < 1e-6f);   // prévia e export: o mesmo quadro
    AUREA_CHECK(sl_diff(a, next) > 0.02f);       // o quadro seguinte anda
    // Prévia em meia resolução: o traço no mesmo lugar.
    fx_set(fx, "flicker_intensity", 0);
    fx_set(fx, "distortion_type", 0);
    const auto halfRes = s.render(FrameIndex{0}, 2);
    AUREA_CHECK(halfRes.v(64, 32).w > 0.9f);
    AUREA_CHECK(halfRes.v(64, 3).w < 0.02f);
}

AUREA_TEST(SaberGpu, AdjustmentLayerDrawsTheSaberOverWhatIsBelow) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    const Vec3 gray = lin3(0.3f, 0.3f, 0.3f);
    s.solid(256, 128, Vec4{0.3f, 0.3f, 0.3f, 1}, 128, 64);
    const LayerId adj = s.solid(16, 16, Vec4{1, 0, 1, 1}, 8, 8);
    s.comp->layer(adj)->adjustment = true;
    auto& fx = s.add_effect(adj, effect_keys::kSaber);
    fx_set(fx, "glow_spread", 10);
    const auto img = s.render();
    AUREA_CHECK(near4(img.v(128, 6), Vec4{gray.x, gray.y, gray.z, 1}, 0.004f));
    AUREA_CHECK(img.v(128, 64).y > 0.8f);
    AUREA_CHECK(near4(img.v(4, 4), Vec4{gray.x, gray.y, gray.z, 1}, 0.004f));   // o sólido da camada de ajuste não aparece
}

AUREA_TEST(SaberGpu, PresetsWriteTheWholeLookAndSaveOpenKeepsTheFrame) {
    AUREA_REQUIRE_GPU();
    const Effect* saber = gpu().effects.find(effect_type_id(effect_keys::kSaber));
    AUREA_CHECK(saber && saber->presets().size() == 9u);
    if (!saber) return;
    // Amostras: cada preset num sólido preto (o uso clássico do Saber).
    for (u32 i = 0; i < saber->presets().size(); ++i) {
        Scene s(320, 180);
        const auto back = s.solid(320, 180, Vec4{0, 0, 0, 1}, 160, 90);
        auto& fx = s.add_effect(back, effect_keys::kSaber);
        fx_apply_preset(fx, i);
        fx_set(fx, "core_start", 0.15f, 0.7f);
        fx_set(fx, "core_end", 0.85f, 0.3f);
        const auto img = s.render(FrameIndex{12});
        AUREA_CHECK_MSG(sl_finite(img), saber->presets()[i].id);
        // A distorção tira o núcleo da reta: o mais claro de uma coluna em volta do meio.
        f32 brightest = 0.0f;
        for (u32 y = 60; y < 120; ++y) brightest = std::max({brightest, img.v(160, y).x, img.v(160, y).y, img.v(160, y).z});
        AUREA_CHECK_MSG(img.v(160, 90).w > 0.99f && brightest > 0.6f, saber->presets()[i].id);
        saber_png((std::string("saber_preset_") + saber->presets()[i].id).c_str(), img);
    }
    // Salvar e abrir: os mesmos valores, o mesmo quadro (Saber com máscara e
    // Luma Dissolve com borda numa segunda camada).
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(256, 128), 128, 64);
    s.comp->layer(layer)->masks.push_back(rect_mask(40, 20, 210, 100));
    auto& fx = s.add_effect(layer, effect_keys::kSaber);
    fx_apply_preset(fx, 4);   // fogo
    fx_set(fx, "core_type", 1);
    fx_set(fx, "end_offset", 70);
    const auto other = s.image(gray_ramp(64, 64), 40, 40);
    auto& ld = s.add_effect(other, effect_keys::kLumaDissolve);
    fx_set(ld, "progress", 40);
    fx_set(ld, "edge_width", 10);
    const auto before = s.render(FrameIndex{9});
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(s.project, SaveOptions{}, bytes).ok());
    Project back;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}).ok());
    s.project = std::move(back);
    s.comp = s.project.timeline().composition(s.project.timeline().root());
    gpu().renderer.release_project_resources();
    const auto after = s.render(FrameIndex{9});
    std::printf("    salvar/abrir: maior diferenca %.6f\n", sl_diff(before, after));
    AUREA_CHECK(sl_diff(before, after) < 1e-5f);
}

AUREA_TEST(LumaDissolveGpu, EndpointsAreExactAndHalfwayRemovesTheDarksFirst) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(gray_ramp(128, 64), 64, 32);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kLumaDissolve);
    fx_set(fx, "progress", 0);
    AUREA_CHECK(sl_diff(s.render(), original) < 1e-6f);
    fx_set(fx, "progress", 100);
    const auto gone = s.render();
    f32 alpha = 0;
    for (u32 y = 0; y < gone.height; ++y) for (u32 x = 0; x < gone.width; ++x) alpha += gone.v(x, y).w;
    AUREA_CHECK(alpha < 1e-4f);
    // 50% pelo brilho da camada: a metade escura some, a clara fica.
    fx_set(fx, "progress", 50);
    fx_set(fx, "softness", 0);
    const auto half = s.render();
    saber_png("luma_dissolve_metade", half);
    AUREA_CHECK(half.v(20, 32).w < 0.01f);
    AUREA_CHECK(half.v(108, 32).w > 0.99f);
    AUREA_CHECK(cc_pixel_diff(half.v(108, 32), original.v(108, 32)) < 1e-3f);
    // Inverter: os claros primeiro.
    fx_set(fx, "invert", 1);
    const auto inverted = s.render();
    AUREA_CHECK(inverted.v(20, 32).w > 0.99f);
    AUREA_CHECK(inverted.v(108, 32).w < 0.01f);
    fx_set(fx, "invert", 0);
    // Aparecer: 100% = a camada inteira (sai da cadeia), 0% = vazia.
    fx_set(fx, "mode", 1);
    fx_set(fx, "progress", 100);
    AUREA_CHECK(sl_diff(s.render(), original) < 1e-6f);
    fx_set(fx, "progress", 0);
    AUREA_CHECK(s.render().v(108, 32).w < 0.01f);
    // Aparecer em 50%: os CLAROS aparecem primeiro (o inverso do sumir).
    fx_set(fx, "progress", 50);
    const auto appear = s.render();
    AUREA_CHECK(appear.v(108, 32).w > 0.99f && appear.v(20, 32).w < 0.01f);
}

AUREA_TEST(LumaDissolveGpu, EdgeColorMapsAndAnotherLayerDriveTheDissolve) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(gray_ramp(128, 64), 64, 32);
    auto& fx = s.add_effect(layer, effect_keys::kLumaDissolve);
    fx_set(fx, "progress", 50);
    fx_set(fx, "softness", 0);
    // Borda verde logo depois da frente.
    fx_set(fx, "edge_width", 15);
    fx_set(fx, "edge_color", 0, 1, 0, 1);
    const auto burn = s.render();
    saber_png("luma_dissolve_borda", burn);
    u32 green = 0;
    for (u32 x = 0; x < 128; ++x) { const Vec4 p = burn.v(x, 32); green += p.w > 0.9f && p.y > 0.8f && p.x < 0.3f ? 1u : 0u; }
    std::printf("    pixels da borda verde: %u\n", green);
    AUREA_CHECK(green >= 4u && green < 40u);
    fx_set(fx, "edge_width", 0);
    // Os mapas: ruído, degradê linear (0° = da esquerda) e radial mudam o quadro.
    const auto byLuma = s.render();
    for (const u32 kind : {1u, 2u, 3u}) {
        fx_set(fx, "map", static_cast<f32>(kind));
        const auto a = s.render(), b = s.render();
        AUREA_CHECK(sl_finite(a) && sl_diff(a, b) < 1e-6f);
        AUREA_CHECK(sl_diff(a, byLuma) > 0.1f || kind == 2u);
    }
    fx_set(fx, "map", 2);
    const auto linear = s.render();
    AUREA_CHECK(linear.v(20, 32).w < 0.01f && linear.v(108, 32).w > 0.99f);
    fx_set(fx, "angle", 180);
    const auto flipped = s.render();
    AUREA_CHECK(flipped.v(20, 32).w > 0.99f && flipped.v(108, 32).w < 0.01f);
    fx_set(fx, "map", 3);
    const auto radial = s.render();
    AUREA_CHECK(radial.v(64, 32).w < 0.01f && radial.v(2, 2).w > 0.99f);   // o centro some primeiro
    // Outra camada (escondida): uma camada branca some pelo degradê dela.
    Scene t(128, 64);
    t.comp->set_transparent_background(true);
    const auto map = t.image(gray_ramp(128, 64), 64, 32);
    t.comp->layer(map)->visible = false;
    const auto white = t.image(uniform_image(128, 64, 255, 255, 255), 64, 32);
    auto& other = t.add_effect(white, effect_keys::kLumaDissolve);
    fx_set(other, "map", 4);
    fx_ref(other, "map_layer", map);
    fx_set(other, "softness", 0);
    const auto viaLayer = t.render();
    AUREA_CHECK(viaLayer.v(20, 32).w < 0.01f && viaLayer.v(108, 32).w > 0.99f);
}

AUREA_TEST(LumaDissolveGpu, AdjustmentLayerDissolvesWhatIsBelow) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    s.comp->set_transparent_background(true);
    s.image(gray_ramp(128, 64), 64, 32);
    const LayerId adj = s.solid(8, 8, Vec4{1, 0, 1, 1}, 4, 4);
    s.comp->layer(adj)->adjustment = true;
    auto& fx = s.add_effect(adj, effect_keys::kLumaDissolve);
    fx_set(fx, "softness", 0);
    const auto img = s.render();
    AUREA_CHECK(img.v(20, 32).w < 0.01f);
    AUREA_CHECK(img.v(108, 32).w > 0.99f);
    fx_set(fx, "progress", 100);
    const auto gone = s.render();
    AUREA_CHECK(gone.v(108, 32).w < 0.01f);
}
