// =============================================================================
//  Pedidos de usuários da 0.4.5 (distorção, ladrilhos e tempo): Tile Shift,
//  Hexagon Tile Rotate/Shift, Hollow Box, Time Displacement, o Mirror do Alight
//  Motion (dois eixos, sobrepor cópia) e o modo do Desfoque de zoom (o "Zoom"
//  do CC Radial Fast Blur). Propriedades da implementação própria — sem
//  paridade numérica com os apps de referência. Filtro: `Distort045`.
// =============================================================================

namespace {
constexpr const char* kD045TileShift = "aurea.distort.tile_shift";
constexpr const char* kD045HexRotate = "aurea.distort.hex_tile_rotate";
constexpr const char* kD045HexShift = "aurea.distort.hex_tile_shift";
constexpr const char* kD045HollowBox = "aurea.distort.hollow_box";
constexpr const char* kD045TimeDisplace = "aurea.time.time_displacement";

/// Maior diferença por canal entre duas imagens do mesmo tamanho.
f32 d045_max_diff(const FloatImage& a, const FloatImage& b) {
    f32 m = 0.0f;
    for (usize i = 0; i < a.px.size() && i < b.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return m;
}

/// Fração de pixels com alguma diferença visível.
f32 d045_changed(const FloatImage& a, const FloatImage& b) {
    u32 changed = 0, total = 0;
    for (usize i = 0; i + 3 < a.px.size() && i + 3 < b.px.size(); i += 4) {
        ++total;
        bool c = false;
        for (int k = 0; k < 4; ++k) c |= std::fabs(a.px[i + k] - b.px[i + k]) > 0.01f;
        changed += c ? 1u : 0u;
    }
    return total ? static_cast<f32>(changed) / static_cast<f32>(total) : 0.0f;
}

bool d045_near(Vec4 a, Vec4 b, f32 tol) {
    return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol
        && std::fabs(a.w - b.w) <= tol;
}
} // namespace

AUREA_TEST(Distort045, NewEffectsAndNewParametersKeepStableIndices) {
    AUREA_REQUIRE_GPU();
    struct Want { const char* key; const char* category; EffectClass cls; u32 params; };
    for (const Want w : {Want{kD045TileShift, "Distorcer", EffectClass::Domain, 9},
                         Want{kD045HexRotate, "Distorcer", EffectClass::Domain, 9},
                         Want{kD045HexShift, "Distorcer", EffectClass::Domain, 9},
                         Want{kD045HollowBox, "Distorcer", EffectClass::Domain, 30},
                         Want{kD045TimeDisplace, "Tempo", EffectClass::Temporal, 6}}) {
        const Effect* fx = gpu().effects.find(effect_type_id(w.key));
        const ParameterRegistry* p = gpu().effects.params(effect_type_id(w.key));
        AUREA_CHECK_MSG(fx != nullptr && p != nullptr, w.key);
        if (!fx || !p) continue;
        AUREA_CHECK(std::strcmp(fx->info().category, w.category) == 0);
        AUREA_CHECK(fx->effect_class() == w.cls);
        AUREA_CHECK_MSG(p->count() == w.params, w.key);
    }
    // Hexagon Tile Rotate gira (ângulo); os outros dois deslocam (% da célula).
    AUREA_CHECK(std::strcmp(gpu().effects.params(effect_type_id(kD045HexRotate))->at(1).id, "rotation") == 0);
    AUREA_CHECK(std::strcmp(gpu().effects.params(effect_type_id(kD045HexShift))->at(1).id, "amount") == 0);
    // Espelho: os três de sempre nos mesmos índices, os do Alight Motion no fim.
    const ParameterRegistry* mirror = gpu().effects.params(effect_type_id(effect_keys::kMirror));
    AUREA_CHECK(mirror && mirror->count() == 7);
    if (mirror && mirror->count() == 7) {
        const char* ids[] = {"center", "angle", "flip_side", "both_axes", "mode", "copy_opacity", "copy_behind"};
        for (u32 i = 0; i < 7; ++i) AUREA_CHECK_MSG(std::strcmp(mirror->at(i).id, ids[i]) == 0, ids[i]);
    }
    // Desfoque de zoom: o modo do CC Radial Fast Blur entrou no fim (3 opções).
    const ParameterRegistry* zoom = gpu().effects.params(effect_type_id(effect_keys::kZoomBlur));
    AUREA_CHECK(zoom && zoom->count() == 5 && std::strcmp(zoom->at(4).id, "mode") == 0 && zoom->at(4).enumCount == 3);
    // Turbulent Displace do AE: tipo (9), quantidade, tamanho, complexidade,
    // evolução, deslocamento, fixação (11) — já estavam no Turbulência.
    const ParameterRegistry* turb = gpu().effects.params(effect_type_id(effect_keys::kTurbulence));
    AUREA_CHECK(turb && turb->count() >= 15);
    if (turb && turb->count() >= 15) {
        AUREA_CHECK(std::strcmp(turb->at(12).id, "displacement") == 0 && turb->at(12).enumCount == 9);
        AUREA_CHECK(std::strcmp(turb->at(11).id, "pinning") == 0 && turb->at(11).enumCount == 11);
        AUREA_CHECK(std::strcmp(turb->at(3).id, "evolution") == 0 && std::strcmp(turb->at(4).id, "offset_x") == 0);
    }
    // Twixtor: velocidade e modo de quadros (mais próximo / mistura / movimento).
    const ParameterRegistry* retime = gpu().effects.params(effect_type_id(effect_keys::kFlowRetime));
    AUREA_CHECK(retime && std::strcmp(retime->at(0).id, "speed") == 0 && std::strcmp(retime->at(1).id, "frames") == 0
                && retime->at(1).enumCount == 3);
}

// Tile Shift: deslocar exatamente UMA célula com "Repetir dentro da célula"
// devolve a imagem (o conteúdo dá a volta inteira); meia célula muda tudo; as
// fileiras alternadas andam para lados opostos.
AUREA_TEST(Distort045, TileShiftWrapsInsideEachCell) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(reference_image(96, 96), 48, 48);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, kD045TileShift);
    e.params[0].constant = ParamValue::scalar(25.0f);    // células de 24 px
    e.params[2].constant = ParamValue::scalar(1.0f);     // fileiras alternadas
    e.params[3].constant = ParamValue::scalar(0.0f);     // sem aleatoriedade
    e.params[1].constant = ParamValue::scalar(100.0f);   // uma célula inteira
    const FloatImage full = s.render();
    s.comp->layer(id)->effects.back().params[1].constant = ParamValue::scalar(50.0f);
    const FloatImage half = s.render();
    std::printf("\n    uma celula: dif %.4f; meia celula: %.0f%% dos pixels mudaram ", d045_max_diff(full, plain),
                100.0f * d045_changed(half, plain));
    AUREA_CHECK_MSG(d045_max_diff(full, plain) < 0.02f, "deslocar uma célula inteira devia devolver a imagem");
    AUREA_CHECK(d045_changed(half, plain) > 0.3f);
    // Fileira par anda para a direita e a ímpar para a esquerda: meia célula
    // (12 px) — o pixel (x, y) mostra o que estava 12 px antes/depois, na célula.
    // Fileiras de 24 px a partir do centro da grade (48): y 88 está na fileira
    // 2 (par, anda para a direita) e y 70 na 1 (ímpar, para a esquerda). Os
    // pontos ficam fora do círculo branco e das linhas da imagem de referência
    // e não dão a volta na célula.
    const Vec4 even = half.v(50, 88), evenSrc = plain.v(38, 88);
    const Vec4 odd = half.v(22, 70), oddSrc = plain.v(34, 70);
    AUREA_CHECK(!d045_near(evenSrc, plain.v(50, 88), 0.03f) && !d045_near(oddSrc, plain.v(22, 70), 0.03f));
    std::printf("fileira par %.3f/%.3f, impar %.3f/%.3f ", even.x, evenSrc.x, odd.x, oddSrc.x);
    AUREA_CHECK(d045_near(even, evenSrc, 0.03f));
    AUREA_CHECK(d045_near(odd, oddSrc, 0.03f));
    // Evolução muda o sorteio no padrão aleatório.
    EffectInstance& r = s.comp->layer(id)->effects.back();
    r.params[2].constant = ParamValue::scalar(0.0f);
    r.params[3].constant = ParamValue::scalar(60.0f);
    const FloatImage evo0 = s.render();
    s.comp->layer(id)->effects.back().params[4].constant = ParamValue::scalar(180.0f);
    const FloatImage evo180 = s.render();
    AUREA_CHECK_MSG(d045_changed(evo0, evo180) > 0.1f, "a evolução devia mudar o sorteio");
}

