// =============================================================================
//  Aurea / tests / test_puppet_solver.cpp
//
//  Solver do Fantoche (effects/Puppet.cpp). Pendência da 0.2.3: com três
//  pinos, dois parados e um arrastado, a camada inclinava.
//
//  Causa comprovada aqui: quando o contorno deixa a malha em PARTES soltas,
//  a parte sem pino (ou com um pino só) não tem equação que a prenda e ficava
//  no chute inicial do solver — o giro rígido de todos os pinos juntos. Ao
//  arrastar um pino de outra parte, ela girava e andava dezenas de px.
//  Agora cada parte segue só os pinos dela (sem pino = repouso, um pino =
//  translação).
//
//  O que NÃO muda: numa malha inteiriça (a caixa toda), o ARAP com pinos
//  pontuais ainda espalha o giro do pino movido para o resto. Isso é do
//  modelo, não do solver: o ARAP exato (local-global até convergir) gira
//  MAIS que o de dois passos. O teste `connected_mesh_keeps_pins` só guarda
//  os pinos exatos e imprime os cantos para acompanhar.
// =============================================================================
#include "TestFramework.hpp"
#include "aurea/effects/Puppet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

/// Contorno 24×24 com duas faixas verticais separadas: colunas 2..6 (o
/// "corpo", à esquerda) e 19..21 (um objeto solto, à direita).
std::vector<u8> two_parts() {
    std::vector<u8> occ(puppet::kOutline * puppet::kOutline, 0);
    for (u32 y = 2; y < 22; ++y) {
        for (u32 x = 2; x < 7; ++x) occ[y * puppet::kOutline + x] = 1;
        for (u32 x = 19; x < 22; ++x) occ[y * puppet::kOutline + x] = 1;
    }
    return occ;
}

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

AUREA_TEST(puppet_solver, part_without_pin_stays_at_rest) {
    // Cabeça, peito e cintura no corpo; só a cabeça anda. O objeto solto à
    // direita não tem pino: não pode girar nem andar.
    const std::vector<u8> occ = two_parts();
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m, occ.data(), puppet::kOutline, puppet::kOutline);
    const Vec2 head{70, 80}, chest{70, 220}, waist{70, 330}, headTo{110, 70};
    std::vector<puppet::Pin> pins = {{head, headTo}, {chest, chest}, {waist, waist}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_EQ(out.size(), m.rest.size());

    AUREA_CHECK_MSG(dist(carry(m, out, head), headTo) < 0.5f, "pino movido fora do alvo");
    AUREA_CHECK_MSG(dist(carry(m, out, chest), chest) < 0.5f, "pino parado (peito) andou");
    AUREA_CHECK_MSG(dist(carry(m, out, waist), waist) < 0.5f, "pino parado (cintura) andou");

    f32 loose = 0.0f;
    u32 looseCount = 0;
    for (usize i = 0; i < out.size(); ++i) {
        if (m.rest[i].x < 250.0f) continue;   // só o objeto solto
        loose = std::max(loose, dist(out[i], m.rest[i]));
        ++looseCount;
    }
    std::printf("\n    objeto sem pino: %u vertices, deslocamento max %.3f px\n", looseCount, loose);
    AUREA_CHECK(looseCount > 0);
    AUREA_CHECK_MSG(loose < 0.01f, "parte sem pino girou com a camada");
}

