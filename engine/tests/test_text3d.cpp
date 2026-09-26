// Testes do texto 3D como GEOMETRIA: contornos com furos, extrusão, chanfro de
// verdade (a malha muda), normais por região, tangentes e material PBR.
//
// O que se mede aqui é a malha — área da tampa, faces invertidas, orientação
// das normais —, não uma imagem: um chanfro falso (brilho no shader) passaria
// num teste de pixel e não passaria nestes.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/scene3d/Animation.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

AUREA_TEST(Text3D, FontAndLetterAnimationSurviveRecipeRoundTrip) {
    Text3DSpec spec;
    spec.content = "A;t=B\nC";
    spec.fontPath = "docs:fontes/fonte;especial.otf";
    spec.animation = 3;
    spec.animationDuration = 3.5f;
    spec.animationStagger = .2f;
    spec.animationAmount = .45f;
    Text3DSpec read;
    AUREA_CHECK(decode_text3d(encode_text3d(spec), read));
    AUREA_CHECK_EQ(read.content, spec.content);
    AUREA_CHECK_EQ(read.fontPath, spec.fontPath);
    AUREA_CHECK_EQ(read.animation, spec.animation);
    AUREA_CHECK_NEAR(read.animationDuration, spec.animationDuration, .0001);
    AUREA_CHECK_NEAR(read.animationStagger, spec.animationStagger, .0001);
    AUREA_CHECK_NEAR(read.animationAmount, spec.animationAmount, .0001);
    // A missing font on another device falls back to the default.
    AUREA_CHECK(text3d_font(read) != nullptr);
}

AUREA_TEST(Text3D, LegacyLetterModesStayStaticWithoutGeneratedClips) {
    const auto font = text::default_font();
    AUREA_CHECK(font != nullptr);
    if (!font) return;
    Text3DSpec spec;
    spec.content = "Ai O"; // dot of i belongs to the same glyph; space has no mesh
    auto still = build_text3d(*font, spec);
    AUREA_CHECK(still.ok());
    for (u32 mode = 1; mode <= 4; ++mode) {
        spec.animation = mode;
        auto result = build_text3d(*font, spec);
        AUREA_CHECK(result.ok());
        if (!result.ok() || !still.ok()) continue;
        const auto& asset = *result.asset;
        AUREA_CHECK_EQ(asset.nodes.size(), usize{1});
        AUREA_CHECK(asset.animations.empty());
        AUREA_CHECK_NEAR(asset.bounds.min.x, still.asset->bounds.min.x, 1e-4);
        AUREA_CHECK_NEAR(asset.bounds.max.x, still.asset->bounds.max.x, 1e-4);
        Pose a, b, again;
        evaluate_pose(asset, 0, 0.f, a);
        evaluate_pose(asset, 0, .5f, b);
        evaluate_pose(asset, 0, 0.f, again);
        const Vec3 probe{.1f, .2f, .3f};
        AUREA_CHECK((a.nodeWorld[0].transform_point(probe) - b.nodeWorld[0].transform_point(probe)).length() < 1e-6f);
        AUREA_CHECK((a.nodeWorld[0].transform_point(probe) - again.nodeWorld[0].transform_point(probe)).length() < 1e-6f);
    }
}

namespace {

/// Conta faces cuja normal geométrica discorda das normais dos vértices, e
/// devolve a área, a caixa e quantas faces caem em cada região (pela normal).
struct MalhaInfo {
    u32 triangulos = 0;
    u32 invertidas = 0;      ///< faces com área de verdade apontando contra a normal
    u32 degeneradas = 0;     ///< área ~zero: não têm orientação, não contam
    f64 areaInvertida = 0;   ///< área somada das invertidas (para ver a escala do problema)
    f64 areaFrente = 0, areaFundo = 0, areaLateral = 0, areaChanfro = 0;
    f32 minZ = 1e30f, maxZ = -1e30f;
    f32 minX = 1e30f, maxX = -1e30f;
    f32 minY = 1e30f, maxY = -1e30f;
};

MalhaInfo medir(const Primitive& p) {
    MalhaInfo m;
    for (const Vec3& v : p.positions) {
        m.minZ = std::min(m.minZ, v.z); m.maxZ = std::max(m.maxZ, v.z);
        m.minX = std::min(m.minX, v.x); m.maxX = std::max(m.maxX, v.x);
        m.minY = std::min(m.minY, v.y); m.maxY = std::max(m.maxY, v.y);
    }
    for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
        const Vec3 a = p.positions[p.indices[t]], b = p.positions[p.indices[t + 1]], c = p.positions[p.indices[t + 2]];
        const Vec3 g = (b - a).cross(c - a);
        const Vec3 n = p.normals[p.indices[t]] + p.normals[p.indices[t + 1]] + p.normals[p.indices[t + 2]];
        const f64 area = g.length() * 0.5;
        ++m.triangulos;
        if (area < 1e-12) { ++m.degeneradas; continue; }   // triângulo de área nula: sem lado
        if (g.dot(n) < 0) { ++m.invertidas; m.areaInvertida += area; }
        if (n.z > 2.9f) m.areaFrente += area;
        else if (n.z < -2.9f) m.areaFundo += area;
        else if (std::fabs(n.z) < 1e-3f) m.areaLateral += area;
        else m.areaChanfro += area;
    }
    return m;
}

