// Testes da matemática e da interpolação de curva.
//
// A interpolação é o que o usuário vê quando mexe numa curva de easing: se ela
// está errada, a animação inteira está — e o erro é sutil o bastante para passar
// por revisão visual. Por isso ela tem teste numérico.
#include "TestFramework.hpp"
#include "aurea/core/Math.hpp"

using namespace aurea;

AUREA_TEST(Math, Vec3CrossEOrtogonal) {
    const Vec3 a{1.0f, 0.0f, 0.0f};
    const Vec3 b{0.0f, 1.0f, 0.0f};
    const Vec3 c = a.cross(b);
    AUREA_CHECK_NEAR(c.x, 0.0f, 1e-6);
    AUREA_CHECK_NEAR(c.y, 0.0f, 1e-6);
    AUREA_CHECK_NEAR(c.z, 1.0f, 1e-6);
}

AUREA_TEST(Math, QuatEulerRoundTrip) {
    // Rotação em Z puro: 90 graus leva X para Y.
    const Quat q = Quat::from_euler_zyx(0.0f, 0.0f, kHalfPi);
    const Mat4 m = Mat4::from_quat(q);
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 r = m.transform_point(x);
    AUREA_CHECK_NEAR(r.x, 0.0f, 1e-5);
    AUREA_CHECK_NEAR(r.y, 1.0f, 1e-5);
    AUREA_CHECK_NEAR(r.z, 0.0f, 1e-5);
}

AUREA_TEST(Math, QuatSlerpEndpoints) {
    const Quat a = Quat::identity();
    const Quat b = Quat::from_euler_zyx(0.0f, 0.0f, kPi);

    const Quat start = Quat::slerp(a, b, 0.0f);
    AUREA_CHECK_NEAR(start.w, 1.0f, 1e-5);

    // Uma volta de 180 graus tem DUAS representações equivalentes (q e -q são a
    // mesma rotação). Qual delas o slerp devolve depende do sinal do produto
    // interno, que a 180 graus é ruído de ponto flutuante. Exigir o sinal exato
    // seria exigir estabilidade numérica que não existe — o invariante correto é
    // que o resultado representa a mesma rotação.
    const Quat end = Quat::slerp(a, b, 1.0f);
    AUREA_CHECK_NEAR(std::abs(end.z), 1.0f, 1e-4);
    AUREA_CHECK_NEAR(std::abs(end.w), 0.0f, 1e-4);
}

AUREA_TEST(Math, QuatSlerpQuarterTurnIsExact) {
    // Uma volta de 90 graus não é ambígua: é o caso que o usuário vê ao animar
    // rotação, e o ponto médio tem que ser 45 graus.
    const Quat a = Quat::identity();
    const Quat b = Quat::from_euler_zyx(0.0f, 0.0f, kHalfPi);
    const Quat mid = Quat::slerp(a, b, 0.5f);

    const f32 expected = std::sin(kHalfPi * 0.25f);   // sin(22.5 graus)
    AUREA_CHECK_NEAR(mid.z, expected, 1e-5);
    AUREA_CHECK_NEAR(mid.w, std::cos(kHalfPi * 0.25f), 1e-5);
}

AUREA_TEST(Math, Mat4PerspectiveDepthRange) {
    // Com profundidade em [0,1] (Vulkan/Metal), um ponto no plano próximo tem
    // z = 0 e no distante, z = 1. Sem isso o compositor 3D desenharia com o
    // intervalo errado e metade da cena seria cortada.
    const Mat4 p = Mat4::perspective(kHalfPi * 0.5f, 16.0f / 9.0f, 1.0f, 100.0f);
    const Vec3 nearPt = p.transform_point(Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 farPt  = p.transform_point(Vec3{0.0f, 0.0f, -100.0f});
    AUREA_CHECK_NEAR(nearPt.z, 0.0f, 1e-3);
    AUREA_CHECK_NEAR(farPt.z, 1.0f, 1e-3);
}

AUREA_TEST(Math, Mat4LookAtDegenerateUp) {
    // Câmera olhando exatamente na direção do vetor "up": sem a proteção contra
    // degenerescência, isso produz NaN e a cena desaparece. A proteção escolhe
    // um eixo alternativo.
    const Mat4 m = Mat4::look_at(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f},
                                 Vec3{0.0f, 1.0f, 0.0f});
    for (int c = 0; c < 4; ++c) {
        AUREA_CHECK(!std::isnan(m.col[c].x));
        AUREA_CHECK(!std::isnan(m.col[c].y));
        AUREA_CHECK(!std::isnan(m.col[c].z));
    }
}

