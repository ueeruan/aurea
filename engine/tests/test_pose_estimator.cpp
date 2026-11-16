// =============================================================================
//  Motion Capture do Aurea PC (só Windows): as contas da ROI do rastreio (sem
//  modelo) e o BlazePose do motor (ncnn) numa figura humana DESENHADA aqui
//  (sem foto de pessoa no repositório): o detector acha a pessoa, os 33 pontos
//  saem finitos, perto das juntas desenhadas e na ordem do corpo, e o rastreio
//  segue a figura quando ela anda sem rodar o detector de novo.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/ai/PoseEstimator.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::mocap;
namespace lm = aurea::rig::lm;

namespace {

constexpr f32 kPiF = 3.14159265358979323846f;

// ---- Figura sintética --------------------------------------------------------
//  A mesma geometria de scratchpad/mocap/figure.py, conferida no TFLite do
//  Google (detector 0,98, presença 1,0): pessoa de frente, braços para baixo.

struct Rgb { u8 r, g, b; };
struct V2 { f32 x, y; };

struct Shape {
    enum Kind { Capsule, Ellipse, Quad, HalfEllipse, Rect } kind;
    V2 a, b, c, d;   // capsule: a-b; ellipse/halfellipse: centro a, raios b; quad: a b c d; rect: a (min) b (max)
    f32 r;
    Rgb color;
    [[nodiscard]] bool contains(f32 x, f32 y) const noexcept {
        switch (kind) {
            case Capsule: {
                const f32 vx = b.x - a.x, vy = b.y - a.y;
                const f32 len2 = vx * vx + vy * vy;
                f32 t = len2 > 0 ? ((x - a.x) * vx + (y - a.y) * vy) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const f32 dx = x - (a.x + vx * t), dy = y - (a.y + vy * t);
                return dx * dx + dy * dy <= r * r;
            }
            case Ellipse:
            case HalfEllipse: {
                const f32 dx = (x - a.x) / b.x, dy = (y - a.y) / b.y;
                return dx * dx + dy * dy <= 1.0f && (kind == Ellipse || y <= a.y);
            }
            case Quad: {
                const V2 p[4]{a, b, c, d};
                bool pos = false, neg = false;
                for (int i = 0; i < 4; ++i) {
                    const V2 u = p[i], v = p[(i + 1) % 4];
                    const f32 cr = (v.x - u.x) * (y - u.y) - (v.y - u.y) * (x - u.x);
                    pos |= cr > 0; neg |= cr < 0;
                }
                return !(pos && neg);
            }
            case Rect: return x >= a.x && x <= b.x && y >= a.y && y <= b.y;
        }
        return false;
    }
};

struct Figure {
    u32 w = 480, h = 360;
    std::vector<u8> rgba;
    // Juntas desenhadas (pixels): índice do modelo de 33 pontos → posição.
    V2 joint[rig::kPoseLandmarks]{};
    bool has[rig::kPoseLandmarks]{};
};

Figure draw_figure(f32 shiftX = 0.0f) {
    Figure f;
    const f32 W = static_cast<f32>(f.w), H = static_cast<f32>(f.h);
    const f32 u = H / 9.0f * 0.9f, top = H * 0.06f, cx = W * 0.5f + shiftX;
    const Rgb bg{178, 186, 196}, floor{120, 110, 100}, skin{224, 172, 140}, shirt{200, 60, 60}, pants{40, 60, 110},
        shoe{30, 30, 30}, hair{50, 35, 25}, eye{20, 20, 20};
    const V2 head{cx, top + 0.6f * u}, neck{cx, top + 1.25f * u};
    const V2 sh[2]{{cx + 0.95f * u, top + 1.55f * u}, {cx - 0.95f * u, top + 1.55f * u}};   // [0] = esquerda da pessoa
    const V2 hip[2]{{cx + 0.55f * u, top + 4.0f * u}, {cx - 0.55f * u, top + 4.0f * u}};
    const V2 knee[2]{{cx + 0.6f * u, top + 5.9f * u}, {cx - 0.6f * u, top + 5.9f * u}};
    const V2 ankle[2]{{cx + 0.65f * u, top + 7.7f * u}, {cx - 0.65f * u, top + 7.7f * u}};
    const V2 elbow[2]{{cx + 1.25f * u, top + 3.0f * u}, {cx - 1.25f * u, top + 3.0f * u}};
    const V2 wrist[2]{{cx + 1.35f * u, top + 4.3f * u}, {cx - 1.35f * u, top + 4.3f * u}};
    std::vector<Shape> s;
    auto cap = [&](V2 a, V2 b, f32 r, Rgb c) { s.push_back({Shape::Capsule, a, b, {}, {}, r, c}); };
    auto ell = [&](V2 c, f32 rx, f32 ry, Rgb col) { s.push_back({Shape::Ellipse, c, {rx, ry}, {}, {}, 0, col}); };
    s.push_back({Shape::Rect, {0, H * 0.82f}, {W, H}, {}, {}, 0, floor});
    for (int i = 0; i < 2; ++i) {
        cap(hip[i], knee[i], 0.33f * u, pants);
        cap(knee[i], ankle[i], 0.27f * u, pants);
        ell({ankle[i].x, ankle[i].y + 0.1f * u}, 0.35f * u, 0.2f * u, shoe);
    }
    s.push_back({Shape::Quad, sh[0], sh[1], {hip[1].x - 0.15f * u, hip[1].y}, {hip[0].x + 0.15f * u, hip[0].y}, 0, shirt});
    ell({cx, hip[0].y}, (hip[0].x - hip[1].x) * 0.5f + 0.3f * u, 0.4f * u, pants);
    for (int i = 0; i < 2; ++i) {
        cap(sh[i], elbow[i], 0.24f * u, shirt);
        cap(elbow[i], wrist[i], 0.2f * u, skin);
        ell({wrist[i].x, wrist[i].y + 0.04f * u}, 0.22f * u, 0.26f * u, skin);
    }
    cap(neck, {cx, top + 1.5f * u}, 0.22f * u, skin);
    const f32 r = 0.5f * u;
    ell(head, r, r * 1.15f, skin);
    s.push_back({Shape::HalfEllipse, {head.x, head.y - 0.4f * r}, {r, 0.8f * r}, {}, {}, 0, hair});
    ell({head.x - 0.38f * r, head.y + 0.035f * r}, 0.1f * r, 0.085f * r, eye);
    ell({head.x + 0.38f * r, head.y + 0.035f * r}, 0.1f * r, 0.085f * r, eye);
    cap({head.x - 0.25f * r, head.y + 0.6f * r}, {head.x + 0.25f * r, head.y + 0.6f * r}, 1.0f, {140, 60, 60});

    f.rgba.assign(static_cast<usize>(f.w) * f.h * 4, 255);
    constexpr int kSub = 3;
    for (u32 y = 0; y < f.h; ++y) {
        for (u32 x = 0; x < f.w; ++x) {
            u32 acc[3]{};
            for (int sy = 0; sy < kSub; ++sy) for (int sx = 0; sx < kSub; ++sx) {
                const f32 px = static_cast<f32>(x) + (sx + 0.5f) / kSub, py = static_cast<f32>(y) + (sy + 0.5f) / kSub;
                Rgb c = bg;
                for (const Shape& sh2 : s) if (sh2.contains(px, py)) c = sh2.color;
                acc[0] += c.r; acc[1] += c.g; acc[2] += c.b;
            }
            u8* p = f.rgba.data() + (static_cast<usize>(y) * f.w + x) * 4;
            for (int k = 0; k < 3; ++k) p[k] = static_cast<u8>(acc[k] / (kSub * kSub));
        }
    }
    auto set = [&](u32 j, V2 v) { f.joint[j] = v; f.has[j] = true; };
    set(lm::LeftShoulder, sh[0]); set(lm::RightShoulder, sh[1]);
    set(lm::LeftElbow, elbow[0]); set(lm::RightElbow, elbow[1]);
    set(lm::LeftWrist, wrist[0]); set(lm::RightWrist, wrist[1]);
    set(lm::LeftHip, hip[0]); set(lm::RightHip, hip[1]);
    set(lm::LeftKnee, knee[0]); set(lm::RightKnee, knee[1]);
    set(lm::LeftAnkle, ankle[0]); set(lm::RightAnkle, ankle[1]);
    set(lm::Nose, {head.x, head.y + 0.25f * r});
    return f;
}

std::string model_dir() { return std::string(AUREA_TEST_DATA_DIR) + "/../../assets/mocap"; }

bool finite_points(const Landmarks& pts) {
    for (const auto& p : pts)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.confidence)
            || p.confidence < 0.0f || p.confidence > 1.0f) return false;
    return true;
}

