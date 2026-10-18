// =============================================================================
//  Aurea / tests / test_puppet_solver.cpp
//
//  Solver do Fantoche (effects/Puppet.cpp). Regressão do vídeo de 17/10/2026:
//  com três pinos, dois parados e um arrastado, a camada INTEIRA inclinava —
//  o gradiente conjugado parava com o giro global da semente rígida ainda lá.
//  Os pinos parados têm de ficar onde estão e a malha longe do pino movido
//  não pode girar.
// =============================================================================
#include "TestFramework.hpp"
#include "aurea/effects/Puppet.hpp"

#include <cmath>
#include <vector>

using namespace aurea;

namespace {

/// Ponto do repouso `p` levado pela malha deformada (baricêntricas no repouso).
Vec2 carry(const puppet::Mesh& m, const std::vector<Vec2>& def, Vec2 p) {
    for (u32 t = 0; t * 3 + 2 < m.tris.size(); ++t) {
        const Vec2 a = m.rest[m.tris[t * 3]], b = m.rest[m.tris[t * 3 + 1]], c = m.rest[m.tris[t * 3 + 2]];
        const f32 d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::fabs(d) < 1e-9f) continue;
        const f32 u = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
        const f32 v = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
        const f32 w = 1.0f - u - v;
        if (u < -1e-4f || v < -1e-4f || w < -1e-4f) continue;
        const Vec2 A = def[m.tris[t * 3]], B = def[m.tris[t * 3 + 1]], C = def[m.tris[t * 3 + 2]];
        return Vec2{u * A.x + v * B.x + w * C.x, u * A.y + v * B.y + w * C.y};
    }
    return p;
}

f32 dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

AUREA_TEST(puppet_solver, unmoved_pins_keep_rest) {
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m);
    std::vector<puppet::Pin> pins = {
        {{200, 80}, {200, 80}}, {{200, 220}, {200, 220}}, {{200, 330}, {200, 330}}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_EQ(out.size(), m.rest.size());
    f32 worst = 0.0f;
    for (usize i = 0; i < out.size(); ++i) worst = std::max(worst, dist(out[i], m.rest[i]));
    AUREA_CHECK_MSG(worst < 0.05f, "pinos parados deformaram a malha");
}

AUREA_TEST(puppet_solver, dragging_one_pin_does_not_tilt_layer) {
    // O cenário do vídeo: cabeça, peito e cintura; só o pino da cabeça anda.
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m);
    const Vec2 head{200, 80}, chest{200, 220}, waist{200, 330};
    const Vec2 headTo{240, 70};
    std::vector<puppet::Pin> pins = {{head, headTo}, {chest, chest}, {waist, waist}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);

    // Os pinos vão exatamente aonde foram postos.
    AUREA_CHECK_MSG(dist(carry(m, out, head), headTo) < 0.5f, "pino movido fora do alvo");
    AUREA_CHECK_MSG(dist(carry(m, out, chest), chest) < 0.5f, "pino parado (peito) andou");
    AUREA_CHECK_MSG(dist(carry(m, out, waist), waist) < 0.5f, "pino parado (cintura) andou");

    // Os cantos de baixo, longe do pino movido, não giram com a camada.
    const Vec2 bl{10, 395}, br{390, 395};
    AUREA_CHECK_MSG(dist(carry(m, out, bl), bl) < 6.0f, "canto inferior esquerdo inclinou");
    AUREA_CHECK_MSG(dist(carry(m, out, br), br) < 6.0f, "canto inferior direito inclinou");
}

AUREA_TEST(puppet_solver, dense_mesh_stays_exact) {
    // Malha densa (limite da régua): o caminho direto ainda resolve.
    puppet::Mesh m;
    puppet::build_mesh(1080.0f, 1920.0f, 1500, 3.0f, m);
    const Vec2 a{540, 300}, b{540, 1000}, c{540, 1700}, aTo{620, 260};
    std::vector<puppet::Pin> pins = {{a, aTo}, {b, b}, {c, c}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_MSG(dist(carry(m, out, aTo == aTo ? a : a), aTo) < 1.0f, "pino movido fora do alvo");
    AUREA_CHECK_MSG(dist(carry(m, out, c), c) < 1.0f, "pino parado andou");
    for (const Vec2& v : out) AUREA_CHECK(std::isfinite(v.x) && std::isfinite(v.y));
}