/// Malha inteira (todas as primitivas) somada.
MalhaInfo medir_tudo(const SceneAsset& a) {
    MalhaInfo m;
    for (const Mesh& mesh : a.meshes)
        for (const Primitive& p : mesh.primitives) {
            const MalhaInfo q = medir(p);
            m.triangulos += q.triangulos; m.invertidas += q.invertidas;
            m.degeneradas += q.degeneradas; m.areaInvertida += q.areaInvertida;
            m.areaFrente += q.areaFrente; m.areaFundo += q.areaFundo;
            m.areaLateral += q.areaLateral; m.areaChanfro += q.areaChanfro;
            m.minZ = std::min(m.minZ, q.minZ); m.maxZ = std::max(m.maxZ, q.maxZ);
            m.minX = std::min(m.minX, q.minX); m.maxX = std::max(m.maxX, q.maxX);
            m.minY = std::min(m.minY, q.minY); m.maxY = std::max(m.maxY, q.maxY);
        }
    return m;
}

/// Maior eixo do contorno 2D, em unidades do motor (1 = altura da fonte).
f64 maior_lado(const SceneAsset& a) {
    MalhaInfo m = medir_tudo(a);
    return std::max<double>(m.maxX - m.minX, m.maxY - m.minY);
}

scene3d::Text3DSpec receita(const char* texto) {
    scene3d::Text3DSpec s;
    s.content = texto;
    s.depth = 0.3f;
    s.color = Vec4{1.0f, 0.5f, 0.0f, 1.0f};
    return s;
}

} // namespace

// -----------------------------------------------------------------------------
// Chanfro
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, BevelChangesTheMeshAndKeepsItClosed) {
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec = receita("AUREA 8&");
    scene3d::ImportResult sem = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(sem.ok());
    if (!sem.ok()) return;
    const MalhaInfo a = medir_tudo(*sem.asset);

    spec.bevel = true;
    spec.bevelWidth = 0.03f;
    spec.bevelDepth = 0.04f;
    spec.bevelSegments = 3;
    spec.bevelRoundness = 1.0f;
    scene3d::ImportResult com = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(com.ok());
    if (!com.ok()) return;
    const MalhaInfo b = medir_tudo(*com.asset);

    std::printf("\n    sem chanfro: %u triangulos, frente %.4f, lateral %.4f, chanfro %.4f, invertidas %u de area %.3e (degeneradas %u)\n",
                a.triangulos, a.areaFrente, a.areaLateral, a.areaChanfro, a.invertidas, a.areaInvertida, a.degeneradas);
    std::printf("    com chanfro: %u triangulos, frente %.4f, lateral %.4f, chanfro %.4f, invertidas %u de area %.3e (degeneradas %u)\n",
                b.triangulos, b.areaFrente, b.areaLateral, b.areaChanfro, b.invertidas, b.areaInvertida, b.degeneradas);

    // A malha mudou de verdade: anel de chanfro, mais triângulos, tampa menor.
    AUREA_CHECK(b.areaChanfro > 0.0);
    AUREA_CHECK(a.areaChanfro == 0.0);
    AUREA_CHECK(b.triangulos > a.triangulos);
    AUREA_CHECK(b.areaFrente < a.areaFrente * 0.98);
    AUREA_CHECK(b.areaFrente > a.areaFrente * 0.50);
    // O chanfro não muda a silhueta nem a profundidade.
    AUREA_CHECK(std::fabs((b.maxX - b.minX) - (a.maxX - a.minX)) < 1e-4);
    AUREA_CHECK(std::fabs((b.maxZ - b.minZ) - 0.3f) < 1e-4);
    // O recuo é o pedido: a faixa de chanfro tem a largura da borda recuada.
    AUREA_CHECK(std::fabs((b.maxX - b.minX) - (a.maxX - a.minX)) < 1e-4);
    // Nenhuma face contra a própria normal, com ou sem chanfro.
    AUREA_CHECK(a.invertidas == 0);
    AUREA_CHECK(b.invertidas == 0);
}

AUREA_TEST(Text3D, BevelRoundnessAndSegmentsShapeTheProfile) {
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec = receita("Aurea");
    spec.bevel = true;
    spec.bevelWidth = 0.04f;
    spec.bevelDepth = 0.05f;

    f64 areaReto = 0, areaRedondo = 0;
    for (u32 modo = 0; modo < 2; ++modo) {
        spec.bevelSegments = 6;
        spec.bevelRoundness = modo == 0 ? 0.0f : 1.0f;
        scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
        AUREA_CHECK(r.ok());
        if (!r.ok()) return;
        const MalhaInfo m = medir_tudo(*r.asset);
        AUREA_CHECK(m.invertidas == 0);
        if (modo == 0) areaReto = m.areaChanfro; else areaRedondo = m.areaChanfro;
    }
    // Um segmento só: reto (chanfro de 45°) tem MENOS área que o filete
    // circular, que sai bojudo. Se o perfil fosse decorativo, empatariam.
    std::printf("\n    chanfro 1 segmento: reto %.4f, redondo %.4f\n", areaReto, areaRedondo);
    AUREA_CHECK(areaRedondo > areaReto * 1.02);
}

