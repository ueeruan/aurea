// =============================================================================
//  Lote M (2026-11-12, gap_system itens 12, 23 e 25): máscaras e rig do After
//  Effects — feather X/Y e de largura variável, RotoBezier, travar/ocultar
//  máscara, Free Transform Points, Create Nulls from Paths (Points Follow
//  Nulls, Nulls Follow Points, Trace Path), Auto-trace e a seção MaskRig do
//  projeto. O AE é só a especificação de comportamento.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/timeline/PathRig.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct MaskRig {
    Engine e;
    MaskRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 shape() {
        const auto id = e.add_shape(10);   // quadrado
        AUREA_CHECK(id.ok());
        return id.ok() ? *id : 0;
    }
    /// Máscara quadrada (cantos vivos) em px da camada.
    u32 square(u64 layer, f32 x0, f32 y0, f32 x1, f32 y1) {
        const f32 pts[24] = {x0, y0, 0, 0, 0, 0, x1, y0, 0, 0, 0, 0, x1, y1, 0, 0, 0, 0, x0, y1, 0, 0, 0, 0};
        const i32 id = e.add_mask(layer, pts, 4, true);
        AUREA_CHECK(id >= 0);
        return static_cast<u32>(std::max(id, 0));
    }
    Mask* M(u64 layer, u32 mask) {
        Layer* l = L(layer);
        if (!l) return nullptr;
        for (Mask& m : l->masks) if (m.id == mask) return &m;
        return nullptr;
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
    Layer* named(const std::string& name) {
        Layer* found = nullptr;
        comp()->layers().for_each([&](LayerId, Layer& l) { if (l.name == name) found = &l; });
        return found;
    }
    /// O ponto de âncora da camada na composição no quadro.
    Vec3 anchor_at(const Layer& l, f64 frame) {
        return world::layer_world_frac(*comp(), l, frame).transform_point(l.transform.anchor);
    }
};

Mask square_mask(u32 id, f32 x0, f32 y0, f32 x1, f32 y1) {
    Mask m;
    m.id = id;
    m.points = {MaskPoint{Vec2{x0, y0}}, MaskPoint{Vec2{x1, y0}}, MaskPoint{Vec2{x1, y1}}, MaskPoint{Vec2{x0, y1}}};
    return m;
}

struct Block {
    std::vector<Vec4> data;
    f32 start = 0;
    u64 key = 0;
    u32 count = 0;
};

Block block_of(const Layer& l, f64 frame = 0.0) {
    Block b;
    b.count = mask::build_block(l, frame, Vec2{0, 0}, 0.2f, b.data, b.start, b.key);
    return b;
}

f32 coverage(const Layer& l, Vec2 p, f64 frame = 0.0) {
    const Block b = block_of(l, frame);
    return mask::coverage_at(b.data.data(), b.count, b.start, p, 1.0f);
}

/// Imagem RGBA: `inside(x, y)` decide o pixel opaco/branco; o resto é transparente/preto.
template <typename Fn>
std::vector<u8> picture(u32 w, u32 h, Fn&& inside) {
    std::vector<u8> px(static_cast<usize>(w) * h * 4, 0);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            if (!inside(x, y)) continue;
            u8* p = &px[(static_cast<usize>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = p[3] = 255;
        }
    return px;
}

void bounds_of(const std::vector<MaskPoint>& pts, Vec2& lo, Vec2& hi) {
    lo = Vec2{1e30f, 1e30f};
    hi = Vec2{-1e30f, -1e30f};
    for (const MaskPoint& p : pts) {
        lo = Vec2{std::min(lo.x, p.position.x), std::min(lo.y, p.position.y)};
        hi = Vec2{std::max(hi.x, p.position.x), std::max(hi.y, p.position.y)};
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Projeto antigo: o bloco da máscara e a cobertura não mudam
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, PlainMaskKeepsTheOldBlockAndCoverage) {
    Layer l;
    l.kind = LayerKind::Shape;
    Mask m = square_mask(0, 100, 100, 200, 200);
    m.feather = 20.0f;
    l.masks.push_back(m);
    const Block b = block_of(l);
    AUREA_CHECK_EQ(b.count, 1u);
    // 3 de cabeçalho + as arestas (Wang reparte a reta sem alças em várias), e
    // h1.w = só a queda (0): nada de bloco estendido.
    AUREA_CHECK(b.data.size() > 3);
    if (b.data.size() <= 3) return;
    AUREA_CHECK_EQ(b.data.size(), static_cast<usize>(3) + static_cast<usize>(b.data[1].z + 0.5f));
    AUREA_CHECK_NEAR(b.data[1].w, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(b.data[0].w, 5.0f, 1e-6f);   // σ = feather / 4
    // A conta de sempre: gaussiana do semiplano na borda, 1 dentro, 0 longe.
    AUREA_CHECK_NEAR(coverage(l, Vec2{150, 150}), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{100, 150}), 0.5f, 1e-3f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{60, 150}), 0.0f, 1e-3f);
    const f32 s = std::sqrt(25.0f + 1.0f / 12.0f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{105, 150}), 0.5f * (1.0f + std::erf(5.0f / (s * 1.41421356f))), 2e-4f);
    // Linear também: h1.w = 1 e a rampa reta.
    l.masks[0].featherFalloff = 1;
    const Block lin = block_of(l);
    AUREA_CHECK_NEAR(lin.data[1].w, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{105, 150}), 5.0f / std::sqrt(400.0f + 1.0f) + 0.5f, 2e-4f);
    // Feather Y igual ao X escrito à mão = o mesmo bloco (a mesma chave de cache).
    l.masks[0].featherFalloff = 0;
    Layer same = l;
    same.masks[0].featherY = 20.0f;
    AUREA_CHECK_EQ(block_of(same).key, b.key);
}

// -----------------------------------------------------------------------------
// Feather X e Y separados
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, FeatherXAndYAreSeparate) {
    Layer l;
    l.kind = LayerKind::Shape;
    Mask m = square_mask(0, 100, 100, 300, 300);
    m.feather = 40.0f;      // X
    m.featherY = 0.0f;      // Y sem feather
    l.masks.push_back(m);
    const Block b = block_of(l);
    AUREA_CHECK(b.data.size() > 3);
    if (b.data.size() <= 3) return;
    const usize ne = static_cast<usize>(b.data[1].z + 0.5f);
    AUREA_CHECK_EQ(b.data.size(), 3 + ne + 1);   // + 1 vec4 do bloco estendido
    // As arestas são as da máscara comum: o feather Y não muda o caminho achatado.
    Layer plainLayer = l;
    plainLayer.masks[0].featherY = -1.0f;
    const Block plainBlock = block_of(plainLayer);
    AUREA_CHECK_EQ(plainBlock.data.size(), 3 + ne);
    AUREA_CHECK_NEAR(b.data[1].w, 2.0f, 1e-6f);
    // 8 px fora da borda ESQUERDA (normal em X): ainda dentro da rampa do feather X.
    const f32 left = coverage(l, Vec2{92, 200});
    AUREA_CHECK(left > 0.15f && left < 0.35f);
    // 8 px fora da borda de CIMA (normal em Y): sem feather, nada.
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 92}), 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 108}), 1.0f, 1e-3f);
    // Trocando: só o Y borra.
    l.masks[0].feather = 0.0f;
    l.masks[0].featherY = 40.0f;
    AUREA_CHECK_NEAR(coverage(l, Vec2{92, 200}), 0.0f, 1e-3f);
    const f32 top = coverage(l, Vec2{200, 92});
    AUREA_CHECK_NEAR(top, left, 1e-3f);
    // Y = X dá a mesma cobertura da máscara comum, em qualquer direção.
    Layer plain = l;
    plain.masks[0].feather = 40.0f;
    plain.masks[0].featherY = -1.0f;
    l.masks[0].feather = 40.0f;
    l.masks[0].featherY = 40.0f;
    for (const Vec2 p : {Vec2{92, 200}, Vec2{200, 92}, Vec2{95, 95}, Vec2{310, 305}, Vec2{150, 104}})
        AUREA_CHECK_NEAR(coverage(l, p), coverage(plain, p), 1e-4f);
}

AUREA_TEST(AeMasks, EngineFeatherXYIsUndoable) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 10, 10, 90, 90);
    AUREA_CHECK(r.e.set_mask_feather_xy(id, m, 30.0f, 6.0f, false));
    AUREA_CHECK_NEAR(r.M(id, m)->feather, 30.0f, 1e-6f);
    AUREA_CHECK_NEAR(r.M(id, m)->featherY, 6.0f, 1e-6f);
    f32 extra[16]{};
    AUREA_CHECK_EQ(r.e.query_mask_extras(id, m, extra, 16), Engine::kMaskExtraHeaderFloats);
    AUREA_CHECK_NEAR(extra[1], 6.0f, 1e-6f);
    AUREA_CHECK(r.e.set_mask_feather_xy(id, m, 30.0f, -1.0f, false));   // Y volta a acompanhar o X
    AUREA_CHECK(r.M(id, m)->featherY < 0.0f);
    r.undo();
    AUREA_CHECK_NEAR(r.M(id, m)->featherY, 6.0f, 1e-6f);
    r.undo();
    AUREA_CHECK(r.M(id, m)->featherY < 0.0f);
    AUREA_CHECK_NEAR(r.M(id, m)->feather, 0.0f, 1e-6f);
    AUREA_CHECK(!r.e.set_mask_feather_xy(id, 99, 1.0f, 1.0f, false));
}

