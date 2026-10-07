// =============================================================================
//  Beta 2140: efeitos de TEMPO numa pré-composição. O vídeo busca o passado no
//  decoder; a pré-composição precisa da FILHA renderizada nos outros instantes.
//  Antes: o eco somava N cópias do quadro de agora (estourava), o Detectar
//  movimento dava preto e o RGB no tempo juntava três canais iguais.
//  (Incluído no fim de test_gpu.cpp: usa Scene, gpu(), max_diff e
//  compare_motion de lá.)
// =============================================================================
namespace {

constexpr u32 kPtW = 128, kPtH = 72;

/// Quadrado branco andando 2 px/quadro, direto na raiz ou dentro de uma
/// pré-composição do mesmo tamanho (parada, sem transformação). `opaque`:
/// a filha tem fundo cinza (uma "fonte" opaca, como um vídeo).
LayerId precomp_temporal_square(Scene& s, bool precomp, bool opaque) {
    Composition* host = s.comp;
    CompositionId childId{};
    if (precomp) {
        childId = s.project.timeline().create_composition("pre-tempo", kPtW, kPtH, 30.0);
        host = s.project.timeline().composition(childId);
        host->set_duration(FrameIndex{300});
        host->set_background(opaque ? Color{0.2f, 0.2f, 0.2f, 1} : Color{0, 0, 0, 0});
        host->set_transparent_background(!opaque);
    }
    const LayerId sq = host->add_layer(LayerKind::Shape, "quadrado");
    Layer* l = host->layer(sq);
    l->shape.bounds = Rect{0, 0, 12, 12};
    l->shape.fillColor = Vec4{1, 1, 1, 1};
    l->transform.anchor = Vec3{6, 6, 0};
    l->transform.position = Vec3{10, 36, 0};
    Track& x = l->tracks.get_or_create(TrackProperty::PositionX);
    (void)x.set(FrameIndex{0}, 10.0f, Interpolation::Linear);
    (void)x.set(FrameIndex{60}, 130.0f, Interpolation::Linear);
    if (!precomp) return sq;
    const LayerId n = s.comp->add_layer(LayerKind::Composition, "pre-tempo");
    Layer* nl = s.comp->layer(n);
    nl->nested.composition = childId;
    nl->transform.anchor = Vec3{kPtW * 0.5f, kPtH * 0.5f, 0};
    nl->transform.position = Vec3{kPtW * 0.5f, kPtH * 0.5f, 0};
    return n;
}

f32 brightest(const FloatImage& img) {
    f32 m = 0.0f;
    for (usize k = 0; k < img.px.size(); k += 4) m = std::fmax(m, std::fmax(img.px[k], std::fmax(img.px[k + 1], img.px[k + 2])));
    return m;
}

} // namespace

AUREA_TEST(Gpu, EchoOnPrecompMatchesTheSameMotionOutside) {
    AUREA_REQUIRE_GPU();
    FloatImage direct, nested, plain;
    {
        Scene s(kPtW, kPtH, 30.0);
        const LayerId id = precomp_temporal_square(s, false, false);
        EffectInstance& e = s.add_effect(id, effect_keys::kEchoTrail);
        e.params[0].constant = ParamValue::scalar(4.0f);   // cópias
        e.params[1].constant = ParamValue::scalar(3.0f);   // intervalo (quadros)
        e.params[2].constant = ParamValue::scalar(0.6f);   // decaimento
        direct = s.render(FrameIndex{30});
    }
    {
        Scene s(kPtW, kPtH, 30.0);
        const LayerId id = precomp_temporal_square(s, true, false);
        plain = s.render(FrameIndex{30});
        EffectInstance& e = s.add_effect(id, effect_keys::kEchoTrail);
        e.params[0].constant = ParamValue::scalar(4.0f);
        e.params[1].constant = ParamValue::scalar(3.0f);
        e.params[2].constant = ParamValue::scalar(0.6f);
        nested = s.render(FrameIndex{30});
        // O export (qualidade final) espera as filhas e dá o mesmo.
        const FloatImage exported = s.render(FrameIndex{30}, 1, true);
        AUREA_CHECK_MSG(max_diff(exported, nested) < 0.02f, "export do eco na pre-composicao difere da previa");
    }
    const f32 d = max_diff(direct, nested);
    std::printf("    eco: direto x pre-composicao %.4f; brilho %.3f x %.3f; sem efeito x com %.3f\n",
                d, brightest(direct), brightest(nested), max_diff(plain, nested));
    AUREA_CHECK_MSG(d < 0.03f, "o eco na pre-composicao devia mostrar o rastro do conteudo");
    AUREA_CHECK_MSG(max_diff(plain, nested) > 0.2f, "o eco na pre-composicao nao apareceu");
    // Cópias que se sobrepõem somam (1 + 0,6 no quadrado de 12 px andando 6 px
    // por cópia) — fora da pré-composição também. Antes: as 5 cópias caíam no
    // MESMO lugar e somavam ~2,3.
    AUREA_CHECK_MSG(brightest(nested) < brightest(direct) + 0.01f, "o eco na pre-composicao estourou o brilho");
}

