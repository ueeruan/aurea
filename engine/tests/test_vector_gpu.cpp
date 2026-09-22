// =============================================================================
//  Gráficos vetoriais (Fase 7D) na GPU real: largura do contorno em pixels,
//  tracejado, furo par-ímpar, degradê, aparar, repetidor, booleana, nitidez
//  com zoom, SVG, texto no caminho, desenho à mão livre e salvar/reabrir.
//  Sem Vulkan no host, os testes avisam e passam vazios.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

// --- CPU (não precisa de GPU) -------------------------------------------------

AUREA_TEST(Vector, TextOnPathGlyphAnglesFollowTheTangent) {
    // Círculo r=100 (sentido horário na tela, começa no topo): a letra no
    // comprimento s fica no ângulo −90° + s/r e gira pela tangente.
    Contour c;
    flatten(make_ellipse(Vec2{0, 0}, Vec2{200, 200}), 0.01f, c);
    const f64 L = length_of(c);
    for (f32 x : {0.0f, 50.0f, 157.08f, 314.16f, 500.0f}) {
        Vec2 p;
        f32 ang = 0.0f;
        AUREA_CHECK(place_on_path(c, 0.0f, false, true, x, 0.0f, p, ang));
        const f32 a = -1.5707963f + x / 100.0f;
        AUREA_CHECK_NEAR(p.x, 100.0f * std::cos(a), 0.2f);
        AUREA_CHECK_NEAR(p.y, 100.0f * std::sin(a), 0.2f);
        // Tangente horária = ângulo do raio + 90°.
        f32 want = (a + 1.5707963f) * 57.29578f;
        f32 d = std::fmod(ang - want + 540.0f, 360.0f) - 180.0f;
        AUREA_CHECK_NEAR(d, 0.0f, 0.6f);
    }
    // Margem inicial desloca; invertido percorre do fim; dy vai para "baixo" (fora do círculo horário = dentro?).
    Vec2 p0, p1;
    f32 a0 = 0, a1 = 0;
    AUREA_CHECK(place_on_path(c, 100.0f, false, true, 0.0f, 0.0f, p0, a0));
    AUREA_CHECK(place_on_path(c, 0.0f, false, true, 100.0f, 0.0f, p1, a1));
    AUREA_CHECK_NEAR(p0.x, p1.x, 1e-3f);
    AUREA_CHECK(place_on_path(c, 0.0f, true, true, 0.0f, 0.0f, p0, a0));
    AUREA_CHECK_NEAR(p0.x, 0.0f, 0.2f);   // o fim do caminho fechado = o começo (topo)
    AUREA_CHECK_NEAR(std::fabs(std::fmod(a0 + 360.0f, 360.0f) - 180.0f), 0.0f, 0.6f);   // sentido oposto
    // Linha de base 10 px abaixo: no topo do círculo horário, "abaixo" aponta para o centro.
    AUREA_CHECK(place_on_path(c, 0.0f, false, true, 0.0f, 10.0f, p0, a0));
    AUREA_CHECK_NEAR(p0.y, -90.0f, 0.2f);
    // Não perpendicular: ângulo 0 em qualquer ponto.
    AUREA_CHECK(place_on_path(c, 0.0f, false, false, 157.0f, 0.0f, p0, a0));
    AUREA_CHECK_NEAR(a0, 0.0f, 1e-6f);
    (void)L;
}

AUREA_TEST(Vector, DocumentCodecRoundTrips) {
    VectorData d;
    VectorGroup g;
    g.name = "Estrela com degradê";
    VectorPath p;
    p.kind = VectorPathKind::Star;
    p.points = 7;
    g.paths.push_back(p);
    VectorPath f;
    f.path = make_ellipse(Vec2{10, 20}, Vec2{30, 40});
    PathKey k;
    k.frame = 12;
    k.path = make_rect(Vec2{0, 0}, Vec2{5, 5}, 1.0f);
    f.keys.push_back(k);
    g.paths.push_back(f);
    g.fill.paint.type = 2;
    g.fill.paint.stops = {VectorStop{0.0f, Vec4{1, 0, 0, 1}}, VectorStop{1.0f, Vec4{0, 0, 1, 0.5f}}};
    g.stroke.enabled = true;
    g.stroke.dashes = {4, 2, 1};
    g.trim.enabled = true;
    g.trim.end = 42.0f;
    g.repeater.enabled = true;
    g.repeater.copies = 6.5f;
    g.merge = 3;
    d.groups = {g, VectorGroup{}};
    std::vector<f32> s;
    std::string names;
    encode_document(d, s, names);
    VectorData back;
    AUREA_CHECK(decode_document(s.data(), s.size(), names, back));
    AUREA_CHECK(back == d);
    // Truncado: falha sem mexer na saída.
    VectorData untouched;
    AUREA_CHECK(!decode_document(s.data(), s.size() / 2, names, untouched));
    AUREA_CHECK(untouched.groups.empty());
}

