// =============================================================================
//  Correções da 0.3.4 · bug 11: "Converter em contorno" (EngineOmegaOutline.cpp).
//   - a forma viva vira camada vetorial com a MESMA silhueta (comparada com a
//     distância com sinal do shader numa grade), cor e traço;
//   - retângulo sai exato (4 pontos), estrela com os cantos vivos, anel com o furo;
//   - um passo de desfazer; travada e vetorial recusam.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <vector>

using namespace aurea;

namespace {

struct OutlineRig {
    Engine e;
    OutlineRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    }
    ~OutlineRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    /// Forma do preset numa caixa 200×160, sem trilhas, sem contorno.
    u64 shape(u32 preset) {
        const auto id = e.add_shape(preset);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return 0;
        Layer* l = L(*id);
        l->tracks = TrackSet{};
        l->shape.bounds = Rect{0.0f, 0.0f, 200.0f, 160.0f};
        l->shape.strokeWidth = 0.0f;
        l->shape.fillColor = Vec4{0.9f, 0.2f, 0.1f, 1.0f};
        l->shape.fillKind = 0;
        return *id;
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
};

/// Par-ímpar sobre os contornos achatados.
bool inside(const std::vector<vector::Contour>& cs, Vec2 p) {
    bool in = false;
    for (const auto& c : cs) {
        const usize n = c.pts.size();
        for (usize i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = c.pts[i], b = c.pts[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
    }
    return in;
}

/// Fração dos pontos da grade em que a silhueta do vetor difere da SDF.
f32 mismatch(const ShapeData& before, const ShapeData& after) {
    std::vector<vector::Contour> cs;
    for (const auto& g : after.vector.groups)
        for (const auto& p : g.paths) {
            vector::Contour c;
            vector::flatten(p.path, 0.1f, c);
            cs.push_back(std::move(c));
        }
    const f32 w = before.bounds.w, h = before.bounds.h;
    const Vec2 half{w * 0.5f, h * 0.5f};
    u32 total = 0, diff = 0;
    for (f32 y = 0.5f; y < h; y += 1.0f)
        for (f32 x = 0.5f; x < w; x += 1.0f) {
            const bool sdf = shape::signed_distance(before, Vec2{x - half.x, y - half.y}, half) < 0.0f;
            if (sdf) ++total;
            if (sdf != inside(cs, Vec2{x, y})) ++diff;
        }
    return total ? static_cast<f32>(diff) / static_cast<f32>(total) : 1.0f;
}

} // namespace

AUREA_TEST(OmegaOutline, StarKeepsTheSilhouetteColorAndSharpCorners) {
    OutlineRig r;
    const u64 id = r.shape(11);   // Estrela de 5 pontas
    const ShapeData before = r.L(id)->shape;
    AUREA_CHECK(r.e.convert_shape_to_outline(id));
    const Layer* l = r.L(id);
    AUREA_CHECK_EQ(l->shape.shapeType, kShapeVector);
    AUREA_CHECK_EQ(l->shape.vector.groups.size(), usize{1});
    const VectorGroup& g = l->shape.vector.groups[0];
    AUREA_CHECK_EQ(g.paths.size(), usize{1});
    AUREA_CHECK(g.paths[0].path.closed);
    AUREA_CHECK(g.paths[0].kind == VectorPathKind::Free);
    // 10 cantos vivos: poucos pontos, nunca a nuvem da grade.
    AUREA_CHECK(g.paths[0].path.v.size() >= 10 && g.paths[0].path.v.size() <= 60);
    AUREA_CHECK(g.fill.enabled);
    AUREA_CHECK_NEAR(g.fill.paint.color.x, 0.9f, 1e-4f);
    AUREA_CHECK_NEAR(g.fill.paint.color.y, 0.2f, 1e-4f);
    AUREA_CHECK(!g.stroke.enabled);
    AUREA_CHECK(mismatch(before, l->shape) < 0.02f);
}

AUREA_TEST(OmegaOutline, RectIsExactAndRingKeepsTheHole) {
    OutlineRig r;
    const u64 rect = r.shape(10);   // Quadrado
    AUREA_CHECK(r.e.convert_shape_to_outline(rect));
    AUREA_CHECK_EQ(r.L(rect)->shape.vector.groups[0].paths[0].path.v.size(), usize{4});

    const u64 ring = r.shape(3);    // Anel
    const ShapeData before = r.L(ring)->shape;
    AUREA_CHECK(r.e.convert_shape_to_outline(ring));
    const VectorGroup& g = r.L(ring)->shape.vector.groups[0];
    AUREA_CHECK_EQ(g.paths.size(), usize{2});   // borda de fora + furo
    AUREA_CHECK_EQ(g.fill.rule, u8{1});
    AUREA_CHECK(mismatch(before, r.L(ring)->shape) < 0.02f);
}

AUREA_TEST(OmegaOutline, EveryLiveShapeConverts) {
    OutlineRig r;
    for (u32 preset = 0; preset < 33; ++preset) {
        const u64 id = r.shape(preset);
        const ShapeData before = r.L(id)->shape;
        AUREA_CHECK_MSG(r.e.convert_shape_to_outline(id), "preset sem contorno");
        AUREA_CHECK_EQ(r.L(id)->shape.shapeType, kShapeVector);
        AUREA_CHECK_MSG(mismatch(before, r.L(id)->shape) < 0.04f, "silhueta diferente da forma viva");
    }
}

AUREA_TEST(OmegaOutline, OneUndoAndRefusals) {
    OutlineRig r;
    const u64 id = r.shape(26);   // Coração
    const u32 type = r.L(id)->shape.shapeType;
    AUREA_CHECK(r.e.convert_shape_to_outline(id));
    // Já é vetor: não converte de novo.
    AUREA_CHECK(!r.e.convert_shape_to_outline(id));
    r.undo();
    AUREA_CHECK_EQ(r.L(id)->shape.shapeType, type);
    // Travada: recusa.
    r.L(id)->locked = true;
    AUREA_CHECK(!r.e.convert_shape_to_outline(id));
    AUREA_CHECK_EQ(r.L(id)->shape.shapeType, type);
}

// =============================================================================
// 0.5.0: contorno exato. O número de pontos depende só da forma e dos
// parâmetros, nunca do tamanho: o que o morph ponto a ponto precisa.
// =============================================================================
namespace {
struct ExactCase { u32 type; u32 paths; u32 points; };   // points 0 = não fixo no teste
const ExactCase kExact[] = {
    {shape::kPolygon, 1, 6},        {shape::kStar, 1, 12},        {shape::kCross, 1, 12},       {shape::kRing, 2, 16},
    {shape::kPie, 1, 0},            {shape::kArc, 1, 0},          {shape::kFlower, 1, 24},      {shape::kArrow, 1, 7},
    {shape::kRightTriangle, 1, 3},  {shape::kTrapezoid, 1, 4},    {shape::kParallelogram, 1, 4},{shape::kGear, 2, 0},
    {shape::kDoubleArrow, 1, 10},   {shape::kLine, 1, 0},         {shape::kDiamond, 1, 4},      {shape::kHeart, 1, 0},
    {shape::kSeal, 1, 0},           {shape::kBubble, 1, 0},       {shape::kBolt, 1, 7},         {shape::kWave, 1, 0},
    {shape::kBlob, 1, 0},           {shape::kMoon, 1, 0},         {shape::kTeardrop, 1, 0},
};
ShapeData configure(u32 type, Vec2 box) {
    ShapeData sh;
    sh.shapeType = type;
    sh.bounds = Rect{0.0f, 0.0f, box.x, box.y};
    sh.points = 6.f; sh.innerRadius = .45f; sh.cornerRadius = 8.f;
    sh.strokeWidth = 0.f;
    sh.fillColor = Vec4{.9f, .2f, .1f, 1.f};
    sh.fillKind = 0;
    // A onda precisa ser suave para a borda não dobrar sobre si (aí é traçado).
    if (type == shape::kWave) { sh.points = 2.f; sh.thickness = .02f; sh.amplitude = .1f; }
    return sh;
}
u32 vertex_total(const ShapeData& vec) {
    u32 n = 0;
    for (const auto& p : vec.vector.groups[0].paths) n += static_cast<u32>(p.path.v.size());
    return n;
}
} // namespace

AUREA_TEST(OmegaOutline, ExactCurvesMatchTheSilhouetteAndNeverDependOnSize) {
    OutlineRig r;
    for (const ExactCase& c : kExact) {
        u32 reference = 0;
        bool first = true;
        for (const Vec2 box : {Vec2{200.f, 200.f}, Vec2{320.f, 180.f}, Vec2{140.f, 300.f}, Vec2{60.f, 60.f}}) {
            const u64 id = r.shape(10);
            Layer* l = r.L(id);
            l->shape = configure(c.type, box);
            const ShapeData before = l->shape;
            AUREA_CHECK(r.e.convert_shape_to_outline(id));
            const ShapeData& after = r.L(id)->shape;
            AUREA_CHECK_EQ(after.vector.groups[0].paths.size(), static_cast<usize>(c.paths));
            const u32 total = vertex_total(after);
            if (first) { reference = total; first = false; }
            const f32 miss = mismatch(before, after);
            // A gota vira círculo quando a ponta cabe dentro da base (caixa baixa).
            if (c.type != shape::kTeardrop) AUREA_CHECK_MSG(total == reference, "o numero de pontos mudou com o tamanho");
            if (c.points) AUREA_CHECK_EQ(total, c.points);
            AUREA_CHECK_MSG(total <= 120u, "pontos demais para editar");
            // A fita fina da onda tem pouca área: a borda de 1 px já pesa na conta.
            const f32 allowed = box.x < 100.f ? 0.06f : c.type == shape::kWave ? 0.03f : 0.015f;
            AUREA_CHECK_MSG(miss < allowed, "silhueta diferente da forma viva");
        }
    }
}

AUREA_TEST(OmegaOutline, TopologyChangesFallBackToTheTrace) {
    OutlineRig r;
    // Flor acima de 0,5: as pétalas se soltam; ainda precisa converter igual.
    const u64 id = r.shape(10);
    Layer* l = r.L(id);
    l->shape = configure(shape::kFlower, Vec2{200.f, 200.f});
    l->shape.depth = .7f;
    const ShapeData before = l->shape;
    AUREA_CHECK(r.e.convert_shape_to_outline(id));
    AUREA_CHECK(mismatch(before, r.L(id)->shape) < 0.04f);
    // Pontas fracionárias no meio de uma animação também.
    const u64 star = r.shape(10);
    Layer* s = r.L(star);
    s->shape = configure(shape::kStar, Vec2{200.f, 200.f});
    s->shape.points = 5.5f;
    AUREA_CHECK(r.e.convert_shape_to_outline(star));
}