AUREA_TEST(Curve, CubicBezierLinearEndpoints) {
    // Control points nos cantos produzem a identidade.
    AUREA_CHECK_NEAR(cubic_bezier(0.0f, 0.0f, 1.0f, 1.0f, 0.0f), 0.0f, 1e-5);
    AUREA_CHECK_NEAR(cubic_bezier(0.0f, 0.0f, 1.0f, 1.0f, 0.5f), 0.5f, 1e-3);
    AUREA_CHECK_NEAR(cubic_bezier(0.0f, 0.0f, 1.0f, 1.0f, 1.0f), 1.0f, 1e-5);
}

AUREA_TEST(Curve, CubicBezierMonotonicInX) {
    // Uma curva de easing válida nunca anda para trás em x. Se andasse, a
    // animação voltaria no tempo no meio do intervalo.
    f32 previous = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const f32 x = static_cast<f32>(i) / 100.0f;
        const f32 y = cubic_bezier(0.42f, 0.0f, 0.58f, 1.0f, x);
        AUREA_CHECK(y >= previous - 1e-4f);
        previous = y;
    }
}

AUREA_TEST(Curve, CubicBezierOutOfRangeControls) {
    // Control points fora de [0,1] são permitidos (é o que produz "overshoot" no
    // estilo do After Effects). O resolvedor precisa aguentar sem travar nem
    // produzir NaN — o fallback por bisseção existe para isso.
    const f32 y = cubic_bezier(1.5f, -0.6f, -0.5f, 1.7f, 0.5f);
    AUREA_CHECK(!std::isnan(y));
    AUREA_CHECK(y > -10.0f && y < 10.0f);
}

AUREA_TEST(Curve, EasingHoldReturnsZero) {
    // Hold devolve o valor do keyframe da esquerda: o fator de mistura é 0 em
    // todo o intervalo, e o valor só troca ao chegar no próximo keyframe.
    for (int i = 0; i <= 10; ++i) {
        const f32 t = static_cast<f32>(i) / 10.0f;
        AUREA_CHECK_NEAR(apply_easing(Interpolation::Hold, t, 0, 0, 1, 1), 0.0f, 1e-6);
    }
}

AUREA_TEST(Curve, EasingEndpointsExceptHold) {
    // Hold é a exceção por definição: ele mantém o valor do keyframe da
    // ESQUERDA durante todo o intervalo, inclusive no fim. O valor só troca
    // quando o próximo keyframe passa a ser o da esquerda — o que na avaliação
    // real acontece porque `find_before` devolve o índice seguinte. Por isso
    // Hold tem teste próprio.
    const Interpolation kinds[] = {
        Interpolation::Linear, Interpolation::EaseIn, Interpolation::EaseOut,
        Interpolation::EaseInOut, Interpolation::Bezier,
    };
    for (Interpolation k : kinds) {
        const f32 at0 = apply_easing(k, 0.0f, 0.33f, 0.0f, 0.67f, 1.0f);
        const f32 at1 = apply_easing(k, 1.0f, 0.33f, 0.0f, 0.67f, 1.0f);
        AUREA_CHECK_NEAR(at0, 0.0f, 1e-5);
        AUREA_CHECK_NEAR(at1, 1.0f, 1e-4);
    }
}

AUREA_TEST(Time, FrameTimeRoundTrip) {
    // 29.97 fps é a taxa que quebra implementações ingênuas: 1 hora de timeline
    // acumula erro de vários frames se a conversão não for estável.
    const f64 fps = 30000.0 / 1001.0;
    for (i64 f = 0; f < 100000; f += 997) {
        const TickNs t = tick_at(FrameIndex{f}, fps);
        const FrameIndex back = frame_at(t, fps);
        AUREA_CHECK_EQ(back.value, f);
    }
}

AUREA_TEST(Time, FrameAtFloorsNegative) {
    // Um instante antes do zero cai no frame -1, não no 0: a truncagem de C
    // para um negativo daria 0, e o frame errado seria desenhado no pré-roll.
    const TickNs t{-1000};
    AUREA_CHECK_EQ(frame_at(t, 30.0).value, -1);
}

AUREA_TEST(Time, FrameDurationIsStable) {
    AUREA_CHECK_EQ(frame_duration(60.0).value, 16666667);
    AUREA_CHECK_EQ(frame_duration(30.0).value, 33333333);
    AUREA_CHECK_EQ(frame_duration(0.0).value, 0);
}