// -----------------------------------------------------------------------------
// Furos e contornos
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, HolesAreNotFilledWithOrWithoutBevel) {
    const auto font = text::default_font();
    if (!font) return;
    // "O", "8" e "B" têm dois furos cada; "&" tem um. O caso que importa: um
    // triângulo da tampa não pode cobrir o vazio da letra — nem com a tampa
    // recuada pelo chanfro, que alarga o furo.
    scene3d::Text3DSpec spec = receita("O8B&");
    spec.depth = 0.2f;
    spec.regionMaterials = true;   // a tampa vira a sua própria primitiva: dá para olhar só ela
    for (u32 modo = 0; modo < 2; ++modo) {
        spec.bevel = modo == 1;
        spec.bevelWidth = 0.02f;
        spec.bevelDepth = 0.03f;
        spec.bevelSegments = 2;
        scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
        AUREA_CHECK(r.ok());
        if (!r.ok()) return;

        TextData td;
        td.content = spec.content;
        td.size = 100.0f;
        td.alignment = spec.alignment;
        std::vector<std::vector<Vec2>> cs;
        AUREA_CHECK(text::outline(*font, td, cs));
        for (auto& c : cs)
            for (Vec2& p : c) { p.x /= 100.0f; p.y = -p.y / 100.0f; }
        auto area_assinada = [](const std::vector<Vec2>& c) {
            f64 a = 0;
            for (usize k = 0, j = c.size() - 1; k < c.size(); j = k++)
                a += static_cast<f64>(c[j].x) * c[k].y - static_cast<f64>(c[k].x) * c[j].y;
            return a * 0.5;
        };
        auto dentro_de = [](const std::vector<Vec2>& c, Vec2 q) {
            bool in = false;
            for (usize k = 0, m = c.size() - 1; k < c.size(); m = k++)
                if (((c[k].y > q.y) != (c[m].y > q.y))
                    && (q.x < (c[m].x - c[k].x) * (q.y - c[k].y) / (c[m].y - c[k].y) + c[k].x))
                    in = !in;
            return in;
        };

        // Só as tampas: nelas a normal é ±z.
        // Só as tampas: nelas TODAS as normais são ±z (a malha vem reordenada
        // pelo otimizador, então o vértice 0 não diz nada sobre a primitiva).
        std::vector<const Primitive*> tampas;
        for (const Mesh& mesh : r.asset->meshes)
            for (const Primitive& p : mesh.primitives) {
                bool so_tampa = !p.normals.empty();
                for (const Vec3& n : p.normals)
                    if (std::fabs(n.z) < 0.99f) { so_tampa = false; break; }
                if (so_tampa) tampas.push_back(&p);
            }
        AUREA_CHECK(!tampas.empty());

        u32 furos = 0, amostras = 0, cobertas = 0;
        for (usize i = 0; i < cs.size(); ++i) {
            const f64 ai = std::fabs(area_assinada(cs[i]));
            u32 dentro = 0;
            for (usize j = 0; j < cs.size(); ++j) {
                if (j == i || std::fabs(area_assinada(cs[j])) <= ai) continue;
                if (dentro_de(cs[j], cs[i][0])) ++dentro;
            }
            if (!(dentro & 1u)) continue;   // é borda, não furo
            ++furos;
            Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
            for (Vec2 p : cs[i]) {
                lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
                hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
            }
            // Grade sobre a caixa do furo: os pontos comprovadamente dentro do
            // contorno são o que a tampa não pode cobrir. (O centro da caixa
            // cairia fora num furo côncavo como o do "&".)
            const f32 dx = (hi.x - lo.x) / 9.0f, dy = (hi.y - lo.y) / 9.0f;
            for (u32 gx = 1; gx < 9; ++gx)
                for (u32 gy = 1; gy < 9; ++gy) {
                    const Vec2 q{lo.x + dx * static_cast<f32>(gx), lo.y + dy * static_cast<f32>(gy)};
                    if (!dentro_de(cs[i], q)) continue;
                    // Folga: exige que a vizinhança também esteja no furo, para
                    // não testar um ponto colado na borda.
                    if (!dentro_de(cs[i], Vec2{q.x + dx * 0.2f, q.y}) || !dentro_de(cs[i], Vec2{q.x - dx * 0.2f, q.y})
                        || !dentro_de(cs[i], Vec2{q.x, q.y + dy * 0.2f}) || !dentro_de(cs[i], Vec2{q.x, q.y - dy * 0.2f}))
                        continue;
                    ++amostras;
                    for (const Primitive* p : tampas) {
                        for (usize t = 0; t + 2 < p->indices.size(); t += 3) {
                            const Vec3 A = p->positions[p->indices[t]], B = p->positions[p->indices[t + 1]],
                                       C = p->positions[p->indices[t + 2]];
                            const f32 d = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
                            if (std::fabs(d) < 1e-12f) continue;
                            const f32 u = ((B.y - C.y) * (q.x - C.x) + (C.x - B.x) * (q.y - C.y)) / d;
                            const f32 v = ((C.y - A.y) * (q.x - C.x) + (A.x - C.x) * (q.y - C.y)) / d;
                            const f32 w = 1.0f - u - v;
                            if (u > 0.0f && v > 0.0f && w > 0.0f) { ++cobertas; t = p->indices.size(); break; }
                        }
                    }
                }
        }
        std::printf("\n    %s: %u furos, %u pontos dentro deles, %u cobertos pela tampa\n", modo ? "com chanfro" : "sem chanfro",
                    furos, amostras, cobertas);
        AUREA_CHECK(furos >= 5);        // O=2, 8=2, B=2, &=1 (a fonte pode variar, mas não zerar)
        AUREA_CHECK(amostras > 20);     // o teste não é vazio
        AUREA_CHECK(cobertas == 0);
    }
}