#if defined(AUREA_TEST_VULKAN)

#include "ImageIO.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"

using namespace aurea::test;

namespace {

bool vulkan_ok() {
    static const int ok = [] {
        vk::Backend b;
        BackendConfig cfg;
        cfg.enableValidation = false;
        const bool r = b.initialize(cfg).ok();
        if (r) b.shutdown();
        return r ? 1 : 0;
    }();
    return ok != 0;
}

#define VEC_REQUIRE_GPU()                                   \
    do {                                                    \
        if (!vulkan_ok()) {                                 \
            std::printf("(sem GPU Vulkan: pulado) ");       \
            return;                                         \
        }                                                   \
    } while (0)

struct Rig {
    Engine e;
    explicit Rig(u32 w = 256, u32 h = 256) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(w, h, 30.0, nullptr).ok());
    }
    ~Rig() { e.shutdown(); }
    Image8 capture(u32 maxDim) {
        Image8 img;
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        AUREA_CHECK(e.capture_frame_rgba(maxDim, rgba, w, h).ok());
        img.width = w;
        img.height = h;
        img.rgba = std::move(rgba);
        return img;
    }
    /// Camada vetorial com estes grupos (via API, como a UI faz).
    u64 layer(const std::vector<VectorGroup>& groups) {
        auto id = e.add_vector_layer(1);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return 0;
        // A API mantém o nº de grupos: acerta a quantidade antes do documento.
        while (true) {
            std::vector<f32> doc;
            std::string names;
            e.vector_document(*id, doc, names);
            VectorData cur;
            decode_document(doc.data(), doc.size(), names, cur);
            if (cur.groups.size() < groups.size()) { e.add_vector_group(*id, 1); continue; }
            if (cur.groups.size() > groups.size()) { e.remove_vector_group(*id, 0); continue; }
            break;
        }
        VectorData d;
        d.groups = groups;
        std::vector<f32> doc;
        std::string names;
        encode_document(d, doc, names);
        AUREA_CHECK(e.set_vector_document(*id, doc.data(), doc.size(), names, false));
        return *id;
    }
};

f32 lin(u8 v) {
    const f32 c = static_cast<f32>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

VectorGroup line_group(Vec2 a, Vec2 b, f32 width) {
    VectorGroup g;
    VectorPath p;
    p.path.closed = false;
    p.path.v = {BezierVertex{a, {}, {}}, BezierVertex{b, {}, {}}};
    g.paths.push_back(p);
    g.fill.enabled = false;
    g.stroke.enabled = true;
    g.stroke.width = width;
    g.stroke.paint.color = Vec4{1, 1, 1, 1};
    return g;
}

VectorGroup rect_group(Vec2 c, Vec2 size, Vec4 color = Vec4{1, 1, 1, 1}) {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = c;
    p.size = size;
    g.paths.push_back(p);
    g.fill.paint.color = color;
    return g;
}

/// Soma da cobertura linear (canal vermelho) numa coluna / linha.
f32 column_coverage(const Image8& img, u32 x) {
    f32 s = 0.0f;
    for (u32 y = 0; y < img.height; ++y) s += lin(img.at(x, y)[0]);
    return s;
}
f32 area_coverage(const Image8& img) {
    f32 s = 0.0f;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) s += lin(img.at(x, y)[0]);
    return s;
}

} // namespace

AUREA_TEST(VectorGpu, StrokeWidthIsExactInPixels) {
    VEC_REQUIRE_GPU();
    for (f32 w : {10.0f, 3.0f, 17.5f}) {
        Rig rig;
        rig.layer({line_group(Vec2{28, 128.3f}, Vec2{228, 128.3f}, w)});
        const Image8 img = rig.capture(256);
        const f32 cov = column_coverage(img, 128);
        std::printf("    contorno %.1f px: cobertura medida %.3f px\n", w, cov);
        AUREA_CHECK_NEAR(cov, w, 0.1f);
        // Ponta reta: nada antes de x = 28 (a 2 px da ponta).
        AUREA_CHECK(lin(img.at(25, 128)[0]) < 0.01f);
    }
}