/// Maior distância (fração da altura do quadro) entre os pontos e as juntas desenhadas.
f32 worst_joint_error(const Landmarks& pts, const Figure& f, u32* which = nullptr) {
    f32 worst = 0.0f;
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j) {
        if (!f.has[j]) continue;
        const f32 dx = pts[j].x * static_cast<f32>(f.w) - f.joint[j].x;
        const f32 dy = pts[j].y * static_cast<f32>(f.h) - f.joint[j].y;
        const f32 e = std::sqrt(dx * dx + dy * dy) / static_cast<f32>(f.h);
        if (e > worst) { worst = e; if (which) *which = j; }
    }
    return worst;
}

} // namespace

// ---- Contas da ROI (sem modelo) ------------------------------------------------

AUREA_TEST(PoseRoi, AlignmentOfAnUprightPersonHasNoRotation) {
    // Quadril em (200, 300); ponto do tamanho do corpo 100 px acima.
    const PoseRoi r = roi_from_alignment(200.0f, 300.0f, 200.0f, 200.0f);
    AUREA_CHECK(r.valid());
    AUREA_CHECK_NEAR(r.cx, 200.0f, 1e-4);
    AUREA_CHECK_NEAR(r.cy, 300.0f, 1e-4);
    AUREA_CHECK_NEAR(r.size, 2.0f * 100.0f * 1.25f, 1e-3);   // 2·|p1−p0|·1,25
    AUREA_CHECK_NEAR(r.rotation, 0.0f, 1e-6);
    // Centro do recorte = quadril; topo do recorte = acima da cabeça.
    f32 x, y;
    roi_to_image(r, 0.5f, 0.5f, x, y);
    AUREA_CHECK_NEAR(x, 200.0f, 1e-3);
    AUREA_CHECK_NEAR(y, 300.0f, 1e-3);
    roi_to_image(r, 0.5f, 0.0f, x, y);
    AUREA_CHECK_NEAR(y, 300.0f - 125.0f, 1e-3);
    roi_to_image(r, 1.0f, 0.5f, x, y);
    AUREA_CHECK_NEAR(x, 325.0f, 1e-3);
}

