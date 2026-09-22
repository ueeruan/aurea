// =============================================================================
//  Rastreio de câmera 3D: solver com câmera conhecida (FOV, trajetória,
//  rotação) e o pipeline de imagem (pontos → Lucas-Kanade → solve).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/tracking/CameraTracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace aurea;
using namespace aurea::tracking;

namespace {

constexpr f64 kPiT = 3.14159265358979323846;

struct TruthCam {
    f64 R[9];   // mundo → câmera
    f64 C[3];   // centro
};

/// R = Rz·Ry·Rx (câmera → mundo) e a câmera do quadro i: travelling em X,
/// subida leve em Y, avanço em Z e giro em Y (panorâmica) e X.
TruthCam truth_camera(u32 i, u32 n) {
    const f64 u = static_cast<f64>(i) / static_cast<f64>(n - 1);
    const f64 ry = (-8.0 + 16.0 * u) * kPiT / 180.0, rx = (3.0 * std::sin(u * kPiT)) * kPiT / 180.0, rz = 1.5 * u * kPiT / 180.0;
    const f64 cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
    // Rwc = Rz·Ry·Rx
    const f64 Rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    const f64 Ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const f64 Rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    f64 T[9], Rwc[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { T[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) T[r * 3 + c] += Ry[r * 3 + k] * Rx[k * 3 + c]; }
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { Rwc[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) Rwc[r * 3 + c] += Rz[r * 3 + k] * T[k * 3 + c]; }
    TruthCam t{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t.R[r * 3 + c] = Rwc[c * 3 + r];   // mundo → câmera = transposta
    t.C[0] = -1.2 + 2.4 * u;
    t.C[1] = -0.3 * u;
    t.C[2] = 0.8 * u;
    return t;
}

bool project_truth(const TruthCam& c, const f64 X[3], f64 f, f64 cx, f64 cy, f64& u, f64& v) {
    const f64 d[3] = {X[0] - c.C[0], X[1] - c.C[1], X[2] - c.C[2]};
    const f64 x = c.R[0] * d[0] + c.R[1] * d[1] + c.R[2] * d[2];
    const f64 y = c.R[3] * d[0] + c.R[4] * d[1] + c.R[5] * d[2];
    const f64 z = c.R[6] * d[0] + c.R[7] * d[1] + c.R[8] * d[2];
    if (z < 0.5) return false;
    u = f * x / z + cx;
    v = f * y / z + cy;
    return true;
}

struct Scene {
    std::vector<std::array<f64, 3>> pts;
};

Scene make_scene(u32 count) {
    Scene s;
    std::mt19937 rng(42u);
    std::uniform_real_distribution<f64> ux(-5.0, 5.0), uy(-3.0, 3.0), uz(5.0, 12.0);
    for (u32 i = 0; i < count; ++i) s.pts.push_back({ux(rng), uy(rng), uz(rng)});
    return s;
}

/// Compara a solução com a verdade no referencial da câmera 0 (centros
/// normalizados pela distância até a última câmera). Erros: posição (fração
/// do caminho) e rotação (graus), os piores do vídeo.
void compare(const CameraSolution& s, u32 n, f64& posErr, f64& rotErr) {
    const TruthCam c0 = truth_camera(0, n), cl = truth_camera(n - 1, n);
    auto rel = [&](const TruthCam& c, f64 out[3]) {
        const f64 d[3] = {c.C[0] - c0.C[0], c.C[1] - c0.C[1], c.C[2] - c0.C[2]};
        for (int r = 0; r < 3; ++r) out[r] = c0.R[r * 3] * d[0] + c0.R[r * 3 + 1] * d[1] + c0.R[r * 3 + 2] * d[2];
    };
    f64 last[3];
    rel(cl, last);
    const f64 tl = std::sqrt(last[0] * last[0] + last[1] * last[1] + last[2] * last[2]);
    const Vec3 el = s.poses[n - 1].center();
    const f64 sl = std::sqrt(static_cast<f64>(el.x) * el.x + static_cast<f64>(el.y) * el.y + static_cast<f64>(el.z) * el.z);
    posErr = 0;
    rotErr = 0;
    for (u32 i = 0; i < n; ++i) {
        if (!s.poses[i].valid) { posErr = 1e9; return; }
        const TruthCam ci = truth_camera(i, n);
        f64 t[3];
        rel(ci, t);
        const Vec3 e = s.poses[i].center();
        const f64 dx = t[0] / tl - e.x / sl, dy = t[1] / tl - e.y / sl, dz = t[2] / tl - e.z / sl;
        posErr = std::max(posErr, std::sqrt(dx * dx + dy * dy + dz * dz));
        // Rotação relativa verdadeira: Ri·R0ᵀ; estimada: Ri (a câmera 0 é a identidade).
        f64 Rt[9];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { Rt[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) Rt[r * 3 + c] += ci.R[r * 3 + k] * c0.R[c * 3 + k]; }
        // ângulo de Rtᵀ·Re: traço
        f64 tr = 0;
        for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) tr += Rt[k * 3 + r] * s.poses[i].R[k * 3 + r];
        rotErr = std::max(rotErr, std::acos(std::clamp((tr - 1.0) * 0.5, -1.0, 1.0)) * 180.0 / kPiT);
    }
}

} // namespace

AUREA_TEST(Tracking, SolverRecoversKnownCameraFovAndPath) {
    const u32 W = 640, H = 360, N = 90;
    const f64 fovTrue = 55.0;
    const f64 f = 0.5 * H / std::tan(0.5 * fovTrue * kPiT / 180.0);
    const Scene sc = make_scene(260);
    Tracks2D T;
    T.frames = N;
    T.width = W;
    T.height = H;
    std::mt19937 rng(5u);
    std::normal_distribution<f64> noise(0.0, 0.3);
    for (const auto& X : sc.pts) {
        std::vector<Vec2> row(N, Vec2{NAN, NAN});
        u32 seen = 0;
        for (u32 i = 0; i < N; ++i) {
            f64 u, v;
            if (project_truth(truth_camera(i, N), X.data(), f, W * 0.5, H * 0.5, u, v) && u > 8 && v > 8 && u < W - 8 && v < H - 8) {
                row[i] = Vec2{static_cast<f32>(u + noise(rng)), static_cast<f32>(v + noise(rng))};
                ++seen;
            }
        }
        if (seen >= 2) T.pos.push_back(std::move(row));
    }
    SolveOptions opt;
    const CameraSolution s = solve_camera(T, opt);
    f64 posErr = 0, rotErr = 0;
    if (s.ok) compare(s, N, posErr, rotErr);
    std::printf("    camera: %s; FOV %.1f (verdade %.1f); erro %.3f px; %u rastros, %u pontos, %u/%u quadros; posicao %.2f%% do caminho; rotacao %.2f graus; confianca %.2f %s\n",
                s.ok ? "resolvida" : "falhou", s.fovY * 180.0 / kPiT, fovTrue, s.rmsError, s.tracks, s.inliers, s.framesSolved, N,
                posErr * 100.0, rotErr, s.confidence, s.failure.c_str());
    AUREA_CHECK(s.ok);
    AUREA_CHECK(!s.rotationOnly);
    AUREA_CHECK(std::fabs(s.fovY * 180.0 / kPiT - fovTrue) < 5.0);
    AUREA_CHECK(s.rmsError < 0.8f);
    AUREA_CHECK(s.framesSolved == N);
    AUREA_CHECK(posErr < 0.03);
    AUREA_CHECK(rotErr < 0.5);
}

AUREA_TEST(Tracking, TripodShotIsSolvedAsRotationOnly) {
    // Só panorâmica (centro parado): sem paralaxe, o solve diz "só rotação".
    const u32 W = 640, H = 360, N = 60;
    const f64 f = 0.5 * H / std::tan(0.5 * 50.0 * kPiT / 180.0);
    const Scene sc = make_scene(220);
    Tracks2D T;
    T.frames = N;
    T.width = W;
    T.height = H;
    for (const auto& X : sc.pts) {
        std::vector<Vec2> row(N, Vec2{NAN, NAN});
        u32 seen = 0;
        for (u32 i = 0; i < N; ++i) {
            TruthCam c = truth_camera(i, N);
            c.C[0] = c.C[1] = c.C[2] = 0.0;
            f64 u, v;
            if (project_truth(c, X.data(), f, W * 0.5, H * 0.5, u, v) && u > 8 && v > 8 && u < W - 8 && v < H - 8) {
                row[i] = Vec2{static_cast<f32>(u), static_cast<f32>(v)};
                ++seen;
            }
        }
        if (seen >= 2) T.pos.push_back(std::move(row));
    }
    const CameraSolution s = solve_camera(T, SolveOptions{});
    std::printf("    tripe: %s, so rotacao %d, FOV %.1f (verdade 50), erro %.3f px\n", s.ok ? "resolvida" : "falhou", s.rotationOnly ? 1 : 0,
                s.fovY * 180.0 / kPiT, s.rmsError);
    AUREA_CHECK(s.ok && s.rotationOnly);
    AUREA_CHECK(s.rmsError < 0.5f);
}

AUREA_TEST(Tracking, FeatureTrackerFollowsRenderedPoints) {
    // Quadros desenhados (manchas gaussianas de brilho variado sobre textura
    // suave) com a câmera verdadeira: os rastros batem com a projeção.
    const u32 W = 480, H = 270, N = 40;
    const f64 f = 0.5 * H / std::tan(0.5 * 55.0 * kPiT / 180.0);
    const Scene sc = make_scene(200);
    FeatureTracker ft(TrackMode::Balanced);
    std::vector<std::vector<Vec2>> truth(N);
    for (u32 i = 0; i < N; ++i) {
        Gray g;
        g.width = W;
        g.height = H;
        g.px.assign(static_cast<usize>(W) * H, 0.0f);
        for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) g.px[static_cast<usize>(y) * W + x] = 0.1f;
        const TruthCam c = truth_camera(i, 90);
        truth[i].resize(sc.pts.size(), Vec2{NAN, NAN});
        for (usize k = 0; k < sc.pts.size(); ++k) {
            f64 u, v;
            if (!project_truth(c, sc.pts[k].data(), f, W * 0.5, H * 0.5, u, v)) continue;
            truth[i][k] = Vec2{static_cast<f32>(u), static_cast<f32>(v)};
            const f32 amp = 0.3f + 0.6f * static_cast<f32>((k * 37) % 100) / 100.0f;
            for (int dy = -5; dy <= 5; ++dy) for (int dx = -5; dx <= 5; ++dx) {
                const i32 X = static_cast<i32>(std::floor(u)) + dx, Y = static_cast<i32>(std::floor(v)) + dy;
                if (X < 0 || Y < 0 || X >= static_cast<i32>(W) || Y >= static_cast<i32>(H)) continue;
                const f64 ddx = X - u, ddy = Y - v;
                g.px[static_cast<usize>(Y) * W + static_cast<usize>(X)] += amp * static_cast<f32>(std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * 1.6 * 1.6)));
            }
        }
        ft.add_frame(g);
    }
    // Cada rastro: a mancha verdadeira mais perto no 1º quadro dele; erro no último.
    const Tracks2D& T = ft.tracks();
    f64 sum = 0;
    u32 n = 0, longTracks = 0;
    std::vector<f64> drifts;
    for (const auto& row : T.pos) {
        u32 first = N, last = N;
        for (u32 i = 0; i < N; ++i) if (Tracks2D::present(row[i])) { if (first == N) first = i; last = i; }
        if (first == N || last - first < 10) continue;
        ++longTracks;
        usize best = 0;
        f64 bd = 1e9;
        for (usize k = 0; k < sc.pts.size(); ++k) {
            const Vec2 t = truth[first][k];
            if (t.x != t.x) continue;
            const f64 d = std::hypot(t.x - row[first].x, t.y - row[first].y);
            if (d < bd) { bd = d; best = k; }
        }
        if (bd > 2.0) continue;
        const Vec2 t = truth[last][best];
        if (t.x != t.x) continue;
        // Deriva = quanto o erro (vetor) mudou do 1º ao último quadro do rastro.
        const Vec2 t0 = truth[first][best];
        const f64 ex0 = row[first].x - t0.x, ey0 = row[first].y - t0.y;
        const f64 dr = std::hypot((row[last].x - t.x) - ex0, (row[last].y - t.y) - ey0);
        drifts.push_back(dr);
        sum += dr;
        ++n;
    }
    const f64 drift = n ? sum / n : 1e9;
    std::sort(drifts.begin(), drifts.end());
    const f64 median = drifts.empty() ? 1e9 : drifts[drifts.size() / 2];
    // Um passo isolado: mancha única deslocada (3,37; −1,62) px.
    Gray a, b;
    a.width = b.width = 64;
    a.height = b.height = 64;
    a.px.assign(64 * 64, 0.1f);
    b.px.assign(64 * 64, 0.1f);
    auto blob = [](Gray& g, f64 cx, f64 cy) {
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                g.px[static_cast<usize>(y) * 64 + static_cast<usize>(x)] += 0.8f * static_cast<f32>(std::exp(-((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (2.0 * 1.6 * 1.6)));
    };
    blob(a, 30.0, 32.0);
    blob(b, 33.37, 30.38);
    FeatureTracker one(TrackMode::Balanced);
    one.add_frame(a);
    one.add_frame(b);
    f64 stepErr = 1e9;
    for (const auto& row : one.tracks().pos) {
        if (!Tracks2D::present(row[0]) || !Tracks2D::present(row[1])) continue;
        stepErr = std::min(stepErr, std::hypot((row[1].x - row[0].x) - 3.37, (row[1].y - row[0].y) + 1.62));
    }
    std::printf("    rastreio 2D: %zu rastros, %u longos (>=10 quadros), deriva mediana %.3f px, media %.3f px em %u; passo isolado erro %.4f px\n",
                T.pos.size(), longTracks, median, drift, n, stepErr);
    AUREA_CHECK(longTracks >= 80);
    AUREA_CHECK(n >= 60);
    AUREA_CHECK(median < 0.3);
    AUREA_CHECK(stepErr < 0.02);
}

// -----------------------------------------------------------------------------
// Motor: vídeo → análise em segundo plano → câmera na timeline
// -----------------------------------------------------------------------------
#include "SyntheticVideo.hpp"
#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"

#include <chrono>
#include <thread>

AUREA_TEST(Tracking, EngineTracksVideoAndPointsStayPinned) {
    using namespace aurea::test;
    SyntheticConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.frameCount = 90;
    cfg.pattern = SyntheticPattern::Scene3D;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.workerCount = 2;
    ec.memoryBudgetBytes = 128ull << 20;
    ec.disableAutosave = true;
    ec.mediaFactory = &factory;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "camera").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "cena";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    auto wait = [&] {
        const auto t0 = std::chrono::steady_clock::now();
        while (e.camera_track_status().state == 1 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(120))
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    };
    AUREA_CHECK(e.start_camera_track(*layer, 1));
    const f64 secs = wait();
    const Engine::CameraTrackStatus st = e.camera_track_status();
    auto cam = e.apply_camera_track();
    AUREA_CHECK(cam.ok());
    // Os pontos reconstruídos, projetados pela câmera do Aurea, caem onde a
    // mancha verdadeira está no vídeo — no começo, no meio e no fim.
    const std::vector<Vec3> pts = e.camera_track_points();
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    std::vector<f64> errs;
    const auto& truth = scene3d_points();
    for (const Vec3& q : pts) {
        auto proj = [&](i64 f, f64& u, f64& v) {
            const Vec4 c = comp_view_projection(*comp, FrameIndex{f}) * Vec4{q.x, q.y, q.z, 1};
            if (c.w <= 0) return false;
            u = c.x / c.w;
            v = c.y / c.w;
            return true;
        };
        f64 u0, v0;
        if (!proj(0, u0, v0)) continue;
        usize best = truth.size();
        f64 bd = 1.5;
        for (usize k = 0; k < truth.size(); ++k) {
            f64 tu, tv;
            if (!scene3d_project(truth[k], 0, 90, 640, 360, tu, tv)) continue;
            const f64 d = std::hypot(tu - u0, tv - v0);
            if (d < bd) { bd = d; best = k; }
        }
        if (best == truth.size()) continue;
        f64 worst = bd;
        for (i64 f : {45, 89}) {
            f64 u, v, tu, tv;
            if (!proj(f, u, v) || !scene3d_project(truth[best], static_cast<u32>(f), 90, 640, 360, tu, tv)) { worst = 1e9; break; }
            worst = std::max(worst, std::hypot(u - tu, v - tv));
        }
        errs.push_back(worst);
    }
    std::sort(errs.begin(), errs.end());
    const f64 med = errs.empty() ? 1e9 : errs[errs.size() / 2];
    const f64 p90 = errs.empty() ? 1e9 : errs[errs.size() * 9 / 10];
    // Pontos seguidos no vídeo (os "pontos do AE"): no quadro 45, cada um cai
    // perto de uma mancha verdadeira; a maioria entrou no solve.
    std::vector<f32> feat(3 * 1000);
    const u32 nf = e.camera_track_features(45, feat.data(), 1000);
    u32 nearTruth = 0, solved = 0, solvedNear = 0;
    for (u32 k = 0; k < nf; ++k) {
        const bool s1 = feat[k * 3 + 2] > 0.5f;
        solved += s1 ? 1u : 0u;
        for (const auto& X : truth) {
            f64 tu, tv;
            if (scene3d_project(X, 45, 90, 640, 360, tu, tv) && std::hypot(tu - feat[k * 3], tv - feat[k * 3 + 1]) < 2.0) {
                ++nearTruth;
                solvedNear += s1 ? 1u : 0u;
                break;
            }
        }
    }
    std::printf("    pontos no video (quadro 45): %u, %u no solve (%u sobre mancha), %u sobre uma mancha verdadeira\n", nf, solved, solvedNear, nearTruth);
    AUREA_CHECK(nf >= 100 && solved * 2 > nf && nearTruth * 10 >= nf * 8 && solvedNear * 10 >= solved * 8);
    // De novo, mesmo vídeo e ajustes: do cache, na hora.
    AUREA_CHECK(e.start_camera_track(*layer, 1));
    const Engine::CameraTrackStatus again = e.camera_track_status();
    // Cancelar (outro modo = outra chave de cache): para e não mexe no projeto.
    const u32 layersBefore = comp->layers().count();
    AUREA_CHECK(e.start_camera_track(*layer, 2));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    e.cancel_camera_track();
    const Engine::CameraTrackStatus cancelled = e.camera_track_status();
    std::printf("    camera no motor: estado %u em %.1f s; FOV %.1f (verdade %.1f); erro %.3f px; %u/%u quadros; %u pontos; confianca %.2f; "
                "presos na cena: mediana %.2f px, p90 %.2f px (%zu pontos); cache %d; cancelado %u; %s\n",
                st.state, secs, st.fovDeg, kScene3DFov, st.rmsError, st.framesSolved, st.frames, st.inliers, st.confidence, med, p90,
                errs.size(), again.cached ? 1 : 0, cancelled.state, st.message.c_str());
    AUREA_CHECK(st.state == 2);
    AUREA_CHECK(std::fabs(st.fovDeg - kScene3DFov) < 5.0);
    AUREA_CHECK(st.rmsError < 1.0f);
    AUREA_CHECK(st.framesSolved >= 88);
    AUREA_CHECK(errs.size() >= 50);
    AUREA_CHECK(med < 1.5);
    AUREA_CHECK(again.cached && again.state == 2);
    AUREA_CHECK(cancelled.state == 4);
    AUREA_CHECK(comp->layers().count() == layersBefore);
    e.shutdown();
}

AUREA_TEST(Tracking, BenchTracker1080p10s) {
    const char* v = std::getenv("AUREA_BENCH");
    if (!v || *v != '1') { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    using namespace aurea::test;
    SyntheticConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::Scene3D;
    SyntheticFactory factory(cfg);
    for (u32 mode : {0u, 1u, 2u}) {
        Engine e;
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 512ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        if (!e.initialize(ec).ok() || !e.new_project(1920, 1080, 30.0, "bench").ok()) return;
        VideoImport imp;
        imp.sourcePath = "sintetico";
        auto layer = e.import_video(imp);
        if (!layer.ok()) return;
        const auto t0 = std::chrono::steady_clock::now();
        AUREA_CHECK(e.start_camera_track(*layer, mode));
        while (e.camera_track_status().state == 1) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
        const Engine::CameraTrackStatus st = e.camera_track_status();
        std::printf("    Tracker 1080p 10 s modo %u: %.1f s (%.1f quadros/s); FOV %.1f; erro %.2f px; %u/%u quadros; %u pontos\n", mode, secs,
                    st.frames / secs, st.fovDeg, st.rmsError, st.framesSolved, st.frames, st.inliers);
        e.shutdown();
    }
}