// Hexagon Tile Rotate: 360° é a volta inteira (imagem igual); 45° muda.
// Hexagon Tile Shift: andar um vetor da colmeia (√3 · raio) com "Repetir
// dentro da célula" também devolve a imagem — o resto na colmeia é exato.
AUREA_TEST(Distort045, HexagonTilesRotateAndShiftInsideTheHoneycomb) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(reference_image(96, 96), 48, 48);
    const FloatImage plain = s.render();
    {
        EffectInstance& e = s.add_effect(id, kD045HexRotate);
        e.params[3].constant = ParamValue::scalar(0.0f);     // todas giram igual
        e.params[1].constant = ParamValue::scalar(360.0f);
    }
    const FloatImage turn = s.render();
    s.comp->layer(id)->effects.back().params[1].constant = ParamValue::scalar(45.0f);
    const FloatImage tilted = s.render();
    std::printf("\n    giro 360: dif %.4f; giro 45: %.0f%% mudaram ", d045_max_diff(turn, plain),
                100.0f * d045_changed(tilted, plain));
    AUREA_CHECK(d045_max_diff(turn, plain) < 0.03f);
    AUREA_CHECK(d045_changed(tilted, plain) > 0.3f);
    s.comp->layer(id)->effects.clear();
    {
        EffectInstance& e = s.add_effect(id, kD045HexShift);
        e.params[2].constant = ParamValue::scalar(1.0f);     // fileiras alternadas: ±x
        e.params[3].constant = ParamValue::scalar(0.0f);
        e.params[1].constant = ParamValue::scalar(173.20508f);
    }
    const FloatImage lattice = s.render();
    s.comp->layer(id)->effects.back().params[1].constant = ParamValue::scalar(60.0f);
    const FloatImage shifted = s.render();
    std::printf("vetor da colmeia: dif %.4f; 60%%: %.0f%% mudaram ", d045_max_diff(lattice, plain),
                100.0f * d045_changed(shifted, plain));
    AUREA_CHECK_MSG(d045_max_diff(lattice, plain) < 0.03f, "um vetor da colmeia devia devolver a imagem");
    AUREA_CHECK(d045_changed(shifted, plain) > 0.3f);
}