// -----------------------------------------------------------------------------
// Feather de largura variável
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, VariableFeatherInterpolatesAlongTheEdge) {
    Layer l;
    l.kind = LayerKind::Shape;
    // Trechos: 0 = cima, 1 = direita, 2 = baixo, 3 = esquerda (todos de 200 px).
    l.masks.push_back(square_mask(0, 100, 100, 300, 300));
    Layer plain = l;
    // Raio 60 no meio de cima, 0 no meio de baixo.
    l.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, 60.0f, 0.0f, false}, MaskFeatherPoint{2, 0.5f, 0.0f, 0.0f, false}};
    const Block b = block_of(l);
    AUREA_CHECK(b.data.size() > 3 + 4 + 1);
    if (b.data.size() <= 8) return;
    AUREA_CHECK_NEAR(b.data[1].w, 2.0f, 1e-6f);                      // estendido
    const u32 ne = static_cast<u32>(b.data[1].z + 0.5f);
    AUREA_CHECK(ne > 4);                                             // as retas foram repartidas
    AUREA_CHECK_EQ(b.data.size(), static_cast<usize>(3 + ne + 1 + ne));
    AUREA_CHECK_NEAR(b.data[3 + ne].y, 1.0f, 1e-6f);                 // tem raio variável
    AUREA_CHECK_NEAR(b.data[3 + ne].z, 60.0f, 1e-4f);                // o maior raio (margem da caixa)
    // 15 px FORA do meio de cada lado.
    const f32 top = coverage(l, Vec2{200, 85}), bottom = coverage(l, Vec2{200, 315});
    const f32 right = coverage(l, Vec2{315, 200}), leftSide = coverage(l, Vec2{85, 200});
    // Em cima o raio é 60: a rampa vai da borda até 60 px fora, meio em 30 px.
    // 15 px fora = (30 − 15) px antes do meio, σ = 60/4.
    const f32 sTop = std::sqrt(15.0f * 15.0f + 1.0f / 12.0f);
    AUREA_CHECK_NEAR(top, 0.5f * (1.0f + std::erf(15.0f / (sTop * 1.41421356f))), 2e-3f);
    AUREA_CHECK_NEAR(bottom, 0.0f, 1e-3f);                           // raio 0: borda viva
    // Os lados ficam no meio do caminho (raio 30) e são iguais entre si.
    AUREA_CHECK(right > bottom + 0.2f && right < top - 0.1f);
    AUREA_CHECK_NEAR(right, leftSide, 2e-3f);
    const f32 sSide = std::sqrt(7.5f * 7.5f + 1.0f / 12.0f);
    AUREA_CHECK_NEAR(right, 0.5f * (1.0f + std::erf(0.0f / (sSide * 1.41421356f))), 3e-3f);   // 15 px = o meio da rampa de 30
    // Para dentro nada muda com raio positivo: 30 px dentro continua cheio... e a máscara comum é viva.
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 290}), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(coverage(plain, Vec2{200, 85}), 0.0f, 1e-3f);
    // O raio cresce de baixo para cima ao longo do lado direito (monótono).
    f32 last = -1.0f;
    for (f32 y = 290.0f; y >= 110.0f; y -= 20.0f) {
        const f32 c = coverage(l, Vec2{310, y});
        AUREA_CHECK(c >= last - 1e-4f);
        last = c;
    }
}

AUREA_TEST(AeMasks, VariableFeatherSinglePointInnerAndHold) {
    Layer l;
    l.kind = LayerKind::Shape;
    l.masks.push_back(square_mask(0, 100, 100, 300, 300));
    // Um ponto só: o mesmo raio na máscara inteira.
    l.masks[0].featherPoints = {MaskFeatherPoint{1, 0.25f, 40.0f, 0.0f, false}};
    const f32 a = coverage(l, Vec2{200, 90}), b = coverage(l, Vec2{310, 200}), c = coverage(l, Vec2{200, 310}), d = coverage(l, Vec2{90, 200});
    AUREA_CHECK(a > 0.6f && a < 0.95f);
    AUREA_CHECK_NEAR(a, b, 2e-3f);
    AUREA_CHECK_NEAR(a, c, 2e-3f);
    AUREA_CHECK_NEAR(a, d, 2e-3f);
    // Raio negativo = feather para DENTRO: fora some, dentro da rampa cai.
    l.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, -40.0f, 0.0f, false}};
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 95}), 0.0f, 2e-2f);
    const f32 inside = coverage(l, Vec2{200, 110});
    AUREA_CHECK(inside > 0.05f && inside < 0.4f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 120}), 0.5f, 5e-3f);       // o meio da rampa de 40 px
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 200}), 1.0f, 1e-3f);
    // Segurar: o raio do ponto vale até o próximo (sem interpolar).
    l.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, 60.0f, 0.0f, true}, MaskFeatherPoint{2, 0.5f, 0.0f, 0.0f, false}};
    Layer uniform = l;
    uniform.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, 60.0f, 0.0f, false}};
    AUREA_CHECK_NEAR(coverage(l, Vec2{315, 200}), coverage(uniform, Vec2{315, 200}), 2e-3f);   // direita: ainda 60
    AUREA_CHECK(coverage(l, Vec2{85, 200}) < coverage(uniform, Vec2{85, 200}) - 0.1f);         // esquerda: interpola de 0 a 60
    // Tensão 1 = curva suave: perto do ponto o raio muda mais devagar que na reta.
    Layer straight = l, smooth = l;
    straight.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, 60.0f, 0.0f, false}, MaskFeatherPoint{2, 0.5f, 0.0f, 0.0f, false}};
    smooth.masks[0].featherPoints = {MaskFeatherPoint{0, 0.5f, 60.0f, 1.0f, false}, MaskFeatherPoint{2, 0.5f, 0.0f, 1.0f, false}};
    AUREA_CHECK(coverage(smooth, Vec2{270, 85}) > coverage(straight, Vec2{270, 85}) + 0.01f);
    AUREA_CHECK_NEAR(coverage(smooth, Vec2{315, 200}), coverage(straight, Vec2{315, 200}), 3e-3f);   // no meio as duas se cruzam
}

AUREA_TEST(AeMasks, EngineFeatherPointsRoundTripAndValidate) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 10, 10, 90, 90);
    const f32 pts[10] = {0, 0.5f, 24.0f, 0.25f, 0, 2, 0.75f, -8.0f, 1.0f, 1};
    AUREA_CHECK(r.e.set_mask_feather_points(id, m, pts, 2, false));
    AUREA_CHECK_EQ(r.M(id, m)->featherPoints.size(), static_cast<usize>(2));
    f32 out[32]{};
    const u32 need = r.e.query_mask_extras(id, m, out, 32);
    AUREA_CHECK_EQ(need, Engine::kMaskExtraHeaderFloats + 10u);
    AUREA_CHECK_NEAR(out[2], 2.0f, 1e-6f);
    for (u32 i = 0; i < 10; ++i) AUREA_CHECK_NEAR(out[Engine::kMaskExtraHeaderFloats + i], pts[i], 1e-6f);
    // Pequeno demais: só o tamanho.
    AUREA_CHECK_EQ(r.e.query_mask_extras(id, m, out, 3), need);
    const f32 bad[5] = {0, 0.5f, std::nanf(""), 0, 0};
    AUREA_CHECK(!r.e.set_mask_feather_points(id, m, bad, 1, false));
    AUREA_CHECK(!r.e.set_mask_feather_points(id, m, pts, kMaxFeatherPoints + 1, false));
    AUREA_CHECK(r.e.set_mask_feather_points(id, m, nullptr, 0, false));
    AUREA_CHECK(r.M(id, m)->featherPoints.empty());
    r.undo();
    AUREA_CHECK_EQ(r.M(id, m)->featherPoints.size(), static_cast<usize>(2));
}