AUREA_TEST(VectorGpu, DashPatternRunsInPixels) {
    VEC_REQUIRE_GPU();
    Rig rig;
    VectorGroup g = line_group(Vec2{20, 128}, Vec2{236, 128}, 8.0f);
    g.stroke.dashes = {24.0f, 12.0f};
    rig.layer({g});
    const Image8 img = rig.capture(256);
    // Corridas acesas/apagadas ao longo da linha (cobertura > 0,5).
    std::vector<u32> on, off;
    bool lit = false;
    u32 run = 0;
    for (u32 x = 20; x < 236; ++x) {
        const bool l = lin(img.at(x, 128)[0]) > 0.5f;
        if (x == 20) { lit = l; run = 1; continue; }
        if (l == lit) { ++run; continue; }
        (lit ? on : off).push_back(run);
        lit = l;
        run = 1;
    }
    std::printf("    corridas acesas:");
    for (u32 r : on) std::printf(" %u", r);
    std::printf(" | apagadas:");
    for (u32 r : off) std::printf(" %u", r);
    std::printf("\n");
    AUREA_CHECK(on.size() >= 5);
    for (u32 r : on) AUREA_CHECK(r >= 23 && r <= 25);
    for (u32 r : off) AUREA_CHECK(r >= 11 && r <= 13);
}

AUREA_TEST(VectorGpu, EvenOddHoleAndBooleanAreas) {
    VEC_REQUIRE_GPU();
    {
        Rig rig;
        VectorGroup g = rect_group(Vec2{128, 128}, Vec2{160, 160});
        VectorPath inner;
        inner.kind = VectorPathKind::Rect;
        inner.center = Vec2{128, 128};
        inner.size = Vec2{80, 80};
        g.paths.push_back(inner);
        g.fill.rule = 1;
        rig.layer({g});
        const Image8 img = rig.capture(256);
        AUREA_CHECK(img.at(128, 128)[0] < 5);     // furo
        AUREA_CHECK(img.at(58, 128)[0] > 250);    // anel
        const f32 a = area_coverage(img);
        std::printf("    par-impar: area %.1f (esperado %.0f)\n", a, 160.0f * 160.0f - 80.0f * 80.0f);
        AUREA_CHECK_NEAR(a, 160.0f * 160.0f - 80.0f * 80.0f, 19200.0f * 0.005f);
    }
    // Booleanas: dois quadrados de 100 com 50 de sobreposição em x e y.
    const f32 expect[4] = {17500.0f, 7500.0f, 2500.0f, 15000.0f};
    for (u8 op = 1; op <= 4; ++op) {
        Rig rig;
        VectorGroup g = rect_group(Vec2{103, 103}, Vec2{100, 100});
        VectorPath b;
        b.kind = VectorPathKind::Rect;
        b.center = Vec2{153, 153};
        b.size = Vec2{100, 100};
        g.paths.push_back(b);
        g.merge = op;
        rig.layer({g});
        const f32 a = area_coverage(rig.capture(256));
        std::printf("    booleana %u: area %.1f (esperado %.0f)\n", op, a, expect[op - 1]);
        AUREA_CHECK_NEAR(a, expect[op - 1], expect[op - 1] * 0.005f);
    }
}