// Hollow Box de frente (sem giro, sem perspectiva, sem luz, escala 100%): a
// frente é a própria camada. Com a frente em "Recorte circular" e o fundo
// oculto, o meio fica vazado (fundo da composição) e o canto continua a camada.
AUREA_TEST(Distort045, HollowBoxFacesTheLayerAndCutsHoles) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    const LayerId id = s.image(uniform_image(64, 64, 200, 120, 40), 64, 64);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, kD045HollowBox);
    const FloatImage tilted = s.render();   // os valores padrão: girada, em perspectiva e com luz
    EffectInstance& b = s.comp->layer(id)->effects.back();
    b.params[6].constant = ParamValue::scalar(0.0f);     // rotação X
    b.params[7].constant = ParamValue::scalar(0.0f);     // rotação Y
    b.params[10].constant = ParamValue::scalar(100.0f);  // escala
    b.params[11].constant = ParamValue::scalar(0.0f);    // sem perspectiva
    b.params[19].constant = ParamValue::boolean(false);  // sem luz
    const FloatImage front = s.render();
    const Vec4 c = front.v(64, 64), p = plain.v(64, 64);
    std::printf("\n    girada: %.0f%% mudaram; de frente: centro %.3f %.3f %.3f (camada %.3f %.3f %.3f) ",
                100.0f * d045_changed(tilted, plain), c.x, c.y, c.z, p.x, p.y, p.z);
    AUREA_CHECK(d045_changed(tilted, plain) > 0.2f);
    AUREA_CHECK(d045_near(c, p, 0.03f));
    AUREA_CHECK(d045_near(front.v(40, 40), plain.v(40, 40), 0.03f));
    (void)e;
    EffectInstance& h = s.comp->layer(id)->effects.back();
    h.params[12].constant = ParamValue::scalar(2.0f);    // frente: recorte circular
    h.params[13].constant = ParamValue::scalar(1.0f);    // trás: oculto
    h.params[18].constant = ParamValue::scalar(50.0f);   // raio = metade da meia face
    const FloatImage holed = s.render();
    const Vec4 mid = holed.v(64, 64), corner = holed.v(36, 36);
    std::printf("recorte: meio %.3f, canto %.3f ", mid.x, corner.x);
    AUREA_CHECK_MSG(mid.x < 0.02f && mid.y < 0.02f, "o meio da frente recortada devia mostrar o fundo");
    AUREA_CHECK(d045_near(corner, plain.v(36, 36), 0.03f));
}