// -----------------------------------------------------------------------------
// RotoBezier
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, RotoBezierDerivesTangentsFromNeighbours) {
    Mask m = square_mask(0, 0, 0, 100, 100);
    std::vector<MaskPoint> plain, roto;
    mask::evaluate_path(m, 0.0, plain);
    for (const MaskPoint& p : plain) AUREA_CHECK(p.inTangent.x == 0.0f && p.outTangent.y == 0.0f);
    m.rotoBezier = true;
    mask::evaluate_path(m, 0.0, roto);
    AUREA_CHECK_EQ(roto.size(), static_cast<usize>(4));
    if (roto.size() != 4) return;
    for (usize i = 0; i < 4; ++i) {
        AUREA_CHECK(roto[i].position.x == plain[i].position.x && roto[i].position.y == plain[i].position.y);
        // A tangente segue a corda entre os vizinhos; entrada e saída opostas.
        const Vec2 chord{plain[(i + 1) % 4].position.x - plain[(i + 3) % 4].position.x,
                         plain[(i + 1) % 4].position.y - plain[(i + 3) % 4].position.y};
        const Vec2 o = roto[i].outTangent, in = roto[i].inTangent;
        AUREA_CHECK_NEAR(o.x * chord.y - o.y * chord.x, 0.0f, 1e-3f);
        AUREA_CHECK(o.x * chord.x + o.y * chord.y > 0.0f);
        AUREA_CHECK_NEAR(o.x + in.x, 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(o.y + in.y, 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(std::sqrt(o.x * o.x + o.y * o.y), 100.0f / 3.0f, 1e-3f);   // um terço do trecho
    }
    // Tensão por vértice: 0 = canto, 0,5 = metade do comprimento.
    m.rotoTension = {0.0f, 0.5f};
    mask::evaluate_path(m, 0.0, roto);
    AUREA_CHECK(roto[0].outTangent.x == 0.0f && roto[0].outTangent.y == 0.0f && roto[0].inTangent.x == 0.0f);
    AUREA_CHECK_NEAR(std::sqrt(roto[1].outTangent.x * roto[1].outTangent.x + roto[1].outTangent.y * roto[1].outTangent.y),
                     100.0f / 6.0f, 1e-3f);
    AUREA_CHECK_NEAR(std::sqrt(roto[2].outTangent.x * roto[2].outTangent.x + roto[2].outTangent.y * roto[2].outTangent.y),
                     100.0f / 3.0f, 1e-3f);   // faltando = 1
    // O recorte acompanha: a curva estufa para fora dos lados do quadrado.
    Layer l;
    l.kind = LayerKind::Shape;
    Mask big = square_mask(0, 100, 100, 300, 300);
    l.masks.push_back(big);
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 90}), 0.0f, 1e-3f);
    l.masks[0].rotoBezier = true;
    AUREA_CHECK(coverage(l, Vec2{200, 90}) > 0.9f);
    AUREA_CHECK_NEAR(coverage(l, Vec2{200, 200}), 1.0f, 1e-3f);
    // Caminho aberto: as pontas só têm a tangente de dentro.
    std::vector<MaskPoint> open = {MaskPoint{Vec2{0, 0}}, MaskPoint{Vec2{50, 50}}, MaskPoint{Vec2{100, 0}}};
    pathrig::roto_bezier(open, false, {});
    AUREA_CHECK(open[0].inTangent.x == 0.0f && open[0].inTangent.y == 0.0f && open[0].outTangent.x > 0.0f);
    AUREA_CHECK(open[2].outTangent.x == 0.0f && open[2].outTangent.y == 0.0f && open[2].inTangent.x < 0.0f);
    AUREA_CHECK_NEAR(open[1].outTangent.y, 0.0f, 1e-4f);
}

AUREA_TEST(AeMasks, EngineMaskFlagsLockHideAndRoto) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 10, 10, 90, 90);
    AUREA_CHECK(r.e.set_mask_flags(id, m, Engine::kMaskRotoBezier, Engine::kMaskRotoBezier));
    AUREA_CHECK(r.M(id, m)->rotoBezier && !r.M(id, m)->locked && !r.M(id, m)->hidden);
    AUREA_CHECK(r.e.set_mask_roto_tension(id, m, 2, 0.25f, false));
    AUREA_CHECK_EQ(r.M(id, m)->rotoTension.size(), static_cast<usize>(4));
    AUREA_CHECK_NEAR(r.M(id, m)->rotoTension[2], 0.25f, 1e-6f);
    AUREA_CHECK_NEAR(r.M(id, m)->rotoTension[0], 1.0f, 1e-6f);
    AUREA_CHECK(r.e.set_mask_roto_tension(id, m, kInvalidIndex, 0.5f, false));
    for (const f32 t : r.M(id, m)->rotoTension) AUREA_CHECK_NEAR(t, 0.5f, 1e-6f);
    AUREA_CHECK(!r.e.set_mask_roto_tension(id, m, 9, 0.5f, false));
    // Travar: os comandos de edição do caminho recusam; ocultar não muda o recorte.
    const u64 keyBefore = block_of(*r.L(id)).key;
    AUREA_CHECK(r.e.set_mask_flags(id, m, Engine::kMaskLocked | Engine::kMaskHidden, Engine::kMaskLocked | Engine::kMaskHidden));
    AUREA_CHECK(r.M(id, m)->locked && r.M(id, m)->hidden && r.M(id, m)->rotoBezier);
    AUREA_CHECK_EQ(block_of(*r.L(id)).key, keyBefore);
    const f32 move[3] = {5.0f, 5.0f, 0.0f};
    AUREA_CHECK_EQ(r.e.edit_mask_path(id, m, Engine::PathOp::MovePoints, move, 3, false), -1);
    const f32 tri[18] = {0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0};
    AUREA_CHECK(!r.e.set_mask_path(id, m, tri, 3, true, true));
    const f32 ident[6] = {2, 0, 0, 2, 0, 0};
    AUREA_CHECK(!r.e.transform_mask_points(id, m, nullptr, 0, ident, false));
    AUREA_CHECK(!r.e.set_mask_roto_tension(id, m, 0, 0.1f, false));
    AUREA_CHECK(!r.e.set_mask_flags(id, m, 0, Engine::kMaskRotoBezier));   // travada: o RotoBezier não troca
    AUREA_CHECK_EQ(r.M(id, m)->points.size(), static_cast<usize>(4));
    f32 extra[16]{};
    (void)r.e.query_mask_extras(id, m, extra, 16);
    AUREA_CHECK_NEAR(extra[0], 7.0f, 1e-6f);
    // Destravar volta a aceitar; desfazer devolve as chaves.
    AUREA_CHECK(r.e.set_mask_flags(id, m, 0, Engine::kMaskLocked));
    AUREA_CHECK(r.e.edit_mask_path(id, m, Engine::PathOp::MovePoints, move, 3, false) >= 0);
    r.undo();
    r.undo();
    AUREA_CHECK(r.M(id, m)->locked && r.M(id, m)->hidden);
    r.undo();
    AUREA_CHECK(!r.M(id, m)->locked && !r.M(id, m)->hidden && r.M(id, m)->rotoBezier);
}

// -----------------------------------------------------------------------------
// Free Transform Points
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, FreeTransformPointsScalesAndRotatesTheSelection) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const f32 pts[24] = {10, 10, -4, 0, 4, 0, 90, 10, 0, 0, 0, 0, 90, 90, 0, 0, 0, 0, 10, 90, 0, 0, 0, 0};
    const i32 mi = r.e.add_mask(id, pts, 4, true);
    AUREA_CHECK(mi >= 0);
    const u32 m = static_cast<u32>(mi);
    // Escala 2× em volta de (50, 50) nos pontos 0 e 1: a b c d tx ty.
    const f32 scale[6] = {2, 0, 0, 2, -50, -50};
    const u32 sel[2] = {0, 1};
    AUREA_CHECK(r.e.transform_mask_points(id, m, sel, 2, scale, false));
    const Mask* mk = r.M(id, m);
    AUREA_CHECK_NEAR(mk->points[0].position.x, -30.0f, 1e-4f);
    AUREA_CHECK_NEAR(mk->points[0].position.y, -30.0f, 1e-4f);
    AUREA_CHECK_NEAR(mk->points[0].outTangent.x, 8.0f, 1e-4f);     // a tangente leva a parte linear
    AUREA_CHECK_NEAR(mk->points[0].inTangent.x, -8.0f, 1e-4f);
    AUREA_CHECK_NEAR(mk->points[1].position.x, 130.0f, 1e-4f);
    AUREA_CHECK_NEAR(mk->points[2].position.x, 90.0f, 1e-4f);      // fora da seleção: parado
    AUREA_CHECK_NEAR(mk->points[3].position.y, 90.0f, 1e-4f);
    // Giro de 90° de todos (sem índices) em volta da origem.
    const f32 rot[6] = {0, 1, -1, 0, 0, 0};
    AUREA_CHECK(r.e.transform_mask_points(id, m, nullptr, 0, rot, false));
    AUREA_CHECK_NEAR(r.M(id, m)->points[2].position.x, -90.0f, 1e-4f);
    AUREA_CHECK_NEAR(r.M(id, m)->points[2].position.y, 90.0f, 1e-4f);
    r.undo();
    r.undo();
    AUREA_CHECK_NEAR(r.M(id, m)->points[0].position.x, 10.0f, 1e-4f);
    const u32 badSel[1] = {7};
    AUREA_CHECK(!r.e.transform_mask_points(id, m, badSel, 1, rot, false));
    // Caminho animado: grava o key do cabeçote.
    AUREA_CHECK(r.e.toggle_mask_path_key(id, m));
    AUREA_CHECK(r.e.transform_mask_points(id, m, nullptr, 0, scale, false));
    AUREA_CHECK_EQ(r.M(id, m)->pathKeys.size(), static_cast<usize>(1));
    AUREA_CHECK_NEAR(r.M(id, m)->pathKeys[0].points[0].position.x, -30.0f, 1e-4f);
}