AUREA_TEST(VectorGpu, LinearGradientIsInterpolatedInLinearLight) {
    VEC_REQUIRE_GPU();
    Rig rig;
    VectorGroup g = rect_group(Vec2{128, 128}, Vec2{200, 100});
    g.fill.paint.type = 1;
    g.fill.paint.start = Vec2{28, 0};
    g.fill.paint.end = Vec2{228, 0};
    g.fill.paint.stops = {VectorStop{0.0f, Vec4{1, 0, 0, 1}}, VectorStop{1.0f, Vec4{0, 0, 1, 1}}};
    rig.layer({g});
    const Image8 img = rig.capture(256);
    for (f32 t : {0.25f, 0.5f, 0.75f}) {
        const u32 x = static_cast<u32>(28.0f + 200.0f * t);
        const f32 tt = (static_cast<f32>(x) + 0.5f - 28.0f) / 200.0f;
        const u8* p = img.at(x, 128);
        std::printf("    t=%.2f: rgb %u %u %u (linear esperado %.3f / %.3f)\n", tt, p[0], p[1], p[2], 1.0f - tt, tt);
        AUREA_CHECK_NEAR(lin(p[0]), 1.0f - tt, 0.01f);
        AUREA_CHECK_NEAR(lin(p[2]), tt, 0.01f);
        AUREA_CHECK(p[1] < 2);
    }
    // Radial: centro = primeira cor, borda do raio = última.
    Rig r2;
    VectorGroup rg = rect_group(Vec2{128, 128}, Vec2{220, 220});
    rg.fill.paint.type = 2;
    rg.fill.paint.start = Vec2{128, 128};
    rg.fill.paint.end = Vec2{228, 128};
    rg.fill.paint.stops = {VectorStop{0.0f, Vec4{1, 1, 1, 1}}, VectorStop{1.0f, Vec4{0, 0, 0, 1}}};
    r2.layer({rg});
    const Image8 ri = r2.capture(256);
    AUREA_CHECK(ri.at(128, 128)[0] > 250);
    AUREA_CHECK_NEAR(lin(ri.at(178, 128)[0]), 0.5f, 0.02f);
    AUREA_CHECK(ri.at(233, 128)[0] < 3);
}

AUREA_TEST(VectorGpu, TrimRepeaterAndSharpnessWhenZoomed) {
    VEC_REQUIRE_GPU();
    auto ring = [](f32 end) {
        VectorGroup g;
        VectorPath p;
        p.kind = VectorPathKind::Ellipse;
        p.center = Vec2{128, 128};
        p.size = Vec2{160, 160};
        g.paths.push_back(p);
        g.fill.enabled = false;
        g.stroke.enabled = true;
        g.stroke.width = 8.0f;
        g.stroke.paint.color = Vec4{1, 1, 1, 1};
        g.trim.enabled = true;
        g.trim.end = end;
        return g;
    };
    f32 full = 0.0f, half = 0.0f;
    {
        Rig rig;
        rig.layer({ring(100.0f)});
        full = area_coverage(rig.capture(256));
    }
    {
        Rig rig;
        rig.layer({ring(50.0f)});
        half = area_coverage(rig.capture(256));
    }
    std::printf("    aparar: anel %.1f, 0-50%% %.1f (razao %.4f)\n", full, half, half / full);
    // Metade do comprimento: metade da área (as pontas retas não somam nada).
    AUREA_CHECK_NEAR(half / full, 0.5f, 0.01f);
    // Repetidor: 4 cópias de um quadrado de 20, a cada 50 px → 4 manchas.
    {
        Rig rig;
        VectorGroup g = rect_group(Vec2{50, 128}, Vec2{20, 20});
        g.repeater.enabled = true;
        g.repeater.copies = 4.0f;
        g.repeater.position = Vec2{50, 0};
        rig.layer({g});
        const Image8 img = rig.capture(256);
        u32 blobs = 0;
        bool prev = false;
        for (u32 x = 0; x < img.width; ++x) {
            const bool on = img.at(x, 128)[0] > 128;
            if (on && !prev) ++blobs;
            prev = on;
        }
        AUREA_CHECK_EQ(blobs, 4u);
        AUREA_CHECK_NEAR(area_coverage(img), 4.0f * 400.0f, 16.0f);
    }
    // Ampliada 4×: a borda continua com ~1 px de transição na tela.
    {
        Rig rig;
        const u64 id = rig.layer({rect_group(Vec2{128, 128}, Vec2{40, 40})});
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp->layer(LayerId::unpack(id));
        l->transform.scale = Vec3{4.0f, 4.0f, 1.0f};
        l->transform.rotation = Vec3{0, 0, 10.0f};
        const Image8 img = rig.capture(256);
        // Varre uma linha e conta pixels de meia cobertura na borda (0,05..0,95).
        u32 partial = 0, edges = 0;
        bool prev = false;
        for (u32 x = 0; x < img.width; ++x) {
            const f32 c = lin(img.at(x, 128)[0]);
            if (c > 0.05f && c < 0.95f) ++partial;
            const bool on = c > 0.5f;
            if (on != prev) ++edges;
            prev = on;
        }
        std::printf("    ampliada 4x: %u pixels de transicao em %u bordas\n", partial, edges);
        AUREA_CHECK_EQ(edges, 2u);
        AUREA_CHECK(partial <= 2u * 2u);
    }
}