// -----------------------------------------------------------------------------
// Normais e tangentes
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, NormalsAndTangentsMatchEachRegion) {
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec = receita("Aurea");
    spec.bevel = true;
    spec.bevelWidth = 0.03f;
    spec.bevelDepth = 0.04f;
    spec.bevelSegments = 3;
    scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    u32 tampas = 0, paredes = 0, chanfros = 0;
    for (const Mesh& mesh : r.asset->meshes)
        for (const Primitive& p : mesh.primitives) {
            AUREA_CHECK(p.positions.size() == p.normals.size());
            AUREA_CHECK(p.positions.size() == p.uv0.size());
            AUREA_CHECK(p.positions.size() == p.tangents.size());
            AUREA_CHECK(p.generatedTangents);
            for (usize i = 0; i < p.positions.size(); ++i) {
                const Vec3 n = p.normals[i];
                AUREA_CHECK(std::fabs(n.length() - 1.0f) < 1e-3f);       // normal unitária
                const Vec4 t = p.tangents[i];
                AUREA_CHECK(std::fabs(Vec3{t.x, t.y, t.z}.length() - 1.0f) < 1e-3f);
                AUREA_CHECK(std::fabs(t.w) == 1.0f);                     // sinal da bitangente
                AUREA_CHECK(std::fabs(Vec3{t.x, t.y, t.z}.dot(n)) < 1e-3f);   // tangente ⟂ normal
                if (std::fabs(n.z) > 0.99f) ++tampas;
                else if (std::fabs(n.z) < 1e-3f) ++paredes;
                else {
                    ++chanfros;
                    // No chanfro a normal sai para fora (±xy) e para a ponta (±z).
                    AUREA_CHECK(std::fabs(n.z) < 0.999f);
                }
            }
        }
    std::printf("\n    vertices: %u de tampa, %u de parede, %u de chanfro\n", tampas, paredes, chanfros);
    AUREA_CHECK(tampas > 0 && paredes > 0 && chanfros > 0);
}

// -----------------------------------------------------------------------------
// Robustez do recuo
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, ThinStrokesCapTheBevelInsteadOfBreaking) {
    const auto font = text::default_font();
    if (!font) return;
    // "iiii" é praticamente só traço fino: um chanfro de 0.3 (30 % da altura da
    // fonte) não caberia — o recuo comeria o traço inteiro e a tampa sairia
    // virada. O recuo tem que encolher até caber, e a malha continuar fechada.
    scene3d::Text3DSpec spec = receita("iiiiII");
    spec.depth = 0.3f;
    spec.bevel = true;
    spec.bevelWidth = 0.3f;
    spec.bevelDepth = 0.3f;
    spec.bevelSegments = 2;
    scene3d::ImportResult pedido = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(pedido.ok());
    if (!pedido.ok()) return;
    const MalhaInfo grande = medir_tudo(*pedido.asset);

    // O mesmo texto com um chanfro que CABE.
    spec.bevelWidth = 0.02f;
    scene3d::ImportResult pequeno = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(pequeno.ok());
    if (!pequeno.ok()) return;
    const MalhaInfo cabe = medir_tudo(*pequeno.asset);

    std::printf("\n    pedido 0.30 -> chanfro %.5f, frente %.5f, invertidas %u\n", grande.areaChanfro, grande.areaFrente,
                grande.invertidas);
    std::printf("    pedido 0.02 -> chanfro %.5f, frente %.5f, invertidas %u\n", cabe.areaChanfro, cabe.areaFrente,
                cabe.invertidas);

    AUREA_CHECK(grande.invertidas == 0);
    AUREA_CHECK(cabe.invertidas == 0);
    AUREA_CHECK(grande.areaFrente > 0.0);
    // O TETO: 15× mais recuo pedido não pode virar 15× mais chanfro. O traço
    // fino limita, e é isso que impede a tampa de sumir.
    AUREA_CHECK(grande.areaChanfro < cabe.areaChanfro * 5.0);
    // O recuo contido deixa a mesma frente do chanfro que cabe — não some.
    AUREA_CHECK(grande.areaFrente > cabe.areaFrente * 0.5);
    scene3d::Text3DSpec sem = spec;
    sem.bevel = false;
    scene3d::ImportResult r2 = scene3d::build_text3d(*font, sem);
    AUREA_CHECK(r2.ok());
    if (!r2.ok()) return;
    AUREA_CHECK(grande.areaFrente < medir_tudo(*r2.asset).areaFrente);
}

