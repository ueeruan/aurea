// =============================================================================
//  Rig 2D de rosto (effects/FaceRig.hpp), sem GPU: pose neutra não mexe em
//  nada, olho fechado leva a pálpebra de cima à linha de baixo, virar mexe
//  mais o que está perto (nariz) que o contorno, nenhuma pose extrema dobra a
//  grade, a busca acha os olhos e a boca de um rosto de desenho e cai nas
//  proporções sem rosto. O render e o salvar/reabrir estão em FaceRigGpu.inl.
// =============================================================================
#include "TestFramework.hpp"
#include "FaceRigFixture.hpp"

#include "aurea/effects/FaceRig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace aurea;
namespace fr = aurea::face_rig;

namespace {

fr::Rig default_rig(f32 w = 512.0f, f32 h = 512.0f) {
    return fr::rig_of(fr::proportional_anchors(0.2f, 0.12f, 0.8f, 0.78f), w, h);
}

f32 dist(Vec2 a, Vec2 b) { return (a - b).length(); }

/// Pior área com sinal (deformada / repouso) entre os triângulos da grade.
f32 worst_area_ratio(const fr::Rig& rig, const fr::Pose& pose) {
    std::vector<f32> xs, ys;
    fr::grid_lines(rig, 48, xs, ys);
    const usize nx = xs.size(), ny = ys.size();
    std::vector<Vec2> def(nx * ny);
    for (usize j = 0; j < ny; ++j)
        for (usize i = 0; i < nx; ++i) def[j * nx + i] = fr::displace(rig, pose, Vec2{xs[i], ys[j]});
    auto area = [](Vec2 a, Vec2 b, Vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    f32 worst = 1e9f;
    for (usize j = 0; j + 1 < ny; ++j)
        for (usize i = 0; i + 1 < nx; ++i) {
            const Vec2 r00{xs[i], ys[j]}, r10{xs[i + 1], ys[j]}, r01{xs[i], ys[j + 1]}, r11{xs[i + 1], ys[j + 1]};
            const f32 a0 = area(r00, r10, r01), a1 = area(r10, r11, r01);
            const Vec2 d00 = def[j * nx + i], d10 = def[j * nx + i + 1], d01 = def[(j + 1) * nx + i], d11 = def[(j + 1) * nx + i + 1];
            if (a0 > 1e-6f) worst = std::min(worst, area(d00, d10, d01) / a0);
            if (a1 > 1e-6f) worst = std::min(worst, area(d10, d11, d01) / a1);
        }
    return worst;
}

} // namespace

AUREA_TEST(FaceRig, NeutralPoseLeavesEveryPointInPlace) {
    const fr::Rig rig = default_rig();
    const fr::Pose pose;
    AUREA_CHECK(fr::is_neutral(pose));
    std::vector<f32> xs, ys;
    fr::grid_lines(rig, 56, xs, ys);
    AUREA_CHECK(xs.front() == 0.0f && xs.back() == 512.0f && ys.front() == 0.0f && ys.back() == 512.0f);
    AUREA_CHECK(xs.size() > 57 && ys.size() > 57);   // linhas-chave dos olhos/boca além das uniformes
    f32 worst = 0.0f;
    for (f32 y : ys)
        for (f32 x : xs) worst = std::max(worst, dist(fr::displace(rig, pose, Vec2{x, y}), Vec2{x, y}));
    std::printf("    pior desvio na pose neutra %.6f px ", worst);
    AUREA_CHECK(worst == 0.0f);
    // Respiração parada no instante 0 (começa em 0) e quantidade 0 = neutra.
    AUREA_CHECK(fr::breath_at(1.0f, 15.0f, 0.0) == 0.0f);
    AUREA_CHECK(fr::breath_at(0.0f, 15.0f, 1.3) == 0.0f);
    AUREA_CHECK(fr::breath_at(1.0f, 15.0f, 2.0) > 0.04f);   // meio ciclo de 4 s: no alto
}

AUREA_TEST(FaceRig, ClosedEyePutsTheUpperLidOnTheLowerLine) {
    const fr::Rig rig = default_rig();
    fr::Pose pose;
    pose.eyeOpen[0] = 0.0f;   // só o olho da esquerda da imagem
    const Vec2 c = rig.eye[0], s = rig.eyeR[0];
    const f32 top = c.y - s.y, low = c.y + 0.7f * s.y;
    // Toda a coluna do meio do olho, do topo à linha de baixo, cai na linha de baixo.
    f32 worst = 0.0f;
    for (f32 y = top; y <= low; y += 0.5f) worst = std::max(worst, std::fabs(fr::displace(rig, pose, Vec2{c.x, y}).y - low));
    std::printf("    pálpebra fora da linha de baixo: %.5f px ", worst);
    AUREA_CHECK(worst < 1e-3f);
    // A pele logo acima desce por cima do olho; acima da faixa nada mexe; abaixo também não.
    const Vec2 lidSkin = fr::displace(rig, pose, Vec2{c.x, top - 0.45f * s.y});
    AUREA_CHECK(lidSkin.y > top + 0.3f * s.y);
    AUREA_CHECK(dist(fr::displace(rig, pose, Vec2{c.x, top - 2.0f * s.y}), Vec2{c.x, top - 2.0f * s.y}) == 0.0f);
    AUREA_CHECK(dist(fr::displace(rig, pose, Vec2{c.x, low + 2.0f}), Vec2{c.x, low + 2.0f}) == 0.0f);
    // O outro olho fica aberto.
    AUREA_CHECK(dist(fr::displace(rig, pose, rig.eye[1]), rig.eye[1]) == 0.0f);
    // Meio aberto: a linha dos cílios (topo) desce até a metade do caminho.
    pose.eyeOpen[0] = 0.5f;
    AUREA_CHECK_NEAR(fr::displace(rig, pose, Vec2{c.x, top}).y, top + 0.5f * (low - top), 1e-3f);
    // Nada dobra (o miolo do olho fechado só encolhe a zero).
    pose.eyeOpen[0] = pose.eyeOpen[1] = 0.0f;
    AUREA_CHECK(worst_area_ratio(rig, pose) > -1e-4f);
}

AUREA_TEST(FaceRig, TurnMovesNearPointsMoreThanTheOutline) {
    const fr::Rig rig = default_rig();
    fr::Pose pose;
    pose.turn = 20.0f * kDeg2Rad;
    const Vec2 eyes = (rig.eye[0] + rig.eye[1]) * 0.5f;
    const Vec2 nose{eyes.x, eyes.y + 0.6f * (rig.mouth.y - eyes.y)};
    const f32 noseMove = fr::displace(rig, pose, nose).x - nose.x;
    const Vec2 cheek{rig.face.x + 0.8f * rig.faceR.x, rig.face.y};
    const f32 cheekMove = fr::displace(rig, pose, cheek).x - cheek.x;
    const Vec2 edge{rig.face.x - rig.faceR.x, rig.face.y};
    const f32 edgeMove = std::fabs(fr::displace(rig, pose, edge).x - edge.x);
    std::printf("    nariz %.2f px, bochecha %.2f px, contorno %.3f px ", noseMove, cheekMove, edgeMove);
    AUREA_CHECK(noseMove > 0.15f * rig.faceR.x);   // virar para a direita: o nariz anda para a direita
    AUREA_CHECK(cheekMove > 0.0f && cheekMove < 0.6f * noseMove);
    AUREA_CHECK(edgeMove < 0.01f * rig.faceR.x);
    // Longe da cabeça (canto de baixo) nada mexe.
    AUREA_CHECK(dist(fr::displace(rig, pose, Vec2{2.0f, 510.0f}), Vec2{2.0f, 510.0f}) == 0.0f);
    // Acenar: o nariz desce.
    fr::Pose nod;
    nod.nod = 20.0f * kDeg2Rad;
    AUREA_CHECK(fr::displace(rig, nod, nose).y - nose.y > 0.15f * rig.faceR.y);
}

AUREA_TEST(FaceRig, TiltMouthAndBodyStayFoldFreeAtTheExtremes) {
    for (f32 w : {512.0f, 1080.0f})
        for (f32 sign : {-1.0f, 1.0f}) {
            const fr::Rig rig = default_rig(w, 0.8f * w + 300.0f);
            fr::Pose pose;
            pose.turn = sign * 30.0f * kDeg2Rad;
            pose.nod = -sign * 30.0f * kDeg2Rad;
            pose.tilt = sign * 30.0f * kDeg2Rad;
            pose.eyeOpen[0] = 0.0f;
            pose.eyeOpen[1] = 0.3f;
            pose.mouthOpen = 1.0f;
            pose.smile = sign;
            pose.brow[0] = sign;
            pose.brow[1] = -sign;
            pose.sway = sign * 10.0f * kDeg2Rad;
            pose.breath = 0.05f;
            const f32 worst = worst_area_ratio(rig, pose);
            std::printf("    [%g %+g] menor área %.3f ", w, sign, worst);
            AUREA_CHECK(worst > -1e-4f);
        }
    // Inclinar: o queixo é o pivô (fica), o topo da cabeça gira.
    const fr::Rig rig = default_rig();
    fr::Pose tilt;
    tilt.tilt = 15.0f * kDeg2Rad;
    AUREA_CHECK(dist(fr::displace(rig, tilt, rig.neck), rig.neck) < 1e-3f);
    const Vec2 top{rig.face.x, rig.face.y - 0.9f * rig.faceR.y};
    AUREA_CHECK(fr::displace(rig, tilt, top).x - top.x > 0.1f * rig.faceR.y);   // horário: o topo vai para a direita
    // Boca aberta: o lábio de baixo desce, a linha estica.
    fr::Pose open;
    open.mouthOpen = 1.0f;
    const Vec2 lower{rig.mouth.x, rig.mouth.y + 2.0f * rig.mouthR.y};
    AUREA_CHECK(fr::displace(rig, open, lower).y - lower.y > 0.1f * (rig.neck.y - rig.mouth.y));
}

AUREA_TEST(FaceRig, AutoPlaceFindsTheEyesAndMouthOfADrawnFace) {
    for (u32 W : {360u, 720u}) {
        const u32 H = W * 440 / 360;
        face_rig_fixture::Truth t;
        const std::vector<u8> px = face_rig_fixture::anime_face(W, H, t);
        fr::Anchors a;
        const u32 q = fr::auto_place(px.data(), W, H, a);
        const f32 fw = static_cast<f32>(W), fh = static_cast<f32>(H);
        const f32 e0 = dist(Vec2{a.eye[0].x * fw, a.eye[0].y * fh}, t.eye[0]);
        const f32 e1 = dist(Vec2{a.eye[1].x * fw, a.eye[1].y * fh}, t.eye[1]);
        const f32 m = dist(Vec2{a.mouth.x * fw, a.mouth.y * fh}, t.mouth);
        std::printf("    [%u] qualidade %u, olhos a %.1f e %.1f px, boca a %.1f px, olho %.1fx%.1f px ", W, q, e0, e1, m,
                    a.eyeR[0].x * fw, a.eyeR[0].y * fh);
        AUREA_CHECK_EQ(q, 2u);
        AUREA_CHECK(e0 < 0.25f * t.eyeR.x && e1 < 0.25f * t.eyeR.x);
        AUREA_CHECK(m < 0.5f * t.eyeR.x);
        // O tamanho do olho é o do olho inteiro, não só do miolo escuro.
        AUREA_CHECK(a.eyeR[0].x * fw > 0.7f * t.eyeR.x && a.eyeR[0].x * fw < 1.6f * t.eyeR.x);
        AUREA_CHECK(a.eyeR[0].y * fh > 0.6f * t.eyeR.y && a.eyeR[0].y * fh < 1.6f * t.eyeR.y);
        // O rosto envolve olhos e boca; o queixo fica abaixo da boca.
        AUREA_CHECK(std::fabs(a.face.x * fw - t.face.x) < 0.1f * t.faceR.x);
        AUREA_CHECK(a.neck.y > a.mouth.y && a.hairTop.y < a.eye[0].y);
    }
}

AUREA_TEST(FaceRig, AutoPlaceFallsBackToProportions) {
    // Imagem lisa (sem rosto): olhos a 42 % da altura e 30 %/70 % da largura da caixa.
    std::vector<u8> flat(200u * 200u * 4u, 200);
    fr::Anchors a;
    AUREA_CHECK_EQ(fr::auto_place(flat.data(), 200, 200, a), 0u);
    const fr::Anchors d = fr::proportional_anchors(0.2f, 0.12f, 0.8f, 0.78f);
    AUREA_CHECK_NEAR(a.eye[0].x, d.eye[0].x, 1e-5f);
    AUREA_CHECK_NEAR(a.eye[0].y, 0.12f + 0.42f * 0.66f, 1e-5f);
    AUREA_CHECK_NEAR(a.eye[1].x, 0.2f + 0.7f * 0.6f, 1e-5f);
    AUREA_CHECK_NEAR(a.mouth.y, 0.12f + 0.70f * 0.66f, 1e-5f);
    AUREA_CHECK_EQ(fr::auto_place(nullptr, 0, 0, a), 0u);
}