AUREA_TEST(PoseRoi, LyingPersonIsRotatedUprightInTheCrop) {
    // Cabeça para a DIREITA da tela: giro de +90°, e o "para cima" do recorte aponta para a cabeça.
    const PoseRoi r = roi_from_alignment(100.0f, 100.0f, 180.0f, 100.0f);
    AUREA_CHECK_NEAR(r.rotation, 0.5f * kPiF, 1e-5);
    AUREA_CHECK_NEAR(r.size, 200.0f, 1e-3);
    f32 x, y;
    roi_to_image(r, 0.5f, 0.0f, x, y);   // topo do recorte
    AUREA_CHECK_NEAR(x, 200.0f, 1e-3);
    AUREA_CHECK_NEAR(y, 100.0f, 1e-3);
    // Cabeça para baixo: ±180°.
    const PoseRoi d = roi_from_alignment(100.0f, 100.0f, 100.0f, 160.0f);
    AUREA_CHECK_NEAR(std::fabs(d.rotation), kPiF, 1e-5);
    roi_to_image(d, 0.5f, 0.0f, x, y);
    AUREA_CHECK_NEAR(x, 100.0f, 1e-3);
    AUREA_CHECK_NEAR(y, 100.0f + 75.0f, 1e-3);
    // Cabeça para a esquerda: −90°.
    AUREA_CHECK_NEAR(roi_from_alignment(100.0f, 100.0f, 40.0f, 100.0f).rotation, -0.5f * kPiF, 1e-5);
}