// Espelho: "dois eixos" repete o quarto de cima à esquerda nos quatro;
// "Sobrepor cópia" com a cópia por cima de uma camada opaca mostra só a
// cópia (a imagem espelhada inteira); por trás, a camada intacta.
AUREA_TEST(Distort045, MirrorBothAxesAndOverlayCopy) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    const LayerId id = s.image(reference_image(64, 64), 32, 32);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kMirror);
    e.params[3].constant = ParamValue::boolean(true);
    const FloatImage quad = s.render();
    AUREA_CHECK(d045_near(quad.v(48, 50), plain.v(15, 13), 0.03f));
    AUREA_CHECK(d045_near(quad.v(10, 50), plain.v(10, 13), 0.03f));
    AUREA_CHECK(d045_near(quad.v(12, 9), plain.v(12, 9), 0.01f));
    EffectInstance& o = s.comp->layer(id)->effects.back();
    o.params[3].constant = ParamValue::boolean(false);
    o.params[4].constant = ParamValue::scalar(1.0f);     // sobrepor cópia
    const FloatImage over = s.render();
    u32 flipped = 0, total = 0;
    for (u32 y = 2; y < 62; y += 5) for (u32 x = 2; x < 62; x += 5) {
        ++total;
        flipped += d045_near(over.v(x, y), plain.v(63 - x, y), 0.03f) ? 1u : 0u;
    }
    std::printf("\n    sobrepor por cima: %u/%u pixels espelhados ", flipped, total);
    AUREA_CHECK(flipped * 10 >= total * 9);
    s.comp->layer(id)->effects.back().params[6].constant = ParamValue::boolean(true);   // cópia atrás
    AUREA_CHECK(d045_max_diff(s.render(), plain) < 0.01f);
    // Opacidade 0 na sobreposição: neutro.
    EffectInstance& z = s.comp->layer(id)->effects.back();
    z.params[6].constant = ParamValue::boolean(false);
    z.params[5].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(d045_max_diff(s.render(), plain) < 0.01f);
}

// Desfoque de zoom: "Mais claro" guarda o máximo do rastro e "Mais escuro" o
// mínimo — o brilho médio fica acima/abaixo da média normal.
AUREA_TEST(Distort045, ZoomBlurBrightestAndDarkestModes) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(reference_image(96, 96), 48, 48);
    EffectInstance& e = s.add_effect(id, effect_keys::kZoomBlur);
    e.params[1].constant = ParamValue::scalar(40.0f);
    auto luma = [](const FloatImage& img) { const Vec4 m = img.mean(); return m.x + m.y + m.z; };
    const f32 normal = luma(s.render());
    s.comp->layer(id)->effects.back().params[4].constant = ParamValue::scalar(1.0f);
    const f32 bright = luma(s.render());
    s.comp->layer(id)->effects.back().params[4].constant = ParamValue::scalar(2.0f);
    const f32 dark = luma(s.render());
    std::printf("\n    brilho medio: mais escuro %.4f < normal %.4f < mais claro %.4f ", dark, normal, bright);
    AUREA_CHECK(bright > normal + 0.02f);
    AUREA_CHECK(dark < normal - 0.02f);
}