// -----------------------------------------------------------------------------
// Material
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, RegionMaterialsSplitFrontSideAndBevel) {
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec = receita("Aurea");
    spec.bevel = true;
    spec.bevelWidth = 0.03f;
    spec.bevelDepth = 0.04f;
    spec.bevelSegments = 2;
    spec.regionMaterials = true;
    spec.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};        // frente branca
    spec.side.color = Vec4{0.0f, 0.0f, 0.0f, 1.0f};   // lateral preta
    spec.side.roughness = 0.8f;
    spec.bevelMat.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    spec.bevelMat.metallic = 1.0f;                     // chanfro cromado
    spec.bevelMat.roughness = 0.05f;
    scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    AUREA_CHECK(r.asset->materials.size() == 3);
    const Mesh& mesh = r.asset->meshes[0];
    AUREA_CHECK(mesh.primitives.size() == 3);
    // Tampa = material 0 (branca, sem metal), parede = 1 (preta), chanfro = 2 (cromado).
    AUREA_CHECK(mesh.primitives[0].material == 0);
    AUREA_CHECK(mesh.primitives[1].material == 1);
    AUREA_CHECK(mesh.primitives[2].material == 2);
    AUREA_CHECK(r.asset->materials[0].metallic == 0.0f && r.asset->materials[0].baseColor.x > 0.99f);
    AUREA_CHECK(r.asset->materials[1].baseColor.x < 0.01f && r.asset->materials[1].roughness > 0.7f);
    AUREA_CHECK(r.asset->materials[2].metallic > 0.99f && r.asset->materials[2].roughness < 0.1f);
    std::printf("\n    material por regiao: %zu primitivas, materiais %.2f/%.2f/%.2f (metal)\n",
                mesh.primitives.size(), static_cast<double>(r.asset->materials[0].metallic),
                static_cast<double>(r.asset->materials[1].metallic), static_cast<double>(r.asset->materials[2].metallic));
    // Todas as três regiões têm geometria — nenhuma primitiva vazia.
    for (const Primitive& p : mesh.primitives) AUREA_CHECK(p.triangle_count() > 0);

    // Com `regionMaterials = false` tudo volta a ser uma primitiva só.
    spec.regionMaterials = false;
    scene3d::ImportResult um = scene3d::build_text3d(*font, spec);
    AUREA_CHECK(um.ok());
    if (um.ok()) AUREA_CHECK(um.asset->meshes[0].primitives.size() == 1 && um.asset->materials.size() == 1);
}

AUREA_TEST(Text3D, PbrAndRecipeRoundTripThroughTheAssetSource) {
    scene3d::Text3DSpec spec = receita("Metal");
    spec.bevel = true;
    spec.bevelWidth = 0.031f;
    spec.bevelDepth = 0.047f;
    spec.bevelSegments = 5;
    spec.bevelRoundness = 0.25f;
    spec.metallic = 0.95f;
    spec.roughness = 0.12f;
    spec.specular = 0.7f;
    spec.occlusion = 0.4f;
    spec.emissive = Vec3{0.1f, 0.2f, 0.3f};
    spec.emissiveStrength = 4.5f;
    spec.regionMaterials = true;
    spec.side.metallic = 0.2f;
    spec.bevelMat.roughness = 0.85f;
    scene3d::Text3DSpec back;
    AUREA_CHECK(scene3d::decode_text3d(scene3d::encode_text3d(spec), back));
    AUREA_CHECK(back.content == spec.content);
    AUREA_CHECK(std::fabs(back.depth - spec.depth) < 1e-4f);
    AUREA_CHECK(back.bevel && back.bevelSegments == 5);
    AUREA_CHECK(std::fabs(back.bevelWidth - 0.031f) < 1e-4f);
    AUREA_CHECK(std::fabs(back.bevelRoundness - 0.25f) < 1e-3f);
    AUREA_CHECK(std::fabs(back.metallic - 0.95f) < 1e-2f);
    AUREA_CHECK(std::fabs(back.roughness - 0.12f) < 1e-2f);
    AUREA_CHECK(std::fabs(back.emissiveStrength - 4.5f) < 1e-1f);
    AUREA_CHECK(back.emissive.z > 0.2f);
    AUREA_CHECK(back.regionMaterials);
    AUREA_CHECK(std::fabs(back.side.metallic - 0.2f) < 1e-2f);
    AUREA_CHECK(std::fabs(back.bevelMat.roughness - 0.85f) < 1e-2f);

    // Receita antiga (v1, colada à mão): só cor/profundidade/alinhamento.
    const std::string v1 = std::string(scene3d::kText3DScheme) + "v1;d=0.4000;a=2;c=ff8000ff;t=Antigo";
    scene3d::Text3DSpec old;
    AUREA_CHECK(scene3d::decode_text3d(v1, old));
    AUREA_CHECK(old.content == "Antigo" && std::fabs(old.depth - 0.4f) < 1e-4f && old.alignment == 2);
    AUREA_CHECK(old.color.x > 0.99f && old.color.y > 0.49f && old.color.z < 0.01f);   // laranja preservado
    AUREA_CHECK(!old.bevel && !old.regionMaterials);
}