AUREA_TEST(PoseRoi, CropMappingRoundTripsAtAnyAngle) {
    for (int k = 0; k < 24; ++k) {
        PoseRoi r;
        r.cx = 30.0f + 17.0f * k;
        r.cy = 400.0f - 11.0f * k;
        r.size = 50.0f + 9.0f * k;
        r.rotation = -kPiF + (2.0f * kPiF) * static_cast<f32>(k) / 24.0f;
        for (f32 u : {0.0f, 0.13f, 0.5f, 0.92f, 1.0f})
            for (f32 v : {0.0f, 0.31f, 0.5f, 1.0f}) {
                f32 x, y, uu, vv;
                roi_to_image(r, u, v, x, y);
                image_to_roi(r, x, y, uu, vv);
                AUREA_CHECK_NEAR(uu, u, 1e-4);
                AUREA_CHECK_NEAR(vv, v, 1e-4);
            }
        // Lados do recorte medem `size` em qualquer giro.
        f32 x0, y0, x1, y1;
        roi_to_image(r, 0.0f, 0.0f, x0, y0);
        roi_to_image(r, 1.0f, 0.0f, x1, y1);
        AUREA_CHECK_NEAR(std::hypot(x1 - x0, y1 - y0), r.size, 1e-2);
    }
}

AUREA_TEST(PoseRoi, TrackedRoiFollowsAuxiliaryPointsAndFallsBackToTheBox) {
    // Pontos auxiliares de um quadro → ROI do próximo: a pessoa andou 40 px e cresceu 10%.
    const PoseRoi a = roi_from_alignment(300.0f, 260.0f, 300.0f, 140.0f);
    const PoseRoi b = roi_from_alignment(340.0f, 260.0f, 340.0f, 128.0f);
    AUREA_CHECK_NEAR(b.cx - a.cx, 40.0f, 1e-4);
    AUREA_CHECK_NEAR(b.size / a.size, 1.1f, 1e-4);
    // Reserva: a caixa dos pontos confiáveis, quadrada, com margem; os incertos ficam de fora.
    Landmarks pts{};
    const f32 xs[4]{0.2f, 0.4f, 0.3f, 0.25f}, ys[4]{0.1f, 0.5f, 0.9f, 0.3f};
    for (int i = 0; i < 4; ++i) pts[i] = {xs[i], ys[i], 0.0f, 0.9f};
    pts[5] = {0.95f, 0.95f, 0.0f, 0.1f};
    const PoseRoi box = roi_from_landmarks(pts, 1000, 500, 0.5f, 1.25f);
    AUREA_CHECK(box.valid());
    AUREA_CHECK_NEAR(box.cx, 300.0f, 1e-3);
    AUREA_CHECK_NEAR(box.cy, 250.0f, 1e-3);
    AUREA_CHECK_NEAR(box.size, 400.0f * 1.25f, 1e-3);   // o lado maior (altura 0,8·500)
    AUREA_CHECK_NEAR(box.rotation, 0.0f, 0.0);
    pts[3].confidence = 0.2f;
    AUREA_CHECK(!roi_from_landmarks(pts, 1000, 500).valid());   // menos de 4 pontos
}