// -----------------------------------------------------------------------------
// Create Nulls from Paths
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, PointsFollowNullsDrivesMaskVertices) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    // A camada sai do centro e ganha escala: o vértice precisa voltar pelo inverso dela.
    r.L(id)->transform.position = Vec3{200.0f, 120.0f, 0.0f};
    r.L(id)->transform.scale = Vec3{2.0f, 2.0f, 1.0f};
    const u32 m = r.square(id, 10, 10, 60, 60);
    std::vector<u64> nulls;
    const auto made = r.e.create_nulls_from_path(id, 0, m, 0, Engine::NullsFromPath::PointsFollowNulls, false, &nulls);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    AUREA_CHECK_EQ(*made, 4u);
    AUREA_CHECK_EQ(nulls.size(), static_cast<usize>(4));
    AUREA_CHECK_EQ(r.M(id, m)->drivers.size(), static_cast<usize>(4));
    if (nulls.size() != 4) return;
    // Os nulos nascem em cima dos vértices: o caminho continua igual.
    const Mat4 world = world::layer_world_frac(*r.comp(), *r.L(id), 0.0);
    std::vector<MaskPoint> pts;
    mask::evaluate_path(*r.M(id, m), 0.0, pts);
    AUREA_CHECK_EQ(pts.size(), static_cast<usize>(4));
    const f32 base[8] = {10, 10, 60, 10, 60, 60, 10, 60};
    for (usize i = 0; i < 4 && i < pts.size(); ++i) {
        AUREA_CHECK_NEAR(pts[i].position.x, base[i * 2], 1e-2f);
        AUREA_CHECK_NEAR(pts[i].position.y, base[i * 2 + 1], 1e-2f);
        const Layer* n = r.L(nulls[i]);
        AUREA_CHECK(n && n->kind == LayerKind::Null);
        if (!n) return;
        const Vec3 want = world.transform_point(Vec3{base[i * 2], base[i * 2 + 1], 0.0f});
        const Vec3 got = r.anchor_at(*n, 0.0);
        AUREA_CHECK_NEAR(got.x, want.x, 1e-2f);
        AUREA_CHECK_NEAR(got.y, want.y, 1e-2f);
    }
    // Mover o nulo 2 (40 px na composição = 20 px da camada, que tem escala 2).
    r.L(nulls[2])->transform.position.x += 40.0f;
    r.L(nulls[2])->transform.position.y -= 10.0f;
    mask::evaluate_path(*r.M(id, m), 0.0, pts);
    AUREA_CHECK_NEAR(pts[2].position.x, 80.0f, 1e-2f);
    AUREA_CHECK_NEAR(pts[2].position.y, 55.0f, 1e-2f);
    AUREA_CHECK_NEAR(pts[0].position.x, 10.0f, 1e-2f);
    // O caminho GRAVADO não muda (a ligação vale por cima) e o recorte acompanha.
    AUREA_CHECK_NEAR(r.M(id, m)->points[2].position.x, 60.0f, 1e-4f);
    AUREA_CHECK(coverage(*r.L(id), Vec2{70, 50}) > 0.9f);
    // Dentro de um Scope (o quadro do renderer) dá o mesmo.
    {
        const expr::Scope scope(r.e.project()->timeline());
        std::vector<MaskPoint> scoped;
        mask::evaluate_path(*r.M(id, m), 0.0, scoped);
        AUREA_CHECK_NEAR(scoped[2].position.x, 80.0f, 1e-2f);
    }
    // Nulo animado: o vértice segue quadro a quadro.
    Layer* n0 = r.L(nulls[0]);
    const f32 x0 = n0->transform.position.x;
    Track& tx = n0->tracks.get_or_create(TrackProperty::PositionX);
    (void)tx.set(FrameIndex{0}, x0, Interpolation::Linear);
    (void)tx.set(FrameIndex{10}, x0 - 100.0f, Interpolation::Linear);
    mask::evaluate_path(*r.M(id, m), 5.0, pts);
    AUREA_CHECK_NEAR(pts[0].position.x, 10.0f - 25.0f, 5e-2f);     // −50 px da composição / escala 2
    mask::evaluate_path(*r.M(id, m), 10.0, pts);
    AUREA_CHECK_NEAR(pts[0].position.x, 10.0f - 50.0f, 5e-2f);
    // Nulo apagado: o vértice volta para o gravado, sem derrubar nada.
    AUREA_CHECK(r.comp()->remove_layer(LayerId::unpack(nulls[2])));
    mask::evaluate_path(*r.M(id, m), 0.0, pts);
    AUREA_CHECK_NEAR(pts[2].position.x, 60.0f, 1e-2f);
    // Soltar: os vértices ficam onde os nulos os deixaram no cabeçote.
    AUREA_CHECK(r.e.clear_path_drivers(id, 0, m, 0));
    AUREA_CHECK(r.M(id, m)->drivers.empty());
    AUREA_CHECK_NEAR(r.M(id, m)->points[0].position.x, 10.0f, 5e-2f);
    AUREA_CHECK(!r.e.clear_path_drivers(id, 0, m, 0));
    // Um passo de desfazer para a criação inteira.
    MaskRig r2;
    const u64 id2 = r2.shape();
    const u32 m2 = r2.square(id2, 10, 10, 60, 60);
    const u32 before = r2.comp()->layers().count();
    AUREA_CHECK(r2.e.create_nulls_from_path(id2, 0, m2, 0, Engine::NullsFromPath::PointsFollowNulls, false).ok());
    AUREA_CHECK_EQ(r2.comp()->layers().count(), before + 4u);
    r2.undo();
    AUREA_CHECK_EQ(r2.comp()->layers().count(), before);
    AUREA_CHECK(r2.M(id2, m2)->drivers.empty());
    AUREA_CHECK(!r2.e.create_nulls_from_path(id2, 0, 77, 0, Engine::NullsFromPath::PointsFollowNulls, false).ok());
}

AUREA_TEST(AeMasks, NullsFollowPointsTracksAnimatedVertices) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 10, 10, 60, 60);
    std::vector<u64> nulls;
    const auto made = r.e.create_nulls_from_path(id, 0, m, 0, Engine::NullsFromPath::NullsFollowPoints, false, &nulls);
    AUREA_CHECK(made.ok() && nulls.size() == 4);
    if (nulls.size() != 4) return;
    AUREA_CHECK(r.M(id, m)->drivers.empty());
    AUREA_CHECK_EQ(r.L(nulls[1])->pathFollow.mode, PathFollow::kVertex);
    AUREA_CHECK_EQ(r.L(nulls[1])->pathFollow.vertex, 1u);
    // O caminho anima (keys de forma) E a camada anda: o nulo segue os dois.
    Mask* mk = r.M(id, m);
    MaskPathKey k0, k1;
    k0.frame = 0;
    k0.points = mk->points;
    k1.frame = 10;
    k1.points = mk->points;
    for (MaskPoint& p : k1.points) p.position.x += 40.0f;
    mk->pathKeys = {k0, k1};
    Layer* l = r.L(id);
    const f32 px = l->transform.position.x;
    Track& tx = l->tracks.get_or_create(TrackProperty::PositionX);
    (void)tx.set(FrameIndex{0}, px, Interpolation::Linear);
    (void)tx.set(FrameIndex{10}, px + 100.0f, Interpolation::Linear);
    for (const f64 frame : {0.0, 5.0, 10.0}) {
        std::vector<MaskPoint> pts;
        mask::evaluate_path(*r.M(id, m), frame, pts);
        const Mat4 world = world::layer_world_frac(*r.comp(), *r.L(id), frame);
        for (usize i = 0; i < 4; ++i) {
            const Vec3 want = world.transform_point(Vec3{pts[i].position.x, pts[i].position.y, 0.0f});
            const Vec3 got = r.anchor_at(*r.L(nulls[i]), frame);
            AUREA_CHECK_NEAR(got.x, want.x, 2e-2f);
            AUREA_CHECK_NEAR(got.y, want.y, 2e-2f);
        }
    }
    // No quadro 10 o vértice 0 andou 40 (forma) + 100 (camada) px.
    const Vec3 a0 = r.anchor_at(*r.L(nulls[0]), 0.0), a10 = r.anchor_at(*r.L(nulls[0]), 10.0);
    AUREA_CHECK_NEAR(a10.x - a0.x, 140.0f, 5e-2f);
    // Nulo com pai: a posição entra no espaço do pai (o ponto na tela é o mesmo).
    const auto parent = r.e.add_null(false);
    AUREA_CHECK(parent.ok());
    if (parent.ok()) {
        r.L(*parent)->transform.position = Vec3{77.0f, 33.0f, 0.0f};
        r.L(*parent)->transform.scale = Vec3{0.5f, 0.5f, 1.0f};
        r.L(nulls[3])->parent = LayerId::unpack(*parent);
        std::vector<MaskPoint> pts;
        mask::evaluate_path(*r.M(id, m), 5.0, pts);
        const Vec3 want = world::layer_world_frac(*r.comp(), *r.L(id), 5.0).transform_point(Vec3{pts[3].position.x, pts[3].position.y, 0.0f});
        const Vec3 got = r.anchor_at(*r.L(nulls[3]), 5.0);
        AUREA_CHECK_NEAR(got.x, want.x, 5e-2f);
        AUREA_CHECK_NEAR(got.y, want.y, 5e-2f);
    }
    // Soltar: a camada fica onde o caminho a deixou.
    f32 rig[9]{};
    AUREA_CHECK_EQ(r.e.query_path_rig(nulls[0], rig, 9), 9u);
    AUREA_CHECK_NEAR(rig[0], 1.0f, 1e-6f);
    const Vec3 here = r.anchor_at(*r.L(nulls[0]), 0.0);
    AUREA_CHECK(r.e.clear_path_follow(nulls[0]));
    AUREA_CHECK_EQ(r.L(nulls[0])->pathFollow.mode, PathFollow::kNone);
    AUREA_CHECK_NEAR(r.anchor_at(*r.L(nulls[0]), 0.0).x, here.x, 1e-2f);
    AUREA_CHECK_NEAR(r.anchor_at(*r.L(nulls[0]), 10.0).x, here.x, 1e-2f);   // não segue mais
    AUREA_CHECK(!r.e.clear_path_follow(nulls[0]));
}

