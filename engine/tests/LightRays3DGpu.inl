// =============================================================================
//  Aurea / tests / LightRays3DGpu.inl
//
//  Raios de luz (aurea.light.rays) fora do 2D chapado — relato do beta:
//  "Light Rays não funciona em textos".
//    - Texto 3D extrudado (camada de modelo, desenhada pela cena 3D): os
//      efeitos da camada nem eram planejados (o grupo 3D saía sem plano), e os
//      raios não apareciam.
//    - Texto 2D com Z/perspectiva (plano na cena 3D): os raios tinham de
//      sair além da caixa projetada da camada.
//  AUREA_RAYS3D_DIR grava os quadros (investigação).
// =============================================================================
namespace rays3d {

std::string dump_dir() {
    const char* v = std::getenv("AUREA_RAYS3D_DIR");
    return v && *v ? std::string(v) : std::string();
}
void dump(const char* name, const Image8& img) {
    const std::string dir = dump_dir();
    if (!dir.empty()) (void)write_png(dir + "/" + name + ".png", img);
}

struct Box { u32 x0 = ~0u, y0 = ~0u, x1 = 0, y1 = 0, n = 0; };
Box lit_box(const Image8& img, u8 above) {
    Box b;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) {
            const u8* p = img.at(x, y);
            if (std::max({p[0], p[1], p[2]}) <= above) continue;
            ++b.n; b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y); b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
        }
    return b;
}

EffectInstance* add_rays(Scene3DRig& rig, u64 layer) {
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(layer);
    add.effect_add.effectType = effect_type_id(effect_keys::kRays); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp ? comp->layer(LayerId::unpack(layer)) : nullptr;
    if (!l || l->effects.empty()) return nullptr;
    EffectInstance& fx = l->effects.back();
    fx.params[0].constant = ParamValue::scalar(3);
    fx.params[1].constant = ParamValue::scalar(85);
    fx.params[2].constant = ParamValue::scalar(10);
    fx.params[3].constant = ParamValue::scalar(10);
    fx.params[5].constant = ParamValue::scalar(64);
    return &fx;
}

/// Pixels de raio: acesos no resultado e escuros no original. `outside`
/// conta os que ficam além da caixa dos glifos (com folga).
struct Beams { u32 off = 0, outside = 0; };
Beams beams(const Image8& original, const Image8& rays, const Box& glyphs, u32 slack) {
    Beams r;
    for (u32 y = 0; y < rays.height; ++y)
        for (u32 x = 0; x < rays.width; ++x) {
            const u8* p = rays.at(x, y); const u8* b = original.at(x, y);
            if (std::max({p[0], p[1], p[2]}) <= 12 || std::max({b[0], b[1], b[2]}) >= 2) continue;
            ++r.off;
            r.outside += x + slack < glyphs.x0 || x > glyphs.x1 + slack || y + slack < glyphs.y0 || y > glyphs.y1 + slack;
        }
    return r;
}

} // namespace rays3d

AUREA_TEST(LightRays3DGpu, ExtrudedText3DCastsBeamsBeyondItsGlyphs) {
    AUREA_REQUIRE_GPU();
    using namespace rays3d;
    Scene3DRig rig(640, 360);
    scene3d::Text3DSpec spec;
    spec.content = "RAYS";
    const auto id = rig.e.add_text3d(spec);
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const Image8 original = rig.capture(640);
    dump("text3d_original", original);
    const Box glyphs = lit_box(original, 32);
    AUREA_CHECK(glyphs.n > 500u);
    EffectInstance* fx = add_rays(rig, *id);
    AUREA_CHECK(fx != nullptr); if (!fx) return;
    const Image8 rays = rig.capture(640);
    dump("text3d_rays", rays);
    const Beams b = beams(original, rays, glyphs, 4);
    std::printf("    texto 3D: pixels=%u, raios fora dos glifos=%u, além da caixa=%u\n", glyphs.n, b.off, b.outside);
    AUREA_CHECK(b.off > 1000u);
    AUREA_CHECK(b.outside > 500u);
    // Intensidade 0: o efeito é identidade — o quadro é o original.
    fx->params[0].constant = ParamValue::scalar(0);
    const Image8 off = rig.capture(640);
    AUREA_CHECK_EQ(max_diff(original, off), 0u);
}

