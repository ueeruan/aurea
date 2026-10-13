// =============================================================================
//  CAMADA DE AJUSTE (beta 0.1.8 BETA 2): todo efeito do catálogo age sobre a
//  composição das camadas abaixo, no escopo da camada de ajuste — como no After
//  Effects e no Alight Motion. Pedido dos testers: "a lot of effects don't work
//  with adjustment layer like certain shakes and motion detect effects".
//
//  A foto de baixo SE MOVE (posição animada): os efeitos de tempo (Detectar
//  movimento, Eco, RGB no tempo, Datamosh...) só aparecem se o passado da
//  composição abaixo for diferente do presente.
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
namespace adjfx {

/// Foto de referência com a posição X animada 40 → 56 px nos quadros 0..30.
/// Escala 1,25: cobre o quadro 96×96 durante todo o movimento.
inline LayerId moving_photo(Scene& s, u32 size, f32 scale, bool transparentCorner, f32 cx = 48.0f) {
    auto pixels = reference_image(size, size);
    if (transparentCorner) {
        for (u32 y = 0; y < size / 2; ++y) for (u32 x = 0; x < size / 2; ++x)
            pixels.rgba[(static_cast<usize>(y) * size + x) * 4 + 3] = 0;
    }
    const LayerId id = s.image(std::move(pixels), cx, 48, scale);
    Track& px = s.comp->layer(id)->tracks.get_or_create(TrackProperty::PositionX);
    px.set(FrameIndex{0}, cx - 8.0f);
    px.set(FrameIndex{30}, cx + 8.0f);
    return id;
}

/// Efeitos que não existem numa camada de ajuste, com o motivo.
inline const char* not_for_adjustment(const Effect& fx) {
    const char* key = fx.info().key;
    const EffectTypeId t = fx.type_id();
    if (std::strncmp(key, "aurea.control.", 14) == 0) return "controle de expressao: nao desenha";
    if (t == effect_type_id(effect_keys::kText3DLayout) || t == effect_type_id(text::kTransformEffect)
        || t == effect_type_id(text::kAnimatorEffect)) return "so em camada de texto";
    if (t == effect_type_id(effect_keys::kShape3DLayout) || t == effect_type_id(effect_keys::kPart3DTransform)
        || t == effect_type_id(effect_keys::kPart3DHide) || t == effect_type_id(effect_keys::kPart3DTexture))
        return "so em camada 3D";
    if (std::strcmp(key, "aurea.layout.grid_builder") == 0 || std::strcmp(key, "aurea.layout.grid_item") == 0)
        return "metadado de grade";
    if (t == effect_type_id(effect_keys::kSceneFlare)) return "so em luz 3D";
    // A segmentação por IA recorta o SUJEITO da própria fonte; a camada de
    // ajuste não tem fonte para o modelo (o recorte é o da camada de vídeo).
    if (t == effect_type_id(effect_keys::kRotobrush)) return "segmentacao da propria fonte";
    // Prende a camada a outra (pai): a camada de ajuste não tem pixels próprios
    // para levar junto — os efeitos dela já valem para o quadro inteiro.
    if (t == effect_type_id(effect_keys::kParentingHelper)) return "vinculo de camada";
    return nullptr;
}

/// Os mesmos ajustes do Gpu.EveryCatalogEffectChangesTheProjectFrame.
inline void configure(const Effect& fx, const ParameterRegistry& params, EffectInstance& e) {
    const char* key = fx.info().key;
    initialize_instance(e, params);
    std::vector<ParamValue> values(params.count());
    for (u32 p = 0; p < params.count(); ++p) values[p] = e.params[p].constant;
    if (fx.demo_values(e, values)) {
        for (u32 p = 0; p < params.count(); ++p) e.params[p].constant = values[p];
    }
    if (fx.type_id() == effect_type_id(effect_keys::kCornerPin)) e.params[0].constant = ParamValue::scalar(20.f);
    if (fx.type_id() == effect_type_id(effect_keys::kCubeLut)) e.params[0].constant.ref = AssetId{12, 1}.pack();
    if (std::strncmp(key, "aurea.repeat.", 13) == 0) {
        e.params[0].constant = ParamValue::scalar(4);
        e.params[1].constant = ParamValue::vec2(22, 22);
        e.params[5].constant = ParamValue::scalar(2);
        e.params[6].constant = ParamValue::scalar(26);
    }
    if (std::strcmp(key, "aurea.key.solid_matte") == 0) e.params[0].constant = ParamValue::color(0, 0, 1, 1);
    // A cor-chave padrão (verde puro) só existe no canto da foto, que a
    // escala/movimento tira do quadro: recorta o disco branco.
    if (std::strcmp(key, "aurea.key.chroma_basic") == 0) e.params[0].constant = ParamValue::color(1, 1, 1, 1);
    if (std::strcmp(key, "aurea.transform.offset") == 0) e.params[0].constant = ParamValue::vec2(17, 11);
}

inline u32 adj_changed(const FloatImage& a, const FloatImage& b) {
    u32 changed = 0;
    for (usize k = 0; k < a.px.size() && k < b.px.size(); ++k) changed += std::fabs(a.px[k] - b.px[k]) > 0.004f ? 1u : 0u;
    return changed;
}

/// Efeito que lê OUTROS instantes da entrada (o passado, um tempo travado ou
/// remapeado): numa camada de ajuste a entrada é a composição abaixo.
inline bool needs_past(const Effect& fx) {
    return fx.effect_class() == EffectClass::Temporal || fx.wants_history()
        || std::strcmp(fx.info().key, "aurea.time.low_fps") == 0;
}

/// Efeito que se move com o tempo (só aparece num quadro adiante).
inline bool time_based(const Effect& fx) {
    const char* key = fx.info().key;
    return needs_past(fx) || std::strcmp(key, "aurea.motion.blink") == 0 || std::strcmp(key, "aurea.motion.swing_range") == 0;
}

/// Efeitos que desenham FORA da caixa da camada (sombra, borda) ou nos pixels
/// transparentes dela: precisam de fundo exposto para aparecer.
inline bool needs_exposed_backdrop(const Effect& fx) {
    const EffectTypeId t = fx.type_id();
    return t == effect_type_id(effect_keys::kShadowStudio3) || t == effect_type_id(effect_keys::kDropShadow)
        || t == effect_type_id(effect_keys::kBorder) || std::strcmp(fx.info().key, "aurea.key.solid_matte") == 0;
}

struct AdjustmentCase {
    u8 scope = 0;               ///< 0 todas abaixo, 1 só a de baixo, 2 alvos
    FrameIndex frame{0};
    bool finalQuality = false;
    /// false: a foto PARADA onde a móvel estaria neste quadro (o presente
    /// igual, sem passado diferente).
    bool moving = true;
};

/// Grava as amostras (AUREA_ADJ_DUMP=pasta) para olhar.
inline void dump(const char* name, const FloatImage& img) {
    const char* dir = std::getenv("AUREA_ADJ_DUMP");
    if (!dir || !*dir) return;
    std::string file = name;
    for (char& ch : file) if (ch == '.' || ch == ' ' || ch == '(' || ch == ')' || ch == ',') ch = '_';
    (void)write_png(std::string(dir) + "/" + file + ".png", img.encoded());
}

/// Renderiza a foto em movimento com a camada de ajuste por cima (o efeito
/// ligado ou desligado) e devolve quantos pixels o efeito mudou.
inline u32 adjustment_change(const Effect& fx, const ParameterRegistry& params, const AdjustmentCase& c,
                             FloatImage* withOut = nullptr, FloatImage* plainOut = nullptr) {
    const bool exposed = needs_exposed_backdrop(fx);
    Scene s(96, 96);
    if (exposed) s.comp->set_background(Color{1, 1, 1, 1});
    const bool solidMatte = std::strcmp(fx.info().key, "aurea.key.solid_matte") == 0;
    const LayerId photo = moving_photo(s, exposed ? 40u : 96u, exposed ? 1.0f : 1.25f, solidMatte);
    if (!c.moving) {
        // Parada exatamente onde a móvel está neste quadro.
        Layer* p = s.comp->layer(photo);
        const f32 x = p->tracks.get_or_create(TrackProperty::PositionX).sample(c.frame);
        p->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::PositionX; });
        p->transform.position.x = x;
    }
    if (std::strcmp(fx.info().key, "aurea.repeat.path") == 0 || std::strcmp(fx.info().key, "aurea.move.path") == 0) {
        // O caminho do Repeat/Move Along Path é a camada logo abaixo: um retângulo
        // oculto (só guia, não desenha).
        const LayerId guide = s.solid(60, 60, Vec4{0, 0, 0, 1}, 48, 48);
        s.comp->layer(guide)->visible = false;
    }
    const LayerId adj = s.solid(96, 96, Vec4{1, 0, 1, 1}, 48, 48);
    Layer* a = s.comp->layer(adj);
    a->adjustment = true;
    a->adjustmentScope = c.scope;
    if (c.scope == 2) a->adjustmentTargets = {photo};
    EffectInstance e;
    e.id = a->alloc_effect_id();
    e.type = fx.type_id();
    configure(fx, params, e);
    a->effects.push_back(std::move(e));
    if (fx.type_id() == effect_type_id(effect_keys::kTimeRemap)) {
        // A curva do Remapear tempo é a da camada: congela no quadro 0.
        a->timeRemap.property = TrackProperty::TimeRemap;
        a->timeRemap.set(FrameIndex{0}, 0.0f);
        a->timeRemap.set(FrameIndex{30}, 0.0f);
        a->timeRemapEnabled = true;
    }
    std::shared_ptr<CubeLut> cube;
    if (fx.type_id() == effect_type_id(effect_keys::kCubeLut)) {
        cube = std::make_shared<CubeLut>(); cube->size = 2; cube->dimensions = 3;
        for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r)
            cube->values.push_back({static_cast<f32>(b), static_cast<f32>(g), static_cast<f32>(r), 1});
        gpu().renderer.set_cube_lookup([](void* data, AssetId) -> std::shared_ptr<const CubeLut> {
            return *static_cast<std::shared_ptr<CubeLut>*>(data);
        }, &cube);
    }
    const bool localAi = std::strncmp(fx.info().key, "aurea.ai.", 9) == 0;
    s.comp->layer(adj)->effects[0].enabled = false;
    const FloatImage plain = s.render(c.frame, 1, c.finalQuality || localAi);
    s.comp->layer(adj)->effects[0].enabled = true;
    const FloatImage with = s.render(c.frame, 1, c.finalQuality || localAi);
    if (cube) gpu().renderer.set_cube_lookup(nullptr, nullptr);
    {
        const std::string base = std::string("adj_") + fx.info().key + "_s" + std::to_string(c.scope) + "_t" +
                                  std::to_string(c.frame.value) + (c.moving ? "" : "_parada");
        dump((base + "_sem").c_str(), plain);
        dump((base + "_com").c_str(), with);
    }
    if (withOut) *withOut = with;
    if (plainOut) *plainOut = plain;
    for (const f32 v : with.px) if (!std::isfinite(v)) return 0;
    return adj_changed(with, plain);
}

} // namespace adjfx