AUREA_TEST(PoseRoi, LetterboxAnchorsAndPackedFrame) {
    const Letterbox l = letterbox(640, 360, 224);
    AUREA_CHECK_NEAR(l.scale, 0.35f, 1e-6);
    AUREA_CHECK_NEAR(l.padX, 0.0f, 1e-4);
    AUREA_CHECK_NEAR(l.padY, (224.0f - 126.0f) * 0.5f, 1e-4);
    const Letterbox t = letterbox(360, 640, 224);
    AUREA_CHECK_NEAR(t.padX, 49.0f, 1e-4);
    AUREA_CHECK_NEAR(t.padY, 0.0f, 1e-4);
    // 28²·2 + 14²·2 + 7²·6 âncoras, centros das células, na ordem célula → âncora.
    AUREA_CHECK_EQ(detector_anchor_count(), 2254u);
    f32 x, y;
    detector_anchor(0, x, y);
    AUREA_CHECK_NEAR(x, 0.5f / 28.0f, 1e-6); AUREA_CHECK_NEAR(y, 0.5f / 28.0f, 1e-6);
    detector_anchor(1, x, y);   // segunda âncora da mesma célula
    AUREA_CHECK_NEAR(x, 0.5f / 28.0f, 1e-6);
    detector_anchor(2, x, y);
    AUREA_CHECK_NEAR(x, 1.5f / 28.0f, 1e-6);
    detector_anchor(1568, x, y);   // primeira do passo 16
    AUREA_CHECK_NEAR(x, 0.5f / 14.0f, 1e-6);
    detector_anchor(2253, x, y);   // última: célula (6, 6) do passo 32
    AUREA_CHECK_NEAR(x, 6.5f / 7.0f, 1e-6); AUREA_CHECK_NEAR(y, 6.5f / 7.0f, 1e-6);
    // O quadro das pontes: tempo, sem mundo, 33 × (x, y, z, confiança).
    Landmarks pts{};
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j) pts[j] = {0.01f * j, 0.02f * j, -0.03f * j, 0.5f};
    std::vector<f32> frame(rig::kMocapFrameFloats, -1.0f);
    pack_frame(1.25, pts, frame.data());
    AUREA_CHECK_NEAR(frame[0], 1.25f, 0.0);
    AUREA_CHECK_NEAR(frame[1], 0.0f, 0.0);
    AUREA_CHECK_NEAR(frame[2 + 32 * 4 + 0], 0.32f, 1e-6);
    AUREA_CHECK_NEAR(frame[2 + 32 * 4 + 2], -0.96f, 1e-6);
    AUREA_CHECK_NEAR(frame[2 + 32 * 4 + 3], 0.5f, 0.0);
    // E o motor lê o mesmo quadro (rig::unpack_frames).
    std::vector<rig::PoseFrame> unpacked;
    rig::unpack_frames(frame.data(), 1, unpacked);
    AUREA_CHECK_EQ(unpacked.size(), static_cast<usize>(1));
    if (!unpacked.empty()) AUREA_CHECK_NEAR(unpacked[0].pts[lm::LeftHip].y, 0.02f * static_cast<f32>(lm::LeftHip), 1e-6);
}

// ---- BlazePose no motor (ncnn) ---------------------------------------------------