AUREA_TEST(LightRays3DGpu, TextWithDepthAndRotationIsNotClippedToItsProjectedBox) {
    AUREA_REQUIRE_GPU();
    using namespace rays3d;
    Scene3DRig rig(640, 360);
    const auto id = rig.e.add_text("LIGHT");
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    {
        auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp ? comp->layer(LayerId::unpack(*id)) : nullptr;
        AUREA_CHECK(l != nullptr); if (!l) return;
        l->transform.position.z = -300.0f;
        l->transform.rotation.y = 25.0f;
        l->transform.rotation.z = 10.0f;
    }
    const Image8 original = rig.capture(640);
    dump("textz_original", original);
    const Box glyphs = lit_box(original, 32);
    AUREA_CHECK(glyphs.n > 500u);
    EffectInstance* fx = add_rays(rig, *id);
    AUREA_CHECK(fx != nullptr); if (!fx) return;
    const Image8 rays = rig.capture(640);
    dump("textz_rays", rays);
    const Beams b = beams(original, rays, glyphs, 4);
    const Box lit = lit_box(rays, 12);
    std::printf("    texto Z: pixels=%u, raios fora=%u, além da caixa=%u, luz x %u..%u y %u..%u (glifos x %u..%u y %u..%u)\n",
                glyphs.n, b.off, b.outside, lit.x0, lit.x1, lit.y0, lit.y1, glyphs.x0, glyphs.x1, glyphs.y0, glyphs.y1);
    AUREA_CHECK(b.off > 1000u);
    AUREA_CHECK(b.outside > 500u);
    fx->params[0].constant = ParamValue::scalar(0);
    AUREA_CHECK_EQ(max_diff(original, rig.capture(640)), 0u);
}

// O corte de verdade: texto com Z LONGE da câmera (aparece pequeno) e raios
// longos. A região dos raios crescia no máximo 512 px DA CAMADA em cada lado
// (teto da cena 3D) — longe da câmera isso é pouco na tela, e os raios
// paravam numa borda reta no meio do quadro. Agora o teto cobre também a
// parte do plano que a tela mostra: os raios vão até a borda da composição.
AUREA_TEST(LightRays3DGpu, FarTextWithDepthBeamsReachTheFrameEdge) {
    AUREA_REQUIRE_GPU();
    using namespace rays3d;
    Scene3DRig rig(640, 360);
    const auto id = rig.e.add_text("LIGHT");
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    {
        auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp ? comp->layer(LayerId::unpack(*id)) : nullptr;
        AUREA_CHECK(l != nullptr); if (!l) return;
        l->text.size = 300.0f;
        l->transform.position.z = 900.0f;
        l->transform.rotation.y = 25.0f;
    }
    const Image8 original = rig.capture(640);
    dump("textfar_original", original);
    const Box glyphs = lit_box(original, 32);
    AUREA_CHECK(glyphs.n > 300u);
    EffectInstance* fx = add_rays(rig, *id);
    AUREA_CHECK(fx != nullptr); if (!fx) return;
    const Image8 rays = rig.capture(640);
    dump("textfar_rays", rays);
    const Beams b = beams(original, rays, glyphs, 4);
    // Luz nas faixas de 24 px junto às bordas esquerda e direita.
    u32 leftEdge = 0, rightEdge = 0;
    for (u32 y = 0; y < rays.height; ++y)
        for (u32 x = 0; x < 24; ++x) {
            const u8* p = rays.at(x, y); const u8* q = rays.at(rays.width - 1 - x, y);
            leftEdge += std::max({p[0], p[1], p[2]}) > 12;
            rightEdge += std::max({q[0], q[1], q[2]}) > 12;
        }
    std::printf("    texto longe: glifos x %u..%u, raios fora=%u, na borda esq=%u dir=%u\n",
                glyphs.x0, glyphs.x1, b.off, leftEdge, rightEdge);
    AUREA_CHECK(b.outside > 1000u);
    AUREA_CHECK(leftEdge > 200u);
    AUREA_CHECK(rightEdge > 200u);
    fx->params[0].constant = ParamValue::scalar(0);
    AUREA_CHECK_EQ(max_diff(original, rig.capture(640)), 0u);
}

// O mesmo caminho vale para os outros efeitos de imagem no texto 3D (antes o
// Brilho também não fazia nada nele): o halo sai em volta das letras.
AUREA_TEST(LightRays3DGpu, GlowOnExtrudedText3DLightsAroundTheGlyphs) {
    AUREA_REQUIRE_GPU();
    using namespace rays3d;
    Scene3DRig rig(640, 360);
    scene3d::Text3DSpec spec;
    spec.content = "GLOW";
    const auto id = rig.e.add_text3d(spec);
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const Image8 original = rig.capture(640);
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(*id);
    add.effect_add.effectType = effect_type_id(effect_keys::kGlow); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    const Image8 glow = rig.capture(640);
    dump("text3d_glow", glow);
    u32 halo = 0;
    for (u32 y = 0; y < glow.height; ++y)
        for (u32 x = 0; x < glow.width; ++x) {
            const u8* p = glow.at(x, y); const u8* b = original.at(x, y);
            halo += std::max({p[0], p[1], p[2]}) > 8 && std::max({b[0], b[1], b[2]}) < 2;
        }
    std::printf("    brilho no texto 3D: pixels de halo fora das letras=%u, diferença máx=%u\n", halo, max_diff(original, glow));
    AUREA_CHECK(halo > 200u);
}
