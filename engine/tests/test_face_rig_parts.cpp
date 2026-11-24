// =============================================================================
//  Rig 2D v2 — separar em partes (effects/FaceRigParts.hpp), sem GPU: as partes
//  somam de volta a imagem, cada parte cai onde devia (olhos nas âncoras,
//  cabelo em cima, óculos cruzando os dois olhos), o rosto preenchido sob os
//  olhos é pele (nada escuro), piscar não mexe nos óculos, a pálpebra corta o
//  olho, a física do cabelo é determinística (sem estado) e a pose neutra não
//  mexe em nada. A imagem de verdade (build/agents/rig108/input.rgba, 960×600)
//  entra quando existe. O render, o motor e salvar/reabrir: FaceRigGpu.inl.
// =============================================================================
#include "TestFramework.hpp"
#include "FaceRigFixture.hpp"

#include "aurea/effects/FaceRigParts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace aurea;
namespace fr = aurea::face_rig;

namespace {

const fr::Part* find_part(const fr::PartSet& s, u32 role) {
    for (const fr::Part& p : s.parts)
        if (p.role == role) return &p;
    return nullptr;
}

/// Centro (pesado pelo alfa) da parte, em px da imagem.
Vec2 part_center(const fr::Part& p) {
    f64 sx = 0, sy = 0, sw = 0;
    for (u32 y = 0; y < p.h; ++y)
        for (u32 x = 0; x < p.w; ++x) {
            const f64 a = p.rgba[(static_cast<usize>(y) * p.w + x) * 4 + 3] / 255.0;
            sx += (p.x0 + x + 0.5) * a; sy += (p.y0 + y + 0.5) * a; sw += a;
        }
    return sw > 0 ? Vec2{static_cast<f32>(sx / sw), static_cast<f32>(sy / sw)} : Vec2{-1e6f, -1e6f};
}

f32 to_linear(u8 v) {
    const f32 c = v / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// Pior e média do erro (linear, pré-multiplicado, como o renderer compõe)
/// entre as partes compostas e a imagem.
void recomposite_error(const fr::PartSet& set, const std::vector<u8>& rgba, f64& worst, f64& mean) {
    std::vector<f32> pm;
    fr::composite_parts(set, pm);
    worst = 0.0;
    f64 sum = 0.0;
    const usize n = static_cast<usize>(set.width) * set.height;
    for (usize i = 0; i < n; ++i) {
        const f32 a = rgba[i * 4 + 3] / 255.0f;
        for (u32 k = 0; k < 4; ++k) {
            const f32 ref = k < 3 ? to_linear(rgba[i * 4 + k]) * a : a;
            const f64 e = std::fabs(static_cast<f64>(pm[i * 4 + k]) - ref);
            worst = std::max(worst, e);
            sum += e;
        }
    }
    mean = sum / static_cast<f64>(n * 4);
}

f32 luma(const u8* p) { return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f; }

/// Pixels da parte `under` (onde ela existe) sob a parte `over`: o que estava
/// escondido e foi preenchido. Devolve quantos e quantos são escuros.
void hidden_under(const fr::PartSet& set, u32 over, u32 under, u32& count, u32& dark) {
    count = dark = 0;
    const fr::Part* o = find_part(set, over);
    const fr::Part* u = find_part(set, under);
    if (!o || !u) return;
    for (u32 y = 0; y < o->h; ++y)
        for (u32 x = 0; x < o->w; ++x) {
            if (o->rgba[(static_cast<usize>(y) * o->w + x) * 4 + 3] < 250) continue;
            const u32 gx = o->x0 + x, gy = o->y0 + y;
            if (gx < u->x0 || gy < u->y0 || gx >= u->x0 + u->w || gy >= u->y0 + u->h) continue;
            const u8* p = &u->rgba[(static_cast<usize>(gy - u->y0) * u->w + (gx - u->x0)) * 4];
            if (p[3] < 250) continue;   // ali a parte de baixo não existe (óculos sobre o cabelo)
            ++count;
            if (luma(p) < 0.45f) ++dark;
        }
}

bool load_real(std::vector<u8>& rgba) {
    const char* env = std::getenv("AUREA_FACERIG_IMAGE");
    const char* path = env && *env ? env : "../../../build/agents/rig108/input.rgba";
    FILE* f = std::fopen(path, "rb");
    if (!f) f = std::fopen("D:/Users/Ruan/Documents/Aureabeta/build/agents/rig108/input.rgba", "rb");
    if (!f) return false;
    rgba.assign(960u * 600u * 4u, 0);
    const usize got = std::fread(rgba.data(), 1, rgba.size(), f);
    std::fclose(f);
    return got == rgba.size();
}

} // namespace

AUREA_TEST(FaceRigParts, DrawnFaceSplitsIntoPartsThatSumBackToTheImage) {
    for (bool glasses : {false, true}) {
        const u32 W = 360, H = 440;
        face_rig_fixture::Truth t;
        const std::vector<u8> px = face_rig_fixture::anime_face(W, H, t, glasses);
        fr::Anchors a;
        AUREA_CHECK_EQ(fr::auto_place(px.data(), W, H, a), 2u);
        fr::PartSet set;
        AUREA_CHECK(fr::split_parts(px.data(), W, H, a, set));
        f64 worst = 0, mean = 0;
        recomposite_error(set, px, worst, mean);
        std::printf("\n    [%s] %zu partes, recomposição: pior %.4f média %.6f ", glasses ? "óculos" : "sem óculos",
                    set.parts.size(), worst, mean);
        AUREA_CHECK(worst < 0.012);
        AUREA_CHECK(mean < 1e-4);
        AUREA_CHECK_EQ(set.glasses, glasses);
        for (u32 role : {fr::kPartHairBack, fr::kPartBody, fr::kPartNeck, fr::kPartFace, fr::kPartIrisL, fr::kPartIrisR,
                         fr::kPartBrowL, fr::kPartBrowR, fr::kPartMouth, fr::kPartHairFront})
            AUREA_CHECK_MSG(find_part(set, role) != nullptr, fr::part_name(role));
        if (glasses) AUREA_CHECK(find_part(set, fr::kPartGlasses) != nullptr);
        // Cada parte onde devia: olhos nas âncoras, boca, cabelo em cima, corpo embaixo.
        for (u32 e = 0; e < 2; ++e)
            if (const fr::Part* iris = find_part(set, fr::kPartIrisL + e)) {
                const f32 d = (part_center(*iris) - t.eye[e]).length();
                std::printf("olho %u a %.1f px ", e, d);
                AUREA_CHECK(d < 0.35f * t.eyeR.x);
            }
        if (const fr::Part* m = find_part(set, fr::kPartMouth)) AUREA_CHECK((part_center(*m) - t.mouth).length() < 8.0f);
        if (const fr::Part* hb = find_part(set, fr::kPartHairBack)) AUREA_CHECK(part_center(*hb).y < t.face.y);
        if (const fr::Part* hf = find_part(set, fr::kPartHairFront)) AUREA_CHECK(part_center(*hf).y < t.eye[0].y);
        if (const fr::Part* b = find_part(set, fr::kPartBody)) AUREA_CHECK(part_center(*b).y > t.mouth.y);
        // O rosto sob os olhos e sob as sobrancelhas foi preenchido com pele (nada escuro).
        for (u32 over : {fr::kPartIrisL, fr::kPartIrisR, fr::kPartBrowL, fr::kPartBrowR, fr::kPartMouth}) {
            u32 count = 0, dark = 0;
            hidden_under(set, over, fr::kPartFace, count, dark);
            AUREA_CHECK(count > 0);
            AUREA_CHECK_MSG(dark == 0, fr::part_name(over));
        }
        // A íris continua por baixo do aro dos óculos (preenchida).
        if (glasses) {
            u32 count = 0, dark = 0;
            hidden_under(set, fr::kPartGlasses, fr::kPartFace, count, dark);
            AUREA_CHECK(count > 0 && dark == 0);
        }
        // Determinístico: separar de novo dá os mesmos bytes.
        fr::PartSet again;
        AUREA_CHECK(fr::split_parts(px.data(), W, H, a, again));
        bool same = again.parts.size() == set.parts.size();
        for (usize k = 0; same && k < set.parts.size(); ++k) same = again.parts[k].rgba == set.parts[k].rgba;
        AUREA_CHECK(same);
    }
}

AUREA_TEST(FaceRigParts, RealAnimeImageSeparatesGlassesEyesHairAndFace) {
    std::vector<u8> px;
    if (!load_real(px)) { std::printf(" (sem input.rgba: pulado) "); return; }
    const u32 W = 960, H = 600;
    fr::Anchors a;
    AUREA_CHECK(fr::auto_place(px.data(), W, H, a) >= 1u);
    fr::PartSet set;
    AUREA_CHECK(fr::split_parts(px.data(), W, H, a, set));
    f64 worst = 0, mean = 0;
    recomposite_error(set, px, worst, mean);
    std::printf("\n    %zu partes, recomposição: pior %.4f média %.6f ", set.parts.size(), worst, mean);
    AUREA_CHECK(worst < 0.012);
    AUREA_CHECK(mean < 1e-4);
    const fr::Rig rig = fr::rig_of(a, static_cast<f32>(W), static_cast<f32>(H));
    // Óculos: uma estrutura que cruza os dois olhos.
    AUREA_CHECK(set.glasses);
    if (const fr::Part* g = find_part(set, fr::kPartGlasses)) {
        std::printf("óculos x %u..%u y %u..%u ", g->x0, g->x0 + g->w, g->y0, g->y0 + g->h);
        AUREA_CHECK(static_cast<f32>(g->x0) < rig.eye[0].x && static_cast<f32>(g->x0 + g->w) > rig.eye[1].x);
        AUREA_CHECK(static_cast<f32>(g->y0) < rig.eye[0].y && static_cast<f32>(g->y0 + g->h) > rig.eye[0].y);
    } else AUREA_CHECK(false);
    for (u32 e = 0; e < 2; ++e) {
        const fr::Part* iris = find_part(set, fr::kPartIrisL + e);
        AUREA_CHECK(iris != nullptr);
        if (iris) {
            const f32 d = (part_center(*iris) - rig.eye[e]).length();
            std::printf("íris %u a %.1f px ", e, d);
            AUREA_CHECK(d < 1.2f * std::max(rig.eyeR[e].x, rig.eyeR[e].y));
        }
    }
    for (u32 role : {fr::kPartHairBack, fr::kPartHairFront, fr::kPartFace, fr::kPartBody, fr::kPartMouth, fr::kPartSideLocks})
        AUREA_CHECK_MSG(find_part(set, role) != nullptr, fr::part_name(role));
    if (const fr::Part* hb = find_part(set, fr::kPartHairBack)) AUREA_CHECK(static_cast<f32>(hb->y0) < rig.eye[0].y - rig.faceR.y);
    // O rosto escondido pelos olhos, óculos e boca virou pele.
    for (u32 over : {fr::kPartIrisL, fr::kPartIrisR, fr::kPartEyeWhiteL, fr::kPartEyeWhiteR, fr::kPartGlasses, fr::kPartMouth}) {
        u32 count = 0, dark = 0;
        hidden_under(set, over, fr::kPartFace, count, dark);
        std::printf("[%s %u/%u] ", fr::part_name(over), dark, count);
        // Os óculos passam também sobre a borda do rosto junto do cabelo (ali o
        // rosto fica com a cor de borda, não é preenchido): até 5 %.
        const u32 limit = over == fr::kPartGlasses ? 5u : 1u;
        AUREA_CHECK_MSG(dark * 100 <= count * limit, fr::part_name(over));
    }
}

AUREA_TEST(FaceRigParts, NeutralPoseKeepsEveryPartInPlaceAndBlinkSparesTheGlasses) {
    face_rig_fixture::Truth t;
    const std::vector<u8> px = face_rig_fixture::anime_face(360, 440, t, true);
    fr::Anchors a;
    (void)fr::auto_place(px.data(), 360, 440, a);
    const fr::Rig rig = fr::rig_of(a, 360.0f, 440.0f);
    const Vec2 lid{-0.7f, 0.85f};
    const fr::Pose neutral;
    for (u32 role = 0; role < fr::kPartRoleCount; ++role)
        for (f32 y = 0; y <= 440.0f; y += 11.0f)
            for (f32 x = 0; x <= 360.0f; x += 9.0f) {
                const Vec2 p{x, y};
                AUREA_CHECK((fr::part_displace(rig, neutral, role, fr::part_default_depth(role), Vec2{}, lid, p) - p).length() == 0.0f);
            }
    // Olhos fechados: os óculos não mexem um pixel; os cílios descem até a linha de baixo.
    fr::Pose closed;
    closed.eyeOpen[0] = closed.eyeOpen[1] = 0.0f;
    f32 glassesMove = 0.0f;
    for (f32 y = rig.eye[0].y - 40; y <= rig.eye[0].y + 40; y += 2.0f)
        for (f32 x = 60; x <= 300; x += 3.0f)
            glassesMove = std::max(glassesMove, (fr::part_displace(rig, closed, fr::kPartGlasses, 0.3f, Vec2{}, lid, Vec2{x, y}) - Vec2{x, y}).length());
    AUREA_CHECK(glassesMove == 0.0f);
    const Vec2 c = rig.eye[0], s = rig.eyeR[0];
    const f32 top = c.y + lid.x * s.y, low = c.y + lid.y * s.y;
    const Vec2 lash = fr::part_displace(rig, closed, fr::kPartLashL, 0.06f, Vec2{}, lid, Vec2{c.x, top});
    std::printf("\n    cílio %.2f -> %.2f (linha de baixo %.2f) ", top, lash.y, low);
    AUREA_CHECK_NEAR(lash.y, low, 0.05f);
    // A pálpebra: aberta não corta nada do olho; fechada corta até a linha de baixo.
    AUREA_CHECK(fr::lid_line(rig, 0, 0.0f, c.x, lid) < top);
    AUREA_CHECK(fr::lid_line(rig, 0, 1.0f, c.x, lid) >= low - 1e-3f);
    AUREA_CHECK(fr::lid_line(rig, 0, 0.5f, c.x, lid) > fr::lid_line(rig, 0, 0.0f, c.x, lid));
    // Virar: a franja anda mais que o rosto, o cabelo de trás ao contrário, o corpo fica.
    fr::Pose turn;
    turn.turn = 20.0f * kDeg2Rad;
    const Vec2 forehead{rig.face.x, rig.eye[0].y - 0.6f * rig.faceR.y};
    const f32 front = fr::part_displace(rig, turn, fr::kPartHairFront, 0.12f, Vec2{}, lid, forehead).x - forehead.x;
    const f32 face = fr::part_displace(rig, turn, fr::kPartFace, 0.0f, Vec2{}, lid, Vec2{rig.face.x + 0.8f * rig.faceR.x, rig.face.y}).x
                   - (rig.face.x + 0.8f * rig.faceR.x);
    const Vec2 side{rig.face.x - 1.1f * rig.faceR.x, rig.face.y};
    const f32 back = fr::part_displace(rig, turn, fr::kPartHairBack, -0.25f, Vec2{}, lid, side).x - side.x;
    const Vec2 belly{rig.neck.x, 430.0f};
    std::printf("franja %.2f rosto(borda) %.2f trás %.2f ", front, face, back);
    AUREA_CHECK(front > face && face >= 0.0f && back < 0.0f);
    AUREA_CHECK((fr::part_displace(rig, turn, fr::kPartBody, 0.0f, Vec2{}, lid, belly) - belly).length() == 0.0f);
}

AUREA_TEST(FaceRigParts, HairSpringIsDeterministicAndFollowsTheHead) {
    // Cabeça parada: nada balança.
    auto still = [](f64) { return Vec2{3.0f, -2.0f}; };
    AUREA_CHECK(fr::hair_swing(still, 2.0, 1.0f).length() < 1e-4f);
    // Cabeça que virou de repente em t = 1 s: logo depois o cabelo fica para
    // trás (o contrário do movimento); muito depois volta ao lugar.
    auto jump = [](f64 t) { return Vec2{t >= 1.0 ? 40.0f : 0.0f, 0.0f}; };
    const Vec2 just = fr::hair_swing(jump, 1.1, 1.0f), late = fr::hair_swing(jump, 6.0, 1.0f);
    std::printf("\n    logo depois %.2f px, bem depois %.4f px ", just.x, late.x);
    AUREA_CHECK(just.x < -5.0f);
    AUREA_CHECK(std::fabs(late.x) < 0.05f);
    // Sem estado: o mesmo instante dá o mesmo valor, em qualquer ordem de pedidos.
    const Vec2 a1 = fr::hair_swing(jump, 1.3, 0.7f);
    (void)fr::hair_swing(jump, 4.0, 0.7f);
    const Vec2 a2 = fr::hair_swing(jump, 1.3, 0.7f);
    AUREA_CHECK(a1.x == a2.x && a1.y == a2.y);
    AUREA_CHECK(fr::hair_swing(jump, 1.1, 0.0f).length() == 0.0f);
    // A ponta balança, a raiz não.
    face_rig_fixture::Truth t;
    const std::vector<u8> px = face_rig_fixture::anime_face(360, 440, t);
    fr::Anchors an;
    (void)fr::auto_place(px.data(), 360, 440, an);
    const fr::Rig rig = fr::rig_of(an, 360.0f, 440.0f);
    AUREA_CHECK(fr::hair_hang(rig, fr::kPartHairBack, Vec2{rig.face.x, rig.hairTop.y}) < 0.05f);
    AUREA_CHECK(fr::hair_hang(rig, fr::kPartHairBack, Vec2{rig.face.x, rig.neck.y + 1.2f * rig.faceR.y}) > 0.95f);
    AUREA_CHECK(fr::hair_hang(rig, fr::kPartFace, Vec2{rig.face.x, 400.0f}) == 0.0f);
}

AUREA_TEST(FaceRigParts, MembraneFillContinuesTheSurroundings) {
    // Degradê horizontal com um buraco no meio: o preenchimento continua o degradê.
    const u32 W = 64, H = 48;
    std::vector<f32> rgb(W * H * 3);
    std::vector<u8> known(W * H, 1), region(W * H, 0);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const usize i = static_cast<usize>(y) * W + x;
            const f32 v = static_cast<f32>(x) / (W - 1);
            rgb[i * 3] = v; rgb[i * 3 + 1] = 0.5f; rgb[i * 3 + 2] = 1.0f - v;
            if (x >= 20 && x < 44 && y >= 14 && y < 34) { known[i] = 0; region[i] = 1; rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = 0.0f; }
        }
    fr::membrane_fill(rgb, known, region, W, H);
    f32 worst = 0.0f;
    for (u32 y = 14; y < 34; ++y)
        for (u32 x = 20; x < 44; ++x) {
            const usize i = static_cast<usize>(y) * W + x;
            worst = std::max(worst, std::fabs(rgb[i * 3] - static_cast<f32>(x) / (W - 1)));
            worst = std::max(worst, std::fabs(rgb[i * 3 + 1] - 0.5f));
        }
    std::printf("\n    erro no buraco %.4f ", worst);
    AUREA_CHECK(worst < 0.08f);
}