AUREA_TEST(PoseEstimator, FindsTheDrawnPersonAndItsJoints) {
    PoseEstimator est;
    const Status s = est.load(model_dir(), PoseEstimator::Backend::Cpu);
    AUREA_CHECK_MSG(s.ok(), "modelos em engine/assets/mocap");
    if (!s.ok()) return;
    const Figure f = draw_figure();
    PoseRoi roi;
    f32 score = 0.0f;
    AUREA_CHECK(est.detect(f.rgba.data(), f.w, f.h, f.w * 4, roi, &score));
    std::printf("    detector %.3f; ROI centro (%.1f, %.1f) lado %.1f giro %.1f graus\n", score, roi.cx, roi.cy, roi.size,
                roi.rotation * 180.0f / kPiF);
    AUREA_CHECK(score > 0.8f);
    AUREA_CHECK_NEAR(roi.rotation, 0.0f, 0.2);                     // em pé
    AUREA_CHECK(roi.cx > 200.0f && roi.cx < 280.0f);               // perto do quadril desenhado
    AUREA_CHECK(roi.size > 0.6f * f.h && roi.size < 1.8f * f.h);   // o corpo todo cabe

    Landmarks pts{};
    PoseRoi next;
    const f32 presence = est.landmarks(f.rgba.data(), f.w, f.h, f.w * 4, roi, pts, &next);
    std::printf("    presenca %.3f; %.1f ms (CPU)\n", presence, est.last_ms());
    AUREA_CHECK(presence > 0.9f);
    AUREA_CHECK(finite_points(pts));
    AUREA_CHECK(next.valid());
    u32 which = 0;
    const f32 err = worst_joint_error(pts, f, &which);
    std::printf("    maior erro nas juntas desenhadas: %.3f da altura (ponto %u)\n", err, which);
    for (u32 j : {lm::Nose, lm::LeftShoulder, lm::RightShoulder, lm::LeftWrist, lm::RightWrist, lm::LeftHip, lm::RightHip,
                  lm::LeftAnkle, lm::RightAnkle})
        std::printf("      %2u: (%.1f, %.1f) desenhado (%.1f, %.1f) conf %.2f z %.3f\n", j, pts[j].x * f.w, pts[j].y * f.h,
                    f.joint[j].x, f.joint[j].y, pts[j].confidence, pts[j].z);
    AUREA_CHECK(err < 0.12f);
    // Ordem do corpo (y para baixo) e lados (de frente: a esquerda da pessoa fica à direita da tela).
    AUREA_CHECK(pts[lm::Nose].y < pts[lm::LeftShoulder].y && pts[lm::LeftShoulder].y < pts[lm::LeftHip].y
                && pts[lm::LeftHip].y < pts[lm::LeftKnee].y && pts[lm::LeftKnee].y < pts[lm::LeftAnkle].y);
    AUREA_CHECK(pts[lm::LeftShoulder].x > pts[lm::RightShoulder].x);
    AUREA_CHECK(pts[lm::LeftHip].x > pts[lm::RightHip].x);
    AUREA_CHECK(pts[lm::LeftAnkle].x > pts[lm::RightAnkle].x);
    for (u32 j : {lm::Nose, lm::LeftShoulder, lm::RightShoulder, lm::LeftHip, lm::RightHip, lm::LeftKnee, lm::RightKnee})
        AUREA_CHECK(pts[j].confidence > 0.5f);
}

AUREA_TEST(PoseEstimator, TrackerFollowsThePersonWithoutRedetecting) {
    PoseEstimator est;
    if (!est.load(model_dir(), PoseEstimator::Backend::Cpu).ok()) { AUREA_CHECK(false); return; }
    PoseTracker tracker(est);
    // A figura anda para a direita 12 px por quadro.
    f32 lastNoseX = -1.0f;
    for (int k = 0; k < 5; ++k) {
        const Figure f = draw_figure(12.0f * k);
        Landmarks pts{};
        AUREA_CHECK(tracker.track(f.rgba.data(), f.w, f.h, f.w * 4, pts));
        AUREA_CHECK(finite_points(pts));
        AUREA_CHECK(worst_joint_error(pts, f) < 0.12f);
        const f32 noseX = pts[lm::Nose].x * f.w;
        if (lastNoseX >= 0.0f) AUREA_CHECK_NEAR(noseX - lastNoseX, 12.0f, 6.0);
        lastNoseX = noseX;
    }
    std::printf("    detector rodou %u vez(es) em 5 quadros\n", tracker.detector_runs());
    AUREA_CHECK_EQ(tracker.detector_runs(), 1u);
    // Quadro vazio: ninguém; a ROI some e o próximo quadro com gente volta ao detector.
    Figure empty = draw_figure();
    for (usize i = 0; i < empty.rgba.size(); i += 4) { empty.rgba[i] = 178; empty.rgba[i + 1] = 186; empty.rgba[i + 2] = 196; }
    Landmarks pts{};
    AUREA_CHECK(!tracker.track(empty.rgba.data(), empty.w, empty.h, empty.w * 4, pts));
    AUREA_CHECK(!tracker.roi().valid());
    AUREA_CHECK_NEAR(pts[lm::Nose].confidence, 0.0f, 0.0);
    const Figure back = draw_figure();
    AUREA_CHECK(tracker.track(back.rgba.data(), back.w, back.h, back.w * 4, pts));
    AUREA_CHECK(tracker.detector_runs() >= 2u);
}