AUREA_TEST(VectorGpu, SvgImportRendersExpectedBounds) {
    VEC_REQUIRE_GPU();
    const std::string svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 400 200">
  <g transform="translate(100,0)"><rect x="0" y="0" width="100" height="50" fill="#ff0000"/>
    <path d="M200,100 a50,50 0 1,0 100,0 a50,50 0 1,0 -100,0z" fill="none" stroke="#00ff00" stroke-width="4"/></g>
  <circle cx="50" cy="150" r="40" fill="#ffff00"/>
</svg>)svg";
    Rig rig(400, 300);
    auto id = rig.e.import_svg(svg, "amostra");
    AUREA_CHECK(id.ok());
    const Image8 img = rig.capture(400);
    // Caixa dos pixels acesos: o desenho 200×100 centrado em (200, 150) → origem (100, 100).
    u32 x0 = 9999, y0 = 9999, x1 = 0, y1 = 0;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) {
            const u8* p = img.at(x, y);
            if (p[0] > 128 || p[1] > 128) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        }
    std::printf("    caixa acesa: (%u,%u)-(%u,%u)\n", x0, y0, x1, y1);
    // Círculo amarelo: centro (125, 175) r 20 → x0 105, y1 194. Retângulo:
    // topo 100. Arco verde: centro (275, 150) r 25 (+1 do contorno) → x1 300.
    AUREA_CHECK(x0 >= 104 && x0 <= 106);
    AUREA_CHECK(y0 >= 99 && y0 <= 101);
    AUREA_CHECK(x1 >= 299 && x1 <= 301);
    AUREA_CHECK(y1 >= 193 && y1 <= 195);
    const u8* c = img.at(125, 175);
    AUREA_CHECK(c[0] > 250 && c[1] > 250 && c[2] < 5);   // amarelo
    const u8* r = img.at(175, 112);
    AUREA_CHECK(r[0] > 250 && r[1] < 5);                 // vermelho
}

AUREA_TEST(VectorGpu, TextFollowsAPathLayer) {
    VEC_REQUIRE_GPU();
    Rig rig(400, 400);
    // Guia: círculo r = 120 no centro, sem tinta (só conduz o texto).
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Ellipse;
    p.center = Vec2{200, 200};
    p.size = Vec2{240, 240};
    g.paths.push_back(p);
    g.fill.enabled = false;
    const u64 guide = rig.layer({g});
    auto text = rig.e.add_text("AUREA AUREA AUREA");
    AUREA_CHECK(text.ok());
    if (!text.ok()) return;
    const Image8 straight = rig.capture(400);
    AUREA_CHECK(rig.e.set_text_path(*text, guide, 0.0f, true, false));
    u64 pl = 0;
    f32 off = -1;
    bool perp = false, rev = true;
    AUREA_CHECK(rig.e.query_text_path(*text, pl, off, perp, rev));
    AUREA_CHECK(pl == guide && off == 0.0f && perp && !rev);
    const Image8 img = rig.capture(400);
    (void)write_png("vetor_texto_caminho.png", img);
    // Pixels do texto: a linha de base no raio 120 e as letras (maiúsculas,
    // ~50 px) para FORA dela — o "alto" da letra aponta para longe do centro.
    u32 lit = 0, near = 0;
    f32 minAng = 1e9f, maxAng = -1e9f;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) {
            if (img.at(x, y)[0] < 128) continue;
            ++lit;
            const f32 dx = static_cast<f32>(x) - 200.0f, dy = static_cast<f32>(y) - 200.0f;
            const f32 d = std::sqrt(dx * dx + dy * dy);
            if (d > 120.0f - 6.0f && d < 120.0f + 60.0f) ++near;
            const f32 a = std::atan2(dy, dx);
            minAng = std::min(minAng, a);
            maxAng = std::max(maxAng, a);
        }
    u32 litStraight = 0;
    for (usize i = 0; i + 3 < straight.rgba.size(); i += 4) litStraight += straight.rgba[i] >= 128;
    std::printf("    texto no caminho: %u px acesos (reto %u), %.1f%% perto do circulo, arco %.0f..%.0f graus\n", lit, litStraight,
                100.0f * static_cast<f32>(near) / static_cast<f32>(std::max(1u, lit)), minAng * 57.3f, maxAng * 57.3f);
    AUREA_CHECK(lit > 200);
    AUREA_CHECK(static_cast<f32>(near) > 0.97f * static_cast<f32>(lit));
    // Começa no topo (−90°, o início do círculo) e anda no sentido horário
    // pelo comprimento do texto (~370 px de 754 = ~175° de arco).
    AUREA_CHECK_NEAR(minAng * 57.2958f, -90.0f, 8.0f);
    AUREA_CHECK(maxAng - minAng > 2.5f && maxAng - minAng < 3.6f);
    // Mesma tinta: a quantidade de pixels muda pouco (as letras só giram).
    AUREA_CHECK(std::fabs(static_cast<f32>(lit) - static_cast<f32>(litStraight)) < 0.25f * static_cast<f32>(litStraight));
}