AUREA_TEST(Gpu, MotionDetectOnPrecompComparesWithThePastContent) {
    AUREA_REQUIRE_GPU();
    Scene s(kPtW, kPtH, 30.0);
    const LayerId id = precomp_temporal_square(s, true, true);
    const FloatImage f30 = s.render(FrameIndex{30});
    const FloatImage f25 = s.render(FrameIndex{25});
    EffectInstance& e = s.add_effect(id, effect_keys::kMotionDetect);
    e.params[0].constant = ParamValue::scalar(5.0f);
    const FloatImage got = s.render(FrameIndex{30});
    const MotionRef r = compare_motion(got, f30, f25, 0, 1.0f);
    std::printf("    detectar movimento (pre-composicao): erro %.4f, movido %.3f, parado %.4f\n", r.worst, r.brightMoved, r.brightStill);
    AUREA_CHECK_MSG(r.worst < 0.03f, "a diferenca devia ser a do conteudo 5 quadros antes");
    AUREA_CHECK_MSG(r.brightMoved > 0.3f, "onde o quadrado andou devia acender (antes: tela preta)");
    AUREA_CHECK_MSG(r.brightStill < 0.01f, "o fundo parado devia ficar preto");
    const FloatImage exported = s.render(FrameIndex{30}, 1, true);
    AUREA_CHECK_MSG(compare_motion(exported, f30, f25, 0, 1.0f).worst < 0.03f, "o export devia ver o mesmo movimento");
}

AUREA_TEST(Gpu, TimeWarpRgbOnPrecompTakesEachChannelFromItsInstant) {
    AUREA_REQUIRE_GPU();
    Scene s(kPtW, kPtH, 30.0);
    const LayerId id = precomp_temporal_square(s, true, true);
    const FloatImage f33 = s.render(FrameIndex{33});
    const FloatImage f30 = s.render(FrameIndex{30});
    const FloatImage f27 = s.render(FrameIndex{27});
    EffectInstance& e = s.add_effect(id, effect_keys::kTimeWarpRgb);
    e.params[0].constant = ParamValue::scalar(3.0f);    // vermelho: 3 quadros adiante
    e.params[1].constant = ParamValue::scalar(0.0f);
    e.params[2].constant = ParamValue::scalar(-3.0f);   // azul: 3 quadros atrás
    const FloatImage got = s.render(FrameIndex{30});
    f32 rErr = 0, gErr = 0, bErr = 0;
    for (usize k = 0; k < got.px.size(); k += 4) {
        rErr = std::fmax(rErr, std::fabs(got.px[k] - f33.px[k]));
        gErr = std::fmax(gErr, std::fabs(got.px[k + 1] - f30.px[k + 1]));
        bErr = std::fmax(bErr, std::fabs(got.px[k + 2] - f27.px[k + 2]));
    }
    std::printf("    RGB no tempo (pre-composicao): erro R %.4f G %.4f B %.4f; sem efeito x com %.3f\n",
                rErr, gErr, bErr, max_diff(got, f30));
    AUREA_CHECK_MSG(rErr < 0.02f, "o vermelho devia vir do quadro 33");
    AUREA_CHECK_MSG(gErr < 0.02f, "o verde devia ficar no quadro 30");
    AUREA_CHECK_MSG(bErr < 0.02f, "o azul devia vir do quadro 27");
    AUREA_CHECK_MSG(max_diff(got, f30) > 0.5f, "os fantasmas coloridos nao apareceram");
}