AUREA_TEST(PoseEstimator, VulkanMatchesCpu) {
    PoseEstimator cpu, gpu;
    if (!cpu.load(model_dir(), PoseEstimator::Backend::Cpu).ok()) { AUREA_CHECK(false); return; }
    if (!gpu.load(model_dir(), PoseEstimator::Backend::Vulkan).ok()) {
        std::printf("    (sem Vulkan para o ncnn: so CPU)\n");
        return;
    }
    const Figure f = draw_figure();
    PoseRoi ra, rb;
    Landmarks a{}, b{};
    AUREA_CHECK(estimate_pose(cpu, f.rgba.data(), f.w, f.h, f.w * 4, ra, a));
    AUREA_CHECK(estimate_pose(gpu, f.rgba.data(), f.w, f.h, f.w * 4, rb, b));
    f32 worst = 0.0f;
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j)
        worst = std::max({worst, std::fabs(a[j].x - b[j].x), std::fabs(a[j].y - b[j].y), std::fabs(a[j].confidence - b[j].confidence)});
    // Tempo por quadro já rastreado (sem o detector), o melhor de 5.
    f32 cpuMs = 1e9f, gpuMs = 1e9f;
    for (int k = 0; k < 5; ++k) {
        PoseRoi r1 = ra, r2 = rb;
        Landmarks t{};
        (void)estimate_pose(cpu, f.rgba.data(), f.w, f.h, f.w * 4, r1, t);
        cpuMs = std::min(cpuMs, cpu.last_ms());
        (void)estimate_pose(gpu, f.rgba.data(), f.w, f.h, f.w * 4, r2, t);
        gpuMs = std::min(gpuMs, gpu.last_ms());
    }
    std::printf("    CPU x Vulkan: maior diferenca %.2e; por quadro rastreado CPU %.1f ms, Vulkan %.1f ms\n", worst, cpuMs, gpuMs);
    AUREA_CHECK(worst < 2e-3f);
}

// ---- Qualidade do PC (13/11): heavy, pirâmide, mundo, rastreio de vídeo ---------------

AUREA_TEST(PoseEstimator, HeavyWithPyramidGivesUprightWorldLandmarks) {
    PoseEstimator est;
    if (!est.load(model_dir(), PoseEstimator::Backend::Cpu, 0, LandmarkModel::Heavy).ok()) { AUREA_CHECK(false); return; }
    AUREA_CHECK(est.model() == LandmarkModel::Heavy);
    est.set_refine(true, true);
    // A figura em 4× (1920×1440): o recorte de 256 sai da pirâmide, não do quadro inteiro.
    const Figure small = draw_figure();
    Figure big;
    big.w = small.w * 4;
    big.h = small.h * 4;
    big.rgba.resize(static_cast<usize>(big.w) * big.h * 4);
    for (u32 y = 0; y < big.h; ++y)
        for (u32 x = 0; x < big.w; ++x)
            std::copy_n(small.rgba.data() + (static_cast<usize>(y / 4) * small.w + x / 4) * 4, 4,
                        big.rgba.data() + (static_cast<usize>(y) * big.w + x) * 4);
    PoseImage img;
    img.build(big.rgba.data(), big.w, big.h, big.w * 4, 4);
    AUREA_CHECK(img.levels.size() >= 4);
    PoseRoi roi;
    AUREA_CHECK(est.detect(img, roi));
    PoseOutput out;
    const f32 presence = est.landmarks(img, roi, out);
    AUREA_CHECK(presence > 0.9f);
    AUREA_CHECK(finite_points(out.image));
    const f32 err = worst_joint_error(out.image, small);
    std::printf("    heavy na piramide: presenca %.3f, maior erro %.3f da altura, %.1f ms (CPU)\n", presence, err, est.last_ms());
    AUREA_CHECK(err < 0.12f);
    // Mundo: metros, origem no meio do quadril, y para baixo; de frente, em pé.
    const Vec3 mid = (out.world[lm::LeftHip] + out.world[lm::RightHip]) * 0.5f;
    const Vec3 ankles = (out.world[lm::LeftAnkle] + out.world[lm::RightAnkle]) * 0.5f;
    const Vec3 shoulders = (out.world[lm::LeftShoulder] + out.world[lm::RightShoulder]) * 0.5f;
    std::printf("    mundo: quadril (%.2f, %.2f, %.2f), tornozelos y %.2f, ombros y %.2f m\n", mid.x, mid.y, mid.z, ankles.y, shoulders.y);
    AUREA_CHECK(mid.length() < 0.05f);
    AUREA_CHECK(ankles.y > 0.5f && ankles.y < 1.2f);
    AUREA_CHECK(shoulders.y < -0.3f && shoulders.y > -0.8f);
    AUREA_CHECK(out.world[lm::LeftHip].x > out.world[lm::RightHip].x);   // esquerda da pessoa à direita da tela
    for (u32 j : {lm::LeftShoulder, lm::LeftHip, lm::LeftKnee}) AUREA_CHECK(out.visibility[j] > 0.5f);
    // O quadro companheiro leva o mundo e a visibilidade.
    std::array<f32, rig::kMocapFrameFloats> packed{};
    pack_world_frame(1.5, out, packed.data());
    AUREA_CHECK_NEAR(packed[1], rig::kMocapWorldFlag, 0.0);
    AUREA_CHECK_NEAR(packed[2 + lm::LeftKnee * 4 + 1], out.world[lm::LeftKnee].y, 1e-6);
}