AUREA_TEST(VectorGpu, FreehandStrokeAndSaveReopenIdenticalFrame) {
    VEC_REQUIRE_GPU();
    Rig rig(320, 240);
    // Desenho à mão livre: um "S" pelo dedo.
    std::vector<f32> xy;
    for (int i = 0; i <= 120; ++i) {
        const f32 t = static_cast<f32>(i) / 120.0f;
        xy.push_back(60.0f + 200.0f * t);
        xy.push_back(120.0f + 60.0f * std::sin(t * 6.2831853f));
    }
    auto fh = rig.e.add_freehand_path(0, xy.data(), xy.size(), 1.0f);
    AUREA_CHECK(fh.ok());
    // Mais uma camada com tudo: degradê, tracejado, repetidor, aparar animado e morph.
    VectorGroup g;
    VectorPath m;
    PathKey k0, k1;
    k0.frame = 0; k0.path = make_rect(Vec2{80, 60}, Vec2{40, 40}, 6.0f);
    k1.frame = 20; k1.path = make_ellipse(Vec2{90, 70}, Vec2{50, 30});
    m.keys = {k0, k1};
    g.paths.push_back(m);
    g.fill.paint.type = 1;
    g.fill.paint.start = Vec2{60, 0};
    g.fill.paint.end = Vec2{120, 0};
    g.fill.paint.stops = {VectorStop{0.0f, Vec4{1, 0.5f, 0, 1}}, VectorStop{0.5f, Vec4{0, 1, 0.5f, 0.7f}}, VectorStop{1.0f, Vec4{0.2f, 0.2f, 1, 1}}};
    g.stroke.enabled = true;
    g.stroke.width = 3.0f;
    g.stroke.dashes = {6, 3};
    g.repeater.enabled = true;
    g.repeater.copies = 3.0f;
    g.repeater.position = Vec2{60, 20};
    g.repeater.rotation = 15.0f;
    g.repeater.endOpacity = 40.0f;
    g.trim.enabled = true;
    const u64 id = rig.layer({g});
    AUREA_CHECK(rig.e.toggle_vector_param_key(id, 0, kVecTrimEnd));   // keyframe em 0 com 100%
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    AUREA_CHECK(rig.e.set_vector_param(id, 0, kVecTrimEnd, 30.0f, false));   // animado: keyframe no 10
    f32 vals[Engine::kVectorParamFloats]{};
    AUREA_CHECK_EQ(rig.e.query_vector_params(id, 0, vals, Engine::kVectorParamFloats), Engine::kVectorParamFloats);
    AUREA_CHECK_NEAR(vals[kVecTrimEnd], 30.0f, 1e-4f);
    AUREA_CHECK((static_cast<u32>(vals[kVecParamCount + 1]) & (1u << kVecTrimEnd)) != 0);
    const Image8 a = rig.capture(320);
    AUREA_CHECK(area_coverage(a) > 500.0f);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_vetor.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    const Image8 b = rig.capture(320);
    u32 maxDiff = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) maxDiff = std::max<u32>(maxDiff, static_cast<u32>(std::abs(a.rgba[i] - b.rgba[i])));
    std::printf("    salvar/reabrir: diferenca maxima %u\n", maxDiff);
    AUREA_CHECK(a.rgba.size() == b.rgba.size());
    AUREA_CHECK_EQ(maxDiff, 0u);
    (void)write_png("vetor_completo.png", b);
    std::remove(path.c_str());
}

#endif