AUREA_TEST(Text3D, Text3DObjectSavesReopensAndUndoes) {
    if (!text::default_font()) return;
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());

    scene3d::Text3DSpec spec = receita("AUREA");
    spec.bevel = true;
    spec.bevelWidth = 0.03f;
    spec.bevelDepth = 0.04f;
    spec.bevelSegments = 2;
    spec.metallic = 0.9f;
    spec.roughness = 0.1f;
    const Result<u64> id = e.add_text3d(spec);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;

    scene3d::Text3DSpec lido;
    AUREA_CHECK(e.query_text3d(*id, lido));
    AUREA_CHECK(lido.bevel && lido.bevelSegments == 2);
    AUREA_CHECK(std::fabs(lido.metallic - 0.9f) < 1e-2f);

    // A malha do objeto recém-criado tem chanfro de verdade (normal inclinada).
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    auto tem_chanfro = [](const std::shared_ptr<const SceneAsset>& a) {
        if (!a) return false;
        for (const Mesh& m : a->meshes)
            for (const Primitive& p : m.primitives)
                for (const Vec3& n : p.normals)
                    if (std::fabs(n.z) > 1e-3f && std::fabs(n.z) < 0.999f) return true;
        return false;
    };
    AUREA_CHECK(tem_chanfro(e.model_asset(l->model.scene.pack())));

    // Desfazer volta a malha anterior; refazer traz de volta.
    AUREA_CHECK(e.set_text3d(*id, [&] { scene3d::Text3DSpec s = spec; s.bevel = false; return s; }()).ok());
    AUREA_CHECK(!tem_chanfro(e.model_asset(comp->layer(LayerId::unpack(*id))->model.scene.pack())));
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(u).ok());
    AUREA_CHECK(tem_chanfro(e.model_asset(comp->layer(LayerId::unpack(*id))->model.scene.pack())));

    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_text3d_geo.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    scene3d::Text3DSpec depois;
    AUREA_CHECK(e.query_text3d(*id, depois));
    AUREA_CHECK(depois.content == spec.content);
    AUREA_CHECK(depois.bevel && depois.bevelSegments == 2);
    AUREA_CHECK(std::fabs(depois.bevelWidth - 0.03f) < 1e-3f);
    AUREA_CHECK(std::fabs(depois.metallic - 0.9f) < 1e-2f);
    // Reaberto: a geometria veio de novo da receita, com o chanfro.
    Composition* comp2 = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* l2 = comp2->layer(LayerId::unpack(*id));
    AUREA_CHECK(l2 != nullptr);
    if (l2) AUREA_CHECK(tem_chanfro(e.model_asset(l2->model.scene.pack())));
    std::printf("\n    texto 3D salvo e reaberto com chanfro e PBR intactos\n");
}

// -----------------------------------------------------------------------------
// Cache da geometria
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, GeometryIsCachedAndMaterialChangesDoNotRebuildIt) {
    const auto font = text::default_font();
    if (!font) return;
    scene3d::Text3DSpec spec = receita("AUREA Texto 3D com chanfro 0123456789");
    spec.bevel = true;
    spec.bevelWidth = 0.02f;
    spec.bevelDepth = 0.03f;
    spec.bevelSegments = 3;

    const auto t0 = std::chrono::steady_clock::now();
    scene3d::ImportResult frio = scene3d::build_text3d(*font, spec);
    const auto t1 = std::chrono::steady_clock::now();
    AUREA_CHECK(frio.ok());
    if (!frio.ok()) return;

    // Mesma geometria, material diferente: só a cor muda.
    spec.color = Vec4{0.1f, 0.9f, 0.2f, 1.0f};
    spec.roughness = 0.9f;
    const auto t2 = std::chrono::steady_clock::now();
    scene3d::ImportResult quente = scene3d::build_text3d(*font, spec);
    const auto t3 = std::chrono::steady_clock::now();
    AUREA_CHECK(quente.ok());
    if (!quente.ok()) return;

    const f64 msFrio = std::chrono::duration<f64, std::milli>(t1 - t0).count();
    const f64 msQuente = std::chrono::duration<f64, std::milli>(t3 - t2).count();
    std::printf("\n    geometria: frio %.2f ms, quente %.2f ms (%.0fx)\n", msFrio, msQuente, msFrio / std::max(msQuente, 1e-3));

    // A geometria é a MESMA (bit a bit): o cache devolve os mesmos vértices…
    const Primitive& a = frio.asset->meshes[0].primitives[0];
    const Primitive& b = quente.asset->meshes[0].primitives[0];
    AUREA_CHECK(a.positions.size() == b.positions.size());
    AUREA_CHECK(a.indices.size() == b.indices.size());
    bool igual = a.positions.size() == b.positions.size();
    for (usize i = 0; igual && i < a.positions.size(); ++i)
        igual = a.positions[i].x == b.positions[i].x && a.positions[i].y == b.positions[i].y && a.positions[i].z == b.positions[i].z;
    for (usize i = 0; igual && i < a.indices.size(); ++i) igual = a.indices[i] == b.indices[i];
    AUREA_CHECK(igual);
    // …e o material mudou de verdade.
    AUREA_CHECK(quente.asset->materials[0].baseColor.y > 0.7f && frio.asset->materials[0].baseColor.y < 0.3f);
    // O trabalho caro não foi refeito: o reaproveitamento é grande, não marginal.
    AUREA_CHECK(msQuente * 3.0 < msFrio);

    // Geometria diferente refaz (e devolve outra malha).
    scene3d::Text3DSpec outro = spec;
    outro.bevelSegments = 5;
    scene3d::ImportResult novo = scene3d::build_text3d(*font, outro);
    AUREA_CHECK(novo.ok());
    if (novo.ok()) AUREA_CHECK(novo.asset->meshes[0].primitives[0].positions.size() != a.positions.size());
}