AUREA_TEST(puppet_solver, part_with_one_pin_only_translates) {
    // Um pino no objeto solto, arrastado: ele só translada, sem giro. O
    // corpo (dois pinos parados) fica no lugar.
    const std::vector<u8> occ = two_parts();
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m, occ.data(), puppet::kOutline, puppet::kOutline);
    const Vec2 a{70, 120}, b{70, 300}, lone{340, 200}, loneTo{360, 150};
    std::vector<puppet::Pin> pins = {{a, a}, {b, b}, {lone, loneTo}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_EQ(out.size(), m.rest.size());

    f32 body = 0.0f, err = 0.0f;
    for (usize i = 0; i < out.size(); ++i) {
        if (m.rest[i].x < 250.0f) {
            body = std::max(body, dist(out[i], m.rest[i]));
        } else {
            const Vec2 want{m.rest[i].x + loneTo.x - lone.x, m.rest[i].y + loneTo.y - lone.y};
            err = std::max(err, dist(out[i], want));
        }
    }
    std::printf("\n    corpo parado: max %.3f px | objeto de um pino fora da translacao: max %.3f px\n", body, err);
    AUREA_CHECK_MSG(body < 0.05f, "corpo com dois pinos parados andou");
    AUREA_CHECK_MSG(err < 0.01f, "parte de um pino girou");
}

AUREA_TEST(puppet_solver, connected_mesh_keeps_pins) {
    // Malha inteiriça (sem contorno), o cenário do vídeo: os pinos ficam
    // exatos. Os cantos de baixo ainda andam (modelo ARAP com pinos
    // pontuais, ver o cabeçalho) — o teto só pega uma piora grosseira.
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m);
    const Vec2 head{200, 80}, chest{200, 220}, waist{200, 330}, headTo{240, 70};
    std::vector<puppet::Pin> pins = {{head, headTo}, {chest, chest}, {waist, waist}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_MSG(dist(carry(m, out, head), headTo) < 0.5f, "pino movido fora do alvo");
    AUREA_CHECK_MSG(dist(carry(m, out, chest), chest) < 0.5f, "pino parado (peito) andou");
    AUREA_CHECK_MSG(dist(carry(m, out, waist), waist) < 0.5f, "pino parado (cintura) andou");
    const Vec2 bl{10, 395}, br{390, 395};
    const f32 dl = dist(carry(m, out, bl), bl), dr = dist(carry(m, out, br), br);
    std::printf("\n    malha inteirica: cantos de baixo %.2f / %.2f px\n", dl, dr);
    AUREA_CHECK(dl < 20.0f && dr < 20.0f);
}

AUREA_TEST(puppet_solver, dense_mesh_stays_exact) {
    puppet::Mesh m;
    puppet::build_mesh(1080.0f, 1920.0f, 1500, 3.0f, m);
    const Vec2 a{540, 300}, b{540, 1000}, c{540, 1700}, aTo{620, 260};
    std::vector<puppet::Pin> pins = {{a, aTo}, {b, b}, {c, c}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    AUREA_CHECK_MSG(dist(carry(m, out, a), aTo) < 1.0f, "pino movido fora do alvo");
    AUREA_CHECK_MSG(dist(carry(m, out, b), b) < 1.0f, "pino parado andou");
    AUREA_CHECK_MSG(dist(carry(m, out, c), c) < 1.0f, "pino parado andou");
    bool finite = true;
    for (const Vec2& v : out) finite = finite && std::isfinite(v.x) && std::isfinite(v.y);
    AUREA_CHECK(finite);
}

AUREA_TEST(puppet_solver, full_rigidity_moves_each_part_rigidly) {
    // Rigidez 100%: cada parte vira o melhor movimento rígido dos pinos DELA.
    const std::vector<u8> occ = two_parts();
    puppet::Mesh m;
    puppet::build_mesh(400.0f, 400.0f, 300, 3.0f, m, occ.data(), puppet::kOutline, puppet::kOutline);
    const Vec2 head{70, 80}, waist{70, 330}, headTo{110, 70};
    std::vector<puppet::Pin> pins = {{head, headTo}, {waist, waist}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 1.0f, out);
    f32 loose = 0.0f;
    for (usize i = 0; i < out.size(); ++i)
        if (m.rest[i].x >= 250.0f) loose = std::max(loose, dist(out[i], m.rest[i]));
    AUREA_CHECK_MSG(loose < 0.01f, "parte sem pino andou com rigidez total");
}