AUREA_TEST(AeMasks, TracePathTravelsWithProgressAndLoop) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 0, 0, 100, 100);   // perímetro 400: 25 % por lado
    const Mat4 world = world::layer_world_frac(*r.comp(), *r.L(id), 0.0);
    auto corner = [&](f32 x, f32 y) { return world.transform_point(Vec3{x, y, 0.0f}); };
    std::vector<u64> nulls;
    const auto made = r.e.create_nulls_from_path(id, 0, m, 0, Engine::NullsFromPath::TracePath, false, &nulls);
    AUREA_CHECK(made.ok() && nulls.size() == 1);
    if (nulls.size() != 1) return;
    Layer* n = r.L(nulls[0]);
    AUREA_CHECK_EQ(n->pathFollow.mode, PathFollow::kTrace);
    AUREA_CHECK(n->pathFollow.progressEffect != kInvalidIndex && n->effects.size() == 1);
    AUREA_CHECK(!n->pathFollow.loop && n->pathFollow.orient);
    // O Progresso nasce com keys 0 → 100 % em 1 s (30 quadros).
    const Track* tr = n->tracks.find(TrackProperty::EffectParam, n->pathFollow.progressEffect, param_track_key(0, 0));
    AUREA_CHECK(tr && tr->keys.size() == 2);
    if (!tr || tr->keys.size() != 2) return;
    AUREA_CHECK_EQ(tr->keys[1].time.value, i64{30});
    auto at = [&](f64 frame) { return r.anchor_at(*r.L(nulls[0]), frame); };
    auto near_point = [&](Vec3 got, Vec3 want) {
        AUREA_CHECK_NEAR(got.x, want.x, 0.3f);
        AUREA_CHECK_NEAR(got.y, want.y, 0.3f);
    };
    near_point(at(0.0), corner(0, 0));           // 0 %
    near_point(at(7.5), corner(100, 0));         // 25 %
    near_point(at(15.0), corner(100, 100));      // 50 %
    near_point(at(22.5), corner(0, 100));        // 75 %
    near_point(at(3.75), corner(50, 0));         // 12,5 %: meio do primeiro lado
    near_point(at(30.0), corner(0, 0));          // 100 % de um caminho fechado = o começo
    near_point(at(45.0), corner(0, 0));          // sem laço: parado no fim
    // Orientar: a rotação soma a tangente (no segundo lado o caminho desce: +90°).
    const Mat4 w10 = world::layer_world_frac(*r.comp(), *r.L(nulls[0]), 10.0);
    AUREA_CHECK_NEAR(std::atan2(w10.col[0].y, w10.col[0].x), 1.5707963f, 2e-2f);
    // Laço: depois do último key o trecho recomeça (45 = 15 de novo).
    r.L(nulls[0])->pathFollow.loop = true;
    near_point(at(45.0), corner(100, 100));
    near_point(at(67.5), corner(100, 0));
    // Progresso além de 100 % com laço dá a volta (keys trocados por um valor parado).
    Layer* nn = r.L(nulls[0]);
    nn->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::EffectParam; });
    nn->effects[0].params[0].constant.v[0] = 150.0f;
    near_point(at(0.0), corner(100, 100));
    nn->pathFollow.loop = false;
    near_point(at(0.0), corner(0, 0));           // sem laço: preso em 100 %
    nn->effects[0].params[0].constant.v[0] = 62.5f;
    near_point(at(0.0), corner(50, 100));
    // Sem o efeito, vale o progresso gravado na ligação.
    nn->effects.clear();
    nn->pathFollow.progress = 25.0f;
    near_point(at(0.0), corner(100, 0));
    // point_on_path direto: caminho aberto vai da primeira à última ponta.
    BezierPath open;
    open.closed = false;
    open.v = {BezierVertex{Vec2{0, 0}}, BezierVertex{Vec2{100, 0}}, BezierVertex{Vec2{100, 50}}};
    Vec2 p, t;
    AUREA_CHECK(pathrig::point_on_path(open, 1.0, p, t));
    AUREA_CHECK_NEAR(p.x, 100.0f, 0.2f);
    AUREA_CHECK_NEAR(p.y, 50.0f, 0.2f);
    AUREA_CHECK(pathrig::point_on_path(open, 1.0 / 3.0, p, t));
    AUREA_CHECK_NEAR(p.x, 50.0f, 0.2f);
    AUREA_CHECK_NEAR(t.x, 1.0f, 1e-3f);
    // Trace Path com laço pedido na criação.
    const auto looped = r.e.create_nulls_from_path(id, 0, m, 0, Engine::NullsFromPath::TracePath, true, &nulls);
    AUREA_CHECK(looped.ok() && nulls.size() == 1);
    if (nulls.size() == 1) AUREA_CHECK(r.L(nulls[0])->pathFollow.loop);
}

AUREA_TEST(AeMasks, VectorPathPointsFollowNullsAndNullsFollowPoints) {
    MaskRig r;
    const auto made = r.e.add_vector_layer(1);   // retângulo paramétrico
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const u64 id = *made;
    Layer* l = r.L(id);
    AUREA_CHECK(l && !l->shape.vector.groups.empty() && !l->shape.vector.groups[0].paths.empty());
    if (!l || l->shape.vector.groups.empty() || l->shape.vector.groups[0].paths.empty()) return;
    const VectorData before = l->shape.vector;
    const BezierPath plain = vector::path_at(vector::evaluate_group(before.groups[0], l->tracks, 0, 0.0).paths[0], 0.0);
    AUREA_CHECK(plain.v.size() >= 4);
    std::vector<u64> nulls;
    const auto count = r.e.create_nulls_from_path(id, 1, 0, 0, Engine::NullsFromPath::PointsFollowNulls, false, &nulls);
    AUREA_CHECK(count.ok());
    if (!count.ok()) return;
    AUREA_CHECK_EQ(static_cast<usize>(*count), plain.v.size());
    AUREA_CHECK_EQ(r.L(id)->vectorDrivers.size(), plain.v.size());
    AUREA_CHECK(r.L(id)->shape.vector == before);   // o documento não muda
    // Sem mexer nos nulos, o caminho avaliado é o mesmo.
    auto evaluated = [&](f64 frame) {
        const Layer* layer = r.L(id);
        const VectorGroup g = vector::evaluate_group(layer->shape.vector.groups[0], layer->tracks, 0, frame);
        return vector::path_at(g.paths[0], frame);
    };
    BezierPath now = evaluated(0.0);
    AUREA_CHECK_EQ(now.v.size(), plain.v.size());
    for (usize i = 0; i < plain.v.size() && i < now.v.size(); ++i) {
        AUREA_CHECK_NEAR(now.v[i].p.x, plain.v[i].p.x, 2e-2f);
        AUREA_CHECK_NEAR(now.v[i].p.y, plain.v[i].p.y, 2e-2f);
    }
    // Mover o nulo 1: só o vértice 1 anda (a camada nasce com espaço = composição).
    r.L(nulls[1])->transform.position.x += 30.0f;
    r.L(nulls[1])->transform.position.y += 12.0f;
    now = evaluated(0.0);
    AUREA_CHECK_NEAR(now.v[1].p.x, plain.v[1].p.x + 30.0f, 5e-2f);
    AUREA_CHECK_NEAR(now.v[1].p.y, plain.v[1].p.y + 12.0f, 5e-2f);
    AUREA_CHECK_NEAR(now.v[0].p.x, plain.v[0].p.x, 2e-2f);
    AUREA_CHECK(r.L(id)->shape.vector == before);
    // A consulta diz quantos vértices vetoriais estão ligados.
    f32 rig[9]{};
    AUREA_CHECK_EQ(r.e.query_path_rig(id, rig, 9), 9u);
    AUREA_CHECK_NEAR(rig[8], static_cast<f32>(plain.v.size()), 1e-6f);
    AUREA_CHECK(r.e.clear_path_drivers(id, 1, 0, 0));
    AUREA_CHECK(r.L(id)->vectorDrivers.empty());
    // Nulls Follow Points num caminho vetorial: o nulo segue o vértice quando o grupo anda.
    const auto followers = r.e.create_nulls_from_path(id, 1, 0, 0, Engine::NullsFromPath::NullsFollowPoints, false, &nulls);
    AUREA_CHECK(followers.ok() && !nulls.empty());
    if (nulls.empty()) return;
    const Vec3 a = r.anchor_at(*r.L(nulls[0]), 0.0);
    r.L(id)->shape.vector.groups[0].position.x += 25.0f;
    const Vec3 b = r.anchor_at(*r.L(nulls[0]), 0.0);
    AUREA_CHECK_NEAR(b.x - a.x, 25.0f, 5e-2f);
    AUREA_CHECK_NEAR(b.y, a.y, 5e-2f);
    AUREA_CHECK(!r.e.create_nulls_from_path(id, 1, 5, 0, Engine::NullsFromPath::NullsFollowPoints, false).ok());
}