// -----------------------------------------------------------------------------
// Os caracteres que a fase exige
// -----------------------------------------------------------------------------

AUREA_TEST(Text3D, MandatoryGlyphsBuildClosedMeshesWithTheirHoles) {
    const auto font = text::default_font();
    if (!font) return;
    // "A", "B", "O", "R", "8", "&", "AUREA": contornos múltiplos, furos e
    // curvas. Cada um tem que sair fechado, com a tampa do tamanho da letra
    // (furos descontados) e sem face virada.
    const char* textos[] = {"A", "B", "O", "R", "8", "&", "AUREA"};
    for (const char* texto : textos) {
        for (u32 modo = 0; modo < 2; ++modo) {
            scene3d::Text3DSpec spec = receita(texto);
            spec.depth = 0.25f;
            spec.bevel = modo == 1;
            spec.bevelWidth = 0.02f;
            spec.bevelDepth = 0.03f;
            spec.bevelSegments = 2;
            scene3d::ImportResult r = scene3d::build_text3d(*font, spec);
            AUREA_CHECK_MSG(r.ok(), texto);
            if (!r.ok()) continue;
            const MalhaInfo m = medir_tudo(*r.asset);
            std::printf("\n    \"%s\" %s: %u triangulos, frente %.5f, fundo %.5f, lateral %.4f, chanfro %.4f, invertidas %u\n",
                        texto, modo ? "chanfro" : "extrusao", m.triangulos, m.areaFrente, m.areaFundo, m.areaLateral,
                        m.areaChanfro, m.invertidas);
            AUREA_CHECK_MSG(m.invertidas == 0, texto);
            AUREA_CHECK_MSG(m.triangulos > 0, texto);
            // Tampa e fundo do mesmo tamanho: a espessura é constante.
            AUREA_CHECK_MSG(std::fabs(m.areaFrente - m.areaFundo) < 1e-4, texto);
            // A espessura pedida é a que saiu.
            AUREA_CHECK_MSG(std::fabs((m.maxZ - m.minZ) - 0.25f) < 1e-4, texto);
            // Com chanfro, a tampa encolhe e aparece o anel (geometria nova).
            if (modo == 1) {
                AUREA_CHECK_MSG(m.areaChanfro > 0.0, texto);
                scene3d::Text3DSpec sem = spec;
                sem.bevel = false;
                scene3d::ImportResult r2 = scene3d::build_text3d(*font, sem);
                AUREA_CHECK(r2.ok());
                if (r2.ok()) AUREA_CHECK_MSG(m.areaFrente < medir_tudo(*r2.asset).areaFrente, texto);
            } else {
                AUREA_CHECK_MSG(m.areaChanfro == 0.0, texto);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Cache de geometria: a chave é a FONTE, não o endereço do objeto.
//
// O cache é global e guarda até 8 malhas. Com o ENDEREÇO do `Font` na chave
// (era `%p`), o alocador reaproveitar o endereço de uma fonte liberada para
// outra fonte fazia o cache devolver a malha da fonte ANTERIOR: o texto
// reaberto saía com a geometria de outra fonte. Estes dois testes prendem a
// propriedade que impede isso: a chave é função dos BYTES da fonte, e o cache
// nunca mistura duas fontes.
// -----------------------------------------------------------------------------
AUREA_TEST(Text3D, GeometryKeyComesFromTheFontBytesNotTheObjectAddress) {
    std::vector<text::FontEntry> fontes = text::FontManager::instance().list();
    AUREA_CHECK(fontes.size() >= 2u);
    if (fontes.size() < 2u) return;
    std::string pathA = fontes[0].path, pathB;
    for (const text::FontEntry& e : fontes) {
        if (e.family != fontes[0].family) { pathB = e.path; break; }
    }
    AUREA_CHECK(!pathB.empty());
    if (pathB.empty()) return;

    // Duas CARGA do MESMO arquivo: objetos diferentes (endereços diferentes).
    auto ca = text::Font::load(pathA);
    auto cb = text::Font::load(pathA);
    auto outra = text::Font::load(pathB);
    AUREA_CHECK(ca && cb && outra);
    if (!ca || !cb || !outra) return;
    AUREA_CHECK(static_cast<const void*>(ca.get()) != static_cast<const void*>(cb.get()));
    AUREA_CHECK_EQ(ca->content_id(), cb->content_id());

    Text3DSpec spec;
    spec.content = "Texto";
    spec.depth = 0.25f;
    // A MESMA fonte em dois objetos: MESMA chave (com o endereço, eram duas).
    AUREA_CHECK_EQ(text3d_geometry_key(*ca, spec), text3d_geometry_key(*cb, spec));
    // Fontes diferentes: chaves diferentes.
    AUREA_CHECK(text3d_geometry_key(*ca, spec) != text3d_geometry_key(*outra, spec));
    // Conteúdo diferente: chave diferente.
    Text3DSpec outro_texto = spec;
    outro_texto.content = "Outro";
    AUREA_CHECK(text3d_geometry_key(*ca, spec) != text3d_geometry_key(*ca, outro_texto));
}

// Reproduz o CICLO REAL: a fonte do projeto anterior morre, outra fonte entra
// e o alocador pode devolver o MESMO endereço. Com o endereço na chave, o
// cache servia a malha da fonte MORTA para a fonte nova — é o texto que
// "vira outra coisa" ao reabrir. Manter as duas fontes vivas (como um teste
// ingênuo faria) nunca colide de endereço e não prova nada.
//
// A prova não depende de o alocador reaproveitar o endereço: compara-se a
// malha suspeita com uma referência da MESMA fonte sob uma chave
// garantidamente nova. `regionMaterials` entra na chave e só troca o número de
// primitivas — nenhum vértice sai do lugar —, então contagem e caixa têm de
// bater. Se o cache tiver servido outra fonte, não batem.
AUREA_TEST(Text3D, MeshCacheNeverServesAnotherFont) {
    std::vector<text::FontEntry> fontes = text::FontManager::instance().list();
    if (fontes.size() < 2u) return;
    std::string pathA = fontes[0].path, pathB;
    for (const text::FontEntry& e : fontes) {
        if (e.family != fontes[0].family) { pathB = e.path; break; }
    }
    if (pathB.empty()) return;

    const auto vertices = [](const ImportResult& r) {
        usize n = 0;
        for (const Mesh& m : r.asset->meshes) {
            for (const Primitive& prim : m.primitives) n += prim.positions.size();
        }
        return n;
    };

    Text3DSpec spec;
    spec.content = "AUREA";
    spec.depth = 0.3f;

    // 1) A fonte A constrói e MORRE — a entrada dela fica no cache global.
    usize vertsA = 0;
    f32 larguraA = 0;
    {
        auto fa = text::Font::load(pathA);
        AUREA_CHECK(!!fa);
        if (!fa) return;
        ImportResult a = build_text3d(*fa, spec);
        AUREA_CHECK(a.ok());
        if (!a.ok()) return;
        vertsA = vertices(a);
        larguraA = a.asset->bounds.max.x - a.asset->bounds.min.x;
    }

    // 2) A fonte B entra no lugar que sobrou. Se o alocador devolver o MESMO
    //    endereço de A, o código antigo servia a malha de A para B — e é isso
    //    que o teste não deixa passar. O reaproveitamento não é forçável de
    //    forma portátil, então a chave (teste acima) é o que prende o defeito;
    //    aqui fica o invariante de ponta a ponta, que nunca falha à toa.
    auto fb = text::Font::load(pathB);
    AUREA_CHECK(!!fb);
    if (!fb) return;
    ImportResult b = build_text3d(*fb, spec);
    AUREA_CHECK(b.ok());
    if (!b.ok()) return;

    // 3) Referência da B por uma chave nova (mesma geometria, outra chave).
    Text3DSpec ref = spec;
    ref.regionMaterials = !spec.regionMaterials;
    ImportResult bref = build_text3d(*fb, ref);
    AUREA_CHECK(bref.ok());
    if (!bref.ok()) return;

    // As duas fontes precisam desenhar diferente, senão o teste não prova nada.
    const f32 larguraB = bref.asset->bounds.max.x - bref.asset->bounds.min.x;
    if (vertices(bref) == vertsA && std::fabs(larguraB - larguraA) < 1e-6f) return;

    AUREA_CHECK_EQ(vertices(b), vertices(bref));
    AUREA_CHECK_NEAR(b.asset->bounds.min.x, bref.asset->bounds.min.x, 1e-6f);
    AUREA_CHECK_NEAR(b.asset->bounds.max.x, bref.asset->bounds.max.x, 1e-6f);
    AUREA_CHECK_NEAR(b.asset->bounds.max.y, bref.asset->bounds.max.y, 1e-6f);
}
