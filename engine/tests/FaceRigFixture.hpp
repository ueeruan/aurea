// =============================================================================
//  Aurea / tests / FaceRigFixture.hpp
//
//  Rosto de desenho sintético para o Rig 2D de rosto: fundo transparente,
//  cabelo escuro, rosto cor de pele, olhos grandes escuros com brilho branco,
//  sobrancelhas finas, boca vermelha pequena, pescoço e ombros. Com `glasses`,
//  óculos rosa: aro redondo em volta de cada olho (passando por cima dele) e a
//  ponte entre os dois (Rig 2D v2: separar em partes).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <cmath>
#include <vector>

namespace face_rig_fixture {

struct Truth {
    aurea::Vec2 eye[2];   ///< centros (px)
    aurea::Vec2 eyeR;     ///< meia largura/altura (px)
    aurea::Vec2 mouth;
    aurea::Vec2 face, faceR;
};

inline std::vector<aurea::u8> anime_face(aurea::u32 W, aurea::u32 H, Truth& t, bool glasses = false) {
    using aurea::f32;
    using aurea::u32;
    std::vector<aurea::u8> px(static_cast<aurea::usize>(W) * H * 4, 0);
    const f32 s = static_cast<f32>(W) / 360.0f;
    auto put = [&](u32 x, u32 y, aurea::u8 r, aurea::u8 g, aurea::u8 b) {
        aurea::u8* p = &px[(static_cast<aurea::usize>(y) * W + x) * 4];
        p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
    };
    auto ellipse = [](f32 x, f32 y, f32 cx, f32 cy, f32 rx, f32 ry) {
        const f32 a = (x - cx) / rx, b = (y - cy) / ry;
        return a * a + b * b <= 1.0f;
    };
    t.face = {180 * s, 215 * s};
    t.faceR = {115 * s, 135 * s};
    t.eye[0] = {130 * s, 232 * s};
    t.eye[1] = {230 * s, 232 * s};
    t.eyeR = {24 * s, 30 * s};
    t.mouth = {180 * s, 302 * s};
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const f32 fx = static_cast<f32>(x) + 0.5f, fy = static_cast<f32>(y) + 0.5f;
            if (ellipse(fx, fy, 180 * s, 170 * s, 150 * s, 150 * s)) put(x, y, 70, 40, 30);   // cabelo
            if (fy > 370 * s && fx > 60 * s && fx < 300 * s) put(x, y, 60, 80, 160);           // ombros
            if (fy > 320 * s && fy <= 372 * s && fx > 150 * s && fx < 210 * s) put(x, y, 245, 218, 198);   // pescoço
            if (ellipse(fx, fy, t.face.x, t.face.y, t.faceR.x, t.faceR.y)) {
                if (fy < 120 * s) put(x, y, 70, 40, 30);   // franja
                else put(x, y, 250, 225, 205);
            }
            for (int i = 0; i < 2; ++i) {
                const aurea::Vec2 c = t.eye[i];
                if (ellipse(fx, fy, c.x, c.y, t.eyeR.x, t.eyeR.y)) put(x, y, 40, 30, 60);
                if (ellipse(fx, fy, c.x - 8 * s, c.y - 10 * s, 6 * s, 6 * s)) put(x, y, 255, 255, 255);
                // Sobrancelha fina bem acima.
                if (std::fabs(fy - 180 * s) < 2 * s && std::fabs(fx - c.x) < 28 * s) put(x, y, 70, 40, 30);
            }
            if (ellipse(fx, fy, t.mouth.x, t.mouth.y, 14 * s, 5 * s)) put(x, y, 170, 50, 60);
            if (glasses) {
                for (int i = 0; i < 2; ++i) {
                    // Aro: anel de 3 px em volta do olho, um pouco mais baixo (cruza o olho em cima).
                    const f32 dx = (fx - t.eye[i].x) / (34 * s), dy = (fy - (t.eye[i].y + 4 * s)) / (32 * s);
                    const f32 rr = std::sqrt(dx * dx + dy * dy);
                    if (std::fabs(rr - 1.0f) * 33 * s < 1.6f * s) put(x, y, 230, 120, 135);
                }
                if (fx > t.eye[0].x + 33 * s && fx < t.eye[1].x - 33 * s && std::fabs(fy - (t.eye[0].y + 2 * s)) < 1.6f * s)
                    put(x, y, 230, 120, 135);   // ponte
            }
        }
    return px;
}

} // namespace face_rig_fixture