AUREA_TEST(PoseEstimator, VideoTrackerFollowsWithFilteredRoiAndKeepsThePerson) {
    PoseEstimator est;
    if (!est.load(model_dir(), PoseEstimator::Backend::Cpu).ok()) { AUREA_CHECK(false); return; }
    est.set_refine(false, true);
    VideoTrackerOptions opt;
    opt.recheckEvery = 0;
    VideoPoseTracker tracker(est, opt);
    f32 lastNoseX = -1.0f;
    for (int k = 0; k < 6; ++k) {
        const Figure f = draw_figure(10.0f * k);
        PoseImage img;
        img.build(f.rgba.data(), f.w, f.h, f.w * 4);
        PoseOutput out;
        AUREA_CHECK(tracker.track(img, 1.0f / 30.0f, out));
        AUREA_CHECK(worst_joint_error(out.image, f) < 0.12f);
        const f32 noseX = out.image[lm::Nose].x * f.w;
        if (lastNoseX >= 0.0f) AUREA_CHECK_NEAR(noseX - lastNoseX, 10.0f, 6.0);
        lastNoseX = noseX;
    }
    std::printf("    detector %u vez(es) em 6 quadros, %u trocas de lado\n", tracker.detector_runs(), tracker.swaps_fixed());
    AUREA_CHECK_EQ(tracker.detector_runs(), 1u);
    AUREA_CHECK_EQ(tracker.swaps_fixed(), 0u);
    // Some e volta: o detector roda de novo e acha a mesma pessoa.
    Figure empty = draw_figure();
    for (usize i = 0; i < empty.rgba.size(); i += 4) { empty.rgba[i] = 178; empty.rgba[i + 1] = 186; empty.rgba[i + 2] = 196; }
    PoseImage e;
    e.build(empty.rgba.data(), empty.w, empty.h, empty.w * 4);
    PoseOutput out;
    AUREA_CHECK(!tracker.track(e, 1.0f / 30.0f, out));
    AUREA_CHECK_NEAR(out.presence, 0.0f, 0.0);
    const Figure back = draw_figure(50.0f);
    PoseImage b;
    b.build(back.rgba.data(), back.w, back.h, back.w * 4);
    AUREA_CHECK(tracker.track(b, 1.0f / 30.0f, out));
    AUREA_CHECK(worst_joint_error(out.image, back) < 0.12f);
}