AUREA_TEST(AeMasks, FollowCycleFallsBackInsteadOfRecursing) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    const u32 m = r.square(id, 10, 10, 60, 60);
    std::vector<u64> nulls;
    AUREA_CHECK(r.e.create_nulls_from_path(id, 0, m, 0, Engine::NullsFromPath::PointsFollowNulls, false, &nulls).ok());
    if (nulls.size() != 4) return;
    // O nulo 0 passa a seguir o vértice 0 — que segue o nulo 0.
    PathFollow f;
    f.mode = PathFollow::kVertex;
    f.source = LayerId::unpack(id);
    f.a = m;
    f.vertex = 0;
    r.L(nulls[0])->pathFollow = f;
    std::vector<MaskPoint> pts;
    mask::evaluate_path(*r.M(id, m), 0.0, pts);
    AUREA_CHECK_EQ(pts.size(), static_cast<usize>(4));
    for (const MaskPoint& p : pts) AUREA_CHECK(std::isfinite(p.position.x) && std::isfinite(p.position.y));
    const Vec3 a = r.anchor_at(*r.L(nulls[0]), 0.0);
    AUREA_CHECK(std::isfinite(a.x) && std::isfinite(a.y));
    // A camada é filha do próprio nulo que a segue: também termina.
    r.L(id)->parent = LayerId::unpack(nulls[1]);
    r.L(nulls[1])->pathFollow = f;
    mask::evaluate_path(*r.M(id, m), 0.0, pts);
    for (const MaskPoint& p : pts) AUREA_CHECK(std::isfinite(p.position.x) && std::isfinite(p.position.y));
    // Máscara solta (fora de qualquer timeline): vale o caminho gravado.
    Mask loose = *r.M(id, m);
    std::vector<MaskPoint> out;
    mask::evaluate_path(loose, 0.0, out);
    AUREA_CHECK_NEAR(out[0].position.x, 10.0f, 1e-4f);
}