AUREA_TEST(Gpu, EveryCatalogEffectChangesTheFrameOnAnAdjustmentLayer) {
    AUREA_REQUIRE_GPU();
    std::vector<std::string> inert;
    const char* only = std::getenv("AUREA_ADJ_ONLY");   // depuração: um efeito
    for (u32 i = 0; i < gpu().effects.count(); ++i) {
        const Effect& fx = gpu().effects.at(i);
        const char* key = fx.info().key;
        if (only && *only && std::strcmp(only, key) != 0) continue;
        if (adjfx::not_for_adjustment(fx)) continue;
        const ParameterRegistry& params = gpu().effects.params_at(i);
        const bool audio = std::strncmp(key, "aurea.audio.", 12) == 0;
        const bool timed = adjfx::time_based(fx);
        // Escopos: todas abaixo (0) e só a de baixo (1). Quadros: 0 e 12 (o
        // passado da foto em movimento existe e é diferente do presente).
        for (u8 scope : {u8{0}, u8{1}}) {
            for (i64 t : {i64{0}, i64{12}}) {
                if (timed && t == 0) continue;   // sem passado no primeiro quadro
                adjfx::AdjustmentCase c;
                c.scope = scope;
                c.frame = FrameIndex{t};
                FloatImage with;
                const u32 changed = adjfx::adjustment_change(fx, params, c, &with);
                const std::string where = std::string(key) + " (escopo " + std::to_string(scope) + ", quadro " +
                                          std::to_string(t) + ")";
                if (audio) {
                    AUREA_CHECK_EQ(changed, 0u);
                } else if (changed == 0) {
                    inert.emplace_back(where);
                } else if (t > 0 && adjfx::needs_past(fx)) {
                    // O passado tem de ser o da composição abaixo: com a foto
                    // PARADA no mesmo lugar, o resultado é outro.
                    adjfx::AdjustmentCase still = c;
                    still.moving = false;
                    FloatImage stillWith;
                    (void)adjfx::adjustment_change(fx, params, still, &stillWith);
                    if (adjfx::adj_changed(with, stillWith) == 0) inert.emplace_back(where + " sem passado");
                }
            }
        }
    }
    for (const std::string& k : inert) std::printf("\n    efeito sem efeito na camada de ajuste: %s", k.c_str());
    AUREA_CHECK_MSG(inert.empty(), "efeito do catalogo numa camada de ajuste nao muda o quadro");
}
// Prévia, export (qualidade final) e scrub dão o MESMO quadro: o passado da
// composição abaixo é re-renderizado no instante pedido, não guardado de um
// quadro anterior da reprodução. E o export de verdade (Engine + sink) leva
// o Detectar movimento e o Agitar de uma camada de ajuste sobre um vídeo.
AUREA_TEST(Gpu, AdjustmentLayerTimeEffectsMatchPreviewExportAndScrub) {
    AUREA_REQUIRE_GPU();
    const char* keys[] = {effect_keys::kMotionDetect, effect_keys::kEchoTrail, effect_keys::kWiggle, effect_keys::kTimeWarpRgb,
                          effect_keys::kPosterizeTime, effect_keys::kDatamosh, effect_keys::kDropShadow, "aurea.transform"};
    for (const char* key : keys) {
        const EffectTypeId type = effect_type_id(key);
        u32 index = ~0u;
        for (u32 i = 0; i < gpu().effects.count(); ++i) if (gpu().effects.at(i).type_id() == type) index = i;
        AUREA_CHECK_MSG(index != ~0u, key);
        if (index == ~0u) continue;
        const Effect& fx = gpu().effects.at(index);
        const ParameterRegistry& params = gpu().effects.params_at(index);
        for (u8 scope : {u8{0}, u8{1}}) {
            Scene s(96, 96);
            const bool exposed = adjfx::needs_exposed_backdrop(fx);
            if (exposed) s.comp->set_background(Color{1, 1, 1, 1});
            (void)adjfx::moving_photo(s, exposed ? 40u : 96u, exposed ? 1.0f : 1.25f, false);
            const LayerId adj = s.solid(96, 96, Vec4{1, 0, 1, 1}, 48, 48);
            Layer* a = s.comp->layer(adj);
            a->adjustment = true;
            a->adjustmentScope = scope;
            EffectInstance e;
            e.id = a->alloc_effect_id();
            e.type = type;
            adjfx::configure(fx, params, e);
            a->effects.push_back(std::move(e));
            const FloatImage first = s.render(FrameIndex{12});
            (void)s.render(FrameIndex{5});            // scrub para trás
            (void)s.render(FrameIndex{20});           // e para a frente
            const FloatImage again = s.render(FrameIndex{12});
            const FloatImage exported = s.render(FrameIndex{12}, 1, true);
            f32 scrub = 0.0f, final = 0.0f;
            for (usize k = 0; k < first.px.size() && k < again.px.size() && k < exported.px.size(); ++k) {
                scrub = std::max(scrub, std::fabs(first.px[k] - again.px[k]));
                final = std::max(final, std::fabs(first.px[k] - exported.px[k]));
            }
            std::printf("\n    %s escopo %u: scrub %.5f, export %.5f", key, scope, scrub, final);
            AUREA_CHECK_MSG(scrub <= 1e-4f, key);
            AUREA_CHECK_MSG(final <= 2.0f / 255.0f, key);
        }
    }

    // Export real: vídeo com um quadrado andando e um ajuste por cima.
    for (const char* key : {effect_keys::kMotionDetect, effect_keys::kWiggle}) {
        f64 meanY[2] = {0, 0};
        u32 bright[2] = {0, 0};
        std::vector<u8> frames[2];
        for (int withEffect = 0; withEffect < 2; ++withEffect) {
            SyntheticConfig cfg;
            cfg.width = 128; cfg.height = 72; cfg.frameCount = 60;
            cfg.pattern = SyntheticPattern::MovingSquare;
            ExportRig rig(cfg, 0);
            set_duration(rig.e, 4);
            const Result<u64> shape = rig.e.add_shape(0);
            AUREA_CHECK(shape.ok());
            if (!shape.ok()) return;
            AUREA_CHECK(rig.e.set_layer_adjustment(*shape, true));
            if (withEffect) {
                Command add;
                add.type = CommandType::EffectAdd;
                add.effect_add.layer = LayerId::unpack(*shape);
                add.effect_add.effectType = effect_type_id(key);
                add.effect_add.index = kInvalidIndex;
                AUREA_CHECK(rig.e.apply_command(add).ok());
            }
            ExportSettings settings; settings.height = 72; settings.fps = 0; settings.dither = false;
            AUREA_CHECK(rig.e.start_export(settings, "unused-adjustment.mp4").ok());
            AUREA_CHECK(wait_export(rig.e, 60000));
            AUREA_CHECK_EQ(rig.e.export_progress().result, Errc::Ok);
            AUREA_CHECK_EQ(rig.cap.y.size(), usize{4});
            if (rig.cap.y.size() < 4) return;
            frames[withEffect] = rig.cap.y[3];
            for (u8 v : rig.cap.y[3]) { meanY[withEffect] += v; bright[withEffect] += v > 60 ? 1u : 0u; }
            meanY[withEffect] /= std::max<usize>(1, rig.cap.y[3].size());
        }
        usize differ = 0;
        for (usize k = 0; k < frames[0].size() && k < frames[1].size(); ++k) differ += std::abs(int(frames[0][k]) - int(frames[1][k])) > 6;
        std::printf("\n    export %s: Y medio %.1f -> %.1f, claros %u -> %u, diferentes %zu", key, meanY[0], meanY[1],
                    bright[0], bright[1], differ);
        AUREA_CHECK_MSG(differ > 50, key);
        if (std::strcmp(key, effect_keys::kMotionDetect) == 0) {
            // Só o que se MOVE acende: o quadro escurece, mas não some.
            AUREA_CHECK(meanY[1] < meanY[0] * 0.8);
            AUREA_CHECK(bright[1] > 0);
        }
    }
}
#endif // AUREA_TEST_VULKAN