// Time Displacement num vídeo em que o cinza diz o quadro: com o degradê
// horizontal e ±0,4 s, a ponta esquerda mostra t − 12, o meio t e a direita
// t + 12 quadros (30 fps). Invertido, as pontas trocam. Com uma camada-mapa
// branca escondida, tudo vem de t + 12.
AUREA_TEST(Distort045, TimeDisplacementPicksEachPixelsInstantFromTheMap) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 24, 30.0);
    SyntheticConfig cfg;
    cfg.width = 96;
    cfg.height = 24;
    cfg.frameCount = 120;
    cfg.gop = 30;
    cfg.pattern = SyntheticPattern::FrameGray;
    const LayerId id = s.video(cfg, 48.0f, 12.0f);
    const FrameIndex t{45};
    auto gray_at = [&](FrameIndex at) {
        SliceFrame f;
        AUREA_CHECK(time_slice_settle(s, at, f, true));
        return f.img.v(48, 12);
    };
    const Vec4 g33 = gray_at(FrameIndex{33}), g45 = gray_at(t), g57 = gray_at(FrameIndex{57});
    EffectInstance& e = s.add_effect(id, kD045TimeDisplace);
    e.params[0].constant = ParamValue::scalar(2.0f);     // degradê horizontal
    e.params[2].constant = ParamValue::scalar(0.4f);     // ±0,4 s = ±12 quadros
    e.params[3].constant = ParamValue::scalar(15.0f);    // 13 níveis (passo de 2 quadros)
    e.params[5].constant = ParamValue::boolean(false);   // sem suavizar: o nível mais próximo
    SliceFrame out;
    AUREA_CHECK_MSG(time_slice_settle(s, t, out, true), "o export nunca fechou os níveis");
    const Vec4 left = out.img.v(1, 12), mid = out.img.v(48, 12), right = out.img.v(94, 12);
    std::printf("\n    cinzas t-12 %.3f, t %.3f, t+12 %.3f; efeito: esq %.3f meio %.3f dir %.3f ",
                g33.x, g45.x, g57.x, left.x, mid.x, right.x);
    AUREA_CHECK(std::fabs(g33.x - g57.x) > 0.02f && std::fabs(g33.x - g45.x) > 0.02f);
    AUREA_CHECK(d045_near(left, g33, 0.01f));
    AUREA_CHECK(d045_near(mid, g45, 0.01f));
    AUREA_CHECK(d045_near(right, g57, 0.01f));
    // Inverter: as pontas trocam.
    s.comp->layer(id)->effects.back().params[4].constant = ParamValue::boolean(true);
    AUREA_CHECK(time_slice_settle(s, t, out, true));
    AUREA_CHECK(d045_near(out.img.v(1, 12), g57, 0.01f));
    AUREA_CHECK(d045_near(out.img.v(94, 12), g33, 0.01f));
    // Camada-mapa branca (escondida): +máx em todo pixel.
    const LayerId map = s.image(uniform_image(96, 24, 255, 255, 255), 48, 12);
    s.comp->layer(map)->visible = false;
    s.comp->reorder_layer(id, s.comp->order().size() - 1);
    EffectInstance& m = s.comp->layer(id)->effects.back();
    m.params[4].constant = ParamValue::boolean(false);
    m.params[0].constant = ParamValue::scalar(0.0f);     // camada do mapa
    m.params[1].constant.ref = map.pack();
    m.params[1].constant.v[0] = static_cast<f32>(map.index);
    AUREA_CHECK(time_slice_settle(s, t, out, true));
    std::printf("mapa branco: esq %.3f meio %.3f ", out.img.v(1, 12).x, out.img.v(48, 12).x);
    AUREA_CHECK(d045_near(out.img.v(1, 12), g57, 0.01f));
    AUREA_CHECK(d045_near(out.img.v(48, 12), g57, 0.01f));
    // Máximo 0: neutro (o quadro de agora).
    s.comp->layer(id)->effects.back().params[2].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(time_slice_settle(s, t, out, true));
    AUREA_CHECK(d045_near(out.img.v(1, 12), g45, 0.01f));
}