// -----------------------------------------------------------------------------
// Auto-trace
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, TraceFindsContoursHolesAndChannels) {
    // Quadrado opaco 20..44 numa imagem de 64 px.
    const std::vector<u8> box = picture(64, 64, [](u32 x, u32 y) { return x >= 20 && x < 44 && y >= 20 && y < 44; });
    masktrace::Options o;
    o.cornerRoundness = 0.0f;
    o.tolerance = 0.5f;
    std::vector<masktrace::Traced> out;
    AUREA_CHECK(masktrace::trace(box.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(1));
    if (out.empty()) return;
    AUREA_CHECK(!out[0].hole);
    Vec2 lo, hi;
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK_NEAR(lo.x, 20.0f, 1.0f);
    AUREA_CHECK_NEAR(lo.y, 20.0f, 1.0f);
    AUREA_CHECK_NEAR(hi.x, 44.0f, 1.0f);
    AUREA_CHECK_NEAR(hi.y, 44.0f, 1.0f);
    AUREA_CHECK_NEAR(out[0].area, 24.0f * 24.0f, 40.0f);
    // A máscara traçada cobre o quadrado (e não o que está fora).
    Layer l;
    l.kind = LayerKind::Shape;
    Mask m;
    m.id = 0;
    m.points = out[0].points;
    l.masks.push_back(m);
    AUREA_CHECK(coverage(l, Vec2{32, 32}) > 0.99f);
    AUREA_CHECK(coverage(l, Vec2{23, 40}) > 0.9f);
    AUREA_CHECK(coverage(l, Vec2{17, 32}) < 0.05f);
    AUREA_CHECK(coverage(l, Vec2{48, 48}) < 0.05f);
    // Escala de saída: px da imagem → px da camada.
    AUREA_CHECK(masktrace::trace(box.data(), 64, 64, o, Vec2{2, 3}, 32, out));
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK_NEAR(lo.x, 40.0f, 2.0f);
    AUREA_CHECK_NEAR(hi.y, 132.0f, 3.0f);
    // Inverter: o que está ABAIXO do limiar (a moldura com o furo do quadrado).
    o.invert = true;
    AUREA_CHECK(masktrace::trace(box.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(2));
    if (out.size() == 2) {
        AUREA_CHECK(!out[0].hole && out[1].hole);
        bounds_of(out[0].points, lo, hi);
        AUREA_CHECK(lo.x < 2.0f && hi.x > 62.0f);
        AUREA_CHECK(out[0].area > out[1].area);
    }
    o.invert = false;
    // Anel: contorno de fora + furo; uma ilha pequena some com a área mínima.
    const std::vector<u8> ring = picture(96, 96, [](u32 x, u32 y) {
        const f32 dx = static_cast<f32>(x) - 40.0f, dy = static_cast<f32>(y) - 48.0f;
        const f32 d = std::sqrt(dx * dx + dy * dy);
        const bool island = x >= 84 && x < 88 && y >= 6 && y < 10;   // 16 px²
        return (d < 30.0f && d > 14.0f) || island;
    });
    o.cornerRoundness = 0.0f;   // a ilha de 4 × 4 px some com a suavização dos cantos
    o.minArea = 4.0f;
    AUREA_CHECK(masktrace::trace(ring.data(), 96, 96, o, Vec2{1, 1}, 32, out));
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(3));
    o.minArea = 60.0f;
    AUREA_CHECK(masktrace::trace(ring.data(), 96, 96, o, Vec2{1, 1}, 32, out));
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(2));
    if (out.size() == 2) {
        AUREA_CHECK(!out[0].hole && out[1].hole);
        AUREA_CHECK_NEAR(out[0].area, 3.14159265f * 30.0f * 30.0f, 120.0f);
        AUREA_CHECK_NEAR(out[1].area, 3.14159265f * 14.0f * 14.0f, 60.0f);
        AUREA_CHECK(out[0].points.size() >= 4 && out[0].points.size() < 64);   // curva, não polígono de pixel
    }
    // `maxContours` corta os menores.
    AUREA_CHECK(masktrace::trace(ring.data(), 96, 96, o, Vec2{1, 1}, 1, out));
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(1));
    // Canais: metade esquerda vermelha opaca, metade direita azul opaca.
    std::vector<u8> rgb(static_cast<usize>(64) * 64 * 4, 0);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) {
            u8* p = &rgb[(static_cast<usize>(y) * 64 + x) * 4];
            p[x < 32 ? 0 : 2] = 255;
            p[3] = 255;
        }
    o = masktrace::Options{};
    o.channel = 2;   // vermelho
    AUREA_CHECK(masktrace::trace(rgb.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK(lo.x < 2.0f && hi.x > 30.0f && hi.x < 34.0f);
    o.channel = 4;   // azul
    AUREA_CHECK(masktrace::trace(rgb.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK(lo.x > 30.0f && lo.x < 34.0f && hi.x > 62.0f);
    o.channel = 0;   // alfa: a imagem inteira
    AUREA_CHECK(masktrace::trace(rgb.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK(lo.x < 2.0f && hi.x > 62.0f);
    // Luminância: o vermelho (0,21) fica abaixo de 0,5 e o azul (0,07) também; limiar 0,15 pega só o vermelho.
    o.channel = 1;
    AUREA_CHECK(!masktrace::trace(rgb.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    o.threshold = 0.15f;
    AUREA_CHECK(masktrace::trace(rgb.data(), 64, 64, o, Vec2{1, 1}, 32, out));
    bounds_of(out[0].points, lo, hi);
    AUREA_CHECK(hi.x < 34.0f);
    // Imagem vazia: falso, sem contorno.
    const std::vector<u8> empty(static_cast<usize>(32) * 32 * 4, 0);
    AUREA_CHECK(!masktrace::trace(empty.data(), 32, 32, masktrace::Options{}, Vec2{1, 1}, 32, out));
    AUREA_CHECK(out.empty());
    AUREA_CHECK(!masktrace::trace(nullptr, 32, 32, masktrace::Options{}, Vec2{1, 1}, 32, out));
}

AUREA_TEST(AeMasks, CornerRoundnessSmoothsTheTracedVertices) {
    const std::vector<u8> box = picture(96, 96, [](u32 x, u32 y) { return x >= 20 && x < 76 && y >= 20 && y < 76; });
    masktrace::Options sharp, round;
    sharp.cornerRoundness = 0.0f;
    round.cornerRoundness = 100.0f;
    std::vector<masktrace::Traced> a, b;
    AUREA_CHECK(masktrace::trace(box.data(), 96, 96, sharp, Vec2{1, 1}, 4, a));
    AUREA_CHECK(masktrace::trace(box.data(), 96, 96, round, Vec2{1, 1}, 4, b));
    if (a.empty() || b.empty()) return;
    // A caixa não muda: só os cantos são cortados.
    Vec2 lo, hi;
    bounds_of(b[0].points, lo, hi);
    AUREA_CHECK_NEAR(lo.x, 20.0f, 1.5f);
    AUREA_CHECK_NEAR(hi.y, 76.0f, 1.5f);
    // O canto vivo cobre o canto do quadrado; o arredondado corta um pedaço dele.
    auto cov = [](const std::vector<MaskPoint>& pts, Vec2 p) {
        Layer l;
        l.kind = LayerKind::Shape;
        Mask m;
        m.id = 0;
        m.points = pts;
        l.masks.push_back(m);
        return coverage(l, p);
    };
    AUREA_CHECK(cov(a[0].points, Vec2{21.5f, 21.5f}) > 0.9f);
    AUREA_CHECK(cov(b[0].points, Vec2{21.5f, 21.5f}) < cov(a[0].points, Vec2{21.5f, 21.5f}) - 0.2f);
    AUREA_CHECK(cov(b[0].points, Vec2{48.0f, 48.0f}) > 0.99f);
    AUREA_CHECK(cov(b[0].points, Vec2{48.0f, 22.5f}) > 0.9f);   // o meio do lado continua coberto
}

AUREA_TEST(AeMasks, EngineAutoTraceOnImageLayer) {
    MaskRig r;
    // Um disco e um quadrado separados, alfa 255 dentro.
    const std::vector<u8> px = picture(160, 120, [](u32 x, u32 y) {
        const f32 dx = static_cast<f32>(x) - 45.0f, dy = static_cast<f32>(y) - 60.0f;
        return dx * dx + dy * dy < 30.0f * 30.0f || (x >= 100 && x < 140 && y >= 30 && y < 60);
    });
    const auto imported = r.e.import_image(px.data(), 160, 120, "recorte");
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) return;
    const u64 id = *imported;
    Engine::AutoTraceOptions o;
    o.cornerRoundness = 0.0f;   // cantos vivos: a caixa dos vértices é a da forma
    u64 target = 0;
    const auto made = r.e.auto_trace(id, o, &target);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    AUREA_CHECK_EQ(*made, 2u);
    AUREA_CHECK_EQ(target, id);
    Layer* l = r.L(id);
    AUREA_CHECK_EQ(l->masks.size(), static_cast<usize>(2));
    if (l->masks.size() != 2) return;
    // Do maior para o menor: o disco (≈ 2827 px²) antes do retângulo (1200 px²).
    Vec2 lo, hi;
    bounds_of(l->masks[0].points, lo, hi);
    AUREA_CHECK_NEAR(lo.x, 15.0f, 1.5f);
    AUREA_CHECK_NEAR(hi.x, 75.0f, 1.5f);
    bounds_of(l->masks[1].points, lo, hi);
    AUREA_CHECK_NEAR(lo.x, 100.0f, 1.5f);
    AUREA_CHECK_NEAR(hi.y, 60.0f, 1.5f);
    AUREA_CHECK(l->masks[0].operation == MaskOperation::Add && l->masks[0].closed && l->masks[0].pathKeys.empty());
    AUREA_CHECK(coverage(*l, Vec2{45, 60}) > 0.99f);
    AUREA_CHECK(coverage(*l, Vec2{120, 45}) > 0.99f);
    AUREA_CHECK(coverage(*l, Vec2{88, 20}) < 0.02f);
    // Um passo de desfazer.
    r.undo();
    AUREA_CHECK(r.L(id)->masks.empty());
    // Em camada nova: sólido filho da imagem, do mesmo tamanho, com as máscaras.
    o.newLayer = true;
    o.minArea = 2000.0f;   // só o disco
    const auto again = r.e.auto_trace(id, o, &target);
    AUREA_CHECK(again.ok());
    if (!again.ok()) return;
    AUREA_CHECK_EQ(*again, 1u);
    AUREA_CHECK(target != id && target != 0);
    AUREA_CHECK(r.L(id)->masks.empty());
    Layer* solid = r.L(target);
    AUREA_CHECK(solid && solid->kind == LayerKind::Shape && solid->parent == LayerId::unpack(id));
    if (!solid) return;
    AUREA_CHECK_NEAR(solid->shape.bounds.w, 160.0f, 1e-4f);
    AUREA_CHECK_NEAR(solid->shape.bounds.h, 120.0f, 1e-4f);
    AUREA_CHECK_EQ(solid->masks.size(), static_cast<usize>(1));
    // O espaço do sólido é o da imagem: o mesmo ponto da camada cai no mesmo ponto da composição.
    const Vec3 a = world::layer_world_frac(*r.comp(), *r.L(id), 0.0).transform_point(Vec3{45, 60, 0});
    const Vec3 b = world::layer_world_frac(*r.comp(), *solid, 0.0).transform_point(Vec3{45, 60, 0});
    AUREA_CHECK_NEAR(a.x, b.x, 1e-3f);
    AUREA_CHECK_NEAR(a.y, b.y, 1e-3f);
    // Inverter + limiar: o fundo com dois furos (o primeiro soma, os furos subtraem).
    o = Engine::AutoTraceOptions{};
    o.invert = true;
    const auto inv = r.e.auto_trace(id, o, nullptr);
    AUREA_CHECK(inv.ok());
    if (inv.ok()) {
        AUREA_CHECK_EQ(*inv, 3u);
        const Layer* li = r.L(id);
        AUREA_CHECK(li->masks[0].operation == MaskOperation::Add);
        AUREA_CHECK(li->masks[1].operation == MaskOperation::Subtract && li->masks[2].operation == MaskOperation::Subtract);
        AUREA_CHECK(coverage(*li, Vec2{88, 20}) > 0.98f);
        AUREA_CHECK(coverage(*li, Vec2{45, 60}) < 0.02f);
    }
    // Camada que não é imagem nem vídeo, canal inválido e camada que não existe: recusa.
    const u64 sh = r.shape();
    AUREA_CHECK(!r.e.auto_trace(sh, Engine::AutoTraceOptions{}, nullptr).ok());
    Engine::AutoTraceOptions bad;
    bad.channel = 9;
    AUREA_CHECK(!r.e.auto_trace(id, bad, nullptr).ok());
    AUREA_CHECK(!r.e.auto_trace(0xDEADBEEFull, Engine::AutoTraceOptions{}, nullptr).ok());
    // Nada acima do limiar (tudo abaixo de um limiar altíssimo no azul de uma imagem sem azul... aqui o alfa do vazio).
    const std::vector<u8> blank(static_cast<usize>(40) * 40 * 4, 0);
    const auto empty = r.e.import_image(blank.data(), 40, 40, "vazio");
    if (empty.ok()) AUREA_CHECK(!r.e.auto_trace(*empty, Engine::AutoTraceOptions{}, nullptr).ok());
}

// -----------------------------------------------------------------------------
// Projeto: a seção MaskRig
// -----------------------------------------------------------------------------
AUREA_TEST(AeMasks, MaskRigSectionRoundTrips) {
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    r.L(id)->name = "dona";
    const u32 m0 = r.square(id, 10, 10, 90, 90);
    const u32 m1 = r.square(id, 20, 20, 60, 60);
    AUREA_CHECK(r.e.set_mask_flags(id, m0, Engine::kMaskRotoBezier | Engine::kMaskHidden, Engine::kMaskRotoBezier | Engine::kMaskHidden));
    AUREA_CHECK(r.e.set_mask_roto_tension(id, m0, 1, 0.3f, false));
    AUREA_CHECK(r.e.set_mask_feather_xy(id, m0, 12.0f, 3.0f, false));
    const f32 fp[10] = {0, 0.5f, 24.0f, 0.25f, 0, 2, 0.75f, -8.0f, 1.0f, 1};
    AUREA_CHECK(r.e.set_mask_feather_points(id, m0, fp, 2, false));
    std::vector<u64> drivers, followers, tracer;
    AUREA_CHECK(r.e.create_nulls_from_path(id, 0, m1, 0, Engine::NullsFromPath::PointsFollowNulls, false, &drivers).ok());
    AUREA_CHECK(r.e.create_nulls_from_path(id, 0, m0, 0, Engine::NullsFromPath::NullsFollowPoints, false, &followers).ok());
    AUREA_CHECK(r.e.create_nulls_from_path(id, 0, m0, 0, Engine::NullsFromPath::TracePath, true, &tracer).ok());
    AUREA_CHECK(r.e.set_mask_flags(id, m1, Engine::kMaskLocked, Engine::kMaskLocked));
    if (drivers.size() != 4 || followers.size() != 4 || tracer.size() != 1) return;
    r.L(drivers[2])->transform.position.x += 17.0f;
    const std::string driverName = r.L(drivers[2])->name, followerName = r.L(followers[3])->name, tracerName = r.L(tracer[0])->name;
    const u32 progressEffect = r.L(tracer[0])->pathFollow.progressEffect;
    // Camada vetorial com um vértice ligado.
    const auto vec = r.e.add_vector_layer(1);
    AUREA_CHECK(vec.ok());
    std::vector<u64> vnulls;
    if (vec.ok()) {
        r.L(*vec)->name = "vetor";
        AUREA_CHECK(r.e.create_nulls_from_path(*vec, 1, 0, 0, Engine::NullsFromPath::PointsFollowNulls, false, &vnulls).ok());
    }
    std::vector<MaskPoint> before;
    mask::evaluate_path(*r.M(id, m1), 0.0, before);
    const Vec3 tracerBefore = r.anchor_at(*r.L(tracer[0]), 15.0);
    const u64 blockBefore = block_of(*r.L(id)).key;

    const std::string path = r.temp("aurea_ae_masks.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    Layer* l = r.named("dona");
    AUREA_CHECK(l != nullptr);
    if (!l) { std::remove(path.c_str()); return; }
    AUREA_CHECK_EQ(l->masks.size(), static_cast<usize>(2));
    if (l->masks.size() != 2) { std::remove(path.c_str()); return; }
    const Mask& a = l->masks[0];
    const Mask& b = l->masks[1];
    AUREA_CHECK(a.rotoBezier && a.hidden && !a.locked);
    AUREA_CHECK(b.locked && !b.hidden && !b.rotoBezier);
    AUREA_CHECK_NEAR(a.featherY, 3.0f, 1e-6f);
    AUREA_CHECK(b.featherY < 0.0f);
    AUREA_CHECK_EQ(a.rotoTension.size(), static_cast<usize>(4));
    if (a.rotoTension.size() == 4) AUREA_CHECK_NEAR(a.rotoTension[1], 0.3f, 1e-6f);
    AUREA_CHECK_EQ(a.featherPoints.size(), static_cast<usize>(2));
    if (a.featherPoints.size() == 2) {
        AUREA_CHECK(a.featherPoints[0] == (MaskFeatherPoint{0, 0.5f, 24.0f, 0.25f, false}));
        AUREA_CHECK(a.featherPoints[1] == (MaskFeatherPoint{2, 0.75f, -8.0f, 1.0f, true}));
    }
    AUREA_CHECK_EQ(b.drivers.size(), static_cast<usize>(4));
    // As ligações apontam para as MESMAS camadas (pelo nome) depois de reabrir.
    Layer* driver = r.named(driverName);
    Layer* follower = r.named(followerName);
    Layer* trace = r.named(tracerName);
    AUREA_CHECK(driver && follower && trace);
    if (!driver || !follower || !trace) { std::remove(path.c_str()); return; }
    bool found = false;
    for (const PathVertexDriver& d : b.drivers) {
        if (d.vertex != 2) continue;
        found = r.comp()->layer(d.driver) == driver;
    }
    AUREA_CHECK(found);
    std::vector<MaskPoint> after;
    mask::evaluate_path(b, 0.0, after);
    AUREA_CHECK_EQ(after.size(), before.size());
    for (usize i = 0; i < after.size() && i < before.size(); ++i) {
        AUREA_CHECK_NEAR(after[i].position.x, before[i].position.x, 1e-3f);
        AUREA_CHECK_NEAR(after[i].position.y, before[i].position.y, 1e-3f);
    }
    AUREA_CHECK_EQ(block_of(*l).key, blockBefore);   // o recorte reabre idêntico
    AUREA_CHECK_EQ(follower->pathFollow.mode, PathFollow::kVertex);
    AUREA_CHECK_EQ(follower->pathFollow.vertex, 3u);
    AUREA_CHECK(r.comp()->layer(follower->pathFollow.source) == l);
    AUREA_CHECK_EQ(follower->pathFollow.a, m0);
    AUREA_CHECK_EQ(trace->pathFollow.mode, PathFollow::kTrace);
    AUREA_CHECK(trace->pathFollow.loop && trace->pathFollow.orient);
    AUREA_CHECK_EQ(trace->pathFollow.progressEffect, progressEffect);
    const Vec3 tracerAfter = r.anchor_at(*trace, 15.0);
    AUREA_CHECK_NEAR(tracerAfter.x, tracerBefore.x, 1e-2f);
    AUREA_CHECK_NEAR(tracerAfter.y, tracerBefore.y, 1e-2f);
    if (Layer* v = r.named("vetor")) {
        AUREA_CHECK_EQ(v->vectorDrivers.size(), vnulls.size());
        for (const VectorPathDriver& d : v->vectorDrivers) AUREA_CHECK(r.comp()->layer(d.driver) != nullptr && d.group == 0 && d.path == 0);
    } else {
        AUREA_CHECK(false);
    }
    std::remove(path.c_str());
}

AUREA_TEST(AeMasks, ProjectWithoutRigWritesNoSectionAndReadsBack) {
    // Projeto sem nada do lote M: a seção não existe e as máscaras reabrem iguais.
    MaskRig r;
    const u64 id = r.shape();
    if (!id) return;
    r.L(id)->name = "simples";
    const u32 m = r.square(id, 10, 10, 90, 90);
    AUREA_CHECK(r.e.set_mask_props(id, m, 0, false, 14.0f, 2.0f, 0.8f));
    const u64 key = block_of(*r.L(id)).key;
    const std::string path = r.temp("aurea_ae_masks_plain.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    auto file_size = [](const std::string& file) -> long {
        long size = -1;
        if (std::FILE* f = std::fopen(file.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            size = std::ftell(f);
            std::fclose(f);
        }
        return size;
    };
    const long plainSize = file_size(path);
    AUREA_CHECK(plainSize > 0);
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    Layer* l = r.named("simples");
    AUREA_CHECK(l && l->masks.size() == 1);
    if (!l || l->masks.size() != 1) { std::remove(path.c_str()); return; }
    {
        const Mask& mk = l->masks[0];
        AUREA_CHECK(!mk.locked && !mk.hidden && !mk.rotoBezier && mk.featherY < 0.0f);
        AUREA_CHECK(mk.featherPoints.empty() && mk.drivers.empty() && mk.rotoTension.empty());
        AUREA_CHECK_EQ(block_of(*l).key, key);
        AUREA_CHECK(l->pathFollow.mode == PathFollow::kNone && l->vectorDrivers.empty());
    }
    // Reaberto e gravado de novo: o mesmo tamanho (nenhuma seção nova apareceu).
    const std::string again = r.temp("aurea_ae_masks_plain2.aurea");
    AUREA_CHECK(r.e.save_project(again.c_str()).ok());
    const long againSize = file_size(again);
    AUREA_CHECK(againSize > 0);
    // Com uma chave do lote M a seção MaskRig entra e o arquivo cresce; reabre com ela.
    u64 reopened = 0;
    r.comp()->layers().for_each([&](LayerId lid, Layer& layer) { if (&layer == l) reopened = lid.pack(); });
    AUREA_CHECK(reopened != 0);
    AUREA_CHECK(r.e.set_mask_flags(reopened, l->masks[0].id, Engine::kMaskLocked, Engine::kMaskLocked));
    AUREA_CHECK(r.e.save_project(again.c_str()).ok());
    AUREA_CHECK(file_size(again) > againSize);
    AUREA_CHECK(r.e.load_project(again.c_str()).ok());
    Layer* locked = r.named("simples");
    AUREA_CHECK(locked && locked->masks.size() == 1 && locked->masks[0].locked);
    if (locked && locked->masks.size() == 1) AUREA_CHECK_EQ(block_of(*locked).key, key);
    std::remove(path.c_str());
    std::remove(again.c_str());
}
